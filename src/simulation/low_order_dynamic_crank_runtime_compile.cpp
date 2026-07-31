#include "simulation/low_order_dynamic_crank_runtime.hpp"

#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/engine_sim_v1_transient_friction.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/free_vehicle_method_registry.hpp"
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

template <typename Point>
void require_release_or_later_control_boundaries(ValidationReport &report,
                                                 const std::vector<Point> &points,
                                                 const contract::RationalRateHz &rate,
                                                 std::uint64_t release_frame,
                                                 const std::string &path,
                                                 const std::string &lane_name) {
    for (std::size_t index = 1; index < points.size(); ++index) {
        const auto boundary = contract::resolve_frame_index(points[index].time_s, rate);
        require(report, boundary.has_value() && *boundary >= release_frame,
                ContractIssueCode::unsupported_value,
                path + ".value[" + std::to_string(index) + "].time_s",
                lane_name +
                    " transitions must occur at or after the fixed held-preparation "
                    "release frame");
    }
}

} // namespace

LowOrderDynamicCrankCompileResult compile_low_order_dynamic_crank_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256,
    LowOrderExecutionExtent execution_extent) {
    ValidationReport report;
    report.append(contract::validate_for_engine(scenario, engine));

    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *free_engine = std::get_if<contract::FreeEngine>(&scenario.mode);
    const auto *held_dyno = std::get_if<contract::HeldDyno>(&scenario.mode);
    const auto *free_vehicle = std::get_if<contract::FreeVehicle>(&scenario.mode);
    const auto *fixed_horizon =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    const auto *fixed_settling =
        std::get_if<contract::FixedSettling>(&scenario.preparation);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "dynamic-crank runtime requires low_order_operating_point_v1");
    const std::uint32_t dynamic_mode_count =
        static_cast<std::uint32_t>(free_engine != nullptr) +
        static_cast<std::uint32_t>(held_dyno != nullptr) +
        static_cast<std::uint32_t>(free_vehicle != nullptr);
    require(report, dynamic_mode_count == 1U, ContractIssueCode::unsupported_value,
            "scenario.mode",
            "dynamic crank runtime requires exactly one FreeEngine, HeldDyno, or "
            "FreeVehicle mode");
    require(report, !simulation_request_identity_v3_sha256.is_zero(),
            ContractIssueCode::missing_value, "simulation_request_identity_v3_sha256",
            "dynamic-crank runtime requires the canonical nonzero request identity");
    if (profile == nullptr || dynamic_mode_count != 1U) {
        return report;
    }
    const double initial_engine_speed_rpm =
        free_engine != nullptr    ? free_engine->initial_engine_speed_rpm.value
        : free_vehicle != nullptr ? free_vehicle->initial_engine_speed_rpm.value
                                  : held_dyno->initial_engine_speed_rpm.value;
    const double initial_theta_rad =
        free_engine != nullptr    ? free_engine->initial_theta_rad.value
        : free_vehicle != nullptr ? free_vehicle->initial_theta_rad.value
                                  : held_dyno->initial_theta_rad.value;
    const double attached_inertia_kg_m2 =
        free_engine != nullptr ? free_engine->attached_inertia_kg_m2.value : 0.0;
    const auto &throttle = free_engine != nullptr    ? free_engine->throttle_01
                           : free_vehicle != nullptr ? free_vehicle->throttle_01
                                                     : held_dyno->throttle_01;
    const bool cold_bootstrap = (free_engine != nullptr || free_vehicle != nullptr) &&
                                initial_engine_speed_rpm == 0.0;
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
                ? "zero-speed dynamic-crank runtime requires canonical zero-duration "
                  "fixed settling"
                : "positive-speed dynamic-crank runtime requires fixed-horizon cycle "
                  "sampling");

    const auto crank_friction_calculation =
        calculate_engine_sim_v1_positive_speed_crank_friction(
            {profile->core.mechanism.crank.running_friction_torque_magnitude_nm.value});
    const auto *crank_friction =
        std::get_if<EngineSimV1PositiveSpeedCrankFriction>(&crank_friction_calculation);
    require(report, crank_friction != nullptr, ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank."
            "running_friction_torque_magnitude_nm.value",
            "dynamic-crank runtime requires finite nonnegative pristine crank "
            "friction");

    report.append(admit_implemented_cycle_accounting_methods(engine, *profile));
    require(
        report,
        free_engine == nullptr ||
            free_engine->crank_dynamics_method.value ==
                nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
        ContractIssueCode::unsupported_value,
        "scenario.mode.crank_dynamics_method.value",
        "dynamic-crank runtime requires its exact nonnegative-speed "
        "centered-slider crank method identity");
    require(
        report,
        free_vehicle == nullptr ||
            free_vehicle->crank_dynamics_method.value ==
                nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
        ContractIssueCode::unsupported_value,
        "scenario.mode.crank_dynamics_method.value",
        "FreeVehicle runtime requires the exact nonnegative-speed centered-slider "
        "crank method identity");
    require(report,
            free_vehicle == nullptr || free_vehicle->road_load_method.value ==
                                           forward_vehicle_road_load_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.road_load_method.value",
            "FreeVehicle runtime requires the exact forward road-load method "
            "identity");
    require(report,
            free_vehicle == nullptr || free_vehicle->clutch_coupling_method.value ==
                                           bounded_clutch_coupling_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.clutch_coupling_method.value",
            "FreeVehicle runtime requires the exact bounded clutch method identity");
    require(report,
            free_vehicle == nullptr ||
                free_vehicle->drivetrain_dynamics_method.value ==
                    bounded_forward_vehicle_drivetrain_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.drivetrain_dynamics_method.value",
            "FreeVehicle runtime requires the exact fixed-128-pass coupled drivetrain "
            "method identity");
    require(report,
            held_dyno == nullptr || held_dyno->constraint_method.value ==
                                        bounded_held_dyno_constraint_method_identity(),
            ContractIssueCode::unsupported_value,
            "scenario.mode.constraint_method.value",
            "held-dyno runtime requires the exact bounded speed-constraint method "
            "identity");
    if (fixed_horizon != nullptr) {
        require(report,
                fixed_horizon->method.value ==
                    contract::fixed_horizon_cycle_sampling_method_identity(),
                ContractIssueCode::unsupported_value,
                "scenario.preparation.method.value",
                "positive-speed dynamic-crank runtime requires the exact implemented "
                "fixed-horizon sampling method");
    }
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "dynamic-crank runtime requires identical physics and capture clocks");
    require(report,
            capture_plan.engine_profile_id == engine.profile_id.value &&
                capture_plan.scenario_id == scenario.scenario_id &&
                capture_plan.capture_buffer.engine_id == engine.id &&
                capture_plan.capture_buffer.rate == scenario.rates.capture &&
                capture_plan.execution_extent == execution_extent,
            ContractIssueCode::inconsistent_semantics, "capture_plan",
            "dynamic-crank capture plan belongs to another engine or scenario");
    require(report, execution_extent.valid(), ContractIssueCode::invalid_value,
            "execution_extent",
            "dynamic-crank runtime requires a valid finite or open-ended execution "
            "extent");
    require(report, held_dyno == nullptr || !execution_extent.is_open_ended(),
            ContractIssueCode::unsupported_value, "execution_extent",
            "held-dyno execution requires a finite authored target lane");
    require(report, free_vehicle == nullptr || !execution_extent.is_open_ended(),
            ContractIssueCode::unsupported_value, "execution_extent",
            "FreeVehicle execution requires a finite authored horizon");

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
            "dynamic-crank release, audible start, and horizon must resolve to ordered "
            "integral physics frames matching capture; cold bootstrap releases at "
            "frame zero");
    if (!report.ok() || crank_friction == nullptr || !release_frame_valid ||
        !end_frame.has_value() || throttle.points.empty() ||
        (free_engine != nullptr &&
         free_engine->external_resisting_torque_nm.points.empty())) {
        return report;
    }

    require_release_or_later_boundaries(
        report, throttle, scenario.rates.physics, release_frame_index,
        "scenario.mode.throttle_01", "dynamic crank throttle");
    if (free_engine != nullptr) {
        require_release_or_later_boundaries(
            report, free_engine->external_resisting_torque_nm, scenario.rates.physics,
            release_frame_index, "scenario.mode.external_resisting_torque_nm",
            "FreeEngine external resisting-torque");
    }
    if (free_vehicle != nullptr) {
        require_release_or_later_control_boundaries(
            report, free_vehicle->selected_gear.value, scenario.rates.physics,
            release_frame_index, "scenario.mode.selected_gear",
            "FreeVehicle selected-gear");
        require_release_or_later_control_boundaries(
            report, free_vehicle->clutch_engagement_01.value, scenario.rates.physics,
            release_frame_index, "scenario.mode.clutch_engagement_01",
            "FreeVehicle clutch");
        require_release_or_later_control_boundaries(
            report, free_vehicle->service_brake_application_01.value,
            scenario.rates.physics, release_frame_index,
            "scenario.mode.service_brake_application_01", "FreeVehicle service brake");
    }
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
                "dynamic-crank chamber binding references an unknown cylinder");
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
                "dynamic-crank runtime requires bit-identical cylinder strokes");
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
                "dynamic-crank physical pressure volume is absent from capture "
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
            "dynamic-crank configuration-dependent inertia rejected the admitted "
            "centered-slider mechanism");
    if (cycle_mean_inertia != nullptr &&
        (free_engine != nullptr || free_vehicle != nullptr)) {
        const double authored_engine_baseline_inertia_kg_m2 =
            free_engine != nullptr ? free_engine->engine_baseline_inertia_kg_m2.value
                                   : free_vehicle->engine_baseline_inertia_kg_m2.value;
        require(report,
                std::bit_cast<std::uint64_t>(authored_engine_baseline_inertia_kg_m2) ==
                    std::bit_cast<std::uint64_t>(
                        cycle_mean_inertia->engine_equivalent_inertia_kg_m2),
                ContractIssueCode::inconsistent_semantics,
                "scenario.mode.engine_baseline_inertia_kg_m2.value",
                "dynamic-crank cycle-mean inertia reference differs from the compiled "
                "engine mechanism");
    }

    CenteredSliderCrankConfigurationInertiaPlan configuration_inertia_plan{
        mechanism.crank.authored_crank_inertia_kg_m2.value,
        attached_inertia_kg_m2,
        {},
    };
    configuration_inertia_plan.cylinders.reserve(mechanism.cylinders.size());
    std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders;
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
            initial_theta_rad, initial_engine_speed_rpm * kLegacyRpmScale);
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
            legacy_wrap_2pi(initial_theta_rad - geometric_tdc_rad),
            initial_engine_speed_rpm * kLegacyRpmScale,
            initial_chamber_pressure_pa_abs,
            0.0,
        });
        require(report, chamber_gas_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                    "].topology.chamber_volume_id",
                "dynamic-crank piston-wall cylinder chamber is absent from the "
                "captured gas transaction");
        require(
            report,
            std::holds_alternative<EngineSimV1PistonWallFrictionStage>(initial_stage),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                "].parameters",
            "dynamic-crank piston-wall source law rejected the resolved "
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
            "dynamic-crank configuration-inertia inventory must cover every "
            "mechanism cylinder exactly once");
    require(report,
            !piston_wall_cylinders.empty() &&
                piston_wall_cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "dynamic-crank piston-wall inventory must cover every mechanism "
            "cylinder exactly once");
    if (!report.ok()) {
        return report;
    }
    const auto initial_configuration_inertia =
        evaluate_centered_slider_crank_configuration_inertia(configuration_inertia_plan,
                                                             initial_theta_rad);
    require(report,
            std::holds_alternative<CenteredSliderCrankConfigurationInertia>(
                initial_configuration_inertia),
            ContractIssueCode::invalid_value, "scenario.mode.initial_theta_rad.value",
            "dynamic-crank configuration inertia rejected the initial crank "
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
            initial_engine_speed_rpm,
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
                       "dynamic-crank variable-speed accountant rejected the admitted "
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
            report.add(
                ContractIssueCode::unsupported_value, "scenario.preparation",
                "dynamic-crank fixed-horizon sampler rejected preparation; code=" +
                    std::to_string(static_cast<std::uint32_t>(error->code)));
            return report;
        }
        sampler.emplace(std::get<FixedHorizonCycleSampler>(std::move(sampling_result)));
    }

    std::optional<HeldDynoMotionPlan> held_dyno_motion;
    if (held_dyno != nullptr) {
        held_dyno_motion.emplace(HeldDynoMotionPlan{
            held_dyno->target_engine_speed_rpm.post_step_rpm,
            held_dyno->maximum_absorbing_torque_nm.value,
            held_dyno->maximum_driving_torque_nm.value,
            std::nullopt,
        });
    }

    std::optional<FreeVehicleMotionPlan> free_vehicle_motion;
    if (free_vehicle != nullptr) {
        const auto &vehicle = free_vehicle->rig.vehicle;
        const auto &transmission = free_vehicle->rig.transmission;
        FreeVehicleMotionPlan plan;
        plan.vehicle_mass_kg = vehicle.mass_kg.value;
        plan.drag_coefficient = vehicle.drag_coefficient.value;
        plan.frontal_area_m2 = vehicle.frontal_area_m2.value;
        plan.differential_ratio = vehicle.differential_ratio.value;
        plan.tire_radius_m = vehicle.tire_radius_m.value;
        plan.rolling_resistance_force_n = vehicle.rolling_resistance_force_n.value;
        plan.maximum_service_brake_force_n =
            vehicle.maximum_service_brake_force_n.has_value()
                ? vehicle.maximum_service_brake_force_n->value
                : 0.0;
        plan.maximum_clutch_torque_nm = transmission.maximum_clutch_torque_nm.value;
        plan.vehicle_speed_m_s = free_vehicle->initial_vehicle_speed_m_s.value;

        plan.gears.reserve(transmission.gears.size());
        for (std::size_t index = 0; index < transmission.gears.size(); ++index) {
            const auto &gear = transmission.gears[index];
            const auto calculation = detail::calculate_forward_gear_reduction({
                vehicle.mass_kg.value,
                gear.ratio.value,
                vehicle.differential_ratio.value,
                vehicle.tire_radius_m.value,
            });
            const auto *reduction =
                std::get_if<detail::ForwardGearReduction>(&calculation);
            require(report, reduction != nullptr, ContractIssueCode::invalid_value,
                    "scenario.mode.rig.transmission.gears[" + std::to_string(index) +
                        "].ratio.value",
                    "FreeVehicle forward reduction rejected the resolved vehicle "
                    "and gear geometry");
            if (reduction != nullptr) {
                plan.gears.push_back({gear.id, *reduction});
            }
        }
        if (!report.ok()) {
            return report;
        }

        plan.selected_gear.reserve(free_vehicle->selected_gear.value.size());
        for (std::size_t index = 0; index < free_vehicle->selected_gear.value.size();
             ++index) {
            const auto &point = free_vehicle->selected_gear.value[index];
            const auto step =
                contract::resolve_frame_index(point.time_s, scenario.rates.physics);
            std::optional<std::size_t> gear_index;
            if (point.gear_id.has_value()) {
                const auto found = std::ranges::find(
                    plan.gears, *point.gear_id, &FreeVehicleGearMotionPlan::gear_id);
                require(report, found != plan.gears.end(),
                        ContractIssueCode::dangling_reference,
                        "scenario.mode.selected_gear.value[" + std::to_string(index) +
                            "].gear_id",
                        "compiled FreeVehicle gear event references no forward "
                        "reduction");
                if (found != plan.gears.end()) {
                    gear_index = static_cast<std::size_t>(found - plan.gears.begin());
                }
            }
            require(report, step.has_value(), ContractIssueCode::inconsistent_semantics,
                    "scenario.mode.selected_gear.value[" + std::to_string(index) +
                        "].time_s",
                    "FreeVehicle gear boundary must resolve to an integral physics "
                    "frame");
            if (step.has_value()) {
                plan.selected_gear.push_back({*step, gear_index});
            }
        }

        const auto compile_scalar_lane =
            [&](const std::vector<contract::ScalarControlPoint> &points,
                const std::string &path,
                std::vector<FreeVehicleScalarControlBoundary> &output) {
                output.reserve(points.size());
                for (std::size_t index = 0; index < points.size(); ++index) {
                    const auto step = contract::resolve_frame_index(
                        points[index].time_s, scenario.rates.physics);
                    require(report, step.has_value(),
                            ContractIssueCode::inconsistent_semantics,
                            path + ".value[" + std::to_string(index) + "].time_s",
                            "FreeVehicle control boundary must resolve to an integral "
                            "physics frame");
                    if (step.has_value()) {
                        output.push_back({*step, points[index].value});
                    }
                }
            };
        compile_scalar_lane(free_vehicle->clutch_engagement_01.value,
                            "scenario.mode.clutch_engagement_01",
                            plan.clutch_engagement_01);
        compile_scalar_lane(free_vehicle->service_brake_application_01.value,
                            "scenario.mode.service_brake_application_01",
                            plan.service_brake_application_01);
        require(report,
                !plan.selected_gear.empty() && !plan.clutch_engagement_01.empty() &&
                    !plan.service_brake_application_01.empty(),
                ContractIssueCode::missing_value, "scenario.mode",
                "FreeVehicle runtime requires nonempty gear, clutch, and service "
                "brake lanes");
        if (!report.ok()) {
            return report;
        }
        plan.current_gear_index = plan.selected_gear.front().gear_index;
        plan.current_clutch_engagement_01 = plan.clutch_engagement_01.front().value;
        plan.current_service_brake_application_01 =
            plan.service_brake_application_01.front().value;
        free_vehicle_motion.emplace(std::move(plan));
    }

    return LowOrderDynamicCrankRuntime{
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
        initial_engine_speed_rpm,
        initial_theta_rad,
        cold_bootstrap,
        crank_friction->torque_nm,
        profile->starter.maximum_torque_nm.value,
        profile->starter.target_speed_rad_s.value,
        std::move(held_dyno_motion),
        std::move(free_vehicle_motion),
        held_dyno != nullptr      ? "low-order-held-dyno"
        : free_vehicle != nullptr ? "low-order-free-vehicle-v1"
                                  : "low-order-free-engine-v1",
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
