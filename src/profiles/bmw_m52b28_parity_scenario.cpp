#include "profiles/bmw_m52b28_parity_request_internal.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::profiles::detail {
namespace {

constexpr double kLegacyPi = 3.14159265359;
constexpr std::string_view kScenarioProfileId = "bmw-m52b28-legacy-low-order-v1";

} // namespace

contract::RenderScenario
build_bmw_m52b28_parity_scenario(BmwProvenanceBuilder &builder,
                                 std::vector<double> post_step_rpm) {
    using Source = BmwResolutionSource;

    contract::RenderScenario scenario;
    scenario.schema_version = 1;
    scenario.scenario_id = "bmw-m52b28-reference-pull-v1";
    scenario.engine_profile_id = kScenarioProfileId;

    scenario.ambient = {
        builder.resolved(101325.0, "scenario.ambient.pressure_pa_abs",
                         Source::legacy_asset),
        builder.resolved(298.15, "scenario.ambient.temperature_k",
                         Source::legacy_asset),
        builder.resolved(0.0, "scenario.ambient.relative_humidity_01",
                         Source::declared_default),
    };
    scenario.fuel = {
        builder.resolved(std::string{"gasoline-legacy-engine-sim-v1"},
                         "scenario.fuel.fuel_id", Source::legacy_asset),
        builder.resolved(48.1e6, "scenario.fuel.lower_heating_value_j_per_kg",
                         Source::legacy_asset),
        builder.derived((12.5 / 0.25) * (0.02897 / 0.100),
                        "scenario.fuel.stoichiometric_air_fuel_mass_ratio",
                        derived_method("legacy-pseudo-gas-mass-afr-v1"),
                        {
                            "engine.physics.legacy-low-order-v1.fuel.molecular_afr",
                            "engine.physics.legacy-low-order-v1.fuel."
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
        builder.resolved(363.15, "scenario.initial_thermal_state.oil_temperature_k",
                         Source::declared_default),
    };
    scenario.crankcase = {
        builder.resolved(101325.0, "scenario.crankcase.pressure_pa_abs",
                         Source::legacy_asset),
        builder.resolved(298.15, "scenario.crankcase.temperature_k",
                         Source::legacy_asset),
    };

    scenario.preparation = contract::FixedSettling{
        builder.resolved(1.0, "scenario.preparation.warm_up_duration_s",
                         Source::profile_contract),
        builder.resolved(1.0, "scenario.preparation.settling_duration_s",
                         Source::profile_contract),
    };
    scenario.operating_state = builder.resolved(
        std::vector<contract::OperatingStatePoint>{
            {
                "direction-acquisition",
                0.0,
                {false, true, true, false, true},
            },
            {
                "direction-lock",
                0.8,
                {false, true, true, true, true},
            },
            {
                "ignition-handoff",
                0.9,
                {true, true, false, true, true},
            },
        },
        "scenario.operating_state", Source::profile_contract);

    scenario.total_duration_s =
        builder.resolved(17.0, "scenario.total_duration_s",
                         Source::profile_contract);
    scenario.audible_start_s =
        builder.resolved(2.0, "scenario.audible_start_s",
                         Source::profile_contract);
    scenario.audible_duration_s =
        builder.derived(15.0, "scenario.audible_duration_s",
                        derived_method("scenario-audible-duration-subtraction-v1"),
                        {
                            "scenario.total_duration_s",
                            "scenario.audible_start_s",
                        });

    scenario.rates = {
        {10000, 1}, {10000, 1}, {192000, 1}, {192000, 1}, {192000, 1},
    };
    scenario.rates_resolution_id =
        builder
            .resolved(scenario.rates, "scenario.rates",
                      Source::profile_contract)
            .resolution_id;
    scenario.quality = builder.resolved(
        contract::RenderQuality{
            "legacy-low-order-parity-v1",
            1,
            200,
            3800,
        },
        "scenario.quality", Source::profile_contract);
    scenario.public_seed = builder.resolved<std::uint64_t>(
        UINT64_C(12648430), "scenario.public_seed",
        Source::profile_contract);

    contract::FixedRateRpmTrajectory rpm{
        {10000, 1},
        0,
        contract::RpmSampleSemantics::post_step_rpm,
        std::move(post_step_rpm),
        {},
        builder
            .resolved(std::string{"fixed-rate-post-step-rpm-binary64-v1"},
                      "scenario.mode.trajectory.rpm", Source::reference_trajectory)
            .resolution_id,
    };
    rpm.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(rpm.post_step_rpm);

    contract::PrescribedKinematicSweep sweep;
    sweep.trajectory.rpm = std::move(rpm);
    sweep.trajectory.initial_theta_rad = builder.resolved(
        120.0 * (kLegacyPi / 180.0), "scenario.mode.trajectory.initial_theta_rad",
        Source::legacy_asset);
    sweep.trajectory.kinematic_resolution = builder.resolved(
        fixed_rate_rpm_method(), "scenario.mode.trajectory.kinematic_resolution",
        Source::profile_contract);
    sweep.throttle_01 = {
        contract::TrajectoryInterpolation::right_continuous_hold,
        {
            {0.0, 0.18},
            {0.9, 0.12},
            {1.0, 0.85},
        },
        builder
            .resolved(std::string{"right-continuous-throttle-v1"},
                      "scenario.mode.throttle_01", Source::profile_contract)
            .resolution_id,
    };
    scenario.mode = std::move(sweep);
    scenario.mode_resolution_id =
        builder
            .resolved(std::string{"prescribed-kinematic-sweep"}, "scenario.mode.kind",
                      Source::profile_contract)
            .resolution_id;
    scenario.provenance_schema_id = "engine-sim-offline.m3-bmw-provenance.v1";
    return scenario;
}

} // namespace engine_sim_offline::profiles::detail
