#include "presentation/exhaust_source_stage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::presentation;

constexpr std::size_t kBmwRouteCount = 2;
constexpr std::array kCanonicalRouteIds{
    contract::RouteId{1},
    contract::RouteId{2},
};

constexpr std::array<RouteConditioningSeeds, kBmwRouteCount> kFrozenSeeds{
    RouteConditioningSeeds{
        {UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
        {UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
    },
    RouteConditioningSeeds{
        {UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
        {UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
    },
};
constexpr RouteConditioningSeeds kThirdRouteSeeds{
    {UINT64_C(0x6d7e86d641b1cabc), UINT64_C(0x69f15ca67f1e52c3)},
    {UINT64_C(0x8a56a93e45d79c29), UINT64_C(0x271d69efba327d71)},
};
constexpr RouteConditioningCalibration kCanonicalConditioning{
    0.5, 10000.0, std::bit_cast<double>(UINT64_C(0x3f847ae140000000)), 1.0, 2000.0,
};
constexpr std::array<double, kBmwRouteCount> kReferenceMassFlowKgS{1.0, 1.0};

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

double synthetic_flow(std::uint64_t global_frame, std::size_t route) {
    return 0.05 * static_cast<double>(route + 1U) *
           static_cast<double>((global_frame % 5U) + 1U);
}

template <class Values, class RouteIds>
ExhaustExcitationBlockView
make_view(std::uint64_t first_frame_index, Values &frame_major_values,
          RouteIds &route_ids,
          std::size_t frame_count = kExcitationFramesPerMethodBlock,
          contract::RationalRateHz rate = kExcitationRateHz) {
    static thread_local std::vector<double> flow;
    flow.resize(frame_count * route_ids.size());
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (std::size_t route = 0; route < route_ids.size(); ++route) {
            flow[frame * route_ids.size() + route] =
                synthetic_flow(first_frame_index + frame, route);
        }
    }
    return ExhaustExcitationBlockView::borrow_for_callback(
        first_frame_index, rate, route_ids, frame_count, frame_major_values, flow);
}

template <class Values, class RouteIds, class Flow>
ExhaustExcitationBlockView
make_view_with_flow(std::uint64_t first_frame_index, Values &frame_major_values,
                    RouteIds &route_ids, Flow &frame_major_flow,
                    std::size_t frame_count = kExcitationFramesPerMethodBlock,
                    contract::RationalRateHz rate = kExcitationRateHz) {
    return ExhaustExcitationBlockView::borrow_for_callback(
        first_frame_index, rate, route_ids, frame_count, frame_major_values,
        frame_major_flow);
}

void fill_block(std::span<double> frame_major_values, std::size_t frame_count,
                std::size_t route_count, std::size_t block_index) {
    expect(frame_major_values.size() == frame_count * route_count,
           "source-stage test fixture has an incomplete route matrix");
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto global = block_index * frame_count + frame;
        for (std::size_t route = 0; route < route_count; ++route) {
            const auto period = route == 0 ? 17U : route == 1 ? 11U : 13U;
            const auto centered =
                static_cast<int>(global % period) - static_cast<int>(period / 2U);
            const auto scale = route == 0 ? 0.125 : route == 1 ? 0.25 : 0.0625;
            frame_major_values[frame * route_count + route] =
                static_cast<double>(centered) * scale;
        }
    }
}

void expect_same_output(std::span<const double> actual,
                        std::span<const double> expected, const char *message) {
    expect(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < actual.size(); ++index) {
        expect(bits(actual[index]) == bits(expected[index]), message);
    }
}

void process_with_continuous_components(CausalReconstruction &reconstruction,
                                        std::vector<RouteConditioner> &conditioners,
                                        std::span<const double> reference_mass_flow,
                                        std::span<const double> input_frame_major,
                                        std::size_t input_frame_count,
                                        std::uint64_t first_input_frame_index,
                                        std::span<double> output_frame_major) {
    const auto route_count = conditioners.size();
    const auto packed_route_count = route_count * 2U;
    expect(reconstruction.route_count() == packed_route_count &&
               reference_mass_flow.size() == route_count,
           "component reference route shape changed");
    std::vector<double> packed_input(input_frame_count * packed_route_count);
    for (std::size_t frame = 0; frame < input_frame_count; ++frame) {
        for (std::size_t route = 0; route < route_count; ++route) {
            packed_input[frame * packed_route_count + route] =
                input_frame_major[frame * route_count + route];
            packed_input[frame * packed_route_count + route_count + route] =
                synthetic_flow(first_input_frame_index + frame, route);
        }
    }
    std::vector<double> reconstructed(
        reconstruction.expected_output_frame_count(input_frame_count) *
        packed_route_count);
    expect(reconstructed.size() == output_frame_major.size() * 2U,
           "component reference output size changed");
    reconstruction.process(packed_input, input_frame_count, reconstructed);
    for (std::size_t frame = 0; frame < output_frame_major.size() / route_count;
         ++frame) {
        for (std::size_t route = 0; route < route_count; ++route) {
            const double activity = std::clamp(
                std::max(
                    reconstructed[frame * packed_route_count + route_count + route],
                    0.0) /
                    reference_mass_flow[route],
                0.0, 1.0);
            output_frame_major[frame * route_count + route] =
                conditioners[route]
                    .process(reconstructed[frame * packed_route_count + route],
                             activity)
                    .conditioned_engine_sim_source_unit;
        }
    }
}

std::vector<RouteConditioner> make_bmw_conditioners() {
    std::vector<RouteConditioner> result;
    result.reserve(kFrozenSeeds.size());
    for (const auto &seeds : kFrozenSeeds) {
        result.emplace_back(seeds.jitter, seeds.air_noise, kCanonicalConditioning);
    }
    return result;
}

void test_exact_block_extent_and_component_wiring() {
    std::vector<double> input(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    fill_block(input, kExcitationFramesPerMethodBlock, kBmwRouteCount, 0);
    std::vector<double> actual(kSourceFramesPerMethodBlock * kBmwRouteCount);

    ExhaustSourceStage stage{kCanonicalRouteIds, kFrozenSeeds, kReferenceMassFlowKgS,
                             kCanonicalConditioning};
    expect(std::ranges::equal(stage.expected_route_ids(), kCanonicalRouteIds) &&
               stage.route_count() == kBmwRouteCount,
           "source stage did not retain its ordered route binding");
    const auto extent = stage.process(make_view(0, input, kCanonicalRouteIds), actual);
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

    CausalReconstruction reconstruction{kBmwRouteCount * 2U};
    auto conditioners = make_bmw_conditioners();
    std::vector<double> expected(kSourceFramesPerMethodBlock * kBmwRouteCount);
    process_with_continuous_components(reconstruction, conditioners,
                                       kReferenceMassFlowKgS, input,
                                       kExcitationFramesPerMethodBlock, 0U, expected);
    expect_same_output(actual, expected,
                       "source stage changed component order or route wiring");
    expect(stage.jitter_rng_state(0) == UINT64_C(0x38c474f6f27476ae) &&
               stage.air_noise_rng_state(0) == UINT64_C(0x6410d400ea8049ca) &&
               stage.jitter_rng_state(1) == UINT64_C(0x90c28d44851509c2) &&
               stage.air_noise_rng_state(1) == UINT64_C(0x2a5d082701ba5e7f),
           "source stage changed per-route random consumption");
}

void test_preview_clock_extent_and_component_wiring() {
    constexpr std::size_t input_frames = kPreviewExcitationFramesPerMethodBlock;
    std::vector<double> input(input_frames * kBmwRouteCount);
    fill_block(input, input_frames, kBmwRouteCount, 0);
    std::vector<double> actual(kSourceFramesPerMethodBlock * kBmwRouteCount);

    ExhaustSourceStage stage{
        kCanonicalRouteIds,       kFrozenSeeds,
        kReferenceMassFlowKgS,    kCanonicalConditioning,
        kPreviewExcitationRateHz, input_frames,
    };
    const auto extent = stage.process(
        make_view(0, input, kCanonicalRouteIds, input_frames, kPreviewExcitationRateHz),
        actual);
    expect(extent ==
                   SourceBlockExtent{
                       0,
                       0,
                       input_frames,
                       kSourceFramesPerMethodBlock,
                   } &&
               stage.input_rate() == kPreviewExcitationRateHz &&
               stage.next_input_frame_index() == input_frames &&
               stage.next_source_frame_index() == kSourceFramesPerMethodBlock &&
               !stage.terminal_failed(),
           "10 kHz source-stage block extent or counters changed");

    CausalReconstruction reconstruction{kBmwRouteCount * 2U,
                                        CausalReconstruction::kPreviewInputRateHz};
    auto conditioners = make_bmw_conditioners();
    std::vector<double> expected(kSourceFramesPerMethodBlock * kBmwRouteCount);
    process_with_continuous_components(reconstruction, conditioners,
                                       kReferenceMassFlowKgS, input, input_frames, 0U,
                                       expected);
    expect_same_output(actual, expected,
                       "10 kHz source stage changed component order or route wiring");
}

void test_explicit_route_ids_preserve_positional_seed_binding() {
    constexpr std::array custom_route_ids{
        contract::RouteId{41},
        contract::RouteId{7},
    };
    std::vector<double> input(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    fill_block(input, kExcitationFramesPerMethodBlock, kBmwRouteCount, 0);

    ExhaustSourceStage canonical{kCanonicalRouteIds, kFrozenSeeds,
                                 kReferenceMassFlowKgS, kCanonicalConditioning};
    ExhaustSourceStage custom{custom_route_ids, kFrozenSeeds, kReferenceMassFlowKgS,
                              kCanonicalConditioning};
    std::vector<double> canonical_output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> custom_output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    static_cast<void>(
        canonical.process(make_view(0, input, kCanonicalRouteIds), canonical_output));
    static_cast<void>(
        custom.process(make_view(0, input, custom_route_ids), custom_output));

    expect(std::ranges::equal(custom.expected_route_ids(), custom_route_ids),
           "source stage changed its explicitly configured route order");
    expect_same_output(custom_output, canonical_output,
                       "route identities changed positional signal or seed ownership");
    expect(custom.jitter_rng_state(0) == canonical.jitter_rng_state(0) &&
               custom.air_noise_rng_state(0) == canonical.air_noise_rng_state(0) &&
               custom.jitter_rng_state(1) == canonical.jitter_rng_state(1) &&
               custom.air_noise_rng_state(1) == canonical.air_noise_rng_state(1),
           "route identities changed positional random-stream ownership");
}

void test_block_continuity_and_session_isolation() {
    std::vector<double> block_0(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> block_1(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    fill_block(block_0, kExcitationFramesPerMethodBlock, kBmwRouteCount, 0);
    fill_block(block_1, kExcitationFramesPerMethodBlock, kBmwRouteCount, 1);

    ExhaustSourceStage first{kCanonicalRouteIds, kFrozenSeeds, kReferenceMassFlowKgS,
                             kCanonicalConditioning};
    ExhaustSourceStage interleaved{kCanonicalRouteIds, kFrozenSeeds,
                                   kReferenceMassFlowKgS, kCanonicalConditioning};
    std::vector<double> first_0(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> first_1(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> other_0(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> other_1(kSourceFramesPerMethodBlock * kBmwRouteCount);

    static_cast<void>(
        first.process(make_view(0, block_0, kCanonicalRouteIds), first_0));
    static_cast<void>(
        interleaved.process(make_view(0, block_0, kCanonicalRouteIds), other_0));
    static_cast<void>(first.process(
        make_view(kExcitationFramesPerMethodBlock, block_1, kCanonicalRouteIds),
        first_1));
    static_cast<void>(interleaved.process(
        make_view(kExcitationFramesPerMethodBlock, block_1, kCanonicalRouteIds),
        other_1));

    expect_same_output(first_0, other_0,
                       "interleaved sessions changed the first block");
    expect_same_output(first_1, other_1,
                       "state reset or crossed sessions at a block boundary");

    CausalReconstruction continuous_reconstruction{kBmwRouteCount * 2U};
    auto continuous_conditioners = make_bmw_conditioners();
    std::vector<double> continuous_0(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> continuous_1(kSourceFramesPerMethodBlock * kBmwRouteCount);
    process_with_continuous_components(
        continuous_reconstruction, continuous_conditioners, kReferenceMassFlowKgS,
        block_0, kExcitationFramesPerMethodBlock, 0U, continuous_0);
    process_with_continuous_components(continuous_reconstruction,
                                       continuous_conditioners, kReferenceMassFlowKgS,
                                       block_1, kExcitationFramesPerMethodBlock,
                                       kExcitationFramesPerMethodBlock, continuous_1);
    expect_same_output(first_1, continuous_1,
                       "source stage reset component state at a method-block boundary");
    expect(first.next_input_frame_index() == 2U * kExcitationFramesPerMethodBlock &&
               first.next_source_frame_index() == 7680,
           "source-stage continuity counters changed after two blocks");
}

void test_structural_rejections_do_not_mutate_state() {
    expect_throw<std::invalid_argument>(
        [] {
            ExhaustSourceStage mismatched_quantum{
                kCanonicalRouteIds,
                kFrozenSeeds,
                kReferenceMassFlowKgS,
                kCanonicalConditioning,
                contract::RationalRateHz{10000, 1},
                kExcitationFramesPerMethodBlock,
            };
        },
        "source stage accepted 400 frames for the 10 kHz method quantum");
    expect_throw<std::invalid_argument>(
        [] {
            ExhaustSourceStage unsupported_rate{
                kCanonicalRouteIds,
                kFrozenSeeds,
                kReferenceMassFlowKgS,
                kCanonicalConditioning,
                contract::RationalRateHz{15000, 1},
                300U,
            };
        },
        "source stage accepted an unsupported 15 kHz input clock");

    std::vector<double> valid(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    fill_block(valid, kExcitationFramesPerMethodBlock, kBmwRouteCount, 0);
    std::vector<double> output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    ExhaustSourceStage candidate{kCanonicalRouteIds, kFrozenSeeds,
                                 kReferenceMassFlowKgS, kCanonicalConditioning};
    const std::array initial_rng_states{
        candidate.jitter_rng_state(0),
        candidate.air_noise_rng_state(0),
        candidate.jitter_rng_state(1),
        candidate.air_noise_rng_state(1),
    };

    auto wrong_routes = kCanonicalRouteIds;
    std::swap(wrong_routes[0], wrong_routes[1]);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(
                candidate.process(make_view(0, valid, wrong_routes), output));
        },
        "source stage accepted swapped route identities");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(
                make_view(0, valid, kCanonicalRouteIds, kExcitationFramesPerMethodBlock,
                          contract::RationalRateHz{10000, 1}),
                output));
        },
        "20 kHz source stage accepted a 10 kHz input view");
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(
                candidate.process(make_view(1, valid, kCanonicalRouteIds), output));
        },
        "source stage accepted a discontinuous first index");

    std::vector<double> partial((kExcitationFramesPerMethodBlock - 1) * kBmwRouteCount);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(
                candidate.process(make_view(0, partial, kCanonicalRouteIds,
                                            kExcitationFramesPerMethodBlock - 1),
                                  output));
        },
        "source stage accepted a partial method block");

    auto incomplete = valid;
    incomplete.pop_back();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(
                make_view(0, incomplete, kCanonicalRouteIds), output));
        },
        "source stage accepted an incomplete input route matrix");

    std::vector<double> short_output(kSourceFramesPerMethodBlock * kBmwRouteCount - 1);
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(make_view(0, valid, kCanonicalRouteIds),
                                                short_output));
        },
        "source stage accepted a short output buffer");

    auto nonfinite = valid;
    nonfinite[199 * kBmwRouteCount + 1] = std::numeric_limits<double>::infinity();
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                candidate.process(make_view(0, nonfinite, kCanonicalRouteIds), output));
        },
        "source stage accepted non-finite excitation");

    std::vector<double> valid_flow(kExcitationFramesPerMethodBlock * kBmwRouteCount,
                                   0.25);
    auto short_flow = valid_flow;
    short_flow.pop_back();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(candidate.process(
                make_view_with_flow(0, valid, kCanonicalRouteIds, short_flow), output));
        },
        "source stage accepted an incomplete exhaust-valve flow matrix");
    auto negative_flow = valid_flow;
    negative_flow[137] = -0.25;
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(candidate.process(
                make_view_with_flow(0, valid, kCanonicalRouteIds, negative_flow),
                output));
        },
        "source stage accepted negative absolute exhaust-valve flow");
    auto nonfinite_flow = valid_flow;
    nonfinite_flow[241] = std::numeric_limits<double>::infinity();
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(candidate.process(
                make_view_with_flow(0, valid, kCanonicalRouteIds, nonfinite_flow),
                output));
        },
        "source stage accepted non-finite exhaust-valve flow");

    expect(candidate.next_input_frame_index() == 0 &&
               candidate.next_source_frame_index() == 0 && !candidate.terminal_failed(),
           "source-stage structural rejection mutated session state");
    expect(
        std::array{
            candidate.jitter_rng_state(0),
            candidate.air_noise_rng_state(0),
            candidate.jitter_rng_state(1),
            candidate.air_noise_rng_state(1),
        } == initial_rng_states,
        "source-stage structural rejection advanced a route-owned RNG");

    ExhaustSourceStage fresh{kCanonicalRouteIds, kFrozenSeeds, kReferenceMassFlowKgS,
                             kCanonicalConditioning};
    std::vector<double> fresh_output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    static_cast<void>(
        candidate.process(make_view(0, valid, kCanonicalRouteIds), output));
    static_cast<void>(
        fresh.process(make_view(0, valid, kCanonicalRouteIds), fresh_output));
    expect_same_output(
        output, fresh_output,
        "source-stage structural rejection changed the next valid block");
}

