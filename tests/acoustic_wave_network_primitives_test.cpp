#include "acoustics/ideal_compact_junction.hpp"
#include "acoustics/uniform_cylindrical_waveguide.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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

UniformCylindricalWaveguideParameters parameters_for_delay(double delay_frames,
                                                           double amplitude_survival) {
    constexpr double sample_rate_hz = 192000.0;
    constexpr double universal_gas_constant = 8.31446261815324;
    constexpr double gas_molar_mass = 0.02897;
    constexpr double heat_capacity_ratio = 1.4;
    constexpr double temperature_k = 600.0;
    constexpr double specific_gas_constant = universal_gas_constant / gas_molar_mass;
    const double sound_speed =
        std::sqrt(heat_capacity_ratio * specific_gas_constant * temperature_k);
    const double length_m = delay_frames * sound_speed / sample_rate_hz;
    const double loss_nepers_per_m = -std::log(amplitude_survival) / length_m;
    return {
        length_m,
        std::numbers::pi * 0.02 * 0.02,
        101325.0,
        temperature_k,
        loss_nepers_per_m,
        sample_rate_hz,
        universal_gas_constant,
        gas_molar_mass,
        heat_capacity_ratio,
    };
}

void test_waveguide_resolves_physical_properties_and_matched_delay() {
    constexpr double requested_delay_frames = 8.25;
    constexpr double requested_survival = 0.8;
    UniformCylindricalWaveguide waveguide{
        parameters_for_delay(requested_delay_frames, requested_survival)};
    const auto &properties = waveguide.properties();

    expect_near(properties.delay_frames, requested_delay_frames, 2.0e-15,
                "waveguide physical delay did not resolve as authored");
    expect(properties.integer_delay_frames == 8U,
           "waveguide integer delay did not resolve as authored");
    expect_near(properties.fractional_delay_frames, 0.25, 2.0e-15,
                "waveguide fractional delay did not resolve as authored");
    expect_near(properties.one_way_amplitude_survival, requested_survival, 2.0e-15,
                "waveguide attenuation did not resolve as authored");
    expect(properties.reference_density_kg_m3 > 0.0 &&
               properties.sound_speed_m_s > 0.0 &&
               properties.characteristic_impedance_pa_s_m3 > 0.0,
           "waveguide did not resolve positive gas properties");

    std::array<WaveguideArrivalFrame, 12> output{};
    for (std::size_t frame = 0; frame < output.size(); ++frame) {
        const WaveguideLaunchFrame input{
            frame == 0U ? 1.0 : 0.0,
            frame == 0U ? -2.0 : 0.0,
        };
        output[frame] = waveguide.process(input);
    }

    for (std::size_t frame = 0; frame < 8U; ++frame) {
        expect(output[frame] == WaveguideArrivalFrame{},
               "waveguide impulse arrived before its causal delay");
    }
    expect_near(output[8].at_downstream_pa, 0.6, 2.0e-15,
                "upstream impulse newer interpolation tap changed");
    expect_near(output[9].at_downstream_pa, 0.2, 2.0e-15,
                "upstream impulse older interpolation tap changed");
    expect_near(output[8].at_upstream_pa, -1.2, 4.0e-15,
                "downstream impulse newer interpolation tap changed");
    expect_near(output[9].at_upstream_pa, -0.4, 4.0e-15,
                "downstream impulse older interpolation tap changed");
    for (std::size_t frame = 10U; frame < output.size(); ++frame) {
        expect(output[frame] == WaveguideArrivalFrame{},
               "waveguide impulse exceeded its two-tap support");
    }
}

std::vector<WaveguideArrivalFrame>
process_waveguide_in_chunks(UniformCylindricalWaveguide &waveguide,
                            const std::vector<WaveguideLaunchFrame> &input,
                            const std::vector<std::size_t> &chunks) {
    std::vector<WaveguideArrivalFrame> output;
    output.reserve(input.size());
    std::size_t offset = 0;
    for (const auto count : chunks) {
        expect(offset + count <= input.size(),
               "waveguide test partition exceeded its input");
        for (std::size_t frame = 0; frame < count; ++frame) {
            output.push_back(waveguide.process(input[offset + frame]));
        }
        offset += count;
    }
    expect(offset == input.size(),
           "waveguide test partitions did not cover their input");
    return output;
}

