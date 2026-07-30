#include "presentation/causal_reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::presentation {

CausalReconstruction::CausalReconstruction(std::size_t route_count)
    : route_count_(route_count) {
    if (route_count_ == 0) {
        throw std::invalid_argument{
            "causal reconstruction requires at least one route"};
    }
    if (route_count_ > std::numeric_limits<std::size_t>::max() /
                           dsp::CausalReconstructionTable::tap_count) {
        throw std::overflow_error{"causal reconstruction history size overflowed"};
    }
    histories_.resize(route_count_ * dsp::CausalReconstructionTable::tap_count);
}

ReconstructionPhase
CausalReconstruction::resolve_phase(std::uint64_t source_interval_offset) {
    if (source_interval_offset >= kSourceRate) {
        throw std::invalid_argument{
            "reconstruction phase offset must be inside one source interval"};
    }

    std::uint64_t remainder = source_interval_offset;
    std::uint16_t phase0 = 0;
    for (std::uint32_t bit = 0; bit < 12; ++bit) {
        phase0 = static_cast<std::uint16_t>(phase0 << 1U);
        if (remainder >= kSourceRate - remainder) {
            remainder = remainder - (kSourceRate - remainder);
            phase0 = static_cast<std::uint16_t>(phase0 | 1U);
        } else {
            remainder = remainder * 2U;
        }
    }

    return {
        phase0,
        remainder,
        static_cast<double>(remainder) / static_cast<double>(kSourceRate),
    };
}

std::size_t
CausalReconstruction::expected_output_frame_count(std::size_t input_frame_count) const {
    if (input_frame_count == 0 || input_frame_count > kExcitationFramesPerMethodBlock) {
        throw std::invalid_argument{
            "reconstruction input block must contain between 1 and 200 frames"};
    }

    auto distance = distance_to_next_output_;
    std::size_t result = 0;
    for (std::size_t frame = 0; frame < input_frame_count; ++frame) {
        const auto remaining = kSourceRate - distance;
        const auto output_count =
            UINT64_C(1) + (remaining - UINT64_C(1)) / kPhysicsRate;
        if (output_count > std::numeric_limits<std::size_t>::max() - result) {
            throw std::overflow_error{"reconstruction output frame count overflowed"};
        }
        result += static_cast<std::size_t>(output_count);
        distance = distance + output_count * kPhysicsRate - kSourceRate;
    }
    return result;
}

void CausalReconstruction::process(std::span<const double> input_frame_major,
                                   std::size_t input_frame_count,
                                   std::span<double> output_frame_major) {
    const auto expected_output_frames = expected_output_frame_count(input_frame_count);
    if (input_frame_count > std::numeric_limits<std::size_t>::max() / route_count_ ||
        input_frame_major.size() != input_frame_count * route_count_) {
        throw std::invalid_argument{
            "reconstruction input must be a complete frame-major route matrix"};
    }
    if (expected_output_frames >
            std::numeric_limits<std::size_t>::max() / route_count_ ||
        output_frame_major.size() != expected_output_frames * route_count_) {
        throw std::invalid_argument{
            "reconstruction output must match the exact frame-major clock count"};
    }
    for (const auto sample : input_frame_major) {
        if (!std::isfinite(sample)) {
            throw std::domain_error{"reconstruction input was non-finite"};
        }
    }

    std::size_t output_index = 0;
    for (std::size_t input_frame = 0; input_frame < input_frame_count; ++input_frame) {
        const auto remaining = kSourceRate - distance_to_next_output_;
        const auto output_count =
            UINT64_C(1) + (remaining - UINT64_C(1)) / kPhysicsRate;
        auto offset = distance_to_next_output_;

        for (std::uint64_t frame = 0; frame < output_count; ++frame) {
            const auto phase = resolve_phase(offset);
            const auto row0 = table_.phase_row(phase.phase0);
            const auto row1 =
                table_.phase_row(static_cast<std::size_t>(phase.phase0) + 1U);

            auto samples =
                output_frame_major.subspan(output_index * route_count_, route_count_);
            std::fill(samples.begin(), samples.end(), 0.0);
            auto history_index = oldest_history_frame_;
            for (std::size_t tap = 0; tap < dsp::CausalReconstructionTable::tap_count;
                 ++tap) {
                const double coefficient =
                    row0[tap] + (row1[tap] - row0[tap]) * phase.mix;
                for (std::size_t route = 0; route < route_count_; ++route) {
                    samples[route] =
                        samples[route] +
                        histories_[route * dsp::CausalReconstructionTable::tap_count +
                                   history_index] *
                            coefficient;
                }
                ++history_index;
                if (history_index == dsp::CausalReconstructionTable::tap_count) {
                    history_index = 0;
                }
            }
            for (const auto sample : samples) {
                if (!std::isfinite(sample)) {
                    throw std::domain_error{"reconstruction output was non-finite"};
                }
            }
            ++output_index;
            offset += kPhysicsRate;
        }

        for (std::size_t route = 0; route < route_count_; ++route) {
            histories_[route * dsp::CausalReconstructionTable::tap_count +
                       oldest_history_frame_] =
                input_frame_major[input_frame * route_count_ + route];
        }
        ++oldest_history_frame_;
        if (oldest_history_frame_ == dsp::CausalReconstructionTable::tap_count) {
            oldest_history_frame_ = 0;
        }
        distance_to_next_output_ = offset - kSourceRate;
    }
}

} // namespace engine_sim_offline::presentation
