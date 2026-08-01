#include "authored_engine_fixture_support.hpp"
#include "engine_sim_offline/contract.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::contract;

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;
constexpr double kLegacyRpmScale = 0.104719755;
constexpr double kStepSeconds = 1.0 / 10000.0;
constexpr double kHeldRpm = 3000.0;
constexpr double kFixedPreparationHorizonS = 0.22;
constexpr std::uint32_t kTrailingCompleteCycleCount = 2;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class T> ResolvedValue<T> resolved(T value, std::string resolution_id) {
    return {std::move(value), std::move(resolution_id)};
}

TorqueValueNm complete_torque(double value_nm, TorqueTermMask included_terms) {
    return {
        value_nm,
        Availability::available,
        Completeness::complete,
        QuantityUnavailableReason::none,
        included_terms,
        0,
    };
}

OperatingPointBoundaryEvidence
boundary_at_lattice_index(double initial_theta_rad, double cycle_reference_theta_rad,
                          std::int64_t cycle_index) {
    const double target_theta =
        cycle_reference_theta_rad +
        static_cast<double>(cycle_index) * kFourStrokeCycleRadians;
    const double angular_speed_rad_s = -(-kHeldRpm * kLegacyRpmScale);
    const double step_rotation_rad = angular_speed_rad_s * kStepSeconds;
    const double post_step_count =
        (target_theta - initial_theta_rad) / step_rotation_rad;
    expect(std::isfinite(post_step_count) && post_step_count >= 1.0,
           "test lattice boundary preceded first post-step sample");
    const double right_step_count = std::ceil(post_step_count);
    expect(right_step_count <=
               static_cast<double>(std::numeric_limits<std::uint64_t>::max()),
           "test lattice boundary sample index is not representable");
    const auto right_sample_index = static_cast<std::uint64_t>(right_step_count) - 1;
    const double right_theta = initial_theta_rad + right_step_count * step_rotation_rad;
    const double right_time_s = static_cast<double>(right_sample_index + 1) / 10000.0;
    if (right_theta == target_theta) {
        return {
            right_sample_index, right_sample_index, 0.0, right_time_s, target_theta,
        };
    }

    expect(right_sample_index > 0,
           "test lattice boundary preceded an interpolation bracket");
    const auto left_sample_index = right_sample_index - 1;
    const double left_step_count = right_step_count - 1.0;
    const double left_theta = initial_theta_rad + left_step_count * step_rotation_rad;
    const double left_time_s = static_cast<double>(left_sample_index + 1) / 10000.0;
    const double fraction = (target_theta - left_theta) / (right_theta - left_theta);
    return {
        left_sample_index, right_sample_index,
        fraction,          left_time_s + fraction * (right_time_s - left_time_s),
        target_theta,
    };
}

