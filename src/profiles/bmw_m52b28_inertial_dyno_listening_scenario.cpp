#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"
#include "simulation/legacy_gas_primitives.hpp"

#include <cstdint>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::profiles::detail {
namespace {

constexpr double kInitialEngineSpeedRpm = 1500.0;
constexpr double kTargetEngineSpeedRpm = 6500.0;
constexpr double kThrottle01 = 0.85;
constexpr double kFixedPreparationHorizonS = 6.44;
constexpr double kAudibleDurationS = 15.0;
constexpr double kTotalDurationS = 21.44;
constexpr double kEquivalentInertiaKgM2 = 7.9;
constexpr double kBrakeTorqueNm = 40.0;
constexpr double kBrakeCurveMinimumRpm = 1000.0;
constexpr double kBrakeCurveMaximumRpm = 7500.0;
constexpr std::uint32_t kTrailingCompleteCycleCount = 32U;
constexpr double kRadiansPerSecondPerRpm = std::numbers::pi_v<double> / 30.0;

} // namespace

contract::RenderScenario
build_bmw_m52b28_inertial_dyno_listening_scenario(BmwProvenanceBuilder &builder,
                                                  const contract::EngineSpec &engine) {
    using Source = BmwResolutionSource;

    const auto &profile =
        std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
    const auto &core = profile.core;

    contract::RenderScenario scenario;
    scenario.schema_version = 1U;
    scenario.scenario_id = "bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2";
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

    scenario.preparation = contract::FixedHorizonCycleSampling{
        builder.resolved(contract::fixed_horizon_cycle_sampling_method_identity(),
                         "scenario.preparation.method", Source::implemented_method),
        builder.resolved(kFixedPreparationHorizonS,
                         "scenario.preparation.fixed_preparation_horizon_s",
                         Source::profile_contract),
        builder.resolved(kTrailingCompleteCycleCount,
                         "scenario.preparation.trailing_complete_cycle_count",
                         Source::profile_contract),
    };
    scenario.operating_state = builder.resolved(
        std::vector<contract::OperatingStatePoint>{
            {
                "inertial-dyno-running",
                0.0,
                {true, true, false, true, false},
            },
        },
        "scenario.operating_state", Source::profile_contract);

    scenario.total_duration_s = builder.resolved(
        kTotalDurationS, "scenario.total_duration_s", Source::profile_contract);
    scenario.audible_start_s =
        builder.resolved(kFixedPreparationHorizonS, "scenario.audible_start_s",
                         Source::profile_contract);
    scenario.audible_duration_s = builder.resolved(
        kAudibleDurationS, "scenario.audible_duration_s", Source::profile_contract);

    scenario.rates = {
        {10000U, 1U}, {10000U, 1U}, {192000U, 1U}, {192000U, 1U}, {192000U, 1U},
    };
    scenario.rates_resolution_id =
        builder.resolved(scenario.rates, "scenario.rates", Source::profile_contract)
            .resolution_id;
    scenario.quality = builder.resolved(
        contract::RenderQuality{
            "low-order-operating-point-listening-v1",
            1U,
            200U,
            3800U,
        },
        "scenario.quality", Source::profile_contract);
    scenario.public_seed = builder.resolved<std::uint64_t>(
        UINT64_C(12648430), "scenario.public_seed", Source::profile_contract);

    const auto resolved_brake_curve = builder.resolved(
        std::vector<contract::BrakeTorquePoint>{
            {kBrakeCurveMinimumRpm * kRadiansPerSecondPerRpm, kBrakeTorqueNm},
            {kBrakeCurveMaximumRpm * kRadiansPerSecondPerRpm, kBrakeTorqueNm},
        },
        "scenario.mode.brake_curve", Source::profile_contract);

    contract::InertialDyno dyno;
    dyno.initial_engine_speed_rpm = builder.resolved(
        kInitialEngineSpeedRpm, "scenario.mode.initial_engine_speed_rpm",
        Source::profile_contract);
    dyno.initial_theta_rad =
        builder.resolved(core.mechanism.crank.crank_tdc_reference_rad.value,
                         "scenario.mode.initial_theta_rad", Source::profile_contract);
    dyno.equivalent_inertia_kg_m2 = builder.resolved(
        kEquivalentInertiaKgM2, "scenario.mode.equivalent_inertia_kg_m2",
        Source::profile_contract);
    dyno.throttle_01 = {
        contract::TrajectoryInterpolation::right_continuous_hold,
        {{0.0, kThrottle01}},
        builder
            .resolved(std::string{"right-continuous-throttle-v1"},
                      "scenario.mode.throttle_01", Source::profile_contract)
            .resolution_id,
    };
    dyno.brake_curve = resolved_brake_curve.value;
    dyno.brake_curve_resolution_id = resolved_brake_curve.resolution_id;
    dyno.crank_dynamics_method = builder.resolved(
        simulation::rigid_crank_zoh_work_energy_method_identity(),
        "scenario.mode.crank_dynamics_method", Source::implemented_method);
    dyno.target_engine_speed_rpm =
        builder.resolved(kTargetEngineSpeedRpm, "scenario.mode.target_engine_speed_rpm",
                         Source::profile_contract);
    dyno.brake_torque_method = builder.resolved(
        simulation::piecewise_linear_positive_speed_passive_brake_method_identity(),
        "scenario.mode.brake_torque_method", Source::implemented_method);
    scenario.mode = std::move(dyno);
    scenario.mode_resolution_id =
        builder
            .resolved(std::string{"inertial-dyno"}, "scenario.mode.kind",
                      Source::profile_contract)
            .resolution_id;
    scenario.provenance_schema_id = std::string{builder.provenance_schema_id()};
    return scenario;
}

} // namespace engine_sim_offline::profiles::detail
