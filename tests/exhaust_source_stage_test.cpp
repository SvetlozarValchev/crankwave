#include "presentation/exhaust_source_stage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;

constexpr std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> kFrozenSeeds{
    RouteConditioningSeeds{
        {UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
        {UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
    },
    RouteConditioningSeeds{
        {UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
        {UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
    },
};

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

template <class Frames>
ExhaustExcitationBlockView
make_view(std::uint64_t first_frame_index, Frames &frames,
          contract::RationalRateHz rate = kExcitationRateHz,
          std::array<contract::RouteId, kExhaustExcitationRouteCount> route_ids =
              kExhaustExcitationRouteIds) {
    return ExhaustExcitationBlockView::borrow_for_callback(first_frame_index, rate,
                                                           route_ids, frames);
}

void fill_block(
    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> &frames,
    std::size_t block_index) {
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        const auto global = block_index * frames.size() + frame;
        const auto route_0 = static_cast<int>(global % 17) - 8;
        const auto route_1 = static_cast<int>(global % 11) - 5;
        frames[frame].route_values_engine_sim_source_unit = {
            static_cast<double>(route_0) * 0.125,
            static_cast<double>(route_1) * 0.25,
        };
    }
}

void expect_same_output(std::span<const ConditionedSourceFrame> actual,
                        std::span<const ConditionedSourceFrame> expected,
                        const char *message) {
    expect(actual.size() == expected.size(), message);
    for (std::size_t frame = 0; frame < actual.size(); ++frame) {
        for (std::size_t route = 0; route < kExhaustExcitationRouteCount; ++route) {
            expect(bits(actual[frame].route_values_engine_sim_source_unit[route]) ==
                       bits(expected[frame].route_values_engine_sim_source_unit[route]),
                   message);
        }
    }
}

void process_with_continuous_components(
    CausalReconstruction &reconstruction,
    std::array<RouteConditioner, kExhaustExcitationRouteCount> &conditioners,
    std::span<const ExhaustExcitationFrame> input,
    std::span<ConditionedSourceFrame> output) {
    std::vector<ReconstructedSourceFrame> reconstructed(
        reconstruction.expected_output_frame_count(input.size()));
    expect(reconstructed.size() == output.size(),
           "component reference output size changed");
    reconstruction.process(input, reconstructed);
    for (std::size_t frame = 0; frame < output.size(); ++frame) {
        for (std::size_t route = 0; route < kExhaustExcitationRouteCount; ++route) {
            output[frame].route_values_engine_sim_source_unit[route] =
                conditioners[route]
                    .process(
                        reconstructed[frame].route_values_engine_sim_source_unit[route])
                    .conditioned_engine_sim_source_unit;
        }
    }
}

void test_exact_block_extent_and_component_wiring() {
    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> input{};
    fill_block(input, 0);
    std::vector<ConditionedSourceFrame> actual(kSourceFramesPerMethodBlock);

    ExhaustSourceStage stage{kFrozenSeeds};
    const auto extent = stage.process(make_view(0, input), actual);
    expect(extent ==
                   SourceBlockExtent{
                       0,
                       0,
                       kExcitationFramesPerMethodBlock,
                       kSourceFramesPerMethodBlock,
                   } &&
               stage.next_input_frame_index() == kExcitationFramesPerMethodBlock &&
               stage.next_source_frame_index() == kSourceFramesPerMethodBlock &&
               !stage.terminal_failed(),
           "source-stage block extent or counters changed");

    CausalReconstruction reconstruction;
    std::array<RouteConditioner, kExhaustExcitationRouteCount> conditioners{
        RouteConditioner{kFrozenSeeds[0].jitter, kFrozenSeeds[0].air_noise},
        RouteConditioner{kFrozenSeeds[1].jitter, kFrozenSeeds[1].air_noise},
    };
    std::vector<ConditionedSourceFrame> expected(kSourceFramesPerMethodBlock);
    process_with_continuous_components(reconstruction, conditioners, input, expected);
    expect_same_output(actual, expected,
                       "source stage changed component order or route wiring");
    expect(stage.jitter_rng_state(0) == UINT64_C(0x38c474f6f27476ae) &&
               stage.air_noise_rng_state(0) == UINT64_C(0x6410d400ea8049ca) &&
               stage.jitter_rng_state(1) == UINT64_C(0x90c28d44851509c2) &&
               stage.air_noise_rng_state(1) == UINT64_C(0x2a5d082701ba5e7f),
           "source stage changed per-route random consumption");
}

void test_block_continuity_and_session_isolation() {
    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> block_0{};
    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> block_1{};
    fill_block(block_0, 0);
    fill_block(block_1, 1);

    ExhaustSourceStage first{kFrozenSeeds};
    ExhaustSourceStage interleaved{kFrozenSeeds};
    std::vector<ConditionedSourceFrame> first_0(kSourceFramesPerMethodBlock);
    std::vector<ConditionedSourceFrame> first_1(kSourceFramesPerMethodBlock);
    std::vector<ConditionedSourceFrame> other_0(kSourceFramesPerMethodBlock);
    std::vector<ConditionedSourceFrame> other_1(kSourceFramesPerMethodBlock);

    static_cast<void>(first.process(make_view(0, block_0), first_0));
    static_cast<void>(interleaved.process(make_view(0, block_0), other_0));
    static_cast<void>(
        first.process(make_view(kExcitationFramesPerMethodBlock, block_1), first_1));
    static_cast<void>(interleaved.process(
        make_view(kExcitationFramesPerMethodBlock, block_1), other_1));

    expect_same_output(first_0, other_0,
                       "interleaved sessions changed the first block");
    expect_same_output(first_1, other_1,
                       "state reset or crossed sessions at a block boundary");

    CausalReconstruction continuous_reconstruction;
    std::array<RouteConditioner, kExhaustExcitationRouteCount> continuous_conditioners{
        RouteConditioner{kFrozenSeeds[0].jitter, kFrozenSeeds[0].air_noise},
        RouteConditioner{kFrozenSeeds[1].jitter, kFrozenSeeds[1].air_noise},
    };
    std::vector<ConditionedSourceFrame> continuous_0(kSourceFramesPerMethodBlock);
    std::vector<ConditionedSourceFrame> continuous_1(kSourceFramesPerMethodBlock);
    process_with_continuous_components(continuous_reconstruction,
                                       continuous_conditioners, block_0, continuous_0);
    process_with_continuous_components(continuous_reconstruction,
                                       continuous_conditioners, block_1, continuous_1);
    expect_same_output(first_1, continuous_1,
                       "source stage reset component state at a method-block boundary");
    expect(first.next_input_frame_index() == 400 &&
               first.next_source_frame_index() == 7680,
           "source-stage continuity counters changed after two blocks");
}

void test_structural_rejections_do_not_mutate_state() {
    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> valid{};
    fill_block(valid, 0);
    std::vector<ConditionedSourceFrame> output(kSourceFramesPerMethodBlock);
    ExhaustSourceStage candidate{kFrozenSeeds};

    auto wrong_routes = kExhaustExcitationRouteIds;
    std::swap(wrong_routes[0], wrong_routes[1]);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(
                make_view(0, valid, kExcitationRateHz, wrong_routes), output));
        },
        "source stage accepted swapped route identities");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(
                make_view(0, valid, contract::RationalRateHz{9999, 1}), output));
        },
        "source stage accepted the wrong input rate");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(candidate.process(make_view(1, valid), output)); },
        "source stage accepted a discontinuous first index");

    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock - 1>
        short_input{};
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(make_view(0, short_input), output));
        },
        "source stage accepted a partial method block");

    std::vector<ConditionedSourceFrame> short_output(kSourceFramesPerMethodBlock - 1);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(make_view(0, valid), short_output));
        },
        "source stage accepted a short output buffer");

    auto nonfinite = valid;
    nonfinite[199].route_values_engine_sim_source_unit[1] =
        std::numeric_limits<double>::infinity();
    expect_throw<std::domain_error>(
        [&] { static_cast<void>(candidate.process(make_view(0, nonfinite), output)); },
        "source stage accepted non-finite excitation");

    expect(candidate.next_input_frame_index() == 0 &&
               candidate.next_source_frame_index() == 0 && !candidate.terminal_failed(),
           "source-stage structural rejection mutated session state");

    ExhaustSourceStage fresh{kFrozenSeeds};
    std::vector<ConditionedSourceFrame> fresh_output(kSourceFramesPerMethodBlock);
    static_cast<void>(candidate.process(make_view(0, valid), output));
    static_cast<void>(fresh.process(make_view(0, valid), fresh_output));
    expect_same_output(
        output, fresh_output,
        "source-stage structural rejection changed the next valid block");
}

