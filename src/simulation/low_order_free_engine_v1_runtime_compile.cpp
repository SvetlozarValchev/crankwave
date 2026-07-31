#include "simulation/low_order_free_engine_v1_runtime.hpp"

#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/engine_sim_v1_transient_friction.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

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
    const auto *fixed_horizon =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    const auto *fixed_settling =
        std::get_if<contract::FixedSettling>(&scenario.preparation);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "free-engine runtime requires low_order_operating_point_v1");
    require(report, free_engine != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode", "free-engine runtime requires FreeEngine mode");
    require(report, !simulation_request_identity_v3_sha256.is_zero(),
            ContractIssueCode::missing_value, "simulation_request_identity_v3_sha256",
            "free-engine runtime requires the canonical nonzero request identity");
    if (profile == nullptr || free_engine == nullptr) {
        return report;
    }
    const bool cold_bootstrap = free_engine->initial_engine_speed_rpm.value == 0.0;
    require(report,
            cold_bootstrap
                ? fixed_settling != nullptr &&
                      fixed_settling->warm_up_duration_s.value == 0.0 &&
                      !std::signbit(fixed_settling->warm_up_duration_s.value) &&
                      fixed_settling->settling_duration_s.value == 0.0 &&
                      !std::signbit(fixed_settling->settling_duration_s.value)
                : fixed_horizon != nullptr,
            ContractIssueCode::unsupported_value, "scenario.preparation",
            cold_bootstrap
                ? "zero-speed free-engine runtime requires canonical zero-duration "
                  "fixed settling"
                : "positive-speed free-engine runtime requires fixed-horizon cycle "
                  "sampling");

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
                nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.crank_dynamics_method.value",
            "free-engine runtime requires its exact nonnegative-speed "
            "centered-slider crank method identity");
    if (fixed_horizon != nullptr) {
        require(report,
                fixed_horizon->method.value ==
                    contract::fixed_horizon_cycle_sampling_method_identity(),
                ContractIssueCode::unsupported_value,
                "scenario.preparation.method.value",
                "positive-speed free-engine runtime requires the exact implemented "
                "fixed-horizon sampling method");
    }
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

    const std::optional<double> release_time_s =
        cold_bootstrap ? std::optional<double>{0.0}
        : fixed_horizon != nullptr
            ? std::optional<double>{fixed_horizon->fixed_preparation_horizon_s.value}
            : std::nullopt;
    std::uint64_t release_frame_index = 0U;
    bool release_frame_valid = false;
    if (release_time_s.has_value()) {
        if (const auto resolved =
                contract::resolve_frame_index(*release_time_s, scenario.rates.physics);
            resolved.has_value()) {
            release_frame_index = *resolved;
            release_frame_valid = true;
        }
    }
    const auto audible_start_frame = contract::resolve_frame_index(
        scenario.audible_start_s.value, scenario.rates.physics);
    const auto end_frame = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    const auto finite_execution = execution_extent.finite_physics_frame_count();
    bool ordered_frame_grid = false;
    if (release_frame_valid && audible_start_frame.has_value() &&
        end_frame.has_value()) {
        ordered_frame_grid =
            release_frame_index <= *audible_start_frame &&
            *end_frame > release_frame_index &&
            (cold_bootstrap ? release_frame_index == 0U : release_frame_index > 0U) &&
            (!finite_execution.has_value() || *finite_execution == *end_frame);
    }
    require(report, ordered_frame_grid, ContractIssueCode::inconsistent_semantics,
            "scenario.preparation.fixed_preparation_horizon_s.value",
            "free-engine release, audible start, and horizon must resolve to ordered "
            "integral physics frames matching capture; cold bootstrap releases at "
            "frame zero");
    if (!report.ok() || crank_friction == nullptr || !release_frame_valid ||
        !end_frame.has_value() || free_engine->throttle_01.points.empty() ||
        free_engine->external_resisting_torque_nm.points.empty()) {
        return report;
    }

    require_release_or_later_boundaries(
        report, free_engine->throttle_01, scenario.rates.physics, release_frame_index,
        "scenario.mode.throttle_01", "free-engine throttle");
    require_release_or_later_boundaries(
        report, free_engine->external_resisting_torque_nm, scenario.rates.physics,
        release_frame_index, "scenario.mode.external_resisting_torque_nm",
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

    const auto &mechanism = profile->core.mechanism;
    const auto cycle_mean_inertia_calculation =
        calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto *cycle_mean_inertia = std::get_if<CenteredSliderCrankCycleMeanInertia>(
        &cycle_mean_inertia_calculation);
    require(report, cycle_mean_inertia != nullptr, ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism",
            "free-engine configuration-dependent inertia rejected the admitted "
            "centered-slider mechanism");
    if (cycle_mean_inertia != nullptr) {
        require(report,
                std::bit_cast<std::uint64_t>(
                    free_engine->engine_baseline_inertia_kg_m2.value) ==
                    std::bit_cast<std::uint64_t>(
                        cycle_mean_inertia->engine_equivalent_inertia_kg_m2),
                ContractIssueCode::inconsistent_semantics,
                "scenario.mode.engine_baseline_inertia_kg_m2.value",
                "free-engine cycle-mean inertia reference differs from the compiled "
                "engine mechanism");
    }

    CenteredSliderCrankConfigurationInertiaPlan configuration_inertia_plan{
        mechanism.crank.authored_crank_inertia_kg_m2.value,
        free_engine->attached_inertia_kg_m2.value,
        {},
    };
    configuration_inertia_plan.cylinders.reserve(mechanism.cylinders.size());
    std::vector<LowOrderFreeEngineV1PistonWallCylinderPlan> piston_wall_cylinders;
    piston_wall_cylinders.reserve(mechanism.cylinders.size());
    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto chamber_gas_index = find_capture_volume_index(
            capture_plan, assembly.topology.chamber_volume_id);
        const auto geometry = derive_legacy_cylinder_geometry(
            parameters.bore_m.value, parameters.crank_radius_m.value,
            parameters.connecting_rod_length_m.value, parameters.deck_height_m.value,
            parameters.piston_compression_height_m.value,
            parameters.head_chamber_volume_m3.value,
            parameters.piston_displacement_term_m3.value);
        const double geometric_tdc_rad =
            legacy_wrap_2pi(mechanism.crank.crank_tdc_reference_rad.value +
                            parameters.journal_angle_rad.value - kLegacyPi / 2.0);
        const auto initial_mechanism = evaluate_centered_slider_crank(
            {
                assembly.topology.cylinder_id,
                geometric_tdc_rad,
                geometry.piston_area_m2,
                parameters.crank_radius_m.value,
                parameters.connecting_rod_length_m.value,
                geometry.clearance_volume_m3,
                parameters.ignition_wire_angle_rad.value,
            },
            free_engine->initial_theta_rad.value,
            free_engine->initial_engine_speed_rpm.value * kLegacyRpmScale);
        const double initial_chamber_pressure_pa_abs =
            initial_mechanism.valid
                ? legacy_gas_pressure_pa(legacy_initialize_gas_cell(
                      scenario.ambient.pressure_pa_abs.value,
                      initial_mechanism.chamber_volume_m3,
                      scenario.initial_thermal_state.gas_temperature_k.value,
                      LegacyGasMixture{0.0, 1.0, 0.0}))
                : 0.0;
        const EngineSimV1PistonWallCylinderPlan friction_plan{
            geometry.piston_area_m2,
            parameters.crank_radius_m.value,
            parameters.connecting_rod_length_m.value,
            parameters.piston_mass_kg.value,
            parameters.connecting_rod_mass_kg.value,
            parameters.connecting_rod_inertia_kg_m2.value,
            scenario.crankcase.pressure_pa_abs.value,
        };
        const auto initial_stage = stage_engine_sim_v1_piston_wall_friction({
            friction_plan,
            legacy_wrap_2pi(free_engine->initial_theta_rad.value - geometric_tdc_rad),
            free_engine->initial_engine_speed_rpm.value * kLegacyRpmScale,
            initial_chamber_pressure_pa_abs,
            0.0,
        });
        require(report, chamber_gas_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                    "].topology.chamber_volume_id",
                "free-engine piston-wall cylinder chamber is absent from the "
                "captured gas transaction");
        require(
            report,
            std::holds_alternative<EngineSimV1PistonWallFrictionStage>(initial_stage),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                "].parameters",
            "free-engine piston-wall source law rejected the resolved "
            "centered-slider mechanism");
        if (chamber_gas_index.has_value() &&
            std::holds_alternative<EngineSimV1PistonWallFrictionStage>(initial_stage)) {
            configuration_inertia_plan.cylinders.push_back({
                geometric_tdc_rad,
                parameters.crank_radius_m.value,
                parameters.connecting_rod_length_m.value,
                parameters.piston_mass_kg.value,
                parameters.connecting_rod_mass_kg.value,
                parameters.connecting_rod_inertia_kg_m2.value,
            });
            piston_wall_cylinders.push_back({
                assembly.topology.cylinder_id,
                assembly.topology.chamber_volume_id,
                index,
                *chamber_gas_index,
                geometric_tdc_rad,
                initial_chamber_pressure_pa_abs,
                friction_plan,
            });
        }
    }
    require(report,
            configuration_inertia_plan.cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "free-engine configuration-inertia inventory must cover every "
            "mechanism cylinder exactly once");
    require(report,
            !piston_wall_cylinders.empty() &&
                piston_wall_cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "free-engine piston-wall inventory must cover every mechanism "
            "cylinder exactly once");
    if (!report.ok()) {
        return report;
    }
    const auto initial_configuration_inertia =
        evaluate_centered_slider_crank_configuration_inertia(
            configuration_inertia_plan, free_engine->initial_theta_rad.value);
    require(report,
            std::holds_alternative<CenteredSliderCrankConfigurationInertia>(
                initial_configuration_inertia),
            ContractIssueCode::invalid_value, "scenario.mode.initial_theta_rad.value",
            "free-engine configuration inertia rejected the initial crank "
            "boundary");
    if (!report.ok()) {
        return report;
    }

    std::optional<OperatingCycleAccountant> accountant;
    std::optional<FixedHorizonCycleSampler> sampler;
    if (!cold_bootstrap) {
        auto accountant_result = compile_operating_cycle_accountant({
            {
                profile->core.mechanism.crank.crank_tdc_reference_rad.value,
                engine.total_displacement_m3.value,
            },
            {
                profile->aggregate_loss.constant_fmep_bar.value,
                profile->aggregate_loss.peak_pressure_coefficient.value,
                profile->aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value,
                profile->aggregate_loss
                    .mean_piston_speed_squared_coefficient_bar_s2_per_m2.value,
            },
            free_engine->initial_engine_speed_rpm.value,
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
                       "free-engine variable-speed accountant rejected the admitted "
                       "profile; code=" +
                           std::to_string(static_cast<std::uint32_t>(error->code)));
            return report;
        }
        accountant.emplace(
            std::get<OperatingCycleAccountant>(std::move(accountant_result)));

        auto sampling_result = compile_fixed_horizon_cycle_sampler({
            fixed_horizon->method.value,
            fixed_horizon->trailing_complete_cycle_count.value,
            fixed_horizon->fixed_preparation_horizon_s.value,
            capture_plan.physical_gas_volume_ids,
        });
        if (const auto *error =
                std::get_if<FixedHorizonCycleSamplingError>(&sampling_result)) {
            report.add(ContractIssueCode::unsupported_value, "scenario.preparation",
                       "free-engine fixed-horizon sampler rejected preparation; code=" +
                           std::to_string(static_cast<std::uint32_t>(error->code)));
            return report;
        }
        sampler.emplace(std::get<FixedHorizonCycleSampler>(std::move(sampling_result)));
    }

    return LowOrderFreeEngineV1Runtime{
        control_schedule.fresh_cursor(),
        std::move(accountant),
        std::move(sampler),
        std::move(physical_gas_step_indices),
        std::move(pressure_samples),
        std::move(configuration_inertia_plan),
        std::move(piston_wall_cylinders),
        scenario.rates.physics,
        execution_extent,
        release_frame_index,
        free_engine->initial_engine_speed_rpm.value,
        free_engine->initial_theta_rad.value,
        cold_bootstrap,
        crank_friction->torque_nm,
        profile->starter.maximum_torque_nm.value,
        profile->starter.target_speed_rad_s.value,
        "low-order-free-engine-v1",
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
