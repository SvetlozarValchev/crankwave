#include "engine_sim_offline/contract/scenario.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

void validate_resolution_id(ValidationReport &report, const std::string &resolution_id,
                            const ProvenanceLedger &provenance,
                            const std::string &field_path,
                            const std::string &parameter_path) {
    detail::require(report, is_valid_semantic_id(resolution_id),
                    ContractIssueCode::invalid_value, field_path,
                    "resolution ID must be a canonical semantic ID");
    const auto *resolution = detail::find_resolution(provenance, resolution_id);
    detail::require(report, resolution != nullptr,
                    ContractIssueCode::dangling_reference, field_path,
                    "resolution ID is not present in the provenance ledger");
    if (resolution != nullptr) {
        detail::require(report, resolution->parameter_path == parameter_path,
                        ContractIssueCode::inconsistent_semantics, field_path,
                        "resolution ID belongs to a different resolved field");
    }
}

void validate_trajectory(ValidationReport &report, const ScalarTrajectory &trajectory,
                         const ProvenanceLedger &provenance, double total_duration_s,
                         bool unit_interval_values, bool nonnegative_values,
                         const std::string &path) {
    using detail::finite;
    using detail::require;

    validate_resolution_id(report, trajectory.resolution_id, provenance,
                           path + ".resolution_id", path);
    require(report, !trajectory.points.empty(), ContractIssueCode::missing_value,
            path + ".points", "trajectory must contain at least one point");
    if (trajectory.points.empty()) {
        return;
    }
    require(report, trajectory.points.front().time_s == 0.0,
            ContractIssueCode::inconsistent_semantics, path + ".points[0].time_s",
            "right-continuous trajectory must begin at time zero");
    for (std::size_t index = 0; index < trajectory.points.size(); ++index) {
        const auto &point = trajectory.points[index];
        const auto point_path = path + ".points[" + std::to_string(index) + "]";
        require(report,
                finite(point.time_s) && point.time_s >= 0.0 &&
                    point.time_s <= total_duration_s,
                ContractIssueCode::invalid_value, point_path + ".time_s",
                "trajectory time must be finite and inside the scenario interval");
        require(report, finite(point.value), ContractIssueCode::invalid_value,
                point_path + ".value", "trajectory value must be finite");
        if (unit_interval_values) {
            require(report, detail::unit_interval(point.value),
                    ContractIssueCode::invalid_value, point_path + ".value",
                    "trajectory value must be in [0, 1]");
        }
        if (nonnegative_values) {
            require(report, point.value >= 0.0, ContractIssueCode::invalid_value,
                    point_path + ".value", "trajectory value must be nonnegative");
        }
        if (index != 0) {
            require(report, point.time_s > trajectory.points[index - 1].time_s,
                    ContractIssueCode::inconsistent_semantics, point_path + ".time_s",
                    "trajectory times must be strictly increasing");
        }
    }
    require(report,
            trajectory.interpolation ==
                    TrajectoryInterpolation::right_continuous_hold ||
                trajectory.interpolation == TrajectoryInterpolation::linear,
            ContractIssueCode::unsupported_value, path + ".interpolation",
            "trajectory interpolation is not recognized");
    if (trajectory.interpolation == TrajectoryInterpolation::linear) {
        require(report, trajectory.points.back().time_s == total_duration_s,
                ContractIssueCode::inconsistent_semantics, path + ".points",
                "linear trajectory must explicitly cover the scenario endpoint");
    }
}

} // namespace

