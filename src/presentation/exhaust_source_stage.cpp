#include "presentation/exhaust_source_stage.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr std::uint64_t kMaximumPcgStream =
    std::numeric_limits<std::uint64_t>::max() >> 1U;

} // namespace

ExhaustSourceRouteIds
ExhaustSourceStage::validate_route_ids(ExhaustSourceRouteIds expected_route_ids) {
    for (std::size_t index = 0; index < expected_route_ids.size(); ++index) {
        if (!expected_route_ids[index].valid()) {
            throw std::invalid_argument{
                "source-stage route IDs must be valid nonzero identities"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (expected_route_ids[index] == expected_route_ids[prior]) {
                throw std::invalid_argument{"source-stage route IDs must be distinct"};
            }
        }
    }
    return expected_route_ids;
}

std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount>
ExhaustSourceStage::validate_seeds(
    std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> seeds) {
    const std::array<Pcg32Seed, kExhaustExcitationRouteCount * 2> flattened{
        seeds[0].jitter,
        seeds[0].air_noise,
        seeds[1].jitter,
        seeds[1].air_noise,
    };
    for (std::size_t index = 0; index < flattened.size(); ++index) {
        if (flattened[index].stream > kMaximumPcgStream) {
            throw std::invalid_argument{
                "source-stage PCG stream exceeded the encodable range"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (flattened[index].stream == flattened[prior].stream) {
                throw std::invalid_argument{
                    "source-stage PCG selectors must identify distinct streams"};
            }
        }
    }
    return seeds;
}

ExhaustSourceStage::ExhaustSourceStage(
    ExhaustSourceRouteIds expected_route_ids,
    std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> route_seeds)
    : expected_route_ids_(validate_route_ids(expected_route_ids)),
      seeds_(validate_seeds(route_seeds)),
      conditioners_{
          RouteConditioner{seeds_[0].jitter, seeds_[0].air_noise},
          RouteConditioner{seeds_[1].jitter, seeds_[1].air_noise},
      } {}

SourceBlockExtent
ExhaustSourceStage::process(ExhaustExcitationBlockView input,
                            std::span<ConditionedSourceFrame> output) {
    if (terminal_failed_) {
        throw std::logic_error{
            "source stage cannot resume after an arithmetic failure"};
    }
    if (input.sample_rate() != kExcitationRateHz) {
        throw std::invalid_argument{
            "source stage requires an exact 10000/1 excitation rate"};
    }
    if (input.route_ids() != expected_route_ids_) {
        throw std::invalid_argument{
            "source-stage input route IDs must match the configured order"};
    }
    if (input.first_frame_index() != next_input_frame_index_) {
        throw std::invalid_argument{
            "source-stage excitation frames must be globally contiguous"};
    }
    if (input.frames().size() != kExcitationFramesPerMethodBlock) {
        throw std::invalid_argument{
            "source stage requires exactly 200 excitation frames per block"};
    }
    if (output.size() != kSourceFramesPerMethodBlock) {
        throw std::invalid_argument{
            "source stage requires exactly 3840 output frames per block"};
    }
    if (next_input_frame_index_ > std::numeric_limits<std::uint64_t>::max() -
                                      kExcitationFramesPerMethodBlock ||
        next_source_frame_index_ >
            std::numeric_limits<std::uint64_t>::max() - kSourceFramesPerMethodBlock) {
        throw std::overflow_error{"source-stage frame counter overflow"};
    }
    for (const auto &frame : input.frames()) {
        for (const auto sample : frame.route_values_engine_sim_source_unit) {
            if (!std::isfinite(sample)) {
                throw std::domain_error{"source-stage excitation input was non-finite"};
            }
        }
    }

    const auto exact_output_count =
        reconstruction_.expected_output_frame_count(kExcitationFramesPerMethodBlock);
    if (exact_output_count != kSourceFramesPerMethodBlock ||
        reconstruction_.distance_to_next_output() != 0) {
        terminal_failed_ = true;
        throw std::logic_error{
            "source-stage method block did not begin on its exact clock phase"};
    }

    const SourceBlockExtent extent{
        next_input_frame_index_,
        next_source_frame_index_,
        kExcitationFramesPerMethodBlock,
        kSourceFramesPerMethodBlock,
    };

    try {
        reconstruction_.process(input.frames(), reconstructed_scratch_);
        for (std::size_t frame = 0; frame < output.size(); ++frame) {
            for (std::size_t route = 0; route < kExhaustExcitationRouteCount; ++route) {
                const auto result = conditioners_[route].process(
                    reconstructed_scratch_[frame]
                        .route_values_engine_sim_source_unit[route]);
                output[frame].route_values_engine_sim_source_unit[route] =
                    result.conditioned_engine_sim_source_unit;
            }
        }
    } catch (...) {
        terminal_failed_ = true;
        throw;
    }

    next_input_frame_index_ += kExcitationFramesPerMethodBlock;
    next_source_frame_index_ += kSourceFramesPerMethodBlock;
    return extent;
}

const ExhaustSourceRouteIds &ExhaustSourceStage::expected_route_ids() const noexcept {
    return expected_route_ids_;
}

std::uint64_t ExhaustSourceStage::next_input_frame_index() const noexcept {
    return next_input_frame_index_;
}

std::uint64_t ExhaustSourceStage::next_source_frame_index() const noexcept {
    return next_source_frame_index_;
}

bool ExhaustSourceStage::terminal_failed() const noexcept {
    return terminal_failed_;
}

std::uint64_t ExhaustSourceStage::jitter_rng_state(std::size_t route) const {
    if (route >= conditioners_.size()) {
        throw std::out_of_range{"source-stage jitter route is out of range"};
    }
    return conditioners_[route].jitter_rng_state();
}

std::uint64_t ExhaustSourceStage::air_noise_rng_state(std::size_t route) const {
    if (route >= conditioners_.size()) {
        throw std::out_of_range{"source-stage air-noise route is out of range"};
    }
    return conditioners_[route].air_noise_rng_state();
}

} // namespace engine_sim_offline::presentation
