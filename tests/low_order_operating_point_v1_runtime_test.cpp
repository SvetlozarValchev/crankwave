#include "authored_engine_fixture_support.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace crankwave;

constexpr double kHeldRpm = 3000.0;
constexpr double kFixedPreparationHorizonS = 0.22;
constexpr std::uint32_t kTrailingCompleteCycleCount = 4U;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class T>
[[nodiscard]] contract::ResolvedValue<T> resolved(T value, std::string resolution_id) {
    return {std::move(value), std::move(resolution_id)};
}

struct Fixture {
    const test::AuthoredEngineFixture *authored = nullptr;
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::Sha256Digest request_identity;
};

[[nodiscard]] simulation::LowOrderExecutionExtent
finite_extent(const contract::RenderScenario &scenario) {
    const auto frame_count = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    expect(frame_count.has_value(),
           "test scenario did not resolve to an integral physics horizon");
    return simulation::LowOrderExecutionExtent::finite_scenario(*frame_count);
}

[[nodiscard]] Fixture fixture(const test::AuthoredEngineFixture &authored) {
    auto engine = authored.engine;
    const auto &profile = test::operating_profile(engine);
    auto scenario = authored.scenario;
    scenario.scenario_id = "low-order-operating-runtime-test";
    scenario.fuel.fuel_id.value = profile.core.fuel.fuel_id.value;
    scenario.fuel.lower_heating_value_j_per_kg.value =
        profile.core.fuel.energy_density_j_per_kg.value;
    scenario.fuel.stoichiometric_air_fuel_mass_ratio.value =
        simulation::legacy_pseudo_gas_stoichiometric_mass_afr(
            profile.core.fuel.molecular_afr.value,
            profile.core.fuel.molecular_mass_kg_per_mol.value);
    scenario.initial_thermal_state.oil_temperature_k.value =
        profile.aggregate_loss.required_oil_temperature_k.value;
    scenario.preparation = contract::FixedHorizonCycleSampling{
        resolved(contract::fixed_horizon_cycle_sampling_method_identity(),
                 "scenario.preparation.method"),
        resolved(kFixedPreparationHorizonS,
                 "scenario.preparation.fixed_preparation_horizon_s"),
        resolved<std::uint32_t>(kTrailingCompleteCycleCount,
                                "scenario.preparation.trailing_complete_cycle_count"),
    };
    scenario.operating_state.value = {
        {
            "held-running",
            0.0,
            {true, true, false, true, false},
        },
    };
    scenario.total_duration_s.value = 0.3;
    scenario.audible_start_s.value = kFixedPreparationHorizonS;
    scenario.audible_duration_s.value = 0.08;
    scenario.rates.physics = {10000U, 1U};
    scenario.rates.capture = scenario.rates.physics;
    scenario.quality.value.capture_block_capacity_frames = 200U;
    scenario.quality.value.event_journal_capacity_records = 3800U;
    scenario.mode = contract::HeldSpeed{
        resolved(kHeldRpm, "scenario.mode.engine_speed_rpm"),
        resolved(profile.core.mechanism.cranks.front().crank_tdc_reference_rad.value,
                 "scenario.mode.initial_theta_rad"),
        resolved(0.85, "scenario.mode.throttle_01"),
    };
    scenario.mode_resolution_id = "scenario.mode";

    contract::Sha256Digest request_identity;
    request_identity.bytes.front() = 1U;
    return {
        &authored,
        std::move(engine),
        std::move(scenario),
        request_identity,
    };
}

[[nodiscard]] simulation::LowOrderCapturePlan capture_plan(const Fixture &value) {
    auto result = simulation::compile_low_order_capture_plan(
        value.engine, value.scenario, finite_extent(value.scenario));
    const auto *plan = std::get_if<simulation::LowOrderCapturePlan>(&result);
    expect(plan != nullptr, "canonical operating capture plan was rejected");
    return *plan;
}

[[nodiscard]] simulation::LowOrderOperatingPointV1Runtime
operating_runtime(const Fixture &value, const simulation::LowOrderCapturePlan &plan) {
    auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    if (report != nullptr) {
        std::string message = "canonical operating runtime was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<simulation::LowOrderOperatingPointV1Runtime>(std::move(result));
}

[[nodiscard]] simulation::LowOrderEngineCoreV1Runtime
core_runtime(const Fixture &value) {
    expect(value.authored != nullptr, "authored fixture context was absent");
    const auto &profile = test::operating_profile(value.engine);
    const auto random_plan = test::compile_fixture_random_plan(
        *value.authored, value.engine, value.scenario);
    auto mechanism_plan_result =
        simulation::compile_mechanism_kinematics_plan(value.engine, profile.core);
    expect(!std::holds_alternative<contract::ValidationReport>(mechanism_plan_result),
           "canonical operating mechanism plan was rejected");
    auto mechanism_plan = std::get<simulation::SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));
    auto result = simulation::compile_low_order_engine_core_v1_runtime(
        value.engine, value.scenario, profile.core, random_plan,
        std::move(mechanism_plan), finite_extent(value.scenario));
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report == nullptr, "canonical operating core was rejected");
    return std::get<simulation::LowOrderEngineCoreV1Runtime>(std::move(result));
}

