#include "presentation/p18_causal_reconstruction.hpp"

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

void expect_same_output(std::span<const P18SourceFrame> actual,
                        std::span<const P18SourceFrame> expected, const char *message) {
    expect(actual.size() == expected.size(), message);
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        for (std::size_t route = 0; route < kP18ExhaustRouteCount; ++route) {
            expect(bits(actual[frame].route_values_engine_sim_source_unit[route]) ==
                       bits(expected[frame].route_values_engine_sim_source_unit[route]),
                   message);
        }
    }
}

std::vector<P18SourceFrame>
process_chunks(P18CausalReconstruction &reconstruction,
               std::span<const ExhaustExcitationFrame> input,
               std::span<const std::size_t> chunks) {
    std::vector<P18SourceFrame> result;
    std::size_t input_offset = 0;
    for (const auto chunk_size : chunks) {
        expect(input_offset + chunk_size <= input.size(),
               "reconstruction test chunk exceeded its input");
        const auto chunk = input.subspan(input_offset, chunk_size);
        std::vector<P18SourceFrame> output(
            reconstruction.expected_output_frame_count(chunk_size));
        reconstruction.process(chunk, output);
        result.insert(result.end(), output.begin(), output.end());
        input_offset += chunk_size;
    }
    expect(input_offset == input.size(),
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
        const auto phase = P18CausalReconstruction::resolve_phase(vector.offset);
        expect(phase.phase0 == vector.phase0 && phase.remainder == vector.remainder &&
                   bits(phase.mix) == vector.mix_bits,
               "P1.8 reconstruction phase resolution changed");
    }
    expect_throw<std::invalid_argument>(
        [] {
            static_cast<void>(P18CausalReconstruction::resolve_phase(
                P18CausalReconstruction::kSourceRate));
        },
        "P1.8 reconstruction accepted an interval-end phase offset");
}

void test_exact_clock_count_and_distance_pattern() {
    P18CausalReconstruction reconstruction;
    expect(reconstruction.expected_output_frame_count(
               kP18PhysicsFramesPerMethodBlock) == kP18SourceFramesPerMethodBlock &&
               reconstruction.distance_to_next_output() == 0,
           "P1.8 200-to-3840 count projection changed or mutated the clock");

    constexpr std::array expected_counts{20U, 19U, 19U, 19U, 19U};
    constexpr std::array<std::uint64_t, 5> expected_distances{8000, 6000, 4000, 2000,
                                                              0};
    std::array<ExhaustExcitationFrame, 1> input{};
    std::size_t total_output = 0;
    for (std::size_t frame = 0; frame < expected_counts.size(); ++frame) {
        const auto count = reconstruction.expected_output_frame_count(1);
        expect(count == expected_counts[frame],
               "P1.8 per-frame source count pattern changed");
        std::vector<P18SourceFrame> output(count);
        reconstruction.process(input, output);
        total_output += output.size();
        expect(reconstruction.distance_to_next_output() == expected_distances[frame],
               "P1.8 shared reconstruction clock distance changed");
    }

    std::array<ExhaustExcitationFrame, 195> remainder{};
    const auto remainder_count =
        reconstruction.expected_output_frame_count(remainder.size());
    expect(remainder_count == kP18SourceFramesPerMethodBlock - total_output,
           "P1.8 remaining method-block source count changed");
    std::vector<P18SourceFrame> remainder_output(remainder_count);
    reconstruction.process(remainder, remainder_output);
    total_output += remainder_output.size();
    expect(total_output == kP18SourceFramesPerMethodBlock &&
               reconstruction.distance_to_next_output() == 0,
           "P1.8 method block did not end at 3840 frames and phase zero");
}

void test_frozen_causal_impulse_and_route_isolation() {
    std::vector<ExhaustExcitationFrame> input(260);
    input.front().route_values_engine_sim_source_unit[0] = 1.0;

    P18CausalReconstruction reconstruction;
    constexpr std::array chunks{std::size_t{200}, std::size_t{60}};
    const auto output = process_chunks(reconstruction, input, chunks);
    expect(output.size() == 4992, "P1.8 260-frame impulse output count changed");

    for (std::size_t frame = 0; frame < 39; ++frame) {
        expect(bits(output[frame].route_values_engine_sim_source_unit[0]) ==
                   UINT64_C(0),
               "P1.8 causal impulse began before its frozen support");
    }

    struct ImpulseProbe {
        std::size_t frame;
        std::uint64_t expected_bits;
    };
    constexpr std::array probes{
        ImpulseProbe{39, UINT64_C(0x3e8b214886bb0e40)},
        ImpulseProbe{40, UINT64_C(0x3e8c7b7ecc7ff249)},
        ImpulseProbe{57, UINT64_C(0xbe8615d5773eaf87)},
        ImpulseProbe{58, UINT64_C(0xbe92a5cabdfbe18e)},
        ImpulseProbe{2457, UINT64_C(0x3f9422674c66a256)},
        ImpulseProbe{2458, UINT64_C(0x3fb21838e746b7a1)},
        ImpulseProbe{2475, UINT64_C(0x3fee00c5eb99bf78)},
        ImpulseProbe{2476, UINT64_C(0x3fee5222f133b8f4)},
        ImpulseProbe{2477, UINT64_C(0x3fee6525361c5cdf)},
        ImpulseProbe{2495, UINT64_C(0x3fba706677ece100)},
        ImpulseProbe{4930, UINT64_C(0xbe8b0d5082953c43)},
        ImpulseProbe{4991, UINT64_C(0x0000000000000000)},
    };
    for (const auto &probe : probes) {
        expect(bits(output[probe.frame].route_values_engine_sim_source_unit[0]) ==
                   probe.expected_bits,
               "P1.8 causal impulse reconstruction changed");
    }

    for (const auto &frame : output) {
        expect(frame.route_values_engine_sim_source_unit[1] == 0.0,
               "P1.8 reconstruction leaked route 0 into route 1");
    }
}

