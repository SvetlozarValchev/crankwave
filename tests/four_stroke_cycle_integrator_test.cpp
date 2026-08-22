#include "simulation/four_stroke_cycle_integrator.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave::simulation;

constexpr double kCycleRadians = 4.0 * std::numbers::pi_v<double>;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] FourStrokeCycleIntegrator
make_integrator(double reference_theta_rad = 0.0, double displacement_m3 = 0.0028) {
    auto compiled =
        compile_four_stroke_cycle_integrator({reference_theta_rad, displacement_m3});
    const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&compiled);
    expect(error == nullptr, "valid cycle-integration plan was rejected");
    return std::get<FourStrokeCycleIntegrator>(std::move(compiled));
}

[[nodiscard]] CycleTorqueSample
sample(std::uint64_t index, double theta_rad, double time_s,
       double indicated_gas_torque_nm,
       double friction_pump_and_accessory_torque_nm = 0.0,
       double starter_torque_nm = 0.0) {
    return {
        index,
        time_s,
        theta_rad,
        indicated_gas_torque_nm,
        friction_pump_and_accessory_torque_nm,
        starter_torque_nm,
    };
}

[[nodiscard]] std::vector<CompletedFourStrokeCycle>
feed(FourStrokeCycleIntegrator &integrator,
     const std::vector<CycleTorqueSample> &samples,
     std::vector<FourStrokeCycleBoundaryCrossing> *crossings = nullptr) {
    std::vector<CompletedFourStrokeCycle> completed;
    for (const auto &value : samples) {
        auto result = integrator.advance(value);
        if (const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&result)) {
            throw std::runtime_error{
                "valid sample stream faulted with code " +
                std::to_string(static_cast<unsigned>(error->code))};
        }
        if (auto *crossing = std::get_if<FourStrokeCycleBoundaryCrossing>(&result)) {
            if (crossing->completed_cycle.has_value()) {
                completed.push_back(*crossing->completed_cycle);
            }
            if (crossings != nullptr) {
                crossings->push_back(std::move(*crossing));
            }
        }
    }
    return completed;
}

void test_exact_and_bracketed_boundary_crossing_evidence() {
    {
        auto integrator = make_integrator();
        const auto initialized = integrator.advance(sample(4, 0.0, 1.0, 12.0));
        expect(std::holds_alternative<NoFourStrokeCycleBoundaryCrossing>(initialized),
               "exact start sample was mislabeled as a crossed segment");

        const auto result = integrator.advance(sample(9, kCycleRadians, 3.0, 20.0));
        const auto *crossing = std::get_if<FourStrokeCycleBoundaryCrossing>(&result);
        expect(crossing != nullptr && crossing->completed_cycle.has_value() &&
                   crossing->boundary == CycleBoundaryEvidence{9, 9, 0.0} &&
                   crossing->theta_rad == kCycleRadians && crossing->time_s == 3.0 &&
                   crossing->completed_cycle->end_boundary == crossing->boundary,
               "exact-right-sample boundary crossing evidence changed");
        expect(interpolate_cycle_boundary_scalar(13.0, 29.0, crossing->boundary) ==
                   29.0,
               "exact-right-sample scalar interpolation selected the left value");
    }

    {
        auto integrator = make_integrator();
        (void)integrator.advance(sample(10, kCycleRadians - 1.0, 10.0, 30.0));
        const auto result =
            integrator.advance(sample(11, kCycleRadians + 3.0, 14.0, 70.0));
        const auto *crossing = std::get_if<FourStrokeCycleBoundaryCrossing>(&result);
        expect(crossing != nullptr && !crossing->completed_cycle.has_value() &&
                   crossing->boundary.left_bracket_sample_index == 10 &&
                   crossing->boundary.right_bracket_sample_index == 11 &&
                   crossing->theta_rad == kCycleRadians,
               "bracketed discarded-partial boundary crossing was not returned");
        expect_near(crossing->boundary.fraction_from_left_01, 0.25, 1e-15,
                    "bracketed boundary fraction changed");
        expect_near(crossing->time_s, 11.0, 1e-15, "bracketed boundary time changed");
        expect_near(
            interpolate_cycle_boundary_scalar(120000.0, 200000.0, crossing->boundary),
            140000.0, 1e-12,
            "external scalar interpolation diverged from boundary evidence");
    }
}

