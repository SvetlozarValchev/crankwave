#include "authored_engine_fixture_support.hpp"

#include "engine_sim_offline/contract/capture.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

namespace contract = engine_sim_offline::contract;
namespace simulation = engine_sim_offline::simulation;
namespace test = engine_sim_offline::test;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result.empty() ? "no validation detail" : result;
}

[[nodiscard]] contract::Sha256Digest nonzero_request_identity() {
    contract::Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

void run(const std::filesystem::path &repository_root) {
    const auto fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/bmw-m52tub28-cleanroom/engine.json",
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "held-dyno-pull-lift-1500-6500rpm.json");
    const auto *dyno = std::get_if<contract::HeldDyno>(&fixture.scenario.mode);
    expect(dyno != nullptr, "authored BMW held-dyno mode was not resolved");
    const auto horizon = contract::resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    const auto release = contract::resolve_frame_index(
        fixture.scenario.audible_start_s.value, fixture.scenario.rates.physics);
    expect(horizon.has_value() && release.has_value() && *release == 30000U &&
               *horizon == 180000U &&
               dyno->target_engine_speed_rpm.post_step_rpm.size() == *horizon,
           "held-dyno authored frame grid was not resolved exactly");

    const auto random_plan = test::compile_fixture_random_plan(fixture);
    auto compile_result = simulation::compile_low_order_capture_session(
        fixture.engine, fixture.scenario, random_plan, nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    if (const auto *report = std::get_if<contract::ValidationReport>(&compile_result)) {
        throw std::runtime_error{"held-dyno runtime admission failed: " +
                                 validation_text(*report)};
    }
    auto session =
        std::get<simulation::LowOrderCaptureSession>(std::move(compile_result));

    std::uint64_t observed_frames = 0U;
    double maximum_pull_tracking_error_rpm = 0.0;
    std::uint64_t maximum_pull_tracking_error_frame = 0U;
    double maximum_pull_tracking_actual_rpm = 0.0;
    double maximum_pull_tracking_target_rpm = 0.0;
    double pull_midpoint_reaction_nm = 0.0;
    double hold_rpm = 0.0;
    double first_lift_throttle = 0.0;
    double final_rpm = 0.0;
    double final_actuator_torque_nm = 0.0;
    double final_dyno_reaction_nm = 0.0;
    bool final_dyno_telemetry_available = false;
    while (true) {
        auto result =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                for (std::size_t index = 0; index < block.engine().size(); ++index) {
                    const auto sample_index = block.clock().first_sample_index + index;
                    const auto &sample = block.engine()[index];
                    expect(sample.dyno_enabled && !sample.starter_enabled &&
                               std::isfinite(sample.engine_speed_rpm) &&
                               sample.engine_speed_rpm > 0.0,
                           "held dyno published an invalid operating state");
                    if (sample_index < *release) {
                        expect(std::bit_cast<std::uint64_t>(sample.engine_speed_rpm) ==
                                   std::bit_cast<std::uint64_t>(1500.0),
                               "held-dyno preparation did not hold exact initial RPM");
                    } else if (sample_index < 140000U) {
                        const double target =
                            dyno->target_engine_speed_rpm.post_step_rpm[sample_index];
                        const double error = std::abs(sample.engine_speed_rpm - target);
                        if (error > maximum_pull_tracking_error_rpm) {
                            maximum_pull_tracking_error_rpm = error;
                            maximum_pull_tracking_error_frame = sample_index;
                            maximum_pull_tracking_actual_rpm = sample.engine_speed_rpm;
                            maximum_pull_tracking_target_rpm = target;
                        }
                    }
                    if (sample_index == 80000U) {
                        expect(sample.torque.dyno_reaction.availability ==
                                       contract::Availability::available &&
                                   sample.torque.actuator.availability ==
                                       contract::Availability::available &&
                                   sample.torque.dyno_reaction.value_nm ==
                                       -sample.torque.actuator.value_nm,
                               "held dyno did not publish exact actuator reaction");
                        pull_midpoint_reaction_nm =
                            sample.torque.dyno_reaction.value_nm;
                    }
                    if (sample_index == 135000U) {
                        hold_rpm = sample.engine_speed_rpm;
                    }
                    if (sample_index == 140000U) {
                        first_lift_throttle = sample.requested_throttle_01;
                    }
                    final_rpm = sample.engine_speed_rpm;
                    final_actuator_torque_nm = sample.torque.actuator.value_nm;
                    final_dyno_reaction_nm = sample.torque.dyno_reaction.value_nm;
                    final_dyno_telemetry_available =
                        sample.torque.actuator.availability ==
                            contract::Availability::available &&
                        sample.torque.dyno_reaction.availability ==
                            contract::Availability::available;
                }
                observed_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            throw std::runtime_error{"held-dyno capture faulted (" +
                                     failure->detail_code +
                                     "): " + failure->state_summary};
        }
        if (const auto *completed =
                std::get_if<simulation::LowOrderCaptureCompleted>(&result)) {
            expect(completed->sample_count == *horizon && observed_frames == *horizon &&
                       session.completed() && !session.faulted(),
                   "held-dyno capture did not complete its exact horizon");
            break;
        }
    }

    std::cout << "maximum-pull-tracking-error-rpm=" << maximum_pull_tracking_error_rpm
              << "; error-frame=" << maximum_pull_tracking_error_frame
              << "; actual-rpm=" << maximum_pull_tracking_actual_rpm
              << "; target-rpm=" << maximum_pull_tracking_target_rpm
              << "; midpoint-dyno-reaction-nm=" << pull_midpoint_reaction_nm
              << "; hold-rpm=" << hold_rpm << "; final-rpm=" << final_rpm << '\n';
    expect(maximum_pull_tracking_error_rpm < 25.0,
           "one-sided bounded dyno pull diverged excessively from its target");
    expect(pull_midpoint_reaction_nm > 0.0,
           "WOT pull did not require positive absorbing reaction");
    expect(std::abs(hold_rpm - 6500.0) < 1e-8, "held-dyno plateau missed 6500 RPM");
    expect(first_lift_throttle == 0.04,
           "lift boundary did not apply the authored throttle");
    expect(final_rpm < 3000.0 && final_rpm > 0.0,
           "ordinary overrun did not produce a positive speed fall");
    expect(final_dyno_telemetry_available && final_actuator_torque_nm == 0.0 &&
               final_dyno_reaction_nm == -final_actuator_torque_nm,
           "zero-drive overrun did not expose an exactly inactive dyno actuator");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "BMW held-dyno failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
