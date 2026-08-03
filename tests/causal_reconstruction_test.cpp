#include "presentation/causal_reconstruction.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace engine_sim_offline::presentation;

constexpr std::size_t kBmwRouteCount = 2;
constexpr std::size_t kCanonicalInputFrames = kExcitationFramesPerMethodBlock;
constexpr std::uint64_t kCanonicalInputRateHz =
    CausalReconstruction::kInputRateHz;

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

double value(std::span<const double> values, std::size_t frame, std::size_t route,
             std::size_t route_count) {
    return values[frame * route_count + route];
}

void expect_same_output(std::span<const double> actual,
                        std::span<const double> expected, const char *message) {
    expect(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < actual.size(); ++index) {
        expect(bits(actual[index]) == bits(expected[index]), message);
    }
}

std::vector<double> process_chunks(CausalReconstruction &reconstruction,
                                   std::span<const double> input_frame_major,
                                   std::span<const std::size_t> chunks) {
    std::vector<double> result;
    std::size_t input_frame_offset = 0;
    for (const auto chunk_size : chunks) {
        expect(input_frame_offset + chunk_size <=
                   input_frame_major.size() / reconstruction.route_count(),
               "reconstruction test chunk exceeded its input");
        const auto chunk =
            input_frame_major.subspan(input_frame_offset * reconstruction.route_count(),
                                      chunk_size * reconstruction.route_count());
        std::vector<double> output(
            reconstruction.expected_output_frame_count(chunk_size) *
            reconstruction.route_count());
        reconstruction.process(chunk, chunk_size, output);
        result.insert(result.end(), output.begin(), output.end());
        input_frame_offset += chunk_size;
    }
    expect(input_frame_offset * reconstruction.route_count() ==
               input_frame_major.size(),
           "reconstruction test chunks did not cover their input");
    return result;
}

void test_frozen_phase_resolution() {
    struct PhaseVector {
        std::uint64_t offset;
        std::uint16_t phase0;
        std::uint64_t remainder;
        std::uint64_t mix_bits;
    };
    constexpr std::array vectors{
        PhaseVector{0, 0, 0, UINT64_C(0x0000000000000000)},
        PhaseVector{2000, 42, 128000, UINT64_C(0x3fe5555555555555)},
        PhaseVector{4000, 85, 64000, UINT64_C(0x3fd5555555555555)},
        PhaseVector{6000, 128, 0, UINT64_C(0x0000000000000000)},
        PhaseVector{8000, 170, 128000, UINT64_C(0x3fe5555555555555)},
        PhaseVector{10000, 213, 64000, UINT64_C(0x3fd5555555555555)},
        PhaseVector{18000, 384, 0, UINT64_C(0x0000000000000000)},
        PhaseVector{188000, 4010, 128000, UINT64_C(0x3fe5555555555555)},
        PhaseVector{190000, 4053, 64000, UINT64_C(0x3fd5555555555555)},
    };

    for (const auto &vector : vectors) {
        const auto phase = CausalReconstruction::resolve_phase(vector.offset);
        expect(phase.phase0 == vector.phase0 && phase.remainder == vector.remainder &&
                   bits(phase.mix) == vector.mix_bits,
               "reconstruction phase resolution changed");
    }
    expect_throw<std::invalid_argument>(
        [] {
            static_cast<void>(
                CausalReconstruction::resolve_phase(
                    CausalReconstruction::kSourceRateHz));
        },
        "reconstruction accepted an interval-end phase offset");
    expect_throw<std::invalid_argument>([] { CausalReconstruction invalid{0, kCanonicalInputRateHz}; },
                                        "reconstruction accepted zero routes");
    expect_throw<std::invalid_argument>([] { CausalReconstruction invalid{1, 10000}; },
                                        "reconstruction accepted the retired 10 kHz "
                                        "input clock");
}