void test_constant_torque_discards_initial_partial_cycle() {
    auto integrator = make_integrator();
    std::vector<CycleTorqueSample> samples;
    std::uint64_t index = 0;
    for (double theta = 1.0; theta <= 2.0 * kCycleRadians + 1.0; theta += 0.37) {
        samples.push_back(sample(index++, theta, theta / 100.0, 100.0, -10.0));
    }
    samples.push_back(sample(index, 2.0 * kCycleRadians + 1.0,
                             (2.0 * kCycleRadians + 1.0) / 100.0, 100.0, -10.0));

    std::vector<FourStrokeCycleBoundaryCrossing> crossings;
    const auto completed = feed(integrator, samples, &crossings);
    expect(completed.size() == 1,
           "initial partial cycle was published or full cycle was lost");
    expect(crossings.size() == 2 && !crossings.front().completed_cycle.has_value() &&
               crossings.back().completed_cycle.has_value(),
           "first discarded-partial boundary was not returned separately");
    const auto &cycle = completed.front();
    expect(cycle.completed_cycle_ordinal == 0 &&
               cycle.start_boundary.left_bracket_sample_index <
                   cycle.start_boundary.right_bracket_sample_index &&
               cycle.end_boundary.left_bracket_sample_index <
                   cycle.end_boundary.right_bracket_sample_index &&
               cycle.start_boundary.fraction_from_left_01 > 0.0 &&
               cycle.start_boundary.fraction_from_left_01 < 1.0 &&
               cycle.end_boundary.fraction_from_left_01 > 0.0 &&
               cycle.end_boundary.fraction_from_left_01 < 1.0,
           "completed cycle boundary evidence is invalid");
    expect_near(cycle.start_theta_rad, kCycleRadians, 1e-13,
                "cycle start boundary changed");
    expect_near(cycle.end_theta_rad, 2.0 * kCycleRadians, 1e-13,
                "cycle end boundary changed");
    expect(crossings.front().boundary == cycle.start_boundary &&
               crossings.front().theta_rad == cycle.start_theta_rad &&
               crossings.front().time_s == cycle.start_time_s &&
               crossings.back().boundary == cycle.end_boundary &&
               crossings.back().theta_rad == cycle.end_theta_rad &&
               crossings.back().time_s == cycle.end_time_s,
           "crossing evidence diverged from completed-cycle boundary evidence");
    expect_near(cycle.summed_torque_work_j, 90.0 * kCycleRadians, 2e-11,
                "constant-torque cycle work changed");
    expect_near(cycle.cycle_mean_summed_torque_nm, 90.0, 2e-12,
                "constant-torque cycle mean changed");
    expect_near(cycle.summed_torque_mean_effective_pressure_pa,
                90.0 * kCycleRadians / 0.0028, 1e-7,
                "constant-torque mean effective pressure changed");
    expect_near(cycle.cycle_mean_summed_power_w, 9000.0, 2e-10,
                "constant-speed cycle power changed");
}

void test_linear_torque_boundary_split_is_analytic() {
    auto integrator = make_integrator();
    constexpr double offset = 7.0;
    constexpr double slope = 2.5;
    constexpr double omega = 123.0;

    std::vector<CycleTorqueSample> samples;
    std::uint64_t index = 0;
    for (double theta = 0.41; theta <= 2.0 * kCycleRadians + 0.8; theta += 0.83) {
        samples.push_back(
            sample(index++, theta, theta / omega, offset + slope * theta));
    }
    samples.push_back(sample(index, 2.0 * kCycleRadians + 0.8,
                             (2.0 * kCycleRadians + 0.8) / omega,
                             offset + slope * (2.0 * kCycleRadians + 0.8)));

    const auto completed = feed(integrator, samples);
    expect(completed.size() == 1,
           "linear-torque stream did not publish one complete cycle");
    const double start = kCycleRadians;
    const double end = 2.0 * kCycleRadians;
    const double expected_work =
        offset * (end - start) + 0.5 * slope * (end * end - start * start);
    expect_near(completed.front().indicated_gas_work_j, expected_work, 3e-11,
                "linear-torque indicated work changed");
    expect_near(completed.front().summed_torque_work_j, expected_work, 3e-11,
                "linear-torque net work changed");
    expect_near(completed.front().cycle_mean_summed_power_w,
                expected_work / ((end - start) / omega), 2e-9,
                "linear-torque mean power changed");
}

