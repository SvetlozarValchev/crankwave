#include "compile/scenario_resolver_internal.hpp"

#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace engine_sim_offline::compile::detail::scenario_resolution {

void ScenarioResolver::register_common_provenance() {
    for (const std::string_view path : {
             "scenario.ambient.pressure_pa_abs",
             "scenario.ambient.temperature_k",
             "scenario.ambient.relative_humidity_01",
             "scenario.fuel.fuel_id",
             "scenario.initial_thermal_state.gas_temperature_k",
             "scenario.initial_thermal_state.wall_temperature_k",
             "scenario.initial_thermal_state.coolant_temperature_k",
             "scenario.initial_thermal_state.oil_temperature_k",
             "scenario.crankcase.pressure_pa_abs",
             "scenario.crankcase.temperature_k",
             "scenario.operating_state",
             "scenario.total_duration_s",
             "scenario.audible_start_s",
             "scenario.audible_duration_s",
             "scenario.rates",
             "scenario.quality",
             "scenario.public_seed",
             "scenario.mode.kind",
         }) {
        provenance_.add_authored(std::string{path});
    }
    const std::array heating_dependency{
        std::string_view{selected_fuel_.heating_value_source_path}};
    provenance_.add_derived("scenario.fuel.lower_heating_value_j_per_kg",
                            fuel_projection_method_identity(), heating_dependency);
    const std::array afr_dependency{
        std::string_view{selected_fuel_.molecular_mass_source_path},
        std::string_view{selected_fuel_.molecular_afr_source_path}};
    provenance_.add_derived("scenario.fuel.stoichiometric_air_fuel_mass_ratio",
                            fuel_projection_method_identity(), afr_dependency);
}

