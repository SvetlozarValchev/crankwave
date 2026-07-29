#include "acoustics/unflanged_pipe_outlet.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {

using namespace engine_sim_offline::acoustics;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 const char *message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

UnflangedPipeOutletParameters candidate_parameters(double distance_m = 1.0) {
    return {
        0.023, 491.0, 180000.0, 192000.0, 1.2, 384.0, distance_m,
    };
}

void test_bilinear_silva_coefficients_dc_and_stability() {
    const auto parameters = candidate_parameters();
    const auto properties = resolve_unflanged_pipe_outlet(parameters);

    constexpr double n1 = 0.167;
    constexpr double d1 = 1.393;
    constexpr double d2 = 0.457;
    const double tau = parameters.outlet_radius_m / parameters.pipe_sound_speed_m_s;
    const double normalized_rate = tau * 2.0 * parameters.sample_rate_hz;
    const double denominator_zero =
        1.0 + d1 * normalized_rate + d2 * normalized_rate * normalized_rate;
    const DigitalReflectionBiquad expected{
        -(1.0 + n1 * normalized_rate) / denominator_zero,
        -2.0 / denominator_zero,
        -(1.0 - n1 * normalized_rate) / denominator_zero,
        (2.0 - 2.0 * d2 * normalized_rate * normalized_rate) / denominator_zero,
        (1.0 - d1 * normalized_rate + d2 * normalized_rate * normalized_rate) /
            denominator_zero,
    };

    expect(properties.reflection == expected,
           "outlet coefficients diverged from the frozen bilinear formula");
    const auto &filter = properties.reflection;
    const double dc_reflection =
        (filter.b0 + filter.b1 + filter.b2) / (1.0 + filter.a1 + filter.a2);
    const double nyquist_reflection =
        (filter.b0 - filter.b1 + filter.b2) / (1.0 - filter.a1 + filter.a2);
    expect_near(dc_reflection, -1.0, 3.0e-15,
                "unflanged outlet did not reflect DC pressure with coefficient -1");
    expect_near(nyquist_reflection, 0.0, 3.0e-15,
                "bilinear outlet did not approach zero reflection at Nyquist");
    expect(properties.first_pole_magnitude < 1.0 &&
               properties.second_pole_magnitude < 1.0,
           "outlet reflection poles were not stable");
    expect(properties.maximum_sampled_reflection_magnitude <= 1.0 + 1.0e-12,
           "outlet reflection magnitude exceeded unity");
    expect(properties.passivity_frequency_sample_count == 65537U,
           "outlet passivity verification did not cover the frozen grid");
    expect_near(properties.observer_delay_frames, 500.0, 1.0e-14,
                "outlet observer delay changed");
    expect_near(properties.monopole_far_field_scale_kg_per_m4,
                1.2 / (4.0 * std::numbers::pi), 2.0e-15,
                "outlet compact-monopole scale changed");
}

void test_reflection_impulse_is_causal_passive_and_matches_difference_equation() {
    const auto parameters = candidate_parameters();
    UnflangedPipeOutlet outlet{parameters};
    const auto filter = outlet.properties().reflection;

    constexpr std::size_t frame_count = 8192;
    std::array<double, 2> x_history{};
    std::array<double, 2> y_history{};
    double reflected_energy = 0.0;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const double input = frame == 0U ? 1.0 : 0.0;
        const double expected = filter.b0 * input + filter.b1 * x_history[0] +
                                filter.b2 * x_history[1] - filter.a1 * y_history[0] -
                                filter.a2 * y_history[1];
        const auto actual = outlet.process(input);
        expect_near(actual.reflected_pressure_wave_pa, expected, 2.0e-15,
                    "outlet reflection diverged from its causal biquad");
        expect_near(actual.outlet_volume_velocity_m3_s,
                    (input - expected) / parameters.characteristic_impedance_pa_s_m3,
                    2.0e-20, "outlet volume velocity changed sign or normalization");
        if (frame < 500U) {
            expect(actual.radiated_pressure_pa == 0.0,
                   "outlet radiated before its observer propagation delay");
        }
        reflected_energy +=
            actual.reflected_pressure_wave_pa * actual.reflected_pressure_wave_pa;
        x_history[1] = x_history[0];
        x_history[0] = input;
        y_history[1] = y_history[0];
        y_history[0] = expected;
    }
    expect(reflected_energy <= 1.0 + 2.0e-14,
           "outlet reflection impulse exceeded its passive energy bound");
}

std::vector<UnflangedPipeOutletFrame>
process_in_chunks(UnflangedPipeOutlet &outlet, const std::vector<double> &input,
                  const std::vector<std::size_t> &chunks) {
    std::vector<UnflangedPipeOutletFrame> output;
    output.reserve(input.size());
    std::size_t offset = 0;
    for (const auto count : chunks) {
        expect(offset + count <= input.size(),
               "outlet test partition exceeded its input");
        for (std::size_t frame = 0; frame < count; ++frame) {
            output.push_back(outlet.process(input[offset + frame]));
        }
        offset += count;
    }
    expect(offset == input.size(), "outlet test partitions did not cover their input");
    return output;
}