void advance_to_fixed_horizon(simulation::LowOrderEngineCoreV1Runtime &core,
                              simulation::LowOrderOperatingPointV1Runtime &operating) {
    while (operating.accepted_sample_count() <
           operating.fixed_preparation_horizon_frame_count()) {
        auto core_result = core.advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&core_result);
        expect(step != nullptr,
               "operating core ended before the fixed preparation horizon");
        auto result = operating.advance(step->mechanics.get(), step->gas.get());
        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            throw std::runtime_error{
                "operating runtime faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
    }
}

void test_complete_cycle_evidence_reaches_public_result(
    const test::AuthoredEngineFixture &authored) {
    auto value = fixture(authored);
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);
    advance_to_fixed_horizon(core, operating);

    expect(operating.finalized() && !operating.faulted() &&
               operating.operating_point_result().has_value(),
           "operating runtime did not finalize a fixed sample");
    const auto &point = *operating.operating_point_result();
    const auto &sampling = point.sampling;
    const auto &sample = sampling.trailing_complete_cycles;
    expect(point.simulation_request_identity_v7_sha256 == value.request_identity &&
               sampling.method ==
                   contract::fixed_horizon_cycle_sampling_method_identity() &&
               sampling.trailing_complete_cycle_count == kTrailingCompleteCycleCount &&
               std::bit_cast<std::uint64_t>(sampling.fixed_preparation_horizon_s) ==
                   std::bit_cast<std::uint64_t>(kFixedPreparationHorizonS) &&
               sample.completed_cycles.size() == kTrailingCompleteCycleCount &&
               sampling.last_eligible_completed_cycle_ordinal_at_fixed_horizon ==
                   sample.cycles.last_completed_cycle_ordinal &&
               sampling.last_eligible_cycle_end_boundary_at_fixed_horizon ==
                   sample.cycles.end_boundary,
           "public fixed-sample policy or last-eligible attestation changed");

    double indicated = 0.0;
    double loss = 0.0;
    double starter = 0.0;
    double brake = 0.0;
    std::vector<double> pressure_sums(sample.mean_boundary_pressures.size(), 0.0);
    for (const auto &cycle : sample.completed_cycles) {
        indicated += cycle.indicated_gas_work_j;
        loss += cycle.aggregate_loss_work_j;
        starter += cycle.starter_work_j;
        brake += cycle.brake_work_j;
        expect(cycle.end_boundary_pressures.size() ==
                   sample.mean_boundary_pressures.size(),
               "public completed cycle omitted end-boundary pressure lanes");
        for (std::size_t index = 0; index < cycle.end_boundary_pressures.size();
             ++index) {
            expect(cycle.end_boundary_pressures[index].gas_volume_id ==
                       sample.mean_boundary_pressures[index].gas_volume_id,
                   "public completed-cycle pressure identity/order changed");
            pressure_sums[index] += cycle.end_boundary_pressures[index].pressure_pa_abs;
        }
    }
    expect(std::bit_cast<std::uint64_t>(indicated) ==
                   std::bit_cast<std::uint64_t>(sample.indicated_gas_work_j) &&
               std::bit_cast<std::uint64_t>(loss) ==
                   std::bit_cast<std::uint64_t>(sample.aggregate_loss_work_j) &&
               std::bit_cast<std::uint64_t>(starter) ==
                   std::bit_cast<std::uint64_t>(sample.starter_work_j) &&
               std::bit_cast<std::uint64_t>(brake) ==
                   std::bit_cast<std::uint64_t>(sample.brake_work_j),
           "public sample totals changed the chronological reduction order");
    for (std::size_t index = 0; index < pressure_sums.size(); ++index) {
        const double mean =
            pressure_sums[index] / static_cast<double>(kTrailingCompleteCycleCount);
        expect(std::bit_cast<std::uint64_t>(mean) ==
                   std::bit_cast<std::uint64_t>(
                       sample.mean_boundary_pressures[index].pressure_pa_abs),
               "public pressure mean changed the chronological reduction order");
    }
}

void test_capture_plan_transplants_are_rejected(
    const test::AuthoredEngineFixture &authored) {
    const auto value = fixture(authored);
    auto plan = capture_plan(value);
    plan.scenario_id = "different-scenario";
    const auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(report->issues,
                                   [](const auto &issue) {
                                       return issue.path == "capture_plan.scenario_id";
                                   }),
           "runtime compiler admitted a capture plan from another scenario");
}

void test_scenario_mass_afr_is_exactly_bound_to_core_conversion(
    const test::AuthoredEngineFixture &authored) {
    auto value = fixture(authored);
    const auto plan = capture_plan(value);
    value.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value =
        std::nextafter(value.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
                       std::numeric_limits<double>::infinity());

    const auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.path ==
                              "scenario.fuel.stoichiometric_air_fuel_mass_ratio.value";
                   }),
           "runtime compiler admitted scenario mass AFR that was not bit-exact "
           "with the core pseudo-gas conversion");
}