HeldSpeedCycleBlockEvidence
block(std::uint64_t first_ordinal, OperatingPointBoundaryEvidence start_boundary,
      OperatingPointBoundaryEvidence end_boundary, double indicated_work_j,
      double aggregate_loss_work_j, double brake_work_j,
      const std::vector<GasVolumeId> &physical_volume_ids, double pressure_offset_pa) {
    const double angle_range =
        static_cast<double>(kTrailingCompleteCycleCount) * kFourStrokeCycleRadians;
    const double duration_s =
        end_boundary.scenario_time_s - start_boundary.scenario_time_s;
    constexpr double starter_work_j = 0.0;
    constexpr double displacement_m3 = 0.002793181;

    std::vector<MeanBoundaryPressurePa> pressures;
    pressures.reserve(physical_volume_ids.size());
    for (const auto id : physical_volume_ids) {
        pressures.push_back({
            id,
            100000.0 + static_cast<double>(id.value) * 1000.0 + pressure_offset_pa,
        });
    }

    const double cycle_indicated_work_j =
        indicated_work_j / static_cast<double>(kTrailingCompleteCycleCount);
    const double cycle_aggregate_loss_work_j =
        aggregate_loss_work_j / static_cast<double>(kTrailingCompleteCycleCount);
    const double cycle_brake_work_j =
        (cycle_indicated_work_j - cycle_aggregate_loss_work_j) + starter_work_j;
    std::vector<HeldSpeedCompletedCycleEvidence> completed_cycles;
    for (std::uint32_t index = 0; index < kTrailingCompleteCycleCount; ++index) {
        HeldSpeedCompletedCycleEvidence completed_cycle{
            first_ordinal + index, cycle_indicated_work_j, cycle_aggregate_loss_work_j,
            starter_work_j,        cycle_brake_work_j,     {},
        };
        completed_cycle.end_boundary_pressures.reserve(pressures.size());
        const double pressure_delta_pa = index == 0 ? -10.0 : 10.0;
        for (const auto &pressure : pressures) {
            completed_cycle.end_boundary_pressures.push_back({
                pressure.gas_volume_id,
                pressure.pressure_pa_abs + pressure_delta_pa,
            });
        }
        completed_cycles.push_back(std::move(completed_cycle));
    }
    expect(cycle_brake_work_j * static_cast<double>(kTrailingCompleteCycleCount) ==
               brake_work_j,
           "test sample brake-work input disagrees with per-cycle source work");

    return {
        {
            kTrailingCompleteCycleCount,
            first_ordinal,
            first_ordinal + kTrailingCompleteCycleCount - 1,
            start_boundary,
            end_boundary,
        },
        std::move(completed_cycles),
        indicated_work_j,
        aggregate_loss_work_j,
        starter_work_j,
        brake_work_j,
        {
            complete_torque(indicated_work_j / angle_range,
                            indicated_gas_torque_term_mask()),
            complete_torque(-aggregate_loss_work_j / angle_range,
                            friction_pump_and_accessory_torque_term_mask()),
            complete_torque(starter_work_j / angle_range,
                            torque_term_mask(TorqueTerm::starter)),
            complete_torque(brake_work_j / angle_range, known_torque_term_mask()),
        },
        // Replaced by the fixture's exact canonical engine displacement below.
        brake_work_j /
            (static_cast<double>(kTrailingCompleteCycleCount) * displacement_m3),
        brake_work_j / duration_s,
        std::move(pressures),
    };
}

struct Fixture {
    EngineSpec engine;
    RenderScenario scenario;
    ProvenanceBundleRef provenance_bundle;
    Sha256Digest request_identity;
    HeldSpeedOperatingPointResult result;
};

Sha256Digest request_identity(const EngineSpec &engine, const RenderScenario &scenario,
                              const ProvenanceBundleRef &provenance) {
    const ResolvedRandomnessPolicy randomness{
        {scenario.scenario_id, {}},
        {pcg32_generator_method_identity(), {}},
        {component_seed_derivation_method_identity(), {}},
    };
    auto random_plan_result =
        compile_random_plan(randomness, engine, PresentationCalibration{}, scenario);
    const auto *random_plan = std::get_if<RandomPlan>(&random_plan_result);
    expect(random_plan != nullptr,
           "canonical simulation-request random plan compilation failed");
    const auto encoded = identity::encode_simulation_request_identity_v4(
        engine, scenario, *random_plan, provenance);
    const auto *encoding =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&encoded);
    expect(encoding != nullptr,
           "canonical simulation-request identity encoding failed");
    return encoding->sha256;
}

