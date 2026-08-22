#include "bmw_m52b28_render_gate_support.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace compile = crankwave::compile;
namespace contract = crankwave::contract;
namespace gate = crankwave::test::bmw_m52b28_render_gate;
namespace simulation = crankwave::simulation;

constexpr double kPreparationDurationS = 5.0;
constexpr double kWotObservationDurationS = 2.0;
constexpr double kCoastObservationDurationS = 4.0;
constexpr double kLongBalanceObservationDurationS = 60.0;
constexpr double kLongBalanceWindowStartS = 22.5;
constexpr double kWotInitialRpm = 1500.0;
constexpr double kCoastInitialRpm = 6000.0;
constexpr double kWotTargetRpm = 7000.0;
constexpr double kPristineWotCrossingS = 0.4399;
// The canonical 20 kHz integration crosses at 0.4592 s. Keep this gate centered on
// the pristine source observation while admitting only that measured 19.3 ms delta.
constexpr double kWotCrossingToleranceS = 0.020;
constexpr double kCoastCrossingToleranceS = 0.010;
constexpr double kPristineLongBalanceRpm = 1041.953;
constexpr double kLongBalanceMeanToleranceRpm = 15.0;
constexpr double kLongBalanceInstantaneousToleranceRpm = 75.0;
constexpr std::uint64_t kExpectedLongBalanceSampleCount = UINT64_C(750001);
constexpr std::array<double, 5> kCoastTargetsRpm{
    5000.0, 4000.0, 3000.0, 2000.0, 1500.0,
};
constexpr std::array<double, kCoastTargetsRpm.size()> kPristineCoastCrossingsS{
    0.3756, 0.6742, 1.0666, 1.6765, 2.1784,
};

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::ostringstream message;
    for (const auto &issue : report.issues) {
        if (message.tellp() > 0) {
            message << "; ";
        }
        message << issue.path << ": " << issue.message;
    }
    return message.str().empty() ? "no validation detail" : message.str();
}

[[nodiscard]] contract::Sha256Digest nonzero_request_identity() {
    contract::Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

[[nodiscard]] simulation::LowOrderCaptureSession
require_session(simulation::LowOrderCaptureCompileResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        throw std::runtime_error{"controlled FreeEngine scenario failed admission: " +
                                 validation_text(*report)};
    }
    return std::get<simulation::LowOrderCaptureSession>(std::move(result));
}

[[nodiscard]] contract::RenderScenario
make_controlled_scenario(const contract::RenderScenario &compiled_scenario,
                         const double initial_rpm, const double released_throttle,
                         const double observation_duration_s) {
    auto scenario = compiled_scenario;
    auto *free_engine = std::get_if<contract::FreeEngine>(&scenario.mode);
    gate::expect(free_engine != nullptr,
                 "compiled BMW fixture is not a FreeEngine scenario");
    auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    gate::expect(preparation != nullptr,
                 "compiled BMW FreeEngine fixture lacks fixed-horizon preparation");

    free_engine->initial_engine_speed_rpm.value = initial_rpm;
    free_engine->throttle_01 = {
        contract::TrajectoryInterpolation::right_continuous_hold,
        released_throttle == 1.0
            ? std::vector<contract::ScalarTrajectoryPoint>{{0.0, 1.0}}
            : std::vector<contract::ScalarTrajectoryPoint>{
                  {0.0, 1.0},
                  {kPreparationDurationS, released_throttle},
              },
        "test.bmw-m52b28-source-parity.throttle",
    };
    free_engine->external_resisting_torque_nm = {
        contract::TrajectoryInterpolation::right_continuous_hold,
        {{0.0, 0.0}},
        "test.bmw-m52b28-source-parity.external-resistance",
    };

    preparation->fixed_preparation_horizon_s.value = kPreparationDurationS;
    scenario.operating_state.value = {
        {
            "test.bmw-m52b28-source-parity.running",
            0.0,
            {true, true, false, false, false},
        },
    };
    scenario.audible_start_s.value = kPreparationDurationS;
    scenario.audible_duration_s.value = observation_duration_s;
    scenario.total_duration_s.value = kPreparationDurationS + observation_duration_s;
    // The callback inspects every native 20 kHz sample, independent of publication
    // block boundaries. Keep the block within the authored event-journal bound.
    scenario.quality.value.capture_block_capacity_frames = 100U;
    return scenario;
}

