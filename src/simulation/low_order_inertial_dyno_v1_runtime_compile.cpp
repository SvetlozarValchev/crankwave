#include "simulation/low_order_inertial_dyno_v1_runtime.hpp"

#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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

[[nodiscard]] contract::RenderScenario
preparation_scenario(const contract::RenderScenario &scenario,
                     const contract::InertialDyno &dyno) {
    auto held = scenario;
    held.mode = contract::HeldSpeed{
        dyno.initial_engine_speed_rpm,
        dyno.initial_theta_rad,
        {dyno.throttle_01.points.front().value, dyno.throttle_01.resolution_id},
    };
    return held;
}

} // namespace

LowOrderInertialDynoV1CompileResult compile_low_order_inertial_dyno_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v6_sha256) {
    ValidationReport report;
    report.append(contract::validate_for_engine(scenario, engine));

    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *dyno = std::get_if<contract::InertialDyno>(&scenario.mode);
    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "inertial runtime requires low_order_operating_point_v1");
    require(report, dyno != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode", "inertial runtime requires inertial-dyno mode");
    require(report, preparation != nullptr, ContractIssueCode::unsupported_value,
            "scenario.preparation",
            "inertial runtime requires fixed-horizon cycle sampling");
    require(report, !simulation_request_identity_v6_sha256.is_zero(),
            ContractIssueCode::missing_value, "simulation_request_identity_v6_sha256",
            "inertial runtime requires the canonical nonzero request identity");
    if (profile == nullptr || dyno == nullptr || preparation == nullptr) {
        return report;
    }
    const auto *output_crank = contract::find_output_crank(profile->core.mechanism);
    require(report, output_crank != nullptr, ContractIssueCode::dangling_reference,
            "engine.physics_profile.mechanism.output_crankshaft_id",
            "inertial runtime requires one resolved output crankshaft");
    if (output_crank == nullptr) {
        return report;
    }

    report.append(admit_implemented_cycle_accounting_methods(engine, *profile));
    require(report,
            dyno->crank_dynamics_method.value ==
                rigid_crank_zoh_work_energy_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.crank_dynamics_method.value",
            "inertial runtime requires its exact rigid-crank method identity");
    require(report,
            dyno->brake_torque_method.value ==
                piecewise_linear_positive_speed_passive_brake_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.brake_torque_method.value",
            "inertial runtime requires its exact passive-brake method identity");
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "inertial runtime requires identical physics and capture clocks");
    require(report,
            capture_plan.engine_profile_id == engine.profile_id.value &&
                capture_plan.scenario_id == scenario.scenario_id,
            ContractIssueCode::inconsistent_semantics, "capture_plan",
            "inertial capture plan belongs to another engine or scenario");

    const auto release_frame = contract::resolve_frame_index(
        scenario.audible_start_s.value, scenario.rates.physics);
    const auto end_frame = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    const auto capture_horizon =
        capture_plan.execution_extent.finite_physics_frame_count();
    require(report,
            release_frame.has_value() && *release_frame > 0U && end_frame.has_value() &&
                *end_frame > *release_frame && capture_horizon.has_value() &&
                *capture_horizon == *end_frame,
            ContractIssueCode::inconsistent_semantics, "scenario.audible_start_s",
            "inertial release and fixed horizon must resolve to ordered integral "
            "physics frames matching capture");
    if (!report.ok() || !release_frame.has_value() || !end_frame.has_value() ||
        dyno->throttle_01.points.empty()) {
        return report;
    }
    for (std::size_t index = 1; index < dyno->throttle_01.points.size(); ++index) {
        const auto boundary = contract::resolve_frame_index(
            dyno->throttle_01.points[index].time_s, scenario.rates.physics);
        require(report, boundary.has_value() && *boundary >= *release_frame,
                ContractIssueCode::unsupported_value,
                "scenario.mode.throttle_01.points[" + std::to_string(index) +
                    "].time_s",
                "inertial throttle transitions must occur at or after the held "
                "preparation release frame");
    }
    if (!report.ok()) {
        return report;
    }

    auto held_scenario = preparation_scenario(scenario, *dyno);
    auto preparation_result = compile_low_order_operating_point_v1_runtime(
        engine, held_scenario, capture_plan, simulation_request_identity_v6_sha256);
    if (auto *nested = std::get_if<ValidationReport>(&preparation_result)) {
        return std::move(*nested);
    }

    std::vector<OperatingCylinderAccountingPlan> cylinders;
    cylinders.reserve(capture_plan.cylinder_chambers.size());
    double stroke_m = 0.0;
    for (std::size_t index = 0; index < capture_plan.cylinder_chambers.size();
         ++index) {
        const auto &binding = capture_plan.cylinder_chambers[index];
        const auto *cylinder = find_cylinder(engine, binding.cylinder_id);
        require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                "capture_plan.cylinder_chambers[" + std::to_string(index) + "]",
                "inertial chamber binding references an unknown cylinder");
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
                "inertial runtime requires bit-identical cylinder strokes");
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
                "inertial physical pressure volume is absent from capture topology");
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
            output_crank->crank_tdc_reference_rad.value,
            engine.total_displacement_m3.value,
        },
        {
            profile->aggregate_loss.constant_fmep_bar.value,
            profile->aggregate_loss.peak_pressure_coefficient.value,
            profile->aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value,
            profile->aggregate_loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2
                .value,
        },
        dyno->initial_engine_speed_rpm.value,
        stroke_m,
        true,
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
                   "variable-speed accountant rejected the admitted profile; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    InertialCrankDynamicsConfiguration dynamics_configuration;
    dynamics_configuration.equivalent_inertia_kg_m2 =
        dyno->equivalent_inertia_kg_m2.value;
    dynamics_configuration.passive_brake_curve.reserve(dyno->brake_curve.size());
    for (const auto &point : dyno->brake_curve) {
        dynamics_configuration.passive_brake_curve.push_back(
            {point.angular_speed_rad_s, point.resisting_torque_nm});
    }
    auto dynamics_result =
        compile_inertial_crank_dynamics(std::move(dynamics_configuration));
    if (const auto *error = std::get_if<InertialCrankDynamicsError>(&dynamics_result)) {
        report.add(ContractIssueCode::unsupported_value, "scenario.mode.brake_curve",
                   "inertial crank compiler rejected the requested dynamics; code=" +
                       std::to_string(static_cast<std::uint32_t>(error->code)));
        return report;
    }

    return LowOrderInertialDynoV1Runtime{
        std::get<LowOrderOperatingPointV1Runtime>(std::move(preparation_result)),
        std::get<OperatingCycleAccountant>(std::move(accountant_result)),
        std::get<InertialCrankDynamics>(std::move(dynamics_result)),
        std::move(physical_gas_step_indices),
        std::move(pressure_samples),
        scenario.rates.physics,
        *end_frame,
        *release_frame,
        dyno->initial_engine_speed_rpm.value,
        dyno->initial_theta_rad.value,
        dyno->target_engine_speed_rpm.value,
        simulation_request_identity_v6_sha256,
        dyno->brake_curve_resolution_id,
        dyno->brake_torque_method.value,
        dyno->crank_dynamics_method.value,
        "low-order-inertial-dyno-v1",
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
