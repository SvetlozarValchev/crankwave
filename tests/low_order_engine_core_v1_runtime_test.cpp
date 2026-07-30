#include "authored_engine_fixture_support.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

inline constexpr std::size_t kStepCount = 401U;
inline constexpr double kRpm = 2400.0;
inline constexpr double kDurationS = static_cast<double>(kStepCount) / 10000.0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[noreturn]] void fail_report(std::string_view context,
                              const contract::ValidationReport &report) {
    std::ostringstream message;
    message << context;
    for (const auto &issue : report.issues) {
        message << "\n  " << issue.path << ": " << issue.message;
    }
    throw std::runtime_error{message.str()};
}

[[nodiscard]] contract::PrescribedKinematicSweep &
prescribed_sweep(contract::RenderScenario &scenario) {
    auto *sweep = std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    expect(sweep != nullptr, "short authored scenario lost its prescribed sweep");
    return *sweep;
}

[[nodiscard]] contract::FixedRateRpmTrajectory &
fixed_rpm(contract::RenderScenario &scenario) {
    auto &sweep = prescribed_sweep(scenario);
    auto *rpm = std::get_if<contract::FixedRateRpmTrajectory>(&sweep.trajectory.rpm);
    expect(rpm != nullptr, "short authored scenario lost its fixed-rate RPM lane");
    return *rpm;
}

[[nodiscard]] test::AuthoredEngineFixture
make_short_request(const test::AuthoredEngineFixture &canonical) {
    std::vector<double> rpm(kStepCount, kRpm);
    auto request = test::make_prescribed_fixture(canonical, std::move(rpm));
    request.scenario.scenario_id = "authored-short-composite-core";
    request.scenario.total_duration_s.value = kDurationS;
    request.scenario.audible_start_s.value = 0.0;
    request.scenario.audible_duration_s.value = kDurationS;
    request.scenario.preparation = contract::FixedSettling{
        {0.0, "authored-fixture.no-warm-up"},
        {0.0, "authored-fixture.no-settling"},
    };
    request.scenario.operating_state.value = {
        {
            "short-run-fired",
            0.0,
            {true, true, false, true, true},
        },
    };
    prescribed_sweep(request.scenario).throttle_01.points = {{0.0, 0.85}};
    auto &trajectory = fixed_rpm(request.scenario);
    trajectory.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(trajectory.post_step_rpm);
    return request;
}

[[nodiscard]] simulation::LowOrderEngineCoreV1Runtime
require_runtime(simulation::LowOrderEngineCoreV1CompileResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        fail_report("valid low-order composite runtime was rejected", *report);
    }
    return std::get<simulation::LowOrderEngineCoreV1Runtime>(std::move(result));
}

[[nodiscard]] contract::RandomPlan
random_plan(const test::AuthoredEngineFixture &request) {
    return test::compile_fixture_random_plan(request);
}

[[nodiscard]] simulation::LowOrderExecutionExtent
finite_extent(const contract::RenderScenario &scenario) {
    const auto frame_count = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    expect(frame_count.has_value(),
           "test scenario did not resolve to an integral physics horizon");
    return simulation::LowOrderExecutionExtent::finite_scenario(*frame_count);
}

void test_prescribed_transaction_and_stable_completion(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    const auto &core = test::low_order_core(request.engine);
    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, core, random_plan(request),
        finite_extent(request.scenario)));
    expect(runtime.execution_extent().finite_physics_frame_count() == kStepCount &&
               runtime.produced_sample_count() == 0U && !runtime.completed() &&
               !runtime.faulted(),
           "fresh composite runtime has the wrong bounded state");

    for (std::uint64_t index = 0; index < kStepCount; ++index) {
        auto result = runtime.advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
        expect(step != nullptr, "composite runtime did not publish one paired step");
        const auto &mechanics = step->mechanics.get();
        const auto &gas = step->gas.get();
        expect(mechanics.sample_index == index &&
                   mechanics.step_end_index == index + 1U &&
                   mechanics.timestamp_tick == mechanics.step_end_index &&
                   gas.sample_index == mechanics.sample_index &&
                   gas.step_end_index == mechanics.step_end_index &&
                   gas.timestamp_tick == mechanics.timestamp_tick &&
                   gas.rate == mechanics.rate &&
                   gas.cylinders.size() == mechanics.cylinders.size() &&
                   std::isfinite(gas.indicated_gas_torque_nm) &&
                   runtime.produced_sample_count() == index + 1U,
               "composite mechanics/gas transaction lost clock, shape, or count "
               "coherence");
    }

    expect(runtime.completed() && !runtime.faulted(),
           "composite runtime did not complete at its compiled horizon");
    const auto first_completion = runtime.advance();
    const auto repeated_completion = runtime.advance();
    const auto *first =
        std::get_if<simulation::LowOrderEngineCoreV1Completed>(&first_completion);
    const auto *repeated =
        std::get_if<simulation::LowOrderEngineCoreV1Completed>(&repeated_completion);
    expect(first != nullptr && repeated != nullptr &&
               first->sample_count == kStepCount && *first == *repeated,
           "composite completion is not terminal and stable");
}