void test_runtime_rejects_an_empty_internal_intake_set(
    const test::AuthoredEngineFixture &authored) {
    auto value = fixture(authored);
    const auto plan = capture_plan(value);
    auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        value.engine.physics_profile);
    profile.core.gas_path.intakes.clear();

    const auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.path ==
                              "engine.physics_profile.core.gas_path.intakes";
                   }),
           "runtime compiler admitted an empty internal intake set");
}

void test_capture_reports_absent_instantaneous_models_truthfully(
    const test::AuthoredEngineFixture &authored) {
    const auto value = fixture(authored);
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);
    auto core_result = core.advance();
    const auto &step = std::get<simulation::LowOrderEngineCoreV1StepView>(core_result);
    const auto result = operating.advance(step.mechanics.get(), step.gas.get());
    const auto *accepted =
        std::get_if<simulation::LowOrderOperatingPointV1Step>(&result);
    expect(
        accepted != nullptr &&
            accepted->capture_torque.pumping_partition.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted &&
            accepted->capture_torque.friction_pump_and_accessory.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted &&
            accepted->capture_torque.instantaneous_net_shaft.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted,
        "capture mislabeled an absent instantaneous model as missing input");
}

void test_runtime_rejects_foreign_controls_and_shape(
    const test::AuthoredEngineFixture &authored) {
    const auto value = fixture(authored);
    const auto plan = capture_plan(value);

    const auto expect_rejected = [&](const auto &mutate,
                                     std::string_view expected_detail,
                                     std::string_view message) {
        auto operating = operating_runtime(value, plan);
        auto core = core_runtime(value);
        auto result = core.advance();
        const auto &step = std::get<simulation::LowOrderEngineCoreV1StepView>(result);
        auto mechanics = step.mechanics.get();
        auto gas = step.gas.get();
        mutate(mechanics, gas);
        const auto rejected = operating.advance(mechanics, gas);
        const auto *failure = std::get_if<contract::FailureContext>(&rejected);
        expect(failure != nullptr && failure->detail_code == expected_detail, message);
    };

    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.requested_throttle_01 =
                std::nextafter(mechanics.requested_throttle_01, 1.0);
        },
        "operating-held-condition-disagreed",
        "runtime admitted a noncanonical held throttle");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.resolved_engine_throttle_01 =
                std::nextafter(mechanics.resolved_engine_throttle_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged resolved-engine throttle");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.intake_plate_position_01 =
                std::nextafter(mechanics.intake_plate_position_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged intake-plate position");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.main_flow_multiplier_01 =
                std::nextafter(mechanics.main_flow_multiplier_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged main-flow multiplier");
    expect_rejected([](auto &mechanics, auto &) { mechanics.limiter_timer_s = -0.0; },
                    "operating-disabled-limiter-disagreed",
                    "runtime admitted a noncanonical disabled-limiter timer");
    expect_rejected(
        [](auto &mechanics, auto &) { mechanics.limiter_cut_active = true; },
        "operating-disabled-limiter-disagreed",
        "runtime admitted an active cut from the disabled limiter");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.events.push_back({
                static_cast<std::uint8_t>(mechanics.events.size()),
                contract::LimiterStateChanged{false, true, false, 0.0},
            });
        },
        "operating-disabled-limiter-disagreed",
        "runtime admitted a mechanics limiter transition while limiter was "
        "disabled");
    expect_rejected(
        [](auto &, auto &gas) {
            gas.events.push_back({
                static_cast<std::uint8_t>(gas.events.size()),
                contract::LimiterStateChanged{false, true, false, 0.0},
            });
        },
        "operating-disabled-limiter-disagreed",
        "runtime admitted a gas limiter transition while limiter was disabled");
    expect_rejected(
        [](auto &, auto &gas) {
            gas.indicated_gas_torque_nm = std::nextafter(
                gas.indicated_gas_torque_nm, std::numeric_limits<double>::infinity());
        },
        "operating-indicated-torque-reduction-disagreed",
        "runtime admitted aggregate torque that differed from its ordered "
        "cylinder reduction");
    expect_rejected([](auto &, auto &gas) { gas.gas_volumes.pop_back(); },
                    "operating-transaction-shape-disagreed",
                    "runtime admitted a foreign gas transaction shape");
}

void run_tests(const std::filesystem::path &repository_root) {
    const auto authored = test::load_canonical_authored_engine_fixture(repository_root);
    test_capture_plan_transplants_are_rejected(authored);
    test_scenario_mass_afr_is_exactly_bound_to_core_conversion(authored);
    test_runtime_rejects_an_empty_internal_intake_set(authored);
    test_capture_reports_absent_instantaneous_models_truthfully(authored);
    test_runtime_rejects_foreign_controls_and_shape(authored);
    test_complete_cycle_evidence_reaches_public_result(authored);
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2,
               "usage: low_order_operating_point_v1_runtime_test <repository-root>");
        run_tests(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "low-order operating runtime test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
