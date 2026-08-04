#include "presentation/causal_reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::presentation {

CausalReconstruction::CausalReconstruction(std::size_t route_count,
                                           std::uint64_t input_rate_hz)
    : route_count_(route_count), input_rate_hz_(input_rate_hz),
      input_frames_per_method_block_(input_rate_hz == kInputRateHz
                                         ? kExcitationFramesPerMethodBlock
                                         : 0U) {
    if (route_count_ == 0) {
        throw std::invalid_argument{
            "causal reconstruction requires at least one route"};
    }
    if (input_frames_per_method_block_ == 0U) {
        throw std::invalid_argument{
            "causal reconstruction admits only exact 20000 Hz input"};
    }
    if (route_count_ > std::numeric_limits<std::size_t>::max() /
                           dsp::CausalReconstructionTable::tap_count) {
        throw std::overflow_error{"causal reconstruction history size overflowed"};
    }
    static_assert(kInputRateHz % kRationalPhaseStepHz == 0U);
    static_assert(kSourceRateHz % kRationalPhaseStepHz == 0U);
    static_assert(kRationalPhaseKernelCount == 48U);
    rational_phase_kernels_.resize(kRationalPhaseKernelCount *
                                   dsp::CausalReconstructionTable::tap_count);
    for (std::size_t kernel = 0; kernel < kRationalPhaseKernelCount; ++kernel) {
        const auto phase = resolve_phase(kernel * kRationalPhaseStepHz);
        const auto row0 = table_.phase_row(phase.phase0);
        const auto row1 =
            table_.phase_row(static_cast<std::size_t>(phase.phase0) + 1U);
        for (std::size_t tap = 0; tap < dsp::CausalReconstructionTable::tap_count;
             ++tap) {
            rational_phase_kernels_[
                kernel * dsp::CausalReconstructionTable::tap_count + tap] =
                row0[tap] + (row1[tap] - row0[tap]) * phase.mix;
        }
    }
    histories_.resize(route_count_ * dsp::CausalReconstructionTable::tap_count);
}

ReconstructionPhase
CausalReconstruction::resolve_phase(std::uint64_t source_interval_offset) {
    if (source_interval_offset >= kSourceRateHz) {
        throw std::invalid_argument{
            "reconstruction phase offset must be inside one source interval"};
    }

    std::uint64_t remainder = source_interval_offset;
    std::uint16_t phase0 = 0;
    for (std::uint32_t bit = 0; bit < 12; ++bit) {
        phase0 = static_cast<std::uint16_t>(phase0 << 1U);
        if (remainder >= kSourceRateHz - remainder) {
            remainder = remainder - (kSourceRateHz - remainder);
            phase0 = static_cast<std::uint16_t>(phase0 | 1U);
        } else {
            remainder = remainder * 2U;
        }
    }

    return {
        phase0,
        remainder,
        static_cast<double>(remainder) / static_cast<double>(kSourceRateHz),
    };
}

std::size_t
CausalReconstruction::expected_output_frame_count(std::size_t input_frame_count) const {
    if (input_frame_count == 0 || input_frame_count > input_frames_per_method_block_) {
        throw std::invalid_argument{
            "reconstruction input block exceeds its configured method quantum"};
    }

    auto distance = distance_to_next_output_;
    std::size_t result = 0;
    for (std::size_t frame = 0; frame < input_frame_count; ++frame) {
        const auto remaining = kSourceRateHz - distance;
        const auto output_count =
            UINT64_C(1) + (remaining - UINT64_C(1)) / input_rate_hz_;
        if (output_count > std::numeric_limits<std::size_t>::max() - result) {
            throw std::overflow_error{"reconstruction output frame count overflowed"};
        }
        result += static_cast<std::size_t>(output_count);
        distance = distance + output_count * input_rate_hz_ - kSourceRateHz;
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
        const auto remaining = kSourceRateHz - distance_to_next_output_;
        const auto output_count =
            UINT64_C(1) + (remaining - UINT64_C(1)) / input_rate_hz_;
        auto offset = distance_to_next_output_;

        for (std::uint64_t frame = 0; frame < output_count; ++frame) {
            if (offset % kRationalPhaseStepHz != 0U) {
                throw std::logic_error{
                    "reconstruction clock left its exact rational phase lattice"};
            }
            const auto kernel_index =
                static_cast<std::size_t>(offset / kRationalPhaseStepHz);
            if (kernel_index >= kRationalPhaseKernelCount) {
                throw std::logic_error{
                    "reconstruction rational phase index exceeded its table"};
            }
            const std::span<const double, dsp::CausalReconstructionTable::tap_count>
                coefficients{
                    rational_phase_kernels_.data() +
                        kernel_index * dsp::CausalReconstructionTable::tap_count,
                    dsp::CausalReconstructionTable::tap_count};

            auto samples =
                output_frame_major.subspan(output_index * route_count_, route_count_);
            for (std::size_t route = 0; route < route_count_; ++route) {
                double sample = 0.0;
                auto history_index = oldest_history_frame_;
                const auto history_offset =
                    route * dsp::CausalReconstructionTable::tap_count;
                for (std::size_t tap = 0;
                     tap < dsp::CausalReconstructionTable::tap_count; ++tap) {
                    sample = sample +
                             histories_[history_offset + history_index] *
                                 coefficients[tap];
                    ++history_index;
                    if (history_index == dsp::CausalReconstructionTable::tap_count) {
                        history_index = 0;
                    }
                }
                if (!std::isfinite(sample)) {
                    throw std::domain_error{"reconstruction output was non-finite"};
                }
                samples[route] = sample;
            }
            ++output_index;
            offset += input_rate_hz_;
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
        distance_to_next_output_ = offset - kSourceRateHz;
    }
}

} // namespace engine_sim_offline::presentation