ValidationReport validate(const RenderScenario &scenario,
                          const ProvenanceLedger &provenance) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report = validate(provenance);
    require(report, scenario.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version", "scenario schema version must be positive");
    require(report, is_valid_semantic_id(scenario.scenario_id),
            ContractIssueCode::invalid_value, "scenario_id",
            "scenario ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(scenario.engine_profile_id),
            ContractIssueCode::invalid_value, "engine_profile_id",
            "engine profile ID must be a canonical semantic ID");
    require(report, scenario.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "scenario and provenance ledger schema IDs must match");

    validate_resolved(report, scenario.ambient.pressure_pa_abs, provenance,
                      "scenario.ambient.pressure_pa_abs");
    validate_resolved(report, scenario.ambient.temperature_k, provenance,
                      "scenario.ambient.temperature_k");
    validate_resolved(report, scenario.ambient.relative_humidity_01, provenance,
                      "scenario.ambient.relative_humidity_01");
    require(report, finite_positive(scenario.ambient.pressure_pa_abs.value),
            ContractIssueCode::invalid_value, "ambient.pressure_pa_abs.value",
            "ambient absolute pressure must be finite and positive");
    require(report, finite_positive(scenario.ambient.temperature_k.value),
            ContractIssueCode::invalid_value, "ambient.temperature_k.value",
            "ambient temperature must be finite and positive");
    require(report, detail::unit_interval(scenario.ambient.relative_humidity_01.value),
            ContractIssueCode::invalid_value, "ambient.relative_humidity_01.value",
            "relative humidity must be in [0, 1]");

    validate_resolved(report, scenario.fuel.fuel_id, provenance,
                      "scenario.fuel.fuel_id");
    validate_resolved(report, scenario.fuel.lower_heating_value_j_per_kg, provenance,
                      "scenario.fuel.lower_heating_value_j_per_kg");
    validate_resolved(report, scenario.fuel.stoichiometric_air_fuel_mass_ratio,
                      provenance, "scenario.fuel.stoichiometric_air_fuel_mass_ratio");
    require(report, is_valid_semantic_id(scenario.fuel.fuel_id.value),
            ContractIssueCode::invalid_value, "fuel.fuel_id.value",
            "fuel ID must be a canonical semantic ID");
    require(report,
            finite_positive(scenario.fuel.lower_heating_value_j_per_kg.value) &&
                finite_positive(scenario.fuel.stoichiometric_air_fuel_mass_ratio.value),
            ContractIssueCode::invalid_value, "fuel",
            "fuel heating value and stoichiometric ratio must be finite and positive");

    const auto validate_temperature = [&](const ResolvedValue<double> &value,
                                          const std::string &path) {
        validate_resolved(report, value, provenance, path);
        require(report, finite_positive(value.value), ContractIssueCode::invalid_value,
                path + ".value",
                "initial absolute temperature must be finite and positive");
    };
    validate_temperature(scenario.initial_thermal_state.gas_temperature_k,
                         "scenario.initial_thermal_state.gas_temperature_k");
    validate_temperature(scenario.initial_thermal_state.wall_temperature_k,
                         "scenario.initial_thermal_state.wall_temperature_k");
    validate_temperature(scenario.initial_thermal_state.coolant_temperature_k,
                         "scenario.initial_thermal_state.coolant_temperature_k");
    validate_temperature(scenario.initial_thermal_state.oil_temperature_k,
                         "scenario.initial_thermal_state.oil_temperature_k");
    validate_resolved(report, scenario.crankcase.pressure_pa_abs, provenance,
                      "scenario.crankcase.pressure_pa_abs");
    require(report, finite_positive(scenario.crankcase.pressure_pa_abs.value),
            ContractIssueCode::invalid_value,
            "scenario.crankcase.pressure_pa_abs.value",
            "crankcase absolute pressure must be finite and positive");
    validate_temperature(scenario.crankcase.temperature_k,
                         "scenario.crankcase.temperature_k");

    validate_resolved(report, scenario.total_duration_s, provenance,
                      "scenario.total_duration_s");
    validate_resolved(report, scenario.audible_start_s, provenance,
                      "scenario.audible_start_s");
    validate_resolved(report, scenario.audible_duration_s, provenance,
                      "scenario.audible_duration_s");
    require(report, finite_positive(scenario.total_duration_s.value),
            ContractIssueCode::invalid_value, "total_duration_s.value",
            "total duration must be finite and positive");
    require(report,
            finite_nonnegative(scenario.audible_start_s.value) &&
                finite_positive(scenario.audible_duration_s.value) &&
                scenario.audible_start_s.value + scenario.audible_duration_s.value <=
                    scenario.total_duration_s.value,
            ContractIssueCode::invalid_value, "audible_duration_s.value",
            "audible half-open interval must fit inside total duration");

    std::visit(
        [&](const auto &preparation) {
            using T = std::decay_t<decltype(preparation)>;
            if constexpr (std::is_same_v<T, FixedSettling>) {
                validate_resolved(report, preparation.warm_up_duration_s, provenance,
                                  "scenario.preparation.warm_up_duration_s");
                validate_resolved(report, preparation.settling_duration_s, provenance,
                                  "scenario.preparation.settling_duration_s");
                require(report,
                        finite_nonnegative(preparation.warm_up_duration_s.value) &&
                            finite_nonnegative(preparation.settling_duration_s.value),
                        ContractIssueCode::invalid_value, "preparation",
                        "fixed preparation durations must be finite and nonnegative");
                require(report,
                        detail::nearly_equal(preparation.warm_up_duration_s.value +
                                                 preparation.settling_duration_s.value,
                                             scenario.audible_start_s.value) &&
                            detail::nearly_equal(scenario.audible_start_s.value +
                                                     scenario.audible_duration_s.value,
                                                 scenario.total_duration_s.value),
                        ContractIssueCode::inconsistent_semantics,
                        "total_duration_s.value",
                        "fixed preparation must end at audible start, and the audible "
                        "interval must end at total duration");
            } else {
                validate_resolved(report, preparation.minimum_warm_up_duration_s,
                                  provenance,
                                  "scenario.preparation.minimum_warm_up_duration_s");
                validate_resolved(report, preparation.minimum_settling_duration_s,
                                  provenance,
                                  "scenario.preparation.minimum_settling_duration_s");
                validate_resolved(
                    report, preparation.maximum_preparation_duration_s, provenance,
                    "scenario.preparation.maximum_preparation_duration_s");
                validate_resolved(report, preparation.comparison_cycle_count,
                                  provenance,
                                  "scenario.preparation.comparison_cycle_count");
                validate_resolved(
                    report, preparation.cycle_mean_torque_tolerance_nm, provenance,
                    "scenario.preparation.cycle_mean_torque_tolerance_nm");
                validate_resolved(report, preparation.pressure_tolerance_pa, provenance,
                                  "scenario.preparation.pressure_tolerance_pa");
                const auto minimum = preparation.minimum_warm_up_duration_s.value +
                                     preparation.minimum_settling_duration_s.value;
                require(
                    report,
                    finite_nonnegative(preparation.minimum_warm_up_duration_s.value) &&
                        finite_nonnegative(
                            preparation.minimum_settling_duration_s.value) &&
                        finite_positive(
                            preparation.maximum_preparation_duration_s.value) &&
                        preparation.maximum_preparation_duration_s.value >= minimum,
                    ContractIssueCode::invalid_value, "preparation",
                    "convergence preparation durations are inconsistent");
                require(report,
                        preparation.comparison_cycle_count.value > 0 &&
                            finite_positive(
                                preparation.cycle_mean_torque_tolerance_nm.value) &&
                            finite_positive(preparation.pressure_tolerance_pa.value),
                        ContractIssueCode::invalid_value, "preparation",
                        "convergence windows and tolerances must be positive");
                require(report,
                        preparation.maximum_preparation_duration_s.value <=
                                scenario.audible_start_s.value &&
                            detail::nearly_equal(scenario.audible_start_s.value +
                                                     scenario.audible_duration_s.value,
                                                 scenario.total_duration_s.value),
                        ContractIssueCode::inconsistent_semantics,
                        "total_duration_s.value",
                        "maximum preparation must fit before audible start, and the "
                        "audible interval must end at total duration");
            }
        },
        scenario.preparation);

    validate_resolved(report, scenario.operating_state, provenance,
                      "scenario.operating_state");
    require(report, !scenario.operating_state.value.empty(),
            ContractIssueCode::missing_value, "operating_state.value",
            "operating-state timeline must contain at least one point");
    std::unordered_set<std::string> event_ids;
    for (std::size_t index = 0; index < scenario.operating_state.value.size();
         ++index) {
        const auto &point = scenario.operating_state.value[index];
        const auto path = "operating_state.value[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(point.event_id),
                ContractIssueCode::invalid_value, path + ".event_id",
                "event ID must be a canonical semantic ID");
        if (!event_ids.insert(point.event_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".event_id",
                       "event IDs must be unique");
        }
        require(report,
                finite(point.time_s) && point.time_s >= 0.0 &&
                    point.time_s <= scenario.total_duration_s.value,
                ContractIssueCode::invalid_value, path + ".time_s",
                "event time must be inside the scenario interval");
        if (index == 0) {
            require(report, point.time_s == 0.0,
                    ContractIssueCode::inconsistent_semantics, path + ".time_s",
                    "right-continuous operating state must begin at time zero");
        } else {
            require(report,
                    point.time_s > scenario.operating_state.value[index - 1].time_s,
                    ContractIssueCode::inconsistent_semantics, path + ".time_s",
                    "operating-state times must be strictly increasing");
        }
    }

    append_prefixed(report, validate(scenario.rates), "rates");
    validate_resolution_id(report, scenario.rates_resolution_id, provenance,
                           "rates_resolution_id", "scenario.rates");
    validate_resolved(report, scenario.quality, provenance, "scenario.quality");
    require(report, is_valid_semantic_id(scenario.quality.value.profile_id),
            ContractIssueCode::invalid_value, "quality.value.profile_id",
            "quality profile ID must be a canonical semantic ID");
    require(report,
            scenario.quality.value.version > 0 &&
                scenario.quality.value.capture_block_capacity_frames > 0,
            ContractIssueCode::invalid_value, "quality.value",
            "quality version and capture block capacity must be positive");
    validate_resolved(report, scenario.public_seed, provenance, "scenario.public_seed");
    validate_resolution_id(report, scenario.mode_resolution_id, provenance,
                           "mode_resolution_id", "scenario.mode.kind");

    std::visit(
        [&](const auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, HeldSpeed>) {
                validate_resolved(report, mode.engine_speed_rpm, provenance,
                                  "scenario.mode.engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.throttle_01, provenance,
                                  "scenario.mode.throttle_01");
                require(report, finite_positive(mode.engine_speed_rpm.value),
                        ContractIssueCode::invalid_value, "mode.engine_speed_rpm.value",
                        "held speed must be finite and positive");
                require(report, finite(mode.initial_theta_rad.value),
                        ContractIssueCode::invalid_value,
                        "mode.initial_theta_rad.value",
                        "initial crank angle must be finite");
                require(report, detail::unit_interval(mode.throttle_01.value),
                        ContractIssueCode::invalid_value, "mode.throttle_01.value",
                        "throttle must be in [0, 1]");
            } else if constexpr (std::is_same_v<T, PrescribedKinematicSweep>) {
                validate_resolved(report, mode.trajectory.initial_theta_rad, provenance,
                                  "scenario.mode.trajectory.initial_theta_rad");
                require(report, finite(mode.trajectory.initial_theta_rad.value),
                        ContractIssueCode::invalid_value,
                        "mode.trajectory.initial_theta_rad.value",
                        "initial crank angle must be finite");
                validate_resolved(report, mode.trajectory.kinematic_resolution,
                                  provenance,
                                  "scenario.mode.trajectory.kinematic_resolution");
                append_prefixed(report,
                                validate(mode.trajectory.kinematic_resolution.value),
                                "mode.trajectory.kinematic_resolution");
                validate_trajectory(report, mode.trajectory.rpm, provenance,
                                    scenario.total_duration_s.value, false, true,
                                    "scenario.mode.trajectory.rpm");
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T, LoadTargetHeldCapture>) {
                validate_resolved(report, mode.engine_speed_rpm, provenance,
                                  "scenario.mode.engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.target_net_bmep_pa, provenance,
                                  "scenario.mode.target_net_bmep_pa");
                validate_resolved(report, mode.target_tolerance_pa, provenance,
                                  "scenario.mode.target_tolerance_pa");
                validate_resolved(report, mode.throttle_lower_bound_01, provenance,
                                  "scenario.mode.throttle_lower_bound_01");
                validate_resolved(report, mode.throttle_upper_bound_01, provenance,
                                  "scenario.mode.throttle_upper_bound_01");
                require(report,
                        finite_positive(mode.engine_speed_rpm.value) &&
                            finite(mode.initial_theta_rad.value) &&
                            finite(mode.target_net_bmep_pa.value) &&
                            finite_positive(mode.target_tolerance_pa.value),
                        ContractIssueCode::invalid_value, "mode",
                        "speed and target tolerance must be positive; signed target "
                        "must be finite");
                require(report,
                        detail::unit_interval(mode.throttle_lower_bound_01.value) &&
                            detail::unit_interval(mode.throttle_upper_bound_01.value) &&
                            mode.throttle_lower_bound_01.value <=
                                mode.throttle_upper_bound_01.value,
                        ContractIssueCode::invalid_value, "mode",
                        "throttle bounds must form an ordered subset of [0, 1]");
                validate_resolved(report, mode.search_method, provenance,
                                  "scenario.mode.search_method");
                append_prefixed(report, validate(mode.search_method.value),
                                "mode.search_method");
            } else {
                validate_resolved(report, mode.initial_engine_speed_rpm, provenance,
                                  "scenario.mode.initial_engine_speed_rpm");
                validate_resolved(report, mode.initial_theta_rad, provenance,
                                  "scenario.mode.initial_theta_rad");
                validate_resolved(report, mode.equivalent_inertia_kg_m2, provenance,
                                  "scenario.mode.equivalent_inertia_kg_m2");
                require(report,
                        finite_nonnegative(mode.initial_engine_speed_rpm.value) &&
                            finite(mode.initial_theta_rad.value) &&
                            finite_positive(mode.equivalent_inertia_kg_m2.value),
                        ContractIssueCode::invalid_value, "mode",
                        "inertial-dyno initial state and inertia are invalid");
                validate_trajectory(report, mode.throttle_01, provenance,
                                    scenario.total_duration_s.value, true, false,
                                    "scenario.mode.throttle_01");
                validate_resolution_id(report, mode.brake_curve_resolution_id,
                                       provenance, "mode.brake_curve_resolution_id",
                                       "scenario.mode.brake_curve");
                require(report, !mode.brake_curve.empty(),
                        ContractIssueCode::missing_value, "mode.brake_curve",
                        "inertial dyno requires a passive brake curve");
                for (std::size_t index = 0; index < mode.brake_curve.size(); ++index) {
                    const auto &point = mode.brake_curve[index];
                    const auto path = "mode.brake_curve[" + std::to_string(index) + "]";
                    require(report,
                            finite_nonnegative(point.angular_speed_rad_s) &&
                                finite_nonnegative(point.resisting_torque_nm),
                            ContractIssueCode::invalid_value, path,
                            "passive brake speed and resisting magnitude must be "
                            "nonnegative");
                    if (index != 0) {
                        require(report,
                                point.angular_speed_rad_s >
                                    mode.brake_curve[index - 1].angular_speed_rad_s,
                                ContractIssueCode::inconsistent_semantics,
                                path + ".angular_speed_rad_s",
                                "brake-curve speeds must be strictly increasing");
                    }
                }
                validate_resolved(report, mode.crank_dynamics_method, provenance,
                                  "scenario.mode.crank_dynamics_method");
                append_prefixed(report, validate(mode.crank_dynamics_method.value),
                                "mode.crank_dynamics_method");
            }
        },
        scenario.mode);

    return report;
}