Fixture fixture(const std::filesystem::path &repository_root) {
    const auto canonical =
        test::load_canonical_authored_engine_fixture(repository_root);
    auto engine = canonical.engine;
    const auto &operating_profile = test::operating_profile(engine);
    const double cycle_reference_theta_rad =
        operating_profile.core.mechanism.crank.crank_tdc_reference_rad.value;

    RenderScenario scenario;
    scenario.schema_version = 1;
    scenario.scenario_id = "held-speed-result-contract-test";
    scenario.engine_profile_id = engine.profile_id.value;
    scenario.ambient = {
        resolved(101325.0, "result-test-ambient-pressure"),
        resolved(293.15, "result-test-ambient-temperature"),
        resolved(0.5, "result-test-ambient-humidity"),
    };
    scenario.fuel = {
        resolved(operating_profile.core.fuel.fuel_id.value, "result-test-fuel-id"),
        resolved(operating_profile.core.fuel.energy_density_j_per_kg.value,
                 "result-test-fuel-lhv"),
        resolved(canonical.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
                 "result-test-fuel-afr"),
    };
    scenario.initial_thermal_state = {
        resolved(350.0, "result-test-gas-temperature"),
        resolved(360.0, "result-test-wall-temperature"),
        resolved(360.0, "result-test-coolant-temperature"),
        resolved(operating_profile.aggregate_loss.required_oil_temperature_k.value,
                 "result-test-oil-temperature"),
    };
    scenario.crankcase = {
        resolved(101325.0, "result-test-crankcase-pressure"),
        resolved(293.15, "result-test-crankcase-temperature"),
    };
    scenario.preparation = FixedHorizonCycleSampling{
        resolved(fixed_horizon_cycle_sampling_method_identity(),
                 "result-test-sampling-method"),
        resolved(kFixedPreparationHorizonS, "result-test-fixed-preparation-horizon"),
        resolved<std::uint32_t>(kTrailingCompleteCycleCount,
                                "result-test-trailing-cycle-count"),
    };
    scenario.operating_state = resolved(
        std::vector<OperatingStatePoint>{
            {
                "held-running",
                0.0,
                {true, true, false, true, false},
            },
        },
        "result-test-operating-state");
    scenario.total_duration_s = resolved(1.0, "result-test-total-duration");
    scenario.audible_start_s =
        resolved(kFixedPreparationHorizonS, "result-test-audible-start");
    scenario.audible_duration_s = resolved(0.78, "result-test-audible-duration");
    scenario.rates = {
        {10000, 1}, {48000, 1}, {48000, 1}, {48000, 1}, {48000, 1},
    };
    scenario.rates_resolution_id = "result-test-rates";
    scenario.quality = resolved(RenderQuality{"result-test-quality", 1, 256, 256},
                                "result-test-quality");
    scenario.public_seed = resolved<std::uint64_t>(17, "result-test-public-seed");
    scenario.mode = HeldSpeed{
        resolved(kHeldRpm, "result-test-held-rpm"),
        resolved(cycle_reference_theta_rad, "result-test-initial-theta"),
        resolved(0.85, "result-test-throttle"),
    };
    scenario.mode_resolution_id = "result-test-mode";
    scenario.provenance_schema_id = "provenance-ledger-v1";

    std::vector<GasVolumeId> physical_volume_ids;
    for (const auto &volume : engine.gas_volumes) {
        if (volume.kind.value != GasVolumeKind::atmosphere) {
            physical_volume_ids.push_back(volume.id);
        }
    }
    std::ranges::sort(physical_volume_ids);
    expect(!physical_volume_ids.empty(),
           "canonical BMW profile has no physical gas volumes");

    const auto boundary_3 = boundary_at_lattice_index(cycle_reference_theta_rad,
                                                      cycle_reference_theta_rad, 3);
    const auto boundary_5 = boundary_at_lattice_index(cycle_reference_theta_rad,
                                                      cycle_reference_theta_rad, 5);
    auto sample = block(2, boundary_3, boundary_5, 2002.0, 200.0, 1802.0,
                        physical_volume_ids, 20.0);
    const double displacement_m3 = engine.total_displacement_m3.value;
    sample.net_bmep_pa =
        sample.brake_work_j /
        (static_cast<double>(kTrailingCompleteCycleCount) * displacement_m3);

    const auto provenance_bundle = canonical.provenance_bundle;
    const auto identity = request_identity(engine, scenario, provenance_bundle);

    HeldSpeedOperatingPointConditions conditions{
        engine.profile_id.value,
        kHeldRpm,
        cycle_reference_theta_rad,
        cycle_reference_theta_rad,
        0.85,
        {10000, 1},
        {
            scenario.ambient.pressure_pa_abs.value,
            scenario.ambient.temperature_k.value,
            scenario.ambient.relative_humidity_01.value,
        },
        {
            scenario.fuel.fuel_id.value,
            scenario.fuel.lower_heating_value_j_per_kg.value,
            scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
        },
        {
            scenario.initial_thermal_state.gas_temperature_k.value,
            scenario.initial_thermal_state.wall_temperature_k.value,
            scenario.initial_thermal_state.coolant_temperature_k.value,
            scenario.initial_thermal_state.oil_temperature_k.value,
        },
        {
            scenario.crankcase.pressure_pa_abs.value,
            scenario.crankcase.temperature_k.value,
        },
        displacement_m3,
        {
            operating_profile.accessory_configuration.configuration_id.value,
            operating_profile.accessory_configuration.content_sha256.value,
        },
        true,
        operating_profile.starter.included_terms.value,
    };
    HeldSpeedOperatingPointResult result{
        identity,
        std::move(conditions),
        std::string{kGenericChenFlynnLowOrderModelPredictionApplicability},
        {
            fixed_horizon_cycle_sampling_method_identity(),
            kTrailingCompleteCycleCount,
            kFixedPreparationHorizonS,
            std::move(sample),
            3,
            boundary_5,
        },
    };
    return {
        std::move(engine), std::move(scenario), provenance_bundle,
        identity,          std::move(result),
    };
}