struct Crossing {
    std::uint64_t capture_sample_index = 0;
    std::uint64_t step_end_index = 0;
    std::uint64_t released_frame_count = 0;
    double elapsed_s = 0.0;
    double observed_rpm = 0.0;
};

struct ControlledObservation {
    std::optional<Crossing> upward;
    std::array<std::optional<Crossing>, kCoastTargetsRpm.size()> downward;
    std::uint64_t balance_sample_count = 0;
    double balance_rpm_sum = 0.0;
    double balance_min_rpm = std::numeric_limits<double>::infinity();
    double balance_max_rpm = -std::numeric_limits<double>::infinity();
    double final_rpm = 0.0;
    std::uint64_t sample_count = 0;
};

[[nodiscard]] Crossing make_crossing(const std::uint64_t capture_sample_index,
                                     const std::uint64_t release_frame,
                                     const contract::EngineCaptureSample &sample,
                                     const contract::RationalRateHz &rate) {
    const auto released_frame_count =
        capture_sample_index - release_frame + UINT64_C(1);
    return {
        capture_sample_index,
        sample.step_end_index,
        released_frame_count,
        static_cast<double>(released_frame_count) *
            static_cast<double>(rate.denominator) / static_cast<double>(rate.numerator),
        sample.engine_speed_rpm,
    };
}

[[nodiscard]] ControlledObservation
run_controlled_scenario(const contract::EngineSpec &engine,
                        const contract::RandomPlan &random_plan,
                        const contract::RenderScenario &scenario, const double held_rpm,
                        const double released_throttle, const bool observe_upward,
                        const bool observe_long_balance = false) {
    const auto &preparation =
        std::get<contract::FixedHorizonCycleSampling>(scenario.preparation);
    const auto release_frame = contract::resolve_frame_index(
        preparation.fixed_preparation_horizon_s.value, scenario.rates.physics);
    const auto horizon = contract::resolve_frame_index(scenario.total_duration_s.value,
                                                       scenario.rates.physics);
    gate::expect(release_frame.has_value() && horizon.has_value() &&
                     *release_frame > 0U && *horizon > *release_frame,
                 "controlled scenario did not resolve to an ordered frame grid");
    const auto balance_window_first_frame =
        contract::resolve_frame_index(kLongBalanceWindowStartS, scenario.rates.physics);
    const auto balance_window_last_frame = contract::resolve_frame_index(
        kLongBalanceObservationDurationS, scenario.rates.physics);
    gate::expect(!observe_long_balance || (balance_window_first_frame.has_value() &&
                                           balance_window_last_frame.has_value()),
                 "long balance window did not resolve to exact physics frames");

    auto session = require_session(simulation::compile_low_order_capture_session(
        engine, scenario, random_plan, nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon)));

    ControlledObservation observation;
    double previous_rpm = held_rpm;
    bool saw_release_frame = false;
    while (true) {
        auto result = session.publish_next_block([&](const contract::CaptureBlockView
                                                         &block) {
            const auto report = contract::validate(block, engine, scenario);
            if (!report.ok()) {
                throw std::runtime_error{
                    "controlled capture block failed validation: " +
                    validation_text(report)};
            }
            gate::expect(block.clock().first_sample_index == observation.sample_count,
                         "controlled capture blocks lost contiguous sample order");

            for (std::size_t local_index = 0; local_index < block.engine().size();
                 ++local_index) {
                const auto capture_sample_index =
                    block.clock().first_sample_index + local_index;
                const auto &sample = block.engine()[local_index];
                gate::expect(std::isfinite(sample.engine_speed_rpm) &&
                                 sample.engine_speed_rpm > 0.0,
                             "controlled FreeEngine produced a nonpositive or "
                             "nonfinite RPM");

                if (capture_sample_index < *release_frame) {
                    gate::expect(
                        std::bit_cast<std::uint64_t>(sample.engine_speed_rpm) ==
                            std::bit_cast<std::uint64_t>(held_rpm),
                        "FreeEngine preparation did not hold the exact "
                        "requested RPM");
                    gate::expect(sample.requested_throttle_01 == 1.0,
                                 "controlled preparation was not full throttle");
                } else {
                    const auto released_frame_count =
                        capture_sample_index - *release_frame + UINT64_C(1);
                    if (capture_sample_index == *release_frame) {
                        saw_release_frame = true;
                        gate::expect(sample.requested_throttle_01 == released_throttle,
                                     "released frame did not apply its exact "
                                     "controlled throttle");
                    }
                    if (observe_upward) {
                        if (!observation.upward.has_value() &&
                            previous_rpm < kWotTargetRpm &&
                            sample.engine_speed_rpm >= kWotTargetRpm) {
                            observation.upward =
                                make_crossing(capture_sample_index, *release_frame,
                                              sample, scenario.rates.physics);
                        }
                    } else {
                        for (std::size_t index = 0; index < kCoastTargetsRpm.size();
                             ++index) {
                            if (!observation.downward[index].has_value() &&
                                previous_rpm > kCoastTargetsRpm[index] &&
                                sample.engine_speed_rpm <= kCoastTargetsRpm[index]) {
                                observation.downward[index] =
                                    make_crossing(capture_sample_index, *release_frame,
                                                  sample, scenario.rates.physics);
                            }
                        }
                    }
                    if (observe_long_balance &&
                        released_frame_count >= *balance_window_first_frame &&
                        released_frame_count <= *balance_window_last_frame) {
                        ++observation.balance_sample_count;
                        observation.balance_rpm_sum += sample.engine_speed_rpm;
                        observation.balance_min_rpm = std::min(
                            observation.balance_min_rpm, sample.engine_speed_rpm);
                        observation.balance_max_rpm = std::max(
                            observation.balance_max_rpm, sample.engine_speed_rpm);
                    }
                }
                observation.final_rpm = sample.engine_speed_rpm;
                previous_rpm = sample.engine_speed_rpm;
            }
            observation.sample_count += block.frame_count();
            return true;
        });

        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            throw std::runtime_error{
                "controlled FreeEngine capture faulted (" + failure->detail_code +
                "): " + failure->state_summary +
                "; time-s=" + std::to_string(failure->scenario_time_s)};
        }
        if (const auto *completed =
                std::get_if<simulation::LowOrderCaptureCompleted>(&result)) {
            gate::expect(completed->sample_count == *horizon &&
                             observation.sample_count == *horizon &&
                             session.completed() && !session.faulted() &&
                             saw_release_frame,
                         "controlled FreeEngine capture did not complete its exact "
                         "frame horizon");
            return observation;
        }
    }
}

