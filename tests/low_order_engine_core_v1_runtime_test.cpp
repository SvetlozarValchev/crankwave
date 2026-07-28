#include "profiles/bmw_m52b28_parity_request_internal.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
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
    expect(sweep != nullptr, "short BMW scenario lost its prescribed sweep");
    return *sweep;
}

[[nodiscard]] contract::FixedRateRpmTrajectory &
fixed_rpm(contract::RenderScenario &scenario) {
    auto &sweep = prescribed_sweep(scenario);
    auto *rpm = std::get_if<contract::FixedRateRpmTrajectory>(&sweep.trajectory.rpm);
    expect(rpm != nullptr, "short BMW scenario lost its fixed-rate RPM lane");
    return *rpm;
}

[[nodiscard]] profiles::BmwM52b28ParityRequest make_short_request() {
    std::vector<double> rpm(kStepCount, kRpm);
    auto request =
        profiles::detail::build_bmw_m52b28_parity_request_unvalidated(std::move(rpm));
    request.scenario.scenario_id = "bmw-m52b28-short-composite-core";
    request.scenario.total_duration_s.value = kDurationS;
    request.scenario.audible_start_s.value = 0.0;
    request.scenario.audible_duration_s.value = kDurationS;
    auto *preparation =
        std::get_if<contract::FixedSettling>(&request.scenario.preparation);
    expect(preparation != nullptr,
           "short BMW scenario lost its fixed preparation policy");
    preparation->warm_up_duration_s.value = 0.0;
    preparation->settling_duration_s.value = 0.0;
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

void test_prescribed_transaction_and_stable_completion() {
    auto request = make_short_request();
    const auto &core =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile)
            .core;
    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, core));
    expect(runtime.expected_sample_count() == kStepCount &&
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

void test_held_speed_reuses_core_without_m3_loss_policy() {
    auto request = make_short_request();
    auto &profile =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile);
    profile.fixed_crank_loss.fixed_crank_friction_magnitude_nm.value = -1.0;
    request.engine.methods.losses.value.id = "not-consumed-by-core";
    request.scenario.mode = contract::HeldSpeed{
        {kRpm, "held-rpm"},
        {profile.core.mechanism.crank.crank_tdc_reference_rad.value, "held-angle"},
        {0.85, "held-throttle"},
    };

    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, profile.core));
    auto result = runtime.advance();
    const auto *step = std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
    expect(step != nullptr && step->mechanics.get().engine_speed_rpm == kRpm &&
               step->mechanics.get().requested_throttle_01 == 0.85 &&
               step->gas.get().sample_index == 0U,
           "held speed did not execute through the shared core");
}

void test_core_ignores_capture_transport_policy() {
    auto request = make_short_request();
    const auto &core =
        std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile)
            .core;
    request.scenario.rates.capture = {48000U, 1U};
    request.scenario.quality.value.capture_block_capacity_frames = 0U;
    request.scenario.quality.value.event_journal_capacity_records = 0U;

    auto runtime = require_runtime(simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, core));
    const auto result = runtime.advance();
    expect(std::holds_alternative<simulation::LowOrderEngineCoreV1StepView>(result),
           "capture transport policy leaked into the shared physics core");
}

void run_tests() {
    test_prescribed_transaction_and_stable_completion();
    test_held_speed_reuses_core_without_m3_loss_policy();
    test_core_ignores_capture_transport_policy();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Low-order composite runtime failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