void run_tests(const std::filesystem::path &repository_root) {
    const auto valid = fixture(repository_root);

    expect(validate(valid.result).ok(),
           "valid held-speed operating-point evidence was rejected");
    expect(validate(valid.result, valid.scenario, valid.engine, valid.request_identity)
               .ok(),
           "valid operating point was not bound to its canonical request");
    expect(&valid.result.reported_block() ==
               &valid.result.sampling.trailing_complete_cycles,
           "reported block accessor did not expose the trailing sample");

    auto wrong_method_digest = valid.result;
    wrong_method_digest.sampling.method.configuration_sha256.bytes[0] ^= 0x01U;
    expect(!validate(wrong_method_digest).ok(),
           "forged sampling implementation digest was accepted");

    auto impossible_kinematics = valid.result;
    auto &impossible_end =
        impossible_kinematics.sampling.trailing_complete_cycles.cycles.end_boundary;
    impossible_end.scenario_time_s =
        std::nextafter(impossible_end.scenario_time_s, 1.0);
    impossible_kinematics.sampling.last_eligible_cycle_end_boundary_at_fixed_horizon =
        impossible_end;
    auto &impossible_sample = impossible_kinematics.sampling.trailing_complete_cycles;
    impossible_sample.mean_power_w =
        impossible_sample.brake_work_j /
        (impossible_end.scenario_time_s -
         impossible_sample.cycles.start_boundary.scenario_time_s);
    expect(!validate(impossible_kinematics).ok(),
           "impossible held-RPM boundary timing was accepted");

    auto stale_horizon = valid.result;
    stale_horizon.sampling.fixed_preparation_horizon_s = 0.25;
    expect(!validate(stale_horizon).ok(),
           "a non-latest sample was accepted at a later fixed horizon");

    auto transplanted_scenario = valid.scenario;
    transplanted_scenario.public_seed.value += 1;
    const auto transplanted_identity =
        request_identity(valid.engine, transplanted_scenario, valid.provenance_bundle);
    expect(!validate(valid.result, transplanted_scenario, valid.engine,
                     transplanted_identity)
                .ok(),
           "operating evidence transplanted across canonical request identities");

    auto forged_block_work = valid.result;
    auto &forged_block = forged_block_work.sampling.trailing_complete_cycles;
    forged_block.brake_work_j += 1.0;
    const double block_angle =
        static_cast<double>(kTrailingCompleteCycleCount) * kFourStrokeCycleRadians;
    const double block_displacement =
        static_cast<double>(kTrailingCompleteCycleCount) *
        forged_block_work.conditions.total_displacement_m3;
    const double block_duration = forged_block.cycles.end_boundary.scenario_time_s -
                                  forged_block.cycles.start_boundary.scenario_time_s;
    forged_block.cycle_mean_torque.net_shaft.value_nm =
        forged_block.brake_work_j / block_angle;
    forged_block.net_bmep_pa = forged_block.brake_work_j / block_displacement;
    forged_block.mean_power_w = forged_block.brake_work_j / block_duration;
    expect(!validate(forged_block_work).ok(),
           "coordinated forged block brake work escaped retained per-cycle "
           "source validation");

    auto forged_cycle_work = valid.result;
    auto &forged_cycle =
        forged_cycle_work.sampling.trailing_complete_cycles.completed_cycles.front();
    forged_cycle.indicated_gas_work_j += 1.0;
    forged_cycle.brake_work_j =
        (forged_cycle.indicated_gas_work_j - forged_cycle.aggregate_loss_work_j) +
        forged_cycle.starter_work_j;
    expect(!validate(forged_cycle_work).ok(),
           "per-cycle work mutation escaped exact stable block reduction");

    auto forged_pressure_means = valid.result;
    for (auto &pressure : forged_pressure_means.sampling.trailing_complete_cycles
                              .mean_boundary_pressures) {
        pressure.pressure_pa_abs += 1000.0;
    }
    expect(!validate(forged_pressure_means).ok(),
           "coordinated forged pressure means escaped retained per-cycle "
           "pressure reduction");

    auto forged_cycle_pressure = valid.result;
    forged_cycle_pressure.sampling.trailing_complete_cycles.completed_cycles.front()
        .end_boundary_pressures.front()
        .pressure_pa_abs += 1.0;
    expect(!validate(forged_cycle_pressure).ok(),
           "per-cycle end-boundary pressure mutation escaped exact stable block "
           "reduction");

    auto transplanted_ordinals = valid.result;
    auto rewrite_ordinals = [](HeldSpeedCycleBlockEvidence &sample,
                               std::uint64_t first) {
        sample.cycles.first_completed_cycle_ordinal = first;
        sample.cycles.last_completed_cycle_ordinal =
            first + kTrailingCompleteCycleCount - 1;
        for (std::size_t index = 0; index < sample.completed_cycles.size(); ++index) {
            sample.completed_cycles[index].completed_cycle_ordinal = first + index;
        }
    };
    rewrite_ordinals(transplanted_ordinals.sampling.trailing_complete_cycles, 10);
    transplanted_ordinals.sampling
        .last_eligible_completed_cycle_ordinal_at_fixed_horizon = 11;
    expect(!validate(transplanted_ordinals).ok(),
           "cycle ordinals detached from the initial held-state lattice were "
           "accepted");

    auto old_bmep = valid.result;
    old_bmep.sampling.trailing_complete_cycles.net_bmep_pa =
        old_bmep.sampling.trailing_complete_cycles.brake_work_j /
        old_bmep.conditions.total_displacement_m3;
    expect(!validate(old_bmep).ok(),
           "old one-cycle BMEP denominator was accepted for an N-cycle block");

    auto rounded_power = valid.result;
    rounded_power.sampling.trailing_complete_cycles.mean_power_w = std::nextafter(
        rounded_power.sampling.trailing_complete_cycles.mean_power_w, 0.0);
    expect(!validate(rounded_power).ok(),
           "one-ULP drift in a directly derived power field was accepted");

    auto nearby_request_value = valid.result;
    nearby_request_value.conditions.throttle_01 =
        std::nextafter(nearby_request_value.conditions.throttle_01, 0.0);
    expect(!validate(nearby_request_value, valid.scenario, valid.engine,
                     valid.request_identity)
                .ok(),
           "numerically close but nonidentical held throttle was accepted");

    auto incomplete_pressure_set = valid.result;
    incomplete_pressure_set.sampling.trailing_complete_cycles.mean_boundary_pressures
        .pop_back();
    expect(!validate(incomplete_pressure_set, valid.scenario, valid.engine,
                     valid.request_identity)
                .ok(),
           "physical gas-volume pressure omission was accepted");

    auto wrong_mask = valid.result;
    wrong_mask.sampling.trailing_complete_cycles.cycle_mean_torque.aggregate_loss
        .included_terms = known_torque_term_mask();
    expect(!validate(wrong_mask).ok(),
           "aggregate loss was allowed to claim the complete net-torque mask");

    auto wrong_attestation = valid.result;
    ++wrong_attestation.sampling.last_eligible_completed_cycle_ordinal_at_fixed_horizon;
    expect(!validate(wrong_attestation).ok(),
           "a last-eligible ordinal detached from the sample was accepted");

    auto wrong_sample_count = valid.result;
    ++wrong_sample_count.sampling.trailing_complete_cycle_count;
    expect(!validate(wrong_sample_count).ok(),
           "a sample whose size differs from M was accepted");

    auto mismatched_request = valid.scenario;
    std::get<FixedHorizonCycleSampling>(mismatched_request.preparation)
        .trailing_complete_cycle_count.value = 3U;
    const auto mismatched_identity =
        request_identity(valid.engine, mismatched_request, valid.provenance_bundle);
    expect(
        !validate(valid.result, mismatched_request, valid.engine, mismatched_identity)
             .ok(),
        "result sampling parameters detached from the request were accepted");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2,
               "usage: held_speed_operating_point_result_test <repository-root>");
        run_tests(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Held-speed operating-point result failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