void print_crossing(const std::string_view label, const Crossing &crossing,
                    const double pristine_elapsed_s) {
    std::cout << label << ": released-frames=" << crossing.released_frame_count
              << ", elapsed-s=" << std::fixed << std::setprecision(4)
              << crossing.elapsed_s << ", pristine-s=" << pristine_elapsed_s
              << ", absolute-error-s="
              << std::abs(crossing.elapsed_s - pristine_elapsed_s)
              << ", capture-sample=" << crossing.capture_sample_index
              << ", step-end=" << crossing.step_end_index
              << ", observed-rpm=" << std::setprecision(9) << crossing.observed_rpm
              << '\n';
}

void require_source_timing(const std::string_view label, const Crossing &crossing,
                           const double pristine_elapsed_s, const double tolerance_s) {
    const auto absolute_error_s = std::abs(crossing.elapsed_s - pristine_elapsed_s);
    if (absolute_error_s <= tolerance_s) {
        return;
    }
    throw std::runtime_error{std::string{label} +
                             " exceeded its pristine timing tolerance; actual-s=" +
                             std::to_string(crossing.elapsed_s) +
                             "; pristine-s=" + std::to_string(pristine_elapsed_s) +
                             "; absolute-error-s=" + std::to_string(absolute_error_s) +
                             "; tolerance-s=" + std::to_string(tolerance_s)};
}