void test_invalid_seed_ownership_and_terminal_arithmetic_failure() {
    constexpr std::array<contract::RouteId, 0> no_route_ids{};
    constexpr std::array<RouteConditioningSeeds, 0> no_seeds{};
    constexpr std::array<double, 0> no_reference_mass_flows{};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{no_route_ids, no_seeds, no_reference_mass_flows,
                                       kCanonicalConditioning};
        },
        "source stage accepted zero routes");

    auto invalid_route_ids = kCanonicalRouteIds;
    invalid_route_ids[0] = {};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{invalid_route_ids, kFrozenSeeds,
                                       kReferenceMassFlowKgS, kCanonicalConditioning};
        },
        "source stage accepted an invalid route identity");

    auto duplicate_route_ids = kCanonicalRouteIds;
    duplicate_route_ids[1] = duplicate_route_ids[0];
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{duplicate_route_ids, kFrozenSeeds,
                                       kReferenceMassFlowKgS, kCanonicalConditioning};
        },
        "source stage accepted duplicate route identities");

    constexpr std::array<RouteConditioningSeeds, 1> short_seeds{kFrozenSeeds[0]};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, short_seeds,
                                       kReferenceMassFlowKgS, kCanonicalConditioning};
        },
        "source stage accepted mismatched route and seed counts");

    constexpr std::array<double, 1> short_reference_mass_flow{1.0};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, kFrozenSeeds,
                                       short_reference_mass_flow,
                                       kCanonicalConditioning};
        },
        "source stage accepted mismatched route and flow-reference counts");
    constexpr std::array<double, kBmwRouteCount> zero_reference_mass_flow{1.0, 0.0};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, kFrozenSeeds,
                                       zero_reference_mass_flow,
                                       kCanonicalConditioning};
        },
        "source stage accepted a zero exhaust-valve flow reference");
    constexpr std::array<double, kBmwRouteCount> nonfinite_reference_mass_flow{
        1.0, std::numeric_limits<double>::quiet_NaN()};
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, kFrozenSeeds,
                                       nonfinite_reference_mass_flow,
                                       kCanonicalConditioning};
        },
        "source stage accepted a non-finite exhaust-valve flow reference");

    auto duplicate_seeds = kFrozenSeeds;
    duplicate_seeds[1].air_noise.stream = duplicate_seeds[0].jitter.stream;
    duplicate_seeds[1].air_noise.initial_state =
        duplicate_seeds[0].jitter.initial_state + UINT64_C(1);
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, duplicate_seeds,
                                       kReferenceMassFlowKgS, kCanonicalConditioning};
        },
        "source stage accepted one selector for two random-stream owners");

    auto oversized_stream = kFrozenSeeds;
    oversized_stream[0].jitter.stream =
        (std::numeric_limits<std::uint64_t>::max() >> 1U) + UINT64_C(1);
    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustSourceStage invalid{kCanonicalRouteIds, oversized_stream,
                                       kReferenceMassFlowKgS, kCanonicalConditioning};
        },
        "source stage accepted an oversized PCG stream");

    std::vector<double> huge(kExcitationFramesPerMethodBlock * kBmwRouteCount,
                             std::numeric_limits<double>::max());
    std::vector<double> output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    ExhaustSourceStage failed{kCanonicalRouteIds, kFrozenSeeds, kReferenceMassFlowKgS,
                              kCanonicalConditioning};
    expect_throw<std::domain_error>(
        [&] {
            static_cast<void>(
                failed.process(make_view(0, huge, kCanonicalRouteIds), output));
        },
        "overflowing source-stage arithmetic was not rejected");
    expect(failed.terminal_failed(),
           "source stage did not become terminal after partial arithmetic");
    expect_throw<std::logic_error>(
        [&] {
            static_cast<void>(
                failed.process(make_view(0, huge, kCanonicalRouteIds), output));
        },
        "terminal source-stage state was reused");
    expect_throw<std::out_of_range>(
        [&] { static_cast<void>(failed.jitter_rng_state(kBmwRouteCount)); },
        "source stage accepted an out-of-range route probe");
}

