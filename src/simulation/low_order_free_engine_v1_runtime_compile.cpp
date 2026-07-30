#include "simulation/low_order_free_engine_v1_runtime.hpp"

#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/engine_sim_v1_transient_friction.hpp"
#include "simulation/free_engine_method_registry.hpp"

#include <bit>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] std::optional<std::size_t>
find_capture_volume_index(const LowOrderCapturePlan &capture_plan,
                          contract::GasVolumeId id) {
    const auto found = std::ranges::find(capture_plan.capture_buffer.gas_volumes, id,
                                         &contract::GasVolumeIdentity::id);
    if (found == capture_plan.capture_buffer.gas_volumes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found -
                                    capture_plan.capture_buffer.gas_volumes.begin());
}

[[nodiscard]] const contract::CylinderSpec *
find_cylinder(const contract::EngineSpec &engine, contract::CylinderId cylinder_id) {
    const auto found =
        std::ranges::find(engine.cylinders, cylinder_id, &contract::CylinderSpec::id);
    return found == engine.cylinders.end() ? nullptr : &*found;
}

void require_release_or_later_boundaries(ValidationReport &report,
                                         const contract::ScalarTrajectory &trajectory,
                                         const contract::RationalRateHz &rate,
                                         std::uint64_t release_frame,
                                         const std::string &path,
                                         const std::string &lane_name) {
    for (std::size_t index = 1; index < trajectory.points.size(); ++index) {
        const auto boundary =
            contract::resolve_frame_index(trajectory.points[index].time_s, rate);
        require(report, boundary.has_value() && *boundary >= release_frame,
                ContractIssueCode::unsupported_value,
                path + ".points[" + std::to_string(index) + "].time_s",
                lane_name +
                    " transitions must occur at or after the fixed held-preparation "
                    "release frame");
    }
}

} // namespace

