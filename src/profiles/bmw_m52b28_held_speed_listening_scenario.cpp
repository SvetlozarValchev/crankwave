#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/legacy_gas_primitives.hpp"

#include <string>

namespace engine_sim_offline::profiles::detail {

contract::RenderScenario build_bmw_m52b28_held_speed_scenario(
    BmwProvenanceBuilder &builder, const contract::EngineSpec &engine,
    const BmwM52b28HeldSpeedScenarioParameters &parameters) {
    using Source = BmwResolutionSource;

    const auto &profile =
        std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
    const auto &core = profile.core;

    contract::RenderScenario scenario;
    scenario.schema_version = 1U;
    scenario.scenario_id = std::string{parameters.scenario_id};
    scenario.engine_profile_id = std::string{builder.engine_profile_id()};

    scenario.ambient = {
        builder.resolved(101325.0, "scenario.ambient.pressure_pa_abs",
                         Source::legacy_asset),
        builder.resolved(298.15, "scenario.ambient.temperature_k",
                         Source::legacy_asset),
        builder.resolved(0.0, "scenario.ambient.relative_humidity_01",
                         Source::declared_default),
    };
    scenario.fuel = {
        builder.resolved(core.fuel.fuel_id.value, "scenario.fuel.fuel_id",
                         Source::legacy_asset),
        builder.resolved(core.fuel.energy_density_j_per_kg.value,
                         "scenario.fuel.lower_heating_value_j_per_kg",
                         Source::legacy_asset),
        builder.derived(
            simulation::legacy_pseudo_gas_stoichiometric_mass_afr(
                core.fuel.molecular_afr.value,
                core.fuel.molecular_mass_kg_per_mol.value),
            "scenario.fuel.stoichiometric_air_fuel_mass_ratio",
            derived_method("legacy-pseudo-gas-mass-afr-v1"),
            {
                "engine.physics.low-order-operating-point-v1.fuel.molecular_afr",
                "engine.physics.low-order-operating-point-v1.fuel."
                "molecular_mass_kg_per_mol",
            }),
    };
    scenario.initial_thermal_state = {
        builder.resolved(298.15, "scenario.initial_thermal_state.gas_temperature_k",
                         Source::legacy_asset),
        builder.resolved(363.15, "scenario.initial_thermal_state.wall_temperature_k",
                         Source::legacy_asset),
        builder.resolved(363.15, "scenario.initial_thermal_state.coolant_temperature_k",
                         Source::declared_default),
        builder.resolved(profile.aggregate_loss.required_oil_temperature_k.value,
                         "scenario.initial_thermal_state.oil_temperature_k",
                         Source::declared_default),
    };
    scenario.crankcase = {
        builder.resolved(101325.0, "scenario.crankcase.pressure_pa_abs",
                         Source::legacy_asset),
        builder.resolved(298.15, "scenario.crankcase.temperature_k",
                         Source::legacy_asset),
    };

    scenario.preparation = contract::ConvergenceSettling{
        builder.resolved(
            contract::adjacent_cycle_block_mean_convergence_method_identity(),
            "scenario.preparation.method", Source::implemented_method),
        builder.resolved(0.0, "scenario.preparation.minimum_warm_up_duration_s",
                         Source::profile_contract),
        builder.resolved(0.0, "scenario.preparation.minimum_settling_duration_s",
                         Source::profile_contract),
        builder.resolved(parameters.maximum_preparation_duration_s,
                         "scenario.preparation.maximum_preparation_duration_s",
                         Source::profile_contract),
        builder.resolved(parameters.comparison_cycle_count,
                         "scenario.preparation.comparison_cycle_count",
                         Source::profile_contract),
        builder.resolved(parameters.cycle_mean_torque_tolerance_nm,
                         "scenario.preparation.cycle_mean_torque_tolerance_nm",
                         Source::profile_contract),
        builder.resolved(parameters.pressure_tolerance_pa,
                         "scenario.preparation.pressure_tolerance_pa",
                         Source::profile_contract),
    };
    scenario.operating_state = builder.resolved(
        std::vector<contract::OperatingStatePoint>{
            {
                std::string{parameters.operating_state_event_id},
                0.0,
                {true, true, false, true, false},
            },
        },
        "scenario.operating_state", Source::profile_contract);

    scenario.total_duration_s =
        builder.resolved(parameters.total_duration_s, "scenario.total_duration_s",
                         Source::profile_contract);
    scenario.audible_start_s =
        builder.resolved(parameters.maximum_preparation_duration_s,
                         "scenario.audible_start_s", Source::profile_contract);
    scenario.audible_duration_s =
        builder.resolved(parameters.evidence_duration_s, "scenario.audible_duration_s",
                         Source::profile_contract);

    scenario.rates = {
        {10000U, 1U}, {10000U, 1U}, {192000U, 1U}, {192000U, 1U}, {192000U, 1U},
    };
    scenario.rates_resolution_id =
        builder.resolved(scenario.rates, "scenario.rates", Source::profile_contract)
            .resolution_id;
    scenario.quality = builder.resolved(
        contract::RenderQuality{
            std::string{parameters.quality_profile_id},
            1U,
            200U,
            3800U,
        },
        "scenario.quality", Source::profile_contract);
    scenario.public_seed = builder.resolved<std::uint64_t>(
        UINT64_C(12648430), "scenario.public_seed", Source::profile_contract);

    scenario.mode = contract::HeldSpeed{
        builder.resolved(parameters.engine_speed_rpm, "scenario.mode.engine_speed_rpm",
                         Source::profile_contract),
        builder.resolved(core.mechanism.crank.crank_tdc_reference_rad.value,
                         "scenario.mode.initial_theta_rad", Source::profile_contract),
        builder.resolved(parameters.throttle_01, "scenario.mode.throttle_01",
                         Source::profile_contract),
    };
    scenario.mode_resolution_id =
        builder
            .resolved(std::string{"held-speed"}, "scenario.mode.kind",
                      Source::profile_contract)
            .resolution_id;
    scenario.provenance_schema_id = std::string{builder.provenance_schema_id()};
    return scenario;
}

contract::RenderScenario
build_bmw_m52b28_held_speed_listening_scenario(BmwProvenanceBuilder &builder,
                                               const contract::EngineSpec &engine) {
    return build_bmw_m52b28_held_speed_scenario(
        builder, engine,
        {
            "bmw-m52b28-held-3000rpm-listening-v1",
            "held-running",
            3000.0,
            0.85,
            3.22,
            16U,
            0.25,
            1500.0,
            15.0,
            18.22,
            "low-order-operating-point-listening-v1",
        });
}

} // namespace engine_sim_offline::profiles::detail