void test_held_speed_reuses_core_without_aggregate_loss_policy(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    auto &profile = test::operating_profile(request.engine);
    profile.aggregate_loss.constant_fmep_bar.value = -1.0;
    request.engine.methods.losses.value.id = "not-consumed-by-core";
    request.scenario.mode = contract::HeldSpeed{
        {kRpm, "held-rpm"},
        {profile.core.mechanism.crank.crank_tdc_reference_rad.value, "held-angle"},
        {0.85, "held-throttle"},
    };

    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, profile.core, random_plan(request),
        finite_extent(request.scenario)));
    auto result = runtime.advance();
    const auto *step = std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
    expect(step != nullptr && step->mechanics.get().engine_speed_rpm == kRpm &&
               step->mechanics.get().requested_throttle_01 == 0.85 &&
               step->gas.get().sample_index == 0U,
           "held speed did not execute through the shared core");
}

void test_core_ignores_capture_transport_policy(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    const auto &core = test::low_order_core(request.engine);
    request.scenario.rates.capture = {48000U, 1U};
    request.scenario.quality.value.capture_block_capacity_frames = 0U;
    request.scenario.quality.value.event_journal_capacity_records = 0U;

    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, core, random_plan(request),
        finite_extent(request.scenario)));
    const auto result = runtime.advance();
    expect(std::holds_alternative<simulation::LowOrderEngineCoreV1StepView>(result),
           "capture transport policy leaked into the shared physics core");
}

void test_core_pairs_external_post_step_motion_with_the_same_gas_transaction(
    const test::AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    const auto &core = test::low_order_core(request.engine);
    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, core, random_plan(request),
        finite_extent(request.scenario)));

    constexpr double kExternalRpm = 1800.0;
    auto result = runtime.advance(simulation::PostStepCrankMotion{kExternalRpm, 0.017});
    const auto *step = std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
    expect(step != nullptr && step->mechanics.get().engine_speed_rpm == kExternalRpm &&
               step->mechanics.get().sample_index == 0U &&
               step->gas.get().sample_index == 0U &&
               step->mechanics.get().timestamp_tick == step->gas.get().timestamp_tick &&
               runtime.produced_sample_count() == 1U,
           "external post-step motion was not paired with one gas transaction");
}

void test_canonical_authored_operating_profile_uses_limiter_disabled_core(
    const test::AuthoredEngineFixture &canonical) {
    auto engine = canonical.engine;
    const auto &operating = test::operating_profile(engine);

    auto scenario = canonical.scenario;
    scenario.scenario_id = "authored-operating-core-limiter-disabled";
    auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    expect(preparation != nullptr,
           "canonical authored scenario lost fixed-horizon preparation");
    preparation->fixed_preparation_horizon_s.value = 0.1;
    preparation->trailing_complete_cycle_count.value = 1U;
    scenario.operating_state.value = {
        {
            "held-running",
            0.0,
            {true, true, false, true, false},
        },
    };
    scenario.total_duration_s.value = 0.1006;
    scenario.audible_start_s.value = 0.1;
    scenario.audible_duration_s.value = 0.0006;
    const auto *inertial = std::get_if<contract::InertialDyno>(&scenario.mode);
    expect(inertial != nullptr,
           "canonical authored scenario lost inertial-dyno ownership");
    auto engine_speed = inertial->initial_engine_speed_rpm;
    engine_speed.value = kRpm;
    const auto initial_theta = inertial->initial_theta_rad;
    auto throttle = contract::ResolvedValue<double>{
        inertial->throttle_01.points.front().value,
        inertial->throttle_01.resolution_id,
    };
    throttle.value = 0.85;
    scenario.mode = contract::HeldSpeed{
        std::move(engine_speed),
        initial_theta,
        std::move(throttle),
    };

    const auto pairing = contract::validate_for_engine(scenario, engine);
    if (!pairing.ok()) {
        fail_report("canonical authored held operating scenario was rejected", pairing);
    }
    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        engine, scenario, operating.core,
        test::compile_fixture_random_plan(canonical, engine, scenario),
        finite_extent(scenario)));
    auto result = runtime.advance();
    const auto *step = std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
    expect(step != nullptr, "canonical authored operating core produced no first step");
    const auto &mechanics = step->mechanics.get();
    expect(!mechanics.operating_state.limiter_enabled &&
               !mechanics.limiter_cut_active && mechanics.limiter_timer_s == 0.0 &&
               !std::signbit(mechanics.limiter_timer_s) &&
               std::none_of(
                   mechanics.events.begin(), mechanics.events.end(),
                   [](const auto &event) {
                       return std::holds_alternative<contract::LimiterStateChanged>(
                           event.payload);
                   }),
           "canonical authored operating core did not preserve disabled-limiter "
           "state");
}

void run_tests(const test::AuthoredEngineFixture &canonical) {
    test_prescribed_transaction_and_stable_completion(canonical);
    test_held_speed_reuses_core_without_aggregate_loss_policy(canonical);
    test_core_ignores_capture_transport_policy(canonical);
    test_core_pairs_external_post_step_motion_with_the_same_gas_transaction(canonical);
    test_canonical_authored_operating_profile_uses_limiter_disabled_core(canonical);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const auto canonical = test::load_canonical_authored_engine_fixture(
            std::filesystem::canonical(argv[1]));
        run_tests(canonical);
    } catch (const std::exception &error) {
        std::cerr << "Low-order composite runtime failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