void test_waveguide_partition_equality_and_bounded_gain() {
    const auto parameters = parameters_for_delay(5.6, 0.73);
    std::vector<WaveguideLaunchFrame> input(257);
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const double phase = static_cast<double>(frame);
        input[frame] = {
            0.6 * std::sin(phase * 0.173) + 0.2 * std::cos(phase * 0.047),
            0.4 * std::cos(phase * 0.113) - 0.1 * std::sin(phase * 0.029),
        };
    }

    UniformCylindricalWaveguide contiguous{parameters};
    UniformCylindricalWaveguide partitioned{parameters};
    const auto expected =
        process_waveguide_in_chunks(contiguous, input, {input.size()});
    const auto actual = process_waveguide_in_chunks(
        partitioned, input, {1, 2, 4, 3, 8, 5, 13, 21, 34, 55, 89, 22});
    expect(actual.size() == expected.size(),
           "waveguide partitioning changed output size");
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        expect(bits(actual[frame].at_upstream_pa) ==
                       bits(expected[frame].at_upstream_pa) &&
                   bits(actual[frame].at_downstream_pa) ==
                       bits(expected[frame].at_downstream_pa),
               "waveguide state changed at a caller partition boundary");
    }

    double maximum_upstream_launch = 0.0;
    double maximum_downstream_launch = 0.0;
    double maximum_upstream_arrival = 0.0;
    double maximum_downstream_arrival = 0.0;
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        maximum_upstream_launch =
            std::max(maximum_upstream_launch, std::abs(input[frame].from_upstream_pa));
        maximum_downstream_launch = std::max(maximum_downstream_launch,
                                             std::abs(input[frame].from_downstream_pa));
        maximum_upstream_arrival =
            std::max(maximum_upstream_arrival, std::abs(actual[frame].at_upstream_pa));
        maximum_downstream_arrival = std::max(maximum_downstream_arrival,
                                              std::abs(actual[frame].at_downstream_pa));
    }
    const double gain = partitioned.properties().one_way_amplitude_survival;
    expect(maximum_downstream_arrival <= maximum_upstream_launch * gain + 1.0e-15,
           "forward fractional delay exceeded its passive peak bound");
    expect(maximum_upstream_arrival <= maximum_downstream_launch * gain + 1.0e-15,
           "reverse fractional delay exceeded its passive peak bound");

    FixedPassiveFractionalDelay impulse_delay{5.6, gain};
    double output_energy = 0.0;
    for (std::size_t frame = 0; frame < 12U; ++frame) {
        const double output = impulse_delay.process(frame == 0U ? 1.0 : 0.0);
        output_energy += output * output;
    }
    expect(output_energy <= gain * gain + 1.0e-15,
           "fractional delay impulse energy exceeded its passive bound");
}

void test_waveguide_rejects_invalid_state_without_cross_direction_mutation() {
    expect_throw<std::invalid_argument>(
        [] { FixedPassiveFractionalDelay delay{1.999, 1.0}; },
        "fractional delay accepted a sub-two-frame delay");
    expect_throw<std::invalid_argument>(
        [] {
            FixedPassiveFractionalDelay delay{std::numeric_limits<double>::quiet_NaN(),
                                              1.0};
        },
        "fractional delay accepted a non-finite delay");
    expect_throw<std::invalid_argument>(
        [] { FixedPassiveFractionalDelay delay{2.0, 1.001}; },
        "fractional delay accepted gain above unity");

    auto invalid_parameters = parameters_for_delay(3.0, 1.0);
    invalid_parameters.propagation_loss_nepers_per_m = -0.01;
    expect_throw<std::invalid_argument>(
        [&] { UniformCylindricalWaveguide waveguide{invalid_parameters}; },
        "waveguide accepted negative propagation loss");

    const auto valid_parameters = parameters_for_delay(3.0, 1.0);
    UniformCylindricalWaveguide candidate{valid_parameters};
    UniformCylindricalWaveguide reference{valid_parameters};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                candidate.process({1.0, std::numeric_limits<double>::quiet_NaN()}));
        },
        "waveguide accepted a non-finite launch");
    const auto candidate_result = candidate.process({2.0, -3.0});
    const auto reference_result = reference.process({2.0, -3.0});
    expect(candidate_result == reference_result,
           "rejected launch mutated one waveguide direction");
}

void test_waveguide_two_phase_step_matches_wrapper_and_preserves_same_frame_arrival() {
    const auto parameters = parameters_for_delay(3.25, 0.9);
    UniformCylindricalWaveguide wrapper{parameters};
    UniformCylindricalWaveguide two_phase{parameters};

    for (std::size_t frame = 0; frame < 40U; ++frame) {
        const double n = static_cast<double>(frame);
        const WaveguideLaunchFrame launch{
            std::sin(0.37 * n),
            -0.5 * std::cos(0.19 * n),
        };
        const auto expected = wrapper.process(launch);
        const auto before_commit = two_phase.arrivals();
        expect(two_phase.arrivals() == before_commit,
               "reading a current waveguide arrival advanced its state");
        two_phase.commit(launch);
        expect(before_commit == expected,
               "two-phase waveguide update diverged from the exact wrapper");
    }

    UniformCylindricalWaveguide rejected{parameters};
    const auto zero_arrival = rejected.arrivals();
    expect_throw<std::domain_error>(
        [&] { rejected.commit({1.0, std::numeric_limits<double>::quiet_NaN()}); },
        "two-phase waveguide commit accepted a non-finite launch");
    expect(rejected.arrivals() == zero_arrival,
           "rejected two-phase launch advanced waveguide state");
}