void test_exact_clock_count_and_distance_pattern() {
    CausalReconstruction reconstruction{kBmwRouteCount, kCanonicalInputRateHz};
    expect(reconstruction.expected_output_frame_count(
               kCanonicalInputFrames) == kSourceFramesPerMethodBlock &&
               reconstruction.distance_to_next_output() == 0,
           "400-to-3840 count projection changed or mutated the clock");

    constexpr std::array expected_counts{10U, 10U, 9U, 10U, 9U};
    constexpr std::array<std::uint64_t, 5> expected_distances{8000, 16000, 4000,
                                                              12000, 0};
    std::array<double, kBmwRouteCount> input{};
    std::size_t total_output = 0;
    for (std::size_t frame = 0; frame < expected_counts.size(); ++frame) {
        const auto count = reconstruction.expected_output_frame_count(1);
        expect(count == expected_counts[frame],
               "per-frame source count pattern changed");
        std::vector<double> output(count * kBmwRouteCount);
        reconstruction.process(input, 1, output);
        total_output += count;
        expect(reconstruction.distance_to_next_output() == expected_distances[frame],
               "shared reconstruction clock distance changed");
    }

    constexpr std::size_t remainder_frames = kCanonicalInputFrames - 5U;
    std::vector<double> remainder(remainder_frames * kBmwRouteCount);
    const auto remainder_count =
        reconstruction.expected_output_frame_count(remainder_frames);
    expect(remainder_count == kSourceFramesPerMethodBlock - total_output,
           "remaining method-block source count changed");
    std::vector<double> remainder_output(remainder_count * kBmwRouteCount);
    reconstruction.process(remainder, remainder_frames, remainder_output);
    total_output += remainder_count;
    expect(total_output == kSourceFramesPerMethodBlock &&
               reconstruction.distance_to_next_output() == 0,
           "method block did not end at 3840 frames and phase zero");
}

void test_canonical_causal_impulse_and_route_isolation() {
    constexpr std::size_t input_frames = 300U;
    std::vector<double> input(input_frames * kBmwRouteCount);
    input[0] = 1.0;

    CausalReconstruction reconstruction{kBmwRouteCount, kCanonicalInputRateHz};
    constexpr std::array chunks{input_frames};
    const auto output = process_chunks(reconstruction, input, chunks);
    expect(output.size() == 2880U * kBmwRouteCount,
           "20 kHz causal impulse produced the wrong extent");

    for (std::size_t frame = 0; frame < 20U; ++frame) {
        expect(bits(value(output, frame, 0, kBmwRouteCount)) == UINT64_C(0),
               "20 kHz causal impulse began before its pinned support");
    }

    struct ImpulseProbe {
        std::size_t frame;
        std::uint64_t expected_bits;
    };
    constexpr std::array probes{
        ImpulseProbe{20U, UINT64_C(0x3e8c7b7ecc7ff249)},
        ImpulseProbe{28U, UINT64_C(0xbe82cd5758678248)},
        ImpulseProbe{29U, UINT64_C(0xbe92a5cabdfbe18e)},
        ImpulseProbe{1227U, UINT64_C(0xbfbad98995897efd)},
        ImpulseProbe{1228U, UINT64_C(0xbf9b800b89e1bdc8)},
        ImpulseProbe{1229U, UINT64_C(0x3fb21838e746b7a1)},
        ImpulseProbe{1237U, UINT64_C(0x3fed724aff174e01)},
        ImpulseProbe{1238U, UINT64_C(0x3fee5222f133b8f4)},
        ImpulseProbe{1239U, UINT64_C(0x3fee395b810818de)},
        ImpulseProbe{2455U, UINT64_C(0x3e94bb6ac9bc2b3d)},
        ImpulseProbe{2465U, UINT64_C(0xbe8b0d5082953c43)},
        ImpulseProbe{2467U, UINT64_C(0xbe8c0e0a32500067)},
    };
    for (const auto &probe : probes) {
        expect(bits(value(output, probe.frame, 0, kBmwRouteCount)) ==
                   probe.expected_bits,
               "20 kHz causal impulse reconstruction changed");
    }
    for (std::size_t frame = 2468U; frame < 2880U; ++frame) {
        expect(bits(value(output, frame, 0, kBmwRouteCount)) == UINT64_C(0),
               "20 kHz causal impulse extended beyond its pinned support");
    }

    for (std::size_t frame = 0; frame < 2880U; ++frame) {
        expect(value(output, frame, 1, kBmwRouteCount) == 0.0,
               "reconstruction leaked route 0 into route 1");
    }
}

void test_split_and_contiguous_processing_are_identical() {
    std::vector<double> input(kCanonicalInputFrames * kBmwRouteCount);
    for (std::size_t frame = 0; frame < kCanonicalInputFrames; ++frame) {
        const auto route_0 = static_cast<int>(frame % 17) - 8;
        const auto route_1 = static_cast<int>(frame % 11) - 5;
        input[frame * kBmwRouteCount] = static_cast<double>(route_0) * 0.125;
        input[frame * kBmwRouteCount + 1] = static_cast<double>(route_1) * 0.25;
    }

    CausalReconstruction contiguous_reconstruction{kBmwRouteCount,
                                                   kCanonicalInputRateHz};
    constexpr std::array contiguous_chunk{kCanonicalInputFrames};
    const auto contiguous =
        process_chunks(contiguous_reconstruction, input, contiguous_chunk);

    CausalReconstruction split_reconstruction{kBmwRouteCount,
                                              kCanonicalInputRateHz};
    constexpr std::array split_chunks{
        std::size_t{1}, std::size_t{2}, std::size_t{4},   std::size_t{3},
        std::size_t{8}, std::size_t{5}, std::size_t{377},
    };
    const auto split = process_chunks(split_reconstruction, input, split_chunks);

    expect_same_output(split, contiguous,
                       "reconstruction changed across caller chunks");
    expect(contiguous_reconstruction.distance_to_next_output() == 0 &&
               split_reconstruction.distance_to_next_output() == 0,
           "reconstruction chunking changed its final clock phase");
}