void test_one_and_three_route_sessions_preserve_bmw_route_arithmetic() {
    constexpr std::array single_route_ids{contract::RouteId{1}};
    constexpr std::array single_route_seeds{kFrozenSeeds[0]};
    constexpr std::array single_reference_mass_flow{kReferenceMassFlowKgS[0]};
    constexpr std::array triple_route_ids{contract::RouteId{1}, contract::RouteId{2},
                                          contract::RouteId{3}};
    constexpr std::array triple_route_seeds{kFrozenSeeds[0], kFrozenSeeds[1],
                                            kThirdRouteSeeds};
    constexpr std::array triple_reference_mass_flow{kReferenceMassFlowKgS[0],
                                                    kReferenceMassFlowKgS[1], 1.0};

    std::vector<double> bmw_input(kExcitationFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> single_input(kExcitationFramesPerMethodBlock);
    std::vector<double> triple_input(kExcitationFramesPerMethodBlock * 3);
    fill_block(bmw_input, kExcitationFramesPerMethodBlock, kBmwRouteCount, 0);
    fill_block(triple_input, kExcitationFramesPerMethodBlock, 3, 0);
    for (std::size_t frame = 0; frame < kExcitationFramesPerMethodBlock; ++frame) {
        single_input[frame] = bmw_input[frame * kBmwRouteCount];
        expect(bits(triple_input[frame * 3]) ==
                       bits(bmw_input[frame * kBmwRouteCount]) &&
                   bits(triple_input[frame * 3 + 1]) ==
                       bits(bmw_input[frame * kBmwRouteCount + 1]),
               "dynamic source fixture changed the BMW route inputs");
    }

    ExhaustSourceStage bmw{kCanonicalRouteIds, kFrozenSeeds, kReferenceMassFlowKgS,
                           kCanonicalConditioning};
    ExhaustSourceStage single{single_route_ids, single_route_seeds,
                              single_reference_mass_flow, kCanonicalConditioning};
    ExhaustSourceStage triple{triple_route_ids, triple_route_seeds,
                              triple_reference_mass_flow, kCanonicalConditioning};
    std::vector<double> bmw_output(kSourceFramesPerMethodBlock * kBmwRouteCount);
    std::vector<double> single_output(kSourceFramesPerMethodBlock);
    std::vector<double> triple_output(kSourceFramesPerMethodBlock * 3);
    static_cast<void>(
        bmw.process(make_view(0, bmw_input, kCanonicalRouteIds), bmw_output));
    static_cast<void>(
        single.process(make_view(0, single_input, single_route_ids), single_output));
    static_cast<void>(
        triple.process(make_view(0, triple_input, triple_route_ids), triple_output));

    for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
        expect(bits(single_output[frame]) == bits(bmw_output[frame * kBmwRouteCount]) &&
                   bits(triple_output[frame * 3]) ==
                       bits(bmw_output[frame * kBmwRouteCount]) &&
                   bits(triple_output[frame * 3 + 1]) ==
                       bits(bmw_output[frame * kBmwRouteCount + 1]),
               "dynamic source route count changed BMW route arithmetic");
    }
}

void run_tests() {
    test_exact_block_extent_and_component_wiring();
    test_preview_clock_extent_and_component_wiring();
    test_explicit_route_ids_preserve_positional_seed_binding();
    test_block_continuity_and_session_isolation();
    test_structural_rejections_do_not_mutate_state();
    test_invalid_seed_ownership_and_terminal_arithmetic_failure();
    test_one_and_three_route_sessions_preserve_bmw_route_arithmetic();
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