void run(const std::filesystem::path &repository_root,
         const bool observe_long_balance) {
    const auto compiled = gate::compile_authored_free_engine_scenario(repository_root);
    const auto inputs = compile::detail::CompiledScenarioViewAccess::inputs(compiled);

    const auto wot_scenario = make_controlled_scenario(
        inputs.scenario.scenario, kWotInitialRpm, 1.0, kWotObservationDurationS);
    const auto coast_scenario =
        make_controlled_scenario(inputs.scenario.scenario, kCoastInitialRpm, 0.0,
                                 observe_long_balance ? kLongBalanceObservationDurationS
                                                      : kCoastObservationDurationS);

    const auto wot =
        run_controlled_scenario(inputs.engine.engine, inputs.scenario.random_plan,
                                wot_scenario, kWotInitialRpm, 1.0, true);
    const auto coast = run_controlled_scenario(
        inputs.engine.engine, inputs.scenario.random_plan, coast_scenario,
        kCoastInitialRpm, 0.0, false, observe_long_balance);

    gate::expect(wot.upward.has_value(),
                 "BMW WOT release did not cross 7000 RPM within the broad "
                 "observation horizon");
    print_crossing("WOT 1500->7000", *wot.upward, kPristineWotCrossingS);
    require_source_timing("WOT 1500->7000", *wot.upward, kPristineWotCrossingS,
                          kWotCrossingToleranceS);

    std::uint64_t previous_crossing_frame = 0;
    for (std::size_t index = 0; index < kCoastTargetsRpm.size(); ++index) {
        gate::expect(coast.downward[index].has_value(),
                     "BMW coast release did not cross every target within the broad "
                     "observation horizon");
        const auto &crossing = *coast.downward[index];
        gate::expect(index == 0U ||
                         crossing.released_frame_count > previous_crossing_frame,
                     "BMW coast target crossings were not strictly ordered");
        previous_crossing_frame = crossing.released_frame_count;
        const auto label =
            "coast 6000->" +
            std::to_string(static_cast<std::uint32_t>(kCoastTargetsRpm[index]));
        print_crossing(label, crossing, kPristineCoastCrossingsS[index]);
        require_source_timing(label, crossing, kPristineCoastCrossingsS[index],
                              kCoastCrossingToleranceS);
    }

    if (observe_long_balance) {
        gate::expect(coast.balance_sample_count == kExpectedLongBalanceSampleCount &&
                         std::isfinite(coast.balance_rpm_sum) &&
                         std::isfinite(coast.balance_min_rpm) &&
                         std::isfinite(coast.balance_max_rpm),
                     "long coast balance window did not produce its exact finite "
                     "sample interval");
        const auto balance_mean_rpm =
            coast.balance_rpm_sum / static_cast<double>(coast.balance_sample_count);
        std::cout << "coast balance 22.5-60.0s: samples=" << coast.balance_sample_count
                  << ", mean-rpm=" << std::fixed << std::setprecision(9)
                  << balance_mean_rpm << ", min-rpm=" << coast.balance_min_rpm
                  << ", max-rpm=" << coast.balance_max_rpm
                  << ", final-rpm=" << coast.final_rpm
                  << ", pristine-center-rpm=" << kPristineLongBalanceRpm << '\n';
        gate::expect(
            std::abs(balance_mean_rpm - kPristineLongBalanceRpm) <=
                    kLongBalanceMeanToleranceRpm &&
                coast.balance_min_rpm >=
                    kPristineLongBalanceRpm - kLongBalanceInstantaneousToleranceRpm &&
                coast.balance_max_rpm <=
                    kPristineLongBalanceRpm + kLongBalanceInstantaneousToleranceRpm &&
                coast.balance_min_rpm <= kPristineLongBalanceRpm &&
                coast.balance_max_rpm >= kPristineLongBalanceRpm,
            "long coast balance mean or instantaneous range escaped its "
            "source-centered acceptance");
    }
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        const bool observe_long_balance =
            argc == 3 && std::string_view{argv[2]} == "--long-balance";
        gate::expect(argc == 2 || observe_long_balance,
                     "usage: bmw_m52b28_free_engine_source_parity_test "
                     "<repository-root> [--long-balance]");
        run(std::filesystem::canonical(argv[1]), observe_long_balance);
    } catch (const std::exception &error) {
        std::cerr << "BMW FreeEngine source-parity gate failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