void test_preflight_failures_do_not_mutate_state() {
    std::vector<double> valid_input(5 * kBmwRouteCount);
    valid_input[0] = 1.0;
    valid_input[1] = -0.5;
    valid_input[6] = 0.25;
    valid_input[7] = 0.75;

    CausalReconstruction candidate{kBmwRouteCount, kCanonicalInputRateHz};
    const auto expected_count = candidate.expected_output_frame_count(5);
    std::vector<double> wrong_size(expected_count * kBmwRouteCount - 1);
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(valid_input, 5, wrong_size); },
        "reconstruction accepted a wrong-sized output span");
    expect(candidate.distance_to_next_output() == 0,
           "wrong output size mutated the reconstruction clock");

    auto incomplete_input = valid_input;
    incomplete_input.pop_back();
    std::vector<double> rejected_output(expected_count * kBmwRouteCount, 13.0);
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(incomplete_input, 5, rejected_output); },
        "reconstruction accepted an incomplete input route matrix");

    auto nonfinite_input = valid_input;
    nonfinite_input[5] = std::numeric_limits<double>::quiet_NaN();
    expect_throw<std::domain_error>(
        [&] { candidate.process(nonfinite_input, 5, rejected_output); },
        "reconstruction accepted a non-finite excitation");
    expect(candidate.distance_to_next_output() == 0,
           "non-finite input mutated the reconstruction clock");
    for (const auto sample : rejected_output) {
        expect(sample == 13.0,
               "non-finite preflight wrote partial reconstruction output");
    }

    std::vector<double> candidate_output(expected_count * kBmwRouteCount);
    candidate.process(valid_input, 5, candidate_output);

    CausalReconstruction fresh{kBmwRouteCount, kCanonicalInputRateHz};
    std::vector<double> fresh_output(expected_count * kBmwRouteCount);
    fresh.process(valid_input, 5, fresh_output);
    expect_same_output(candidate_output, fresh_output,
                       "reconstruction preflight failure changed a later valid result");
}

void test_dynamic_route_counts_preserve_independent_route_arithmetic() {
    constexpr std::size_t kMultiRouteCount = 3;
    std::vector<double> single_input(kCanonicalInputFrames);
    std::vector<double> multi_input(kCanonicalInputFrames * kMultiRouteCount);
    for (std::size_t frame = 0; frame < kCanonicalInputFrames; ++frame) {
        const auto centered = static_cast<int>(frame % 13) - 6;
        single_input[frame] = static_cast<double>(centered) * 0.0625;
        multi_input[frame * kMultiRouteCount] = single_input[frame];
        multi_input[frame * kMultiRouteCount + 1] =
            static_cast<double>(static_cast<int>(frame % 9) - 4) * 0.25;
        multi_input[frame * kMultiRouteCount + 2] = single_input[frame];
    }

    CausalReconstruction single{1, kCanonicalInputRateHz};
    CausalReconstruction multi{kMultiRouteCount, kCanonicalInputRateHz};
    std::vector<double> single_output(kSourceFramesPerMethodBlock);
    std::vector<double> multi_output(kSourceFramesPerMethodBlock * kMultiRouteCount);
    single.process(single_input, kCanonicalInputFrames, single_output);
    multi.process(multi_input, kCanonicalInputFrames, multi_output);

    for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
        expect(bits(single_output[frame]) ==
                       bits(value(multi_output, frame, 0, kMultiRouteCount)) &&
                   bits(single_output[frame]) ==
                       bits(value(multi_output, frame, 2, kMultiRouteCount)),
               "dynamic reconstruction route count changed isolated arithmetic");
    }
}

void run_tests() {
    test_frozen_phase_resolution();
    test_exact_clock_count_and_distance_pattern();
    test_canonical_causal_impulse_and_route_isolation();
    test_split_and_contiguous_processing_are_identical();
    test_preflight_failures_do_not_mutate_state();
    test_dynamic_route_counts_preserve_independent_route_arithmetic();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "causal reconstruction test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