void test_partition_invariance_and_reference_distance_scaling() {
    std::vector<double> input(2048);
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const double n = static_cast<double>(frame);
        input[frame] = 300.0 * std::sin(0.071 * n) + 90.0 * std::cos(0.019 * n);
    }
    UnflangedPipeOutlet contiguous{candidate_parameters()};
    UnflangedPipeOutlet partitioned{candidate_parameters()};
    const auto expected = process_in_chunks(contiguous, input, {input.size()});
    const auto actual = process_in_chunks(
        partitioned, input, {1, 3, 2, 11, 5, 37, 19, 64, 7, 251, 509, 1024, 115});
    expect(actual.size() == expected.size(), "outlet partitioning changed output size");
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        expect(bits(actual[frame].reflected_pressure_wave_pa) ==
                       bits(expected[frame].reflected_pressure_wave_pa) &&
                   bits(actual[frame].outlet_volume_velocity_m3_s) ==
                       bits(expected[frame].outlet_volume_velocity_m3_s) &&
                   bits(actual[frame].radiated_pressure_pa) ==
                       bits(expected[frame].radiated_pressure_pa),
               "outlet state changed at a caller partition boundary");
    }

    UnflangedPipeOutlet one_metre{candidate_parameters(1.0)};
    UnflangedPipeOutlet two_metres{candidate_parameters(2.0)};
    std::vector<double> radiated_one_metre(1800);
    std::vector<double> radiated_two_metres(1800);
    for (std::size_t frame = 0; frame < radiated_one_metre.size(); ++frame) {
        const double impulse = frame == 0U ? 100.0 : 0.0;
        radiated_one_metre[frame] = one_metre.process(impulse).radiated_pressure_pa;
        radiated_two_metres[frame] = two_metres.process(impulse).radiated_pressure_pa;
    }
    for (std::size_t frame = 500U; frame < 1300U; ++frame) {
        expect_near(radiated_two_metres[frame + 500U], 0.5 * radiated_one_metre[frame],
                    2.0e-18,
                    "compact-monopole pressure did not scale as inverse distance");
    }
}

void test_steady_volume_flow_has_zero_radiated_dc() {
    UnflangedPipeOutlet outlet{candidate_parameters()};
    double maximum_late_radiated_pressure = 0.0;
    double last_reflection = 0.0;
    for (std::size_t frame = 0; frame < 10000U; ++frame) {
        const auto output = outlet.process(250.0);
        if (frame >= 8000U) {
            maximum_late_radiated_pressure = std::max(
                maximum_late_radiated_pressure, std::abs(output.radiated_pressure_pa));
        }
        last_reflection = output.reflected_pressure_wave_pa;
    }
    expect_near(last_reflection, -250.0, 2.0e-12,
                "outlet reflection did not settle to its DC open-end limit");
    expect(maximum_late_radiated_pressure < 1.0e-12,
           "compact-monopole projection radiated steady volume-flow DC");
}

void test_invalid_parameters_and_samples_are_rejected_without_state_mutation() {
    auto invalid = candidate_parameters();
    invalid.outlet_radius_m = 0.0;
    expect_throw<std::invalid_argument>([&] { UnflangedPipeOutlet outlet{invalid}; },
                                        "outlet accepted a zero radius");
    invalid = candidate_parameters();
    invalid.characteristic_impedance_pa_s_m3 = std::numeric_limits<double>::infinity();
    expect_throw<std::invalid_argument>(
        [&] { UnflangedPipeOutlet outlet{invalid}; },
        "outlet accepted a non-finite characteristic impedance");
    invalid = candidate_parameters();
    invalid.ambient_density_kg_m3 = -1.0;
    expect_throw<std::invalid_argument>([&] { UnflangedPipeOutlet outlet{invalid}; },
                                        "outlet accepted a negative ambient density");
    invalid = candidate_parameters();
    invalid.observation_distance_m = 0.0;
    expect_throw<std::invalid_argument>([&] { UnflangedPipeOutlet outlet{invalid}; },
                                        "outlet accepted a zero observation distance");

    UnflangedPipeOutlet candidate{candidate_parameters()};
    UnflangedPipeOutlet reference{candidate_parameters()};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                candidate.process(std::numeric_limits<double>::quiet_NaN()));
        },
        "outlet accepted a non-finite incident pressure wave");
    expect(candidate.process(125.0) == reference.process(125.0),
           "rejected outlet input mutated causal state");
}

void run_tests() {
    test_bilinear_silva_coefficients_dc_and_stability();
    test_reflection_impulse_is_causal_passive_and_matches_difference_equation();
    test_partition_invariance_and_reference_distance_scaling();
    test_steady_volume_flow_has_zero_radiated_dc();
    test_invalid_parameters_and_samples_are_rejected_without_state_mutation();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Unflanged pipe outlet test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