void test_junction_analytic_matrix_and_pressure_flow_constraints() {
    const CompactFourPortWaves impedances{2.0, 3.0, 5.0, 7.0};
    const CompactFourPortWaves arrivals{1.25, -0.5, 0.75, 2.0};
    IdealCompactFourPortJunction junction{impedances};
    const auto result = junction.scatter(arrivals);

    CompactFourPortWaves admittances{};
    double admittance_sum = 0.0;
    double weighted_arrival = 0.0;
    for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
        admittances[port] = 1.0 / impedances[port];
        admittance_sum += admittances[port];
        weighted_arrival += admittances[port] * arrivals[port];
    }
    const double expected_pressure = 2.0 * weighted_arrival / admittance_sum;
    expect_near(result.common_pressure_pa, expected_pressure, 2.0e-15,
                "junction common pressure diverged from the analytic solution");

    double flow_sum_m3_s = 0.0;
    for (std::size_t row = 0; row < kCompactFourPortCount; ++row) {
        double matrix_departure = 0.0;
        for (std::size_t column = 0; column < kCompactFourPortCount; ++column) {
            const double scattering = 2.0 * admittances[column] / admittance_sum -
                                      (row == column ? 1.0 : 0.0);
            matrix_departure += scattering * arrivals[column];
        }
        expect_near(result.departing_pressure_waves_pa[row], matrix_departure, 3.0e-15,
                    "junction update diverged from its analytic matrix");
        expect_near(arrivals[row] + result.departing_pressure_waves_pa[row],
                    result.common_pressure_pa, 2.0e-15,
                    "junction did not enforce pressure continuity");
        flow_sum_m3_s +=
            (arrivals[row] - result.departing_pressure_waves_pa[row]) / impedances[row];
    }
    expect_near(flow_sum_m3_s, 0.0, 8.0e-16,
                "junction did not conserve signed volume flow");
}

void test_junction_lossless_passivity_and_partition_equality() {
    const CompactFourPortWaves impedances{4.0, 4.0, 8.0, 2.0};
    IdealCompactFourPortJunction junction{impedances};

    std::vector<CompactFourPortWaves> input(93);
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const double n = static_cast<double>(frame);
        input[frame] = {
            std::sin(0.13 * n),
            0.3 * std::cos(0.07 * n),
            -0.7 * std::sin(0.19 * n),
            0.2 * std::cos(0.23 * n),
        };
    }

    std::vector<CompactFourPortScattering> contiguous;
    std::vector<CompactFourPortScattering> partitioned;
    contiguous.reserve(input.size());
    partitioned.reserve(input.size());
    for (const auto &frame : input) {
        contiguous.push_back(junction.scatter(frame));
    }
    constexpr std::array chunks{std::size_t{1},  std::size_t{7}, std::size_t{3},
                                std::size_t{17}, std::size_t{2}, std::size_t{31},
                                std::size_t{32}};
    std::size_t offset = 0;
    for (const auto count : chunks) {
        for (std::size_t frame = 0; frame < count; ++frame) {
            partitioned.push_back(junction.scatter(input[offset + frame]));
        }
        offset += count;
    }
    expect(offset == input.size() && partitioned == contiguous,
           "junction output changed across caller partitions");

    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        double arriving_power_measure = 0.0;
        double departing_power_measure = 0.0;
        for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
            arriving_power_measure +=
                input[frame][port] * input[frame][port] / impedances[port];
            const double departure =
                contiguous[frame].departing_pressure_waves_pa[port];
            departing_power_measure += departure * departure / impedances[port];
        }
        expect_near(departing_power_measure, arriving_power_measure, 2.0e-15,
                    "lossless junction increased or destroyed wave power");
    }

    const auto impulse = junction.scatter({1.0, 0.0, 0.0, 0.0});
    double impulse_output_energy = 0.0;
    for (std::size_t port = 0; port < kCompactFourPortCount; ++port) {
        const double departure = impulse.departing_pressure_waves_pa[port];
        impulse_output_energy += departure * departure / impedances[port];
    }
    expect_near(impulse_output_energy, 1.0 / impedances[0], 2.0e-16,
                "junction impulse violated lossless passivity");
}

void test_junction_rejects_invalid_inputs() {
    expect_throw<std::invalid_argument>(
        [] { IdealCompactFourPortJunction junction{{1.0, 0.0, 1.0, 1.0}}; },
        "junction accepted a zero characteristic impedance");
    IdealCompactFourPortJunction junction{{1.0, 2.0, 3.0, 4.0}};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(junction.scatter(
                {0.0, std::numeric_limits<double>::infinity(), 0.0, 0.0}));
        },
        "junction accepted a non-finite arriving wave");
}

void run_tests() {
    test_waveguide_resolves_physical_properties_and_matched_delay();
    test_waveguide_partition_equality_and_bounded_gain();
    test_waveguide_rejects_invalid_state_without_cross_direction_mutation();
    test_waveguide_two_phase_step_matches_wrapper_and_preserves_same_frame_arrival();
    test_junction_analytic_matrix_and_pressure_flow_constraints();
    test_junction_lossless_passivity_and_partition_equality();
    test_junction_rejects_invalid_inputs();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Acoustic wave-network primitive test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