void test_component_work_and_stable_summed_signs() {
    auto integrator = make_integrator();
    const std::vector samples{
        sample(0, 0.0, 0.0, 120.0, -20.0, 5.0),
        sample(1, kCycleRadians, 2.0, 120.0, -20.0, 5.0),
    };
    const auto completed = feed(integrator, samples);
    expect(completed.size() == 1, "exact-boundary component cycle did not complete");
    const auto &cycle = completed.front();
    expect_near(cycle.indicated_gas_work_j, 120.0 * kCycleRadians, 1e-12,
                "indicated component work changed");
    expect_near(cycle.friction_pump_and_accessory_work_j, -20.0 * kCycleRadians, 1e-12,
                "friction component work changed");
    expect_near(cycle.starter_work_j, 5.0 * kCycleRadians, 1e-12,
                "starter component work changed");
    expect_near(cycle.summed_torque_work_j, 105.0 * kCycleRadians, 1e-12,
                "stable net component order changed");
    expect_near(cycle.cycle_mean_summed_torque_nm, 105.0, 1e-13,
                "component cycle mean changed");
    expect_near(cycle.cycle_mean_summed_power_w, 105.0 * kCycleRadians / 2.0, 1e-12,
                "cycle duration did not own mean power");
    expect_near(
        cycle.summed_torque_work_j,
        (cycle.indicated_gas_work_j + cycle.friction_pump_and_accessory_work_j) +
            cycle.starter_work_j,
        2e-12, "component work does not reconstruct the summed work");
    expect(cycle.start_boundary == CycleBoundaryEvidence{0, 0, 0.0} &&
               cycle.end_boundary == CycleBoundaryEvidence{1, 1, 0.0},
           "exact sample boundaries were mislabeled as interpolated");
}

void test_linear_sample_refinement_invariance() {
    const auto make_stream = [](double step) {
        std::vector<CycleTorqueSample> result;
        std::uint64_t index = 0;
        for (double theta = 0.25; theta <= 2.0 * kCycleRadians + 0.5; theta += step) {
            result.push_back(sample(index++, theta,
                                    0.01 * theta + 0.0001 * theta * theta,
                                    3.0 + 0.75 * theta, -1.0 + 0.1 * theta, 0.25));
        }
        result.push_back(sample(index, 2.0 * kCycleRadians + 0.5,
                                0.01 * (2.0 * kCycleRadians + 0.5) +
                                    0.0001 * (2.0 * kCycleRadians + 0.5) *
                                        (2.0 * kCycleRadians + 0.5),
                                3.0 + 0.75 * (2.0 * kCycleRadians + 0.5),
                                -1.0 + 0.1 * (2.0 * kCycleRadians + 0.5), 0.25));
        return result;
    };

    auto coarse_integrator = make_integrator();
    auto fine_integrator = make_integrator();
    const auto coarse = feed(coarse_integrator, make_stream(0.79));
    const auto fine = feed(fine_integrator, make_stream(0.19));
    expect(coarse.size() == 1 && fine.size() == 1,
           "refined streams did not retain one complete cycle");
    expect_near(coarse.front().indicated_gas_work_j, fine.front().indicated_gas_work_j,
                5e-11, "linear indicated work depends on sample refinement");
    expect_near(coarse.front().friction_pump_and_accessory_work_j,
                fine.front().friction_pump_and_accessory_work_j, 5e-11,
                "linear friction work depends on sample refinement");
    expect_near(coarse.front().starter_work_j, fine.front().starter_work_j, 5e-11,
                "constant starter work depends on sample refinement");
    expect_near(coarse.front().summed_torque_work_j, fine.front().summed_torque_work_j,
                8e-11, "linear net work depends on sample refinement");
    expect_near(coarse.front().start_time_s, fine.front().start_time_s, 2e-5,
                "boundary time unexpectedly depends strongly on refinement");
    expect_near(coarse.front().end_time_s, fine.front().end_time_s, 2e-5,
                "end time unexpectedly depends strongly on refinement");
}

