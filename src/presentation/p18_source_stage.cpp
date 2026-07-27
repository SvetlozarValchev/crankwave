#include "presentation/p18_source_stage.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr std::uint64_t kMaximumPcgStream =
    std::numeric_limits<std::uint64_t>::max() >> 1U;

} // namespace

std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount>
P18SourceStage::validate_seeds(
    std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount> seeds) {
    const std::array<P18Pcg32Seed, kP18ExhaustRouteCount * 2> flattened{
        seeds[0].jitter,
        seeds[0].air_noise,
        seeds[1].jitter,
        seeds[1].air_noise,
    };
    for (std::size_t index = 0; index < flattened.size(); ++index) {
        if (flattened[index].stream > kMaximumPcgStream) {
            throw std::invalid_argument{
                "P1.8 source-stage PCG stream exceeded the encodable range"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (flattened[index].stream == flattened[prior].stream) {
                throw std::invalid_argument{
                    "P1.8 source-stage PCG selectors must identify distinct streams"};
            }
        }
    }
    return seeds;
}

P18SourceStage::P18SourceStage(
    std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount> seeds)
    : seeds_(validate_seeds(seeds)),
      conditioners_{
          P18RouteConditioner{seeds_[0].jitter, seeds_[0].air_noise},
          P18RouteConditioner{seeds_[1].jitter, seeds_[1].air_noise},
      } {}

P18SourceBlockExtent
P18SourceStage::process(ExhaustExcitationBlockView input,
                        std::span<P18ConditionedSourceFrame> output) {
    if (terminal_failed_) {
        throw std::logic_error{
            "P1.8 source stage cannot resume after an arithmetic failure"};
    }
    if (input.sample_rate() != kP18ExcitationRateHz) {
        throw std::invalid_argument{
            "P1.8 source stage requires an exact 10000/1 excitation rate"};
    }
    if (input.route_ids() != kP18ReferenceRouteIds) {
        throw std::invalid_argument{
            "P1.8 source stage requires ordered local route IDs 1 then 2"};
    }
    if (input.first_frame_index() != next_input_frame_index_) {
        throw std::invalid_argument{
            "P1.8 source-stage excitation frames must be globally contiguous"};
    }
    if (input.frames().size() != kP18PhysicsFramesPerMethodBlock) {
        throw std::invalid_argument{
            "P1.8 source stage requires exactly 200 excitation frames per block"};
    }
    if (output.size() != kP18SourceFramesPerMethodBlock) {
        throw std::invalid_argument{
            "P1.8 source stage requires exactly 3840 output frames per block"};
    }
    if (next_input_frame_index_ > std::numeric_limits<std::uint64_t>::max() -
                                      kP18PhysicsFramesPerMethodBlock ||
        next_source_frame_index_ > std::numeric_limits<std::uint64_t>::max() -
                                       kP18SourceFramesPerMethodBlock) {
        throw std::overflow_error{"P1.8 source-stage frame counter overflow"};
    }
    for (const auto &frame : input.frames()) {
        for (const auto sample : frame.route_values_engine_sim_source_unit) {
            if (!std::isfinite(sample)) {
                throw std::domain_error{
                    "P1.8 source-stage excitation input was non-finite"};
            }
        }
    }

    const auto exact_output_count =
        reconstruction_.expected_output_frame_count(kP18PhysicsFramesPerMethodBlock);
    if (exact_output_count != kP18SourceFramesPerMethodBlock ||
        reconstruction_.distance_to_next_output() != 0) {
        terminal_failed_ = true;
        throw std::logic_error{
            "P1.8 source-stage method block did not begin on its exact clock phase"};
    }

    const P18SourceBlockExtent extent{
        next_input_frame_index_,
        next_source_frame_index_,
        kP18PhysicsFramesPerMethodBlock,
        kP18SourceFramesPerMethodBlock,
    };

    try {
        reconstruction_.process(input.frames(), reconstructed_scratch_);
        for (std::size_t frame = 0; frame < output.size(); ++frame) {
            for (std::size_t route = 0; route < kP18ExhaustRouteCount; ++route) {
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

    next_input_frame_index_ += kP18PhysicsFramesPerMethodBlock;
    next_source_frame_index_ += kP18SourceFramesPerMethodBlock;
    return extent;
}

std::uint64_t P18SourceStage::next_input_frame_index() const noexcept {
    return next_input_frame_index_;
}

std::uint64_t P18SourceStage::next_source_frame_index() const noexcept {
    return next_source_frame_index_;
}

bool P18SourceStage::terminal_failed() const noexcept {
    return terminal_failed_;
}

std::uint64_t P18SourceStage::jitter_rng_state(std::size_t route) const {
    if (route >= conditioners_.size()) {
        throw std::out_of_range{"P1.8 source-stage jitter route is out of range"};
    }
    return conditioners_[route].jitter_rng_state();
}

std::uint64_t P18SourceStage::air_noise_rng_state(std::size_t route) const {
    if (route >= conditioners_.size()) {
        throw std::out_of_range{"P1.8 source-stage air-noise route is out of range"};
    }
    return conditioners_[route].air_noise_rng_state();
}

} // namespace engine_sim_offline::presentation