ValidationReport validate_for_engine(const RenderScenario &scenario,
                                     const EngineSpec &spec) {
    ValidationReport report;
    if (scenario.engine_profile_id != spec.profile_id.value) {
        report.add(ContractIssueCode::inconsistent_semantics, "engine_profile_id",
                   "scenario and engine profile IDs must match");
    }
    std::visit(
        [&](const auto &profile) {
            if (scenario.fuel.fuel_id.value != profile.fuel.fuel_id.value ||
                scenario.fuel.lower_heating_value_j_per_kg.value !=
                    profile.fuel.energy_density_j_per_kg.value) {
                report.add(
                    ContractIssueCode::inconsistent_semantics, "fuel",
                    "scenario fuel identity and heating value must exactly match "
                    "the executable engine fuel");
            }
        },
        spec.physics_profile);

    const bool needs_complete_cycle_result =
        std::holds_alternative<HeldSpeed>(scenario.mode) ||
        std::holds_alternative<LoadTargetHeldCapture>(scenario.mode);
    if (needs_complete_cycle_result &&
        (!spec.torque_capability.value.physical_net_complete ||
         !spec.torque_capability.value.cycle_integration_available)) {
        report.add(ContractIssueCode::unsupported_value, "mode",
                   "held-speed and load-target scenarios require complete cycle-mean "
                   "net torque");
    }
    if (std::holds_alternative<InertialDyno>(scenario.mode) &&
        !spec.torque_capability.value.physical_net_complete) {
        report.add(ContractIssueCode::unsupported_value, "mode",
                   "inertial dyno requires complete modeled net engine torque");
    }
    return report;
}

} // namespace engine_sim_offline::contract