void test_invalid_seed_ownership_and_terminal_arithmetic_failure() {
    auto duplicate_seeds = kFrozenSeeds;
    duplicate_seeds[1].air_noise.stream = duplicate_seeds[0].jitter.stream;
    duplicate_seeds[1].air_noise.initial_state =
        duplicate_seeds[0].jitter.initial_state + UINT64_C(1);
    expect_throw<std::invalid_argument>(
        [&] { ExhaustSourceStage invalid{duplicate_seeds}; },
        "source stage accepted one selector for two random-stream owners");

    auto oversized_stream = kFrozenSeeds;
    oversized_stream[0].jitter.stream =
        (std::numeric_limits<std::uint64_t>::max() >> 1U) + UINT64_C(1);
    expect_throw<std::invalid_argument>(
        [&] { ExhaustSourceStage invalid{oversized_stream}; },
        "source stage accepted an oversized PCG stream");

    std::array<ExhaustExcitationFrame, kExcitationFramesPerMethodBlock> huge{};
    for (auto &frame : huge) {
        frame.route_values_engine_sim_source_unit = {
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
        };
    }
    std::vector<ConditionedSourceFrame> output(kSourceFramesPerMethodBlock);
    ExhaustSourceStage failed{kFrozenSeeds};
    expect_throw<std::domain_error>(
        [&] { static_cast<void>(failed.process(make_view(0, huge), output)); },
        "overflowing source-stage arithmetic was not rejected");
    expect(failed.terminal_failed(),
           "source stage did not become terminal after partial arithmetic");
    expect_throw<std::logic_error>(
        [&] { static_cast<void>(failed.process(make_view(0, huge), output)); },
        "terminal source-stage state was reused");
    expect_throw<std::out_of_range>(
        [&] { static_cast<void>(failed.jitter_rng_state(2)); },
        "source stage accepted an out-of-range route probe");
}

void run_tests() {
    test_exact_block_extent_and_component_wiring();
    test_block_continuity_and_session_isolation();
    test_structural_rejections_do_not_mutate_state();
    test_invalid_seed_ownership_and_terminal_arithmetic_failure();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "source-stage test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