void test_split_and_contiguous_processing_are_identical() {
    std::array<ExhaustExcitationFrame, 200> input{};
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        const auto route_0 = static_cast<int>(frame % 17) - 8;
        const auto route_1 = static_cast<int>(frame % 11) - 5;
        input[frame].route_values_engine_sim_source_unit[0] =
            static_cast<double>(route_0) * 0.125;
        input[frame].route_values_engine_sim_source_unit[1] =
            static_cast<double>(route_1) * 0.25;
    }

    P18CausalReconstruction contiguous_reconstruction;
    constexpr std::array contiguous_chunk{std::size_t{200}};
    const auto contiguous =
        process_chunks(contiguous_reconstruction, input, contiguous_chunk);

    P18CausalReconstruction split_reconstruction;
    constexpr std::array split_chunks{
        std::size_t{1}, std::size_t{2}, std::size_t{4},   std::size_t{3},
        std::size_t{8}, std::size_t{5}, std::size_t{177},
    };
    const auto split = process_chunks(split_reconstruction, input, split_chunks);

    expect_same_output(split, contiguous,
                       "P1.8 reconstruction changed across caller chunks");
    expect(contiguous_reconstruction.distance_to_next_output() == 0 &&
               split_reconstruction.distance_to_next_output() == 0,
           "P1.8 reconstruction chunking changed its final clock phase");
}

void test_preflight_failures_do_not_mutate_state() {
    std::array<ExhaustExcitationFrame, 5> valid_input{};
    valid_input[0].route_values_engine_sim_source_unit = {1.0, -0.5};
    valid_input[3].route_values_engine_sim_source_unit = {0.25, 0.75};

    P18CausalReconstruction candidate;
    const auto expected_count =
        candidate.expected_output_frame_count(valid_input.size());
    std::vector<P18SourceFrame> wrong_size(expected_count - 1);
    expect_throw<std::invalid_argument>(
        [&] { candidate.process(valid_input, wrong_size); },
        "P1.8 reconstruction accepted a wrong-sized output span");
    expect(candidate.distance_to_next_output() == 0,
           "wrong output size mutated the P1.8 reconstruction clock");

    auto nonfinite_input = valid_input;
    nonfinite_input[2].route_values_engine_sim_source_unit[1] =
        std::numeric_limits<double>::quiet_NaN();
    std::vector<P18SourceFrame> rejected_output(expected_count);
    for (auto &frame : rejected_output) {
        frame.route_values_engine_sim_source_unit = {13.0, -17.0};
    }
    expect_throw<std::domain_error>(
        [&] { candidate.process(nonfinite_input, rejected_output); },
        "P1.8 reconstruction accepted a non-finite excitation");
    expect(candidate.distance_to_next_output() == 0,
           "non-finite input mutated the P1.8 reconstruction clock");
    for (const auto &frame : rejected_output) {
        expect(frame.route_values_engine_sim_source_unit ==
                   std::array<double, kP18ExhaustRouteCount>{13.0, -17.0},
               "non-finite preflight wrote partial reconstruction output");
    }

    std::vector<P18SourceFrame> candidate_output(expected_count);
    candidate.process(valid_input, candidate_output);

    P18CausalReconstruction fresh;
    std::vector<P18SourceFrame> fresh_output(expected_count);
    fresh.process(valid_input, fresh_output);
    expect_same_output(
        candidate_output, fresh_output,
        "P1.8 reconstruction preflight failure changed a later valid result");
}

void run_tests() {
    test_frozen_phase_resolution();
    test_exact_clock_count_and_distance_pattern();
    test_frozen_causal_impulse_and_route_isolation();
    test_split_and_contiguous_processing_are_identical();
    test_preflight_failures_do_not_mutate_state();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "P1.8 causal reconstruction test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