LowOrderFreeEngineV1CompileResult compile_low_order_free_engine_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256,
    LowOrderExecutionExtent execution_extent) {
    ValidationReport report;
    report.append(contract::validate_for_engine(scenario, engine));

    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *free_engine = std::get_if<contract::FreeEngine>(&scenario.mode);
    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "free-engine runtime requires low_order_operating_point_v1");
    require(report, free_engine != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode", "free-engine runtime requires FreeEngine mode");
    require(report, preparation != nullptr, ContractIssueCode::unsupported_value,
            "scenario.preparation",
            "free-engine runtime requires fixed-horizon cycle sampling");
    require(report, !simulation_request_identity_v3_sha256.is_zero(),
            ContractIssueCode::missing_value, "simulation_request_identity_v3_sha256",
            "free-engine runtime requires the canonical nonzero request identity");
    if (profile == nullptr || free_engine == nullptr || preparation == nullptr) {
        return report;
    }

    const auto crank_friction_calculation =
        calculate_engine_sim_v1_positive_speed_crank_friction(
            {profile->core.mechanism.crank.running_friction_torque_magnitude_nm.value});
    const auto *crank_friction =
        std::get_if<EngineSimV1PositiveSpeedCrankFriction>(&crank_friction_calculation);
    require(report, crank_friction != nullptr, ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank."
            "running_friction_torque_magnitude_nm.value",
            "free-engine runtime requires finite nonnegative pristine crank "
            "friction");

    report.append(admit_implemented_cycle_accounting_methods(engine, *profile));
    require(report,
            free_engine->crank_dynamics_method.value ==
                warm_running_free_engine_rigid_crank_zoh_work_energy_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.crank_dynamics_method.value",
            "free-engine runtime requires its exact warm-running rigid-crank method "
            "identity");
    require(report,
            preparation->method.value ==
                contract::fixed_horizon_cycle_sampling_method_identity(),
            ContractIssueCode::unsupported_value, "scenario.preparation.method.value",
            "free-engine runtime requires the exact implemented fixed-horizon "
            "sampling method");
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "free-engine runtime requires identical physics and capture clocks");
    require(report,
            capture_plan.engine_profile_id == engine.profile_id.value &&
                capture_plan.scenario_id == scenario.scenario_id &&
                capture_plan.capture_buffer.engine_id == engine.id &&
                capture_plan.capture_buffer.rate == scenario.rates.capture &&
                capture_plan.execution_extent == execution_extent,
            ContractIssueCode::inconsistent_semantics, "capture_plan",
            "free-engine capture plan belongs to another engine or scenario");
    require(report, execution_extent.valid(), ContractIssueCode::invalid_value,
            "execution_extent",
            "free-engine runtime requires a valid finite or open-ended execution "
            "extent");

    const auto release_frame = contract::resolve_frame_index(
        scenario.audible_start_s.value, scenario.rates.physics);
    const auto end_frame = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    const auto finite_execution = execution_extent.finite_physics_frame_count();
    require(report,
            release_frame.has_value() && *release_frame > 0U && end_frame.has_value() &&
                *end_frame > *release_frame &&
                (!finite_execution.has_value() || *finite_execution == *end_frame),
            ContractIssueCode::inconsistent_semantics, "scenario.audible_start_s.value",
            "free-engine release and fixed horizon must resolve to ordered integral "
            "physics frames matching capture");
    if (!report.ok() || crank_friction == nullptr || !release_frame.has_value() ||
        !end_frame.has_value() || free_engine->throttle_01.points.empty() ||
        free_engine->external_resisting_torque_nm.points.empty()) {
        return report;
    }

    require_release_or_later_boundaries(
        report, free_engine->throttle_01, scenario.rates.physics, *release_frame,
        "scenario.mode.throttle_01", "free-engine throttle");
    require_release_or_later_boundaries(
        report, free_engine->external_resisting_torque_nm, scenario.rates.physics,
        *release_frame, "scenario.mode.external_resisting_torque_nm",
        "free-engine external resisting-torque");
    if (!report.ok()) {
        return report;
    }

    auto control_schedule_result =
        compile_scenario_control_schedule(scenario, execution_extent);
    if (auto *nested = std::get_if<ValidationReport>(&control_schedule_result)) {
        return std::move(*nested);
    }
    auto control_schedule =
        std::get<ScenarioControlSchedule>(std::move(control_schedule_result));

    std::vector<OperatingCylinderAccountingPlan> cylinders;
    cylinders.reserve(capture_plan.cylinder_chambers.size());
    double stroke_m = 0.0;
    for (std::size_t index = 0; index < capture_plan.cylinder_chambers.size();
         ++index) {
        const auto &binding = capture_plan.cylinder_chambers[index];
        const auto *cylinder = find_cylinder(engine, binding.cylinder_id);
        require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                "capture_plan.cylinder_chambers[" + std::to_string(index) + "]",
                "free-engine chamber binding references an unknown cylinder");
        if (cylinder == nullptr) {
            continue;
        }
        if (index == 0U) {
            stroke_m = cylinder->stroke_m.value;
        }
        require(report,
                std::bit_cast<std::uint64_t>(cylinder->stroke_m.value) ==
                    std::bit_cast<std::uint64_t>(stroke_m),
                ContractIssueCode::inconsistent_semantics, "engine.cylinders",
                "free-engine runtime requires bit-identical cylinder strokes");
        cylinders.push_back({
            binding.cylinder_id,
            binding.chamber_volume_id,
            std::numbers::pi_v<double> * cylinder->bore_m.value *
                cylinder->bore_m.value * cylinder->stroke_m.value / 4.0,
        });
    }

    std::vector<std::size_t> physical_gas_step_indices;
    std::vector<OperatingGasVolumePressureSample> pressure_samples;
    physical_gas_step_indices.reserve(capture_plan.physical_gas_volume_ids.size());
    pressure_samples.reserve(capture_plan.physical_gas_volume_ids.size());
    for (std::size_t index = 0; index < capture_plan.physical_gas_volume_ids.size();
         ++index) {
        const auto id = capture_plan.physical_gas_volume_ids[index];
        const auto gas_index = find_capture_volume_index(capture_plan, id);
        require(report, gas_index.has_value(), ContractIssueCode::dangling_reference,
                "capture_plan.physical_gas_volume_ids[" + std::to_string(index) + "]",
                "free-engine physical pressure volume is absent from capture "
                "topology");
        if (gas_index.has_value()) {
            physical_gas_step_indices.push_back(*gas_index);
            pressure_samples.push_back({id, 0.0});
        }
    }
    if (!report.ok()) {
        return report;
    }

    auto accountant_result = compile_operating_cycle_accountant({
        {
            profile->core.mechanism.crank.crank_tdc_reference_rad.value,
            engine.total_displacement_m3.value,
        },
        {
            profile->aggregate_loss.constant_fmep_bar.value,
            profile->aggregate_loss.peak_pressure_coefficient.value,
            profile->aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value,
            profile->aggregate_loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2
                .value,
        },
        free_engine->initial_engine_speed_rpm.value,
        stroke_m,
        profile->starter.mechanically_disengaged.value,
        contract::indicated_gas_torque_term_mask(),
        profile->aggregate_loss.included_terms.value,
        profile->starter.included_terms.value,
        std::move(cylinders),
        capture_plan.physical_gas_volume_ids,
        true,
    });
    if (const auto *error =
            std::get_if<OperatingCycleAccountingError>(&accountant_result)) {
        report.add(ContractIssueCode::unsupported_value, "engine.physics_profile",
                   "free-engine variable-speed accountant rejected the admitted "
                   "profile; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    auto sampling_result = compile_fixed_horizon_cycle_sampler({
        preparation->method.value,
        preparation->trailing_complete_cycle_count.value,
        preparation->fixed_preparation_horizon_s.value,
        capture_plan.physical_gas_volume_ids,
    });
    if (const auto *error =
            std::get_if<FixedHorizonCycleSamplingError>(&sampling_result)) {
        report.add(ContractIssueCode::unsupported_value, "scenario.preparation",
                   "free-engine fixed-horizon sampler rejected preparation; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    return LowOrderFreeEngineV1Runtime{
        control_schedule.fresh_cursor(),
        std::get<OperatingCycleAccountant>(std::move(accountant_result)),
        std::get<FixedHorizonCycleSampler>(std::move(sampling_result)),
        std::move(physical_gas_step_indices),
        std::move(pressure_samples),
        scenario.rates.physics,
        execution_extent,
        *release_frame,
        free_engine->initial_engine_speed_rpm.value,
        free_engine->initial_theta_rad.value,
        free_engine->total_equivalent_inertia_kg_m2.value,
        crank_friction->torque_nm,
        "low-order-free-engine-v1",
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
