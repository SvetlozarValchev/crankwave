#include "dsp/six_channel_causal_resampler.hpp"

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
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using engine_sim_offline::dsp::SixChannelCausalResampler;
using engine_sim_offline::dsp::SixChannelResamplerFrame;

void expect(bool condition, const char *message) {
    if (!condition) {
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

void expect_same_frames(std::span<const SixChannelResamplerFrame> actual,
                        std::span<const SixChannelResamplerFrame> expected,
                        const char *message) {
    expect(actual.size() == expected.size(), message);
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        for (std::size_t channel = 0;
             channel < SixChannelCausalResampler::channel_count; ++channel) {
            expect(bits(actual[frame][channel]) == bits(expected[frame][channel]),
                   message);
        }
    }
}

std::vector<SixChannelResamplerFrame>
process_chunks(SixChannelCausalResampler &resampler,
               std::span<const SixChannelResamplerFrame> input,
               std::span<const std::size_t> chunk_sizes) {
    std::vector<SixChannelResamplerFrame> result;
    std::size_t input_offset = 0;
    for (const auto chunk_size : chunk_sizes) {
        expect(input_offset + chunk_size <= input.size(),
               "resampler test chunk exceeded its input");
        const auto chunk = input.subspan(input_offset, chunk_size);
        std::vector<SixChannelResamplerFrame> output(
            resampler.expected_output_frame_count(chunk.size()));
        resampler.process(chunk, output);
        result.insert(result.end(), output.begin(), output.end());
        input_offset += chunk_size;
    }
    expect(input_offset == input.size(),
           "resampler test chunks did not cover the input");
    return result;
}

void test_frozen_configuration_and_exact_clock() {
    static_assert(SixChannelCausalResampler::input_rate_hz == 80'000);
    static_assert(SixChannelCausalResampler::output_rate_hz == 192'000);
    static_assert(SixChannelCausalResampler::reduced_rate_numerator == 12);
    static_assert(SixChannelCausalResampler::reduced_rate_denominator == 5);
    static_assert(SixChannelCausalResampler::channel_count == 6);
    static_assert(SixChannelCausalResampler::tap_count == 257);
    static_assert(SixChannelCausalResampler::group_delay_input_frames == 128);
    static_assert(SixChannelCausalResampler::phase_interval_count == 4'096);
    static_assert(SixChannelCausalResampler::phase_row_count == 4'097);
    static_assert(SixChannelCausalResampler::kaiser_beta == 12.0);
    static_assert(SixChannelCausalResampler::source_nyquist_cutoff == 0.95);

    SixChannelCausalResampler resampler;
    expect(resampler.expected_output_frame_count(0) == 0,
           "empty resampler input did not map to empty output");
    expect(resampler.expected_output_frame_count(
               SixChannelCausalResampler::canonical_input_frames_per_block) ==
               SixChannelCausalResampler::canonical_output_frames_per_block,
           "1,600 source intervals did not map exactly to 3,840 acoustic frames");
    expect(resampler.distance_to_next_output() == 0,
           "output-count projection mutated the resampler clock");

    constexpr std::array expected_counts{3U, 2U, 3U, 2U, 2U};
    constexpr std::array<std::uint64_t, 5> expected_distances{48'000, 16'000, 64'000,
                                                              32'000, 0};
    std::array<SixChannelResamplerFrame, 1> input{};
    std::size_t total = 0;
    for (std::size_t interval = 0; interval < expected_counts.size(); ++interval) {
        const auto count = resampler.expected_output_frame_count(1);
        expect(count == expected_counts[interval],
               "12/5 per-interval output cadence changed");
        std::vector<SixChannelResamplerFrame> output(count);
        resampler.process(input, output);
        total += output.size();
        expect(resampler.distance_to_next_output() == expected_distances[interval],
               "12/5 rational-clock state changed");
    }
    expect(total == 12 && resampler.distance_to_next_output() == 0,
           "five source intervals did not close on twelve acoustic frames");

    expect(resampler.expected_output_frame_count(
               SixChannelCausalResampler::canonical_input_frames_per_block) ==
               SixChannelCausalResampler::canonical_output_frames_per_block,
           "full-block cadence depended on a prior closed clock cycle");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(resampler.expected_output_frame_count(
                SixChannelCausalResampler::maximum_input_frames_per_call + 1));
        },
        "resampler accepted more than one bounded source block");
}

void test_phase_rows_cover_the_exact_rational_positions() {
    struct Vector {
        std::uint64_t offset;
        std::uint16_t row0;
        std::uint64_t remainder;
        double mix;
    };
    constexpr std::array vectors{
        Vector{0, 0, 0, 0.0},
        Vector{16'000, 341, 64'000, 1.0 / 3.0},
        Vector{32'000, 682, 128'000, 2.0 / 3.0},
        Vector{48'000, 1'024, 0, 0.0},
        Vector{64'000, 1'365, 64'000, 1.0 / 3.0},
        Vector{80'000, 1'706, 128'000, 2.0 / 3.0},
        Vector{160'000, 3'413, 64'000, 1.0 / 3.0},
        Vector{176'000, 3'754, 128'000, 2.0 / 3.0},
    };
    for (const auto &vector : vectors) {
        const auto phase = SixChannelCausalResampler::resolve_phase(vector.offset);
        expect(phase.row0 == vector.row0 && phase.remainder == vector.remainder &&
                   bits(phase.row_mix) == bits(vector.mix),
               "resampler phase-table interpolation position changed");
    }
    expect_throw<std::invalid_argument>(
        [] {
            static_cast<void>(SixChannelCausalResampler::resolve_phase(
                SixChannelCausalResampler::output_rate_hz));
        },
        "resampler accepted an interval-end phase offset");
}

void test_partition_equality_and_channel_isolation() {
    std::array<SixChannelResamplerFrame, 1'600> input{};
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        for (std::size_t channel = 0;
             channel < SixChannelCausalResampler::channel_count; ++channel) {
            const auto signed_value =
                static_cast<int>((frame * (channel + 3) + channel) % 37) - 18;
            input[frame][channel] =
                static_cast<double>(signed_value) /
                static_cast<double>(std::size_t{1} << (channel + 1));
        }
    }

    SixChannelCausalResampler contiguous_resampler;
    constexpr std::array contiguous_chunks{std::size_t{1'600}};
    const auto contiguous =
        process_chunks(contiguous_resampler, input, contiguous_chunks);

    SixChannelCausalResampler partitioned_resampler;
    constexpr std::array partitioned_chunks{
        std::size_t{1},   std::size_t{7}, std::size_t{93},
        std::size_t{511}, std::size_t{2}, std::size_t{986},
    };
    const auto partitioned =
        process_chunks(partitioned_resampler, input, partitioned_chunks);

    expect(contiguous.size() == 3'840 && partitioned.size() == 3'840,
           "partitioned resampling changed the exact block cadence");
    expect_same_frames(partitioned, contiguous,
                       "resampler output changed across caller partitions");
    expect(contiguous_resampler.distance_to_next_output() == 0 &&
               partitioned_resampler.distance_to_next_output() == 0,
           "caller partitioning changed the terminal rational-clock state");

    std::array<SixChannelResamplerFrame, 400> isolated_input{};
    isolated_input.front()[4] = 1.0;
    SixChannelCausalResampler isolation_resampler;
    std::vector<SixChannelResamplerFrame> isolated_output(
        isolation_resampler.expected_output_frame_count(isolated_input.size()));
    isolation_resampler.process(isolated_input, isolated_output);
    bool lane_four_was_nonzero = false;
    for (const auto &frame : isolated_output) {
        lane_four_was_nonzero = lane_four_was_nonzero || frame[4] != 0.0;
        for (std::size_t channel = 0;
             channel < SixChannelCausalResampler::channel_count; ++channel) {
            if (channel != 4) {
                expect(frame[channel] == 0.0,
                       "resampler leaked one source lane into another");
            }
        }
    }
    expect(lane_four_was_nonzero,
           "isolated source impulse did not reach its output lane");
}

void test_causal_impulse_support_and_group_delay() {
    std::array<SixChannelResamplerFrame, 300> input{};
    input.front()[2] = 1.0;

    SixChannelCausalResampler resampler;
    std::vector<SixChannelResamplerFrame> output(
        resampler.expected_output_frame_count(input.size()));
    resampler.process(input, output);

    std::size_t peak_frame = 0;
    double peak_magnitude = 0.0;
    for (std::size_t frame = 0; frame < output.size(); ++frame) {
        const double magnitude = std::abs(output[frame][2]);
        if (magnitude > peak_magnitude) {
            peak_magnitude = magnitude;
            peak_frame = frame;
        }
        for (std::size_t channel = 0;
             channel < SixChannelCausalResampler::channel_count; ++channel) {
            if (channel != 2) {
                expect(output[frame][channel] == 0.0,
                       "causal impulse leaked into a second source lane");
            }
        }
    }

    expect(output[0][2] == 0.0 && output[1][2] == 0.0 && output[2][2] == 0.0,
           "resampler impulse escaped its causal support at render start");
    for (std::size_t frame = 0; frame < 5; ++frame) {
        expect(output[frame][2] == 0.0,
               "post-interval source influenced output before its timestamp");
    }
    expect(output[5][2] != 0.0,
           "post-interval source did not begin inside its causal support");
    expect(peak_frame == 310,
           "post-interval resampler impulse peak moved away from its timestamp plus "
           "128-input-frame delay");
    expect(bits(output[peak_frame][2]) == UINT64_C(0x3fed297dbb0b4cc7),
           "post-interval resampler impulse peak value changed");
    expect(peak_magnitude > 0.90 && peak_magnitude < 0.92,
           "resampler impulse peak magnitude changed materially");
    expect(output[616][2] != 0.0, "resampler impulse ended before its finite support");
    for (std::size_t frame = 617; frame < output.size(); ++frame) {
        expect(output[frame][2] == 0.0,
               "resampler impulse continued beyond its finite 257-tap support");
    }
}

void test_dc_preservation_and_stopband_rejection() {
    constexpr std::size_t input_frame_count = 1'000;
    std::array<SixChannelResamplerFrame, input_frame_count> input{};
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const double phase = static_cast<double>(frame);
        input[frame][0] = 1.0;
        input[frame][1] = -0.25;
        input[frame][2] =
            std::sin(2.0 * std::numbers::pi * 10'000.0 * phase / 80'000.0);
        input[frame][3] =
            std::sin(2.0 * std::numbers::pi * 39'600.0 * phase / 80'000.0);
    }

    SixChannelCausalResampler resampler;
    std::vector<SixChannelResamplerFrame> output(
        resampler.expected_output_frame_count(input.size()));
    resampler.process(input, output);

    constexpr std::size_t settled_output_frame = 800;
    double passband_square_sum = 0.0;
    double stopband_square_sum = 0.0;
    std::size_t measured_frames = 0;
    for (std::size_t frame = settled_output_frame; frame < output.size(); ++frame) {
        expect(std::abs(output[frame][0] - 1.0) < 2e-14,
               "resampler did not preserve positive DC after startup");
        expect(std::abs(output[frame][1] + 0.25) < 5e-15,
               "resampler did not preserve signed DC after startup");
        passband_square_sum += output[frame][2] * output[frame][2];
        stopband_square_sum += output[frame][3] * output[frame][3];
        ++measured_frames;
    }
    const double passband_rms =
        std::sqrt(passband_square_sum / static_cast<double>(measured_frames));
    const double stopband_rms =
        std::sqrt(stopband_square_sum / static_cast<double>(measured_frames));
    expect(passband_rms > 0.69 && passband_rms < 0.72,
           "resampler materially changed an admitted passband tone");
    expect(stopband_rms < passband_rms * 1e-4,
           "resampler did not reject energy above its 0.95-Nyquist cutoff");
}

void test_failures_are_transactional_and_finite() {
    std::array<SixChannelResamplerFrame, 9> valid_input{};
    valid_input[0] = {1.0, -0.5, 0.25, -0.125, 0.0625, -0.03125};
    valid_input[7] = {-0.25, 0.5, -0.75, 1.0, -1.25, 1.5};

    SixChannelCausalResampler candidate;
    const auto expected_count =
        candidate.expected_output_frame_count(valid_input.size());

    std::vector<SixChannelResamplerFrame> wrong_size(expected_count - 1);
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(valid_input, wrong_size); },
        "resampler accepted a wrong-sized output span");
    expect(candidate.distance_to_next_output() == 0,
           "wrong output size advanced the resampler clock");

    auto nonfinite_input = valid_input;
    nonfinite_input[4][5] = std::numeric_limits<double>::quiet_NaN();
    std::vector<SixChannelResamplerFrame> untouched_output(expected_count);
    for (auto &frame : untouched_output) {
        frame.fill(19.0);
    }
    expect_throw<std::domain_error>(
        [&] { candidate.process(nonfinite_input, untouched_output); },
        "resampler accepted a non-finite source sample");
    expect(candidate.distance_to_next_output() == 0,
           "non-finite input advanced the resampler clock");
    for (const auto &frame : untouched_output) {
        expect(std::ranges::all_of(frame, [](double sample) { return sample == 19.0; }),
               "non-finite input partially overwrote caller output");
    }

    // Align finite maximum-magnitude samples with the phase-zero sinc signs at
    // source interval 300. Its L1 gain is greater than one, so the fixed traversal
    // must detect the resulting binary64 overflow before publishing any state.
    std::array<SixChannelResamplerFrame, 301> overflowing_input{};
    for (std::size_t tap = 1; tap + 1 < SixChannelCausalResampler::tap_count; ++tap) {
        const double distance =
            static_cast<double>(tap) -
            static_cast<double>(SixChannelCausalResampler::group_delay_input_frames);
        const double argument =
            SixChannelCausalResampler::source_nyquist_cutoff * distance;
        const double sinc_sign =
            argument == 0.0 ? 1.0 : std::sin(std::numbers::pi * argument) / argument;
        overflowing_input[44 + tap].fill(
            std::copysign(std::numeric_limits<double>::max(), sinc_sign));
    }
    std::vector<SixChannelResamplerFrame> overflow_output(
        candidate.expected_output_frame_count(overflowing_input.size()));
    for (auto &frame : overflow_output) {
        frame.fill(19.0);
    }
    expect_throw<std::domain_error>(
        [&] { candidate.process(overflowing_input, overflow_output); },
        "resampler accepted a non-finite convolution result");
    expect(candidate.distance_to_next_output() == 0,
           "non-finite output advanced the resampler clock");
    for (const auto &frame : overflow_output) {
        expect(std::ranges::all_of(frame, [](double sample) { return sample == 19.0; }),
               "non-finite output partially overwrote caller output");
    }

    std::vector<SixChannelResamplerFrame> candidate_output(expected_count);
    candidate.process(valid_input, candidate_output);
    SixChannelCausalResampler fresh;
    std::vector<SixChannelResamplerFrame> fresh_output(expected_count);
    fresh.process(valid_input, fresh_output);
    expect_same_frames(candidate_output, fresh_output,
                       "a rejected call changed later resampler output");

    for (const auto &frame : candidate_output) {
        for (const double sample : frame) {
            expect(std::isfinite(sample),
                   "valid resampler input produced a non-finite output");
        }
    }
}

void run_tests() {
    test_frozen_configuration_and_exact_clock();
    test_phase_rows_cover_the_exact_rational_positions();
    test_partition_equality_and_channel_isolation();
    test_causal_impulse_support_and_group_delay();
    test_dc_preservation_and_stopband_rejection();
    test_failures_are_transactional_and_finite();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "six-channel causal resampler test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