void test_indexed_boundaries_remain_stable_over_many_cycles() {
    constexpr double reference = -0.375;
    constexpr std::uint64_t cycle_count = 4096;
    auto integrator = make_integrator(reference);
    auto first = integrator.advance(sample(0, reference, 0.0, 17.0));
    expect(std::holds_alternative<NoFourStrokeCycleBoundaryCrossing>(first),
           "negative nonzero cycle reference did not initialize");

    for (std::uint64_t cycle = 1; cycle <= cycle_count; ++cycle) {
        const double theta = reference + static_cast<double>(cycle) * kCycleRadians;
        auto result = integrator.advance(
            sample(cycle, theta, static_cast<double>(cycle) * 0.02, 17.0));
        const auto *crossing = std::get_if<FourStrokeCycleBoundaryCrossing>(&result);
        expect(crossing != nullptr && crossing->completed_cycle.has_value(),
               "indexed exact boundary failed to complete its cycle");
        const auto &completed = *crossing->completed_cycle;
        expect(completed.completed_cycle_ordinal == cycle - 1,
               "long-run completed-cycle ordinal drifted");
        expect(completed.end_theta_rad == theta && crossing->theta_rad == theta,
               "long-run boundary drifted from reference-plus-index grid");
        expect(completed.end_boundary == CycleBoundaryEvidence{cycle, cycle, 0.0} &&
                   crossing->boundary == completed.end_boundary,
               "exact long-run boundary lost exact-sample evidence");
        expect_near(completed.cycle_mean_summed_torque_nm, 17.0, 2e-11,
                    "constant torque drifted over indexed cycles");
    }
    expect(integrator.completed_cycle_count() == cycle_count,
           "long-run completed-cycle count drifted");
}

void test_move_terminalizes_the_source() {
    auto source = make_integrator();
    (void)source.advance(sample(7, 0.0, 0.0, 21.0));
    auto destination = std::move(source);

    const auto moved_from = source.advance(sample(8, kCycleRadians, 1.0, 21.0));
    const auto *move_error = std::get_if<FourStrokeCycleIntegrationError>(&moved_from);
    expect(move_error != nullptr &&
               move_error->code == FourStrokeCycleIntegrationErrorCode::moved_from &&
               move_error->sample_index == 7 && source.faulted() &&
               source.completed_cycle_count() == 0,
           "move construction left a second working integrator");

    const auto completed = destination.advance(sample(8, kCycleRadians, 1.0, 21.0));
    const auto *crossing = std::get_if<FourStrokeCycleBoundaryCrossing>(&completed);
    expect(crossing != nullptr && crossing->completed_cycle.has_value() &&
               destination.completed_cycle_count() == 1,
           "move construction did not preserve destination state");

    auto assigned = make_integrator(0.25);
    assigned = std::move(destination);
    const auto assigned_source =
        destination.advance(sample(9, 2.0 * kCycleRadians, 2.0, 21.0));
    expect(std::get<FourStrokeCycleIntegrationError>(assigned_source).code ==
                   FourStrokeCycleIntegrationErrorCode::moved_from &&
               assigned.completed_cycle_count() == 1,
           "move assignment did not transfer unique integration state");
}

void test_two_boundary_segment_is_rejected() {
    std::optional<std::pair<double, double>> adversarial_segment;
    for (std::int64_t index = 1; index < 10000; ++index) {
        const double first_boundary = static_cast<double>(index) * kCycleRadians;
        const double following_boundary =
            static_cast<double>(index + 1) * kCycleRadians;
        const double left =
            std::nextafter(first_boundary, -std::numeric_limits<double>::infinity());
        const double right = left + kCycleRadians;
        if (right >= following_boundary && right - left <= kCycleRadians) {
            adversarial_segment = std::pair{left, right};
            break;
        }
    }
    expect(adversarial_segment.has_value(),
           "test could not construct the binary64 two-boundary segment");

    auto integrator = make_integrator();
    (void)integrator.advance(sample(0, adversarial_segment->first, 0.0, 10.0));
    const auto result =
        integrator.advance(sample(1, adversarial_segment->second, 1.0, 10.0));
    const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&result);
    expect(
        error != nullptr &&
            error->code ==
                FourStrokeCycleIntegrationErrorCode::multiple_boundaries_in_segment &&
            integrator.completed_cycle_count() == 0,
        "one-result advance silently consumed two cycle boundaries");
    const auto stable =
        integrator.advance(sample(2, adversarial_segment->second + 0.5, 2.0, 10.0));
    expect(std::get<FourStrokeCycleIntegrationError>(stable) == *error,
           "multi-boundary failure was not terminal and stable");
}