void ScenarioResolver::register_provenance() {
    register_common_provenance();
    std::visit(
        [&](const auto &preparation) {
            using T = std::decay_t<decltype(preparation)>;
            if constexpr (std::is_same_v<T, contract::FixedSettling>) {
                provenance_.add_authored("scenario.preparation.warm_up_duration_s");
                provenance_.add_authored("scenario.preparation.settling_duration_s");
            } else {
                provenance_.add_authored(
                    "scenario.preparation.fixed_preparation_horizon_s");
                provenance_.add_authored(
                    "scenario.preparation.trailing_complete_cycle_count");
                constexpr std::array<std::string_view, 2> dependencies{
                    "scenario.preparation.fixed_preparation_horizon_s",
                    "scenario.preparation.trailing_complete_cycle_count",
                };
                provenance_.add_derived(
                    "scenario.preparation.method",
                    contract::fixed_horizon_cycle_sampling_method_identity(),
                    dependencies);
            }
        },
        scenario_.preparation);

    std::visit(
        [&](const auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, contract::HeldSpeed>) {
                provenance_.add_authored("scenario.mode.engine_speed_rpm");
                provenance_.add_authored("scenario.mode.initial_theta_rad");
                provenance_.add_authored("scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T,
                                                contract::PrescribedKinematicSweep>) {
                provenance_.add_authored(
                    "scenario.authoring.mode.engine_speed_trajectory");
                constexpr std::array<std::string_view, 1> source_dependency{
                    "scenario.authoring.mode.engine_speed_trajectory"};
                provenance_.add_derived("scenario.mode.trajectory.rpm",
                                        fixed_rate_post_step_rpm_method_identity(),
                                        source_dependency);
                provenance_.add_authored("scenario.mode.trajectory.initial_theta_rad");
                constexpr std::array<std::string_view, 1> rpm_dependency{
                    "scenario.mode.trajectory.rpm"};
                provenance_.add_derived("scenario.mode.trajectory.kinematic_resolution",
                                        fixed_rate_post_step_rpm_method_identity(),
                                        rpm_dependency);
                provenance_.add_authored("scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T, contract::LoadTargetHeldCapture>) {
                for (const std::string_view path : {
                         "scenario.mode.engine_speed_rpm",
                         "scenario.mode.initial_theta_rad",
                         "scenario.mode.target_net_bmep_pa",
                         "scenario.mode.target_tolerance_pa",
                         "scenario.mode.throttle_lower_bound_01",
                         "scenario.mode.throttle_upper_bound_01",
                     }) {
                    provenance_.add_authored(std::string{path});
                }
                constexpr std::array<std::string_view, 1> dependency{
                    "scenario.mode.kind"};
                provenance_.add_derived("scenario.mode.search_method",
                                        mode.search_method.value, dependency);
            } else {
                for (const std::string_view path : {
                         "scenario.mode.initial_engine_speed_rpm",
                         "scenario.mode.initial_theta_rad",
                         "scenario.mode.equivalent_inertia_kg_m2",
                         "scenario.mode.throttle_01",
                         "scenario.mode.brake_curve",
                         "scenario.mode.target_engine_speed_rpm",
                     }) {
                    provenance_.add_authored(std::string{path});
                }
                constexpr std::array<std::string_view, 1> dependency{
                    "scenario.mode.kind"};
                provenance_.add_derived("scenario.mode.crank_dynamics_method",
                                        mode.crank_dynamics_method.value, dependency);
                provenance_.add_derived("scenario.mode.brake_torque_method",
                                        mode.brake_torque_method.value, dependency);
            }
        },
        scenario_.mode);
}

std::string ScenarioResolver::resolution_id(std::string_view path) {
    const auto *resolution = find_resolution_path(combined_provenance_, path);
    if (resolution == nullptr) {
        add(authoring::DiagnosticCode::internal_failure, "",
            "scenario provenance omitted resolved field '" + std::string{path} + "'");
        return {};
    }
    return resolution->id;
}

void ScenarioResolver::bind_resolution_ids() {
    bind(scenario_.ambient.pressure_pa_abs, "scenario.ambient.pressure_pa_abs");
    bind(scenario_.ambient.temperature_k, "scenario.ambient.temperature_k");
    bind(scenario_.ambient.relative_humidity_01,
         "scenario.ambient.relative_humidity_01");
    bind(scenario_.fuel.fuel_id, "scenario.fuel.fuel_id");
    bind(scenario_.fuel.lower_heating_value_j_per_kg,
         "scenario.fuel.lower_heating_value_j_per_kg");
    bind(scenario_.fuel.stoichiometric_air_fuel_mass_ratio,
         "scenario.fuel.stoichiometric_air_fuel_mass_ratio");
    bind(scenario_.initial_thermal_state.gas_temperature_k,
         "scenario.initial_thermal_state.gas_temperature_k");
    bind(scenario_.initial_thermal_state.wall_temperature_k,
         "scenario.initial_thermal_state.wall_temperature_k");
    bind(scenario_.initial_thermal_state.coolant_temperature_k,
         "scenario.initial_thermal_state.coolant_temperature_k");
    bind(scenario_.initial_thermal_state.oil_temperature_k,
         "scenario.initial_thermal_state.oil_temperature_k");
    bind(scenario_.crankcase.pressure_pa_abs, "scenario.crankcase.pressure_pa_abs");
    bind(scenario_.crankcase.temperature_k, "scenario.crankcase.temperature_k");
    bind(scenario_.operating_state, "scenario.operating_state");
    bind(scenario_.total_duration_s, "scenario.total_duration_s");
    bind(scenario_.audible_start_s, "scenario.audible_start_s");
    bind(scenario_.audible_duration_s, "scenario.audible_duration_s");
    scenario_.rates_resolution_id = resolution_id("scenario.rates");
    bind(scenario_.quality, "scenario.quality");
    bind(scenario_.public_seed, "scenario.public_seed");
    scenario_.mode_resolution_id = resolution_id("scenario.mode.kind");
    scenario_.provenance_schema_id = combined_provenance_.schema_id;

    std::visit(
        [&](auto &preparation) {
            using T = std::decay_t<decltype(preparation)>;
            if constexpr (std::is_same_v<T, contract::FixedSettling>) {
                bind(preparation.warm_up_duration_s,
                     "scenario.preparation.warm_up_duration_s");
                bind(preparation.settling_duration_s,
                     "scenario.preparation.settling_duration_s");
            } else {
                bind(preparation.method, "scenario.preparation.method");
                bind(preparation.fixed_preparation_horizon_s,
                     "scenario.preparation.fixed_preparation_horizon_s");
                bind(preparation.trailing_complete_cycle_count,
                     "scenario.preparation.trailing_complete_cycle_count");
            }
        },
        scenario_.preparation);
    std::visit(
        [&](auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, contract::HeldSpeed>) {
                bind(mode.engine_speed_rpm, "scenario.mode.engine_speed_rpm");
                bind(mode.initial_theta_rad, "scenario.mode.initial_theta_rad");
                bind(mode.throttle_01, "scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T,
                                                contract::PrescribedKinematicSweep>) {
                auto &rpm =
                    std::get<contract::FixedRateRpmTrajectory>(mode.trajectory.rpm);
                rpm.resolution_id = resolution_id("scenario.mode.trajectory.rpm");
                bind(mode.trajectory.initial_theta_rad,
                     "scenario.mode.trajectory.initial_theta_rad");
                bind(mode.trajectory.kinematic_resolution,
                     "scenario.mode.trajectory.kinematic_resolution");
                mode.throttle_01.resolution_id =
                    resolution_id("scenario.mode.throttle_01");
            } else if constexpr (std::is_same_v<T, contract::LoadTargetHeldCapture>) {
                bind(mode.engine_speed_rpm, "scenario.mode.engine_speed_rpm");
                bind(mode.initial_theta_rad, "scenario.mode.initial_theta_rad");
                bind(mode.target_net_bmep_pa, "scenario.mode.target_net_bmep_pa");
                bind(mode.target_tolerance_pa, "scenario.mode.target_tolerance_pa");
                bind(mode.throttle_lower_bound_01,
                     "scenario.mode.throttle_lower_bound_01");
                bind(mode.throttle_upper_bound_01,
                     "scenario.mode.throttle_upper_bound_01");
                bind(mode.search_method, "scenario.mode.search_method");
            } else {
                bind(mode.initial_engine_speed_rpm,
                     "scenario.mode.initial_engine_speed_rpm");
                bind(mode.initial_theta_rad, "scenario.mode.initial_theta_rad");
                bind(mode.equivalent_inertia_kg_m2,
                     "scenario.mode.equivalent_inertia_kg_m2");
                mode.throttle_01.resolution_id =
                    resolution_id("scenario.mode.throttle_01");
                mode.brake_curve_resolution_id =
                    resolution_id("scenario.mode.brake_curve");
                bind(mode.crank_dynamics_method, "scenario.mode.crank_dynamics_method");
                bind(mode.target_engine_speed_rpm,
                     "scenario.mode.target_engine_speed_rpm");
                bind(mode.brake_torque_method, "scenario.mode.brake_torque_method");
            }
        },
        scenario_.mode);
}

} // namespace engine_sim_offline::compile::detail::scenario_resolution
