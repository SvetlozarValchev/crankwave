#include "presentation/exhaust_source_stage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace engine_sim_offline::presentation {

std::vector<contract::RouteId> ExhaustSourceStage::validate_route_ids(
    std::span<const contract::RouteId> expected_route_ids) {
    if (expected_route_ids.empty()) {
        throw std::invalid_argument{"source stage requires at least one route"};
    }
    if (expected_route_ids.size() >
        std::numeric_limits<std::size_t>::max() / kSourceFramesPerMethodBlock) {
        throw std::overflow_error{"source-stage scratch size overflowed"};
    }
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
    return {expected_route_ids.begin(), expected_route_ids.end()};
}

std::vector<RouteConditioningSeeds>
ExhaustSourceStage::validate_seeds(std::span<const RouteConditioningSeeds> seeds,
                                   std::size_t route_count) {
    if (seeds.size() != route_count) {
        throw std::invalid_argument{
            "source-stage route IDs and conditioning seeds must have equal counts"};
    }
    const auto stream_at = [seeds](std::size_t index) {
        const auto &seed = seeds[index / 2U];
        return index % 2U == 0U ? seed.jitter.stream : seed.air_noise.stream;
    };
    for (std::size_t index = 0; index < seeds.size() * 2U; ++index) {
        if (stream_at(index) > dsp::kMaximumPcg32Stream) {
            throw std::invalid_argument{
                "source-stage PCG stream exceeded the encodable range"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (stream_at(index) == stream_at(prior)) {
                throw std::invalid_argument{
                    "source-stage PCG selectors must identify distinct streams"};
            }
        }
    }
    return {seeds.begin(), seeds.end()};
}

ExhaustSourceStage::ExhaustSourceStage(
    std::span<const contract::RouteId> expected_route_ids,
    std::span<const RouteConditioningSeeds> route_seeds,
    RouteConditioningCalibration conditioning, contract::RationalRateHz input_rate,
    std::size_t input_frames_per_block)
    : expected_route_ids_(validate_route_ids(expected_route_ids)),
      seeds_(validate_seeds(route_seeds, expected_route_ids_.size())),
      conditioning_(conditioning), input_rate_(input_rate),
      input_frames_per_block_(input_frames_per_block),
      reconstruction_(expected_route_ids_.size(), input_rate.numerator) {
    if (input_rate_ != kExcitationRateHz) {
        throw std::invalid_argument{
            "source stage admits only the exact 20000/1 excitation rate"};
    }
    if (input_frames_per_block_ != reconstruction_.input_frames_per_method_block()) {
        throw std::invalid_argument{
            "source-stage input frame count must span one exact 20 ms block"};
    }
    reconstructed_scratch_.resize(kSourceFramesPerMethodBlock *
                                  expected_route_ids_.size());
    conditioners_.reserve(seeds_.size());
    for (const auto &seed : seeds_) {
        conditioners_.emplace_back(seed.jitter, seed.air_noise, conditioning_);
    }
}

SourceBlockExtent ExhaustSourceStage::process(ExhaustExcitationBlockView input,
                                              std::span<double> output_frame_major) {
    if (terminal_failed_) {
        throw std::logic_error{
            "source stage cannot resume after an arithmetic failure"};
    }
    if (input.sample_rate() != input_rate_) {
        throw std::invalid_argument{
            "source-stage excitation rate differs from its configured input clock"};
    }
    if (!std::ranges::equal(input.route_ids(), expected_route_ids_)) {
        throw std::invalid_argument{
            "source-stage input route IDs must match the configured order"};
    }
    if (input.first_frame_index() != next_input_frame_index_) {
        throw std::invalid_argument{
            "source-stage excitation frames must be globally contiguous"};
    }
    if (input.route_count() != expected_route_ids_.size() ||
        input.frame_count() != input_frames_per_block_) {
        throw std::invalid_argument{
            "source-stage excitation frame count differs from its configured "
            "method block"};
    }
    if (input.frame_count() >
            std::numeric_limits<std::size_t>::max() / input.route_count() ||
        input.values_engine_sim_source_unit().size() !=
            input.frame_count() * input.route_count()) {
        throw std::invalid_argument{
            "source-stage input must be a complete frame-major route matrix"};
    }
    if (kSourceFramesPerMethodBlock >
            std::numeric_limits<std::size_t>::max() / expected_route_ids_.size() ||
        output_frame_major.size() !=
            kSourceFramesPerMethodBlock * expected_route_ids_.size()) {
        throw std::invalid_argument{
            "source stage requires exactly 3840 complete output frames per block"};
    }
    if (next_input_frame_index_ >
            std::numeric_limits<std::uint64_t>::max() - input_frames_per_block_ ||
        next_source_frame_index_ >
            std::numeric_limits<std::uint64_t>::max() - kSourceFramesPerMethodBlock) {
        throw std::overflow_error{"source-stage frame counter overflow"};
    }
    for (const auto sample : input.values_engine_sim_source_unit()) {
        if (!std::isfinite(sample)) {
            throw std::domain_error{"source-stage excitation input was non-finite"};
        }
    }

    const auto exact_output_count =
        reconstruction_.expected_output_frame_count(input_frames_per_block_);
    if (exact_output_count != kSourceFramesPerMethodBlock ||
        reconstruction_.distance_to_next_output() != 0) {
        terminal_failed_ = true;
        throw std::logic_error{
            "source-stage method block did not begin on its exact clock phase"};
    }

    const SourceBlockExtent extent{
        next_input_frame_index_,
        next_source_frame_index_,
        input_frames_per_block_,
        kSourceFramesPerMethodBlock,
    };

    try {
        reconstruction_.process(input.values_engine_sim_source_unit(),
                                input.frame_count(), reconstructed_scratch_);
        if (reconstruction_.distance_to_next_output() != 0U) {
            throw std::logic_error{
                "source-stage method block did not return to exact clock phase"};
        }
        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            for (std::size_t route = 0; route < route_count(); ++route) {
                const auto result = conditioners_[route].process(
                    reconstructed_scratch_[frame * route_count() + route]);
                output_frame_major[frame * route_count() + route] =
                    result.conditioned_engine_sim_source_unit;
            }
        }
    } catch (...) {
        terminal_failed_ = true;
        throw;
    }

    next_input_frame_index_ += input_frames_per_block_;
    next_source_frame_index_ += kSourceFramesPerMethodBlock;
    return extent;
}

std::span<const contract::RouteId>
ExhaustSourceStage::expected_route_ids() const noexcept {
    return expected_route_ids_;
}

std::size_t ExhaustSourceStage::route_count() const noexcept {
    return expected_route_ids_.size();
}

contract::RationalRateHz ExhaustSourceStage::input_rate() const noexcept {
    return input_rate_;
}

std::size_t ExhaustSourceStage::input_frames_per_block() const noexcept {
    return input_frames_per_block_;
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