void test_invalid_and_terminal_failures() {
    {
        auto invalid = compile_four_stroke_cycle_integrator({0.0, 0.0});
        const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&invalid);
        expect(error != nullptr &&
                   error->code == FourStrokeCycleIntegrationErrorCode::invalid_plan,
               "zero-displacement plan was admitted");
    }
    {
        auto invalid = compile_four_stroke_cycle_integrator(
            {std::numeric_limits<double>::quiet_NaN(), 0.0028});
        expect(std::holds_alternative<FourStrokeCycleIntegrationError>(invalid),
               "nonfinite cycle reference was admitted");
    }
    {
        auto integrator = make_integrator();
        const auto first = integrator.advance(sample(0, 0.0, 0.0, 10.0));
        expect(std::holds_alternative<NoFourStrokeCycleBoundaryCrossing>(first),
               "first valid sample did not initialize the integrator");
        const auto bad = integrator.advance(sample(1, 0.0, 1.0, 10.0));
        const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&bad);
        expect(error != nullptr &&
                   error->code ==
                       FourStrokeCycleIntegrationErrorCode::nonmonotonic_sample,
               "nonmonotonic theta did not fail");
        const auto stable = integrator.advance(sample(2, kCycleRadians, 2.0, 10.0));
        expect(std::get<FourStrokeCycleIntegrationError>(stable) == *error &&
                   integrator.faulted(),
               "cycle integration failure was not terminal and stable");
    }
    {
        auto integrator = make_integrator();
        (void)integrator.advance(sample(0, 0.0, 0.0, 10.0));
        const auto too_wide =
            integrator.advance(sample(1, 2.0 * kCycleRadians + 0.01, 1.0, 10.0));
        const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&too_wide);
        expect(
            error != nullptr &&
                error->code ==
                    FourStrokeCycleIntegrationErrorCode::multiple_boundaries_in_segment,
            "segment crossing multiple cycle boundaries was admitted");
    }
    {
        auto integrator = make_integrator();
        const auto nonfinite = integrator.advance(
            sample(0, 0.0, 0.0, std::numeric_limits<double>::infinity()));
        const auto *error = std::get_if<FourStrokeCycleIntegrationError>(&nonfinite);
        expect(error != nullptr &&
                   error->code == FourStrokeCycleIntegrationErrorCode::nonfinite_sample,
               "nonfinite torque was admitted");
    }
    {
        auto integrator =
            make_integrator(0.0, std::numeric_limits<double>::denorm_min());
        (void)integrator.advance(sample(0, 0.0, 0.0, 10.0));
        const auto derived_overflow =
            integrator.advance(sample(1, kCycleRadians, 1.0, 10.0));
        const auto *error =
            std::get_if<FourStrokeCycleIntegrationError>(&derived_overflow);
        expect(error != nullptr &&
                   error->code ==
                       FourStrokeCycleIntegrationErrorCode::nonfinite_result &&
                   integrator.completed_cycle_count() == 0,
               "failed derived cycle was counted as completed");
    }
}

void run_tests() {
    test_exact_and_bracketed_boundary_crossing_evidence();
    test_constant_torque_discards_initial_partial_cycle();
    test_linear_torque_boundary_split_is_analytic();
    test_component_work_and_stable_summed_signs();
    test_linear_sample_refinement_invariance();
    test_indexed_boundaries_remain_stable_over_many_cycles();
    test_move_terminalizes_the_source();
    test_two_boundary_segment_is_rejected();
    test_invalid_and_terminal_failures();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "four-stroke cycle integrator failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
