#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::dsp {

inline constexpr std::size_t kSixChannelResamplerChannelCount = 6;

using SixChannelResamplerFrame = std::array<double, kSixChannelResamplerChannelCount>;

struct SixChannelResamplerPhase {
    std::uint16_t row0 = 0;
    std::uint64_t remainder = 0;
    double row_mix = 0.0;

    friend bool operator==(const SixChannelResamplerPhase &,
                           const SixChannelResamplerPhase &) = default;
};

// Fixed-rate, causal, band-limited reconstruction for six synchronous scalar
// histories. The class deliberately has no exhaust, port, or presentation units;
// callers own the meaning of each lane.
//
// Input frame n is an interval-end observation at (n + 1) / 80 kHz. Output frame
// zero is anchored at time zero; frames preceding an input timestamp are emitted
// before that input is committed. A call is bounded to one 1,600-frame source block.
// State is continuous between calls, so callers may partition that block arbitrarily
// without changing output.
class SixChannelCausalResampler {
  public:
    static constexpr std::uint64_t input_rate_hz = 80'000;
    static constexpr std::uint64_t output_rate_hz = 192'000;
    static constexpr std::uint64_t reduced_rate_numerator = 12;
    static constexpr std::uint64_t reduced_rate_denominator = 5;

    static constexpr std::size_t channel_count = kSixChannelResamplerChannelCount;
    static constexpr std::size_t tap_count = 257;
    static constexpr std::size_t group_delay_input_frames = 128;
    static constexpr std::size_t phase_interval_count = 4'096;
    static constexpr std::size_t phase_row_count = phase_interval_count + 1;
    static constexpr std::size_t maximum_input_frames_per_call = 1'600;
    static constexpr std::size_t canonical_input_frames_per_block = 1'600;
    static constexpr std::size_t canonical_output_frames_per_block = 3'840;
    static constexpr double kaiser_beta = 12.0;
    static constexpr double source_nyquist_cutoff = 0.95;

    SixChannelCausalResampler() = default;

    [[nodiscard]] static SixChannelResamplerPhase
    resolve_phase(std::uint64_t source_interval_offset);

    [[nodiscard]] std::size_t
    expected_output_frame_count(std::size_t input_frame_count) const;

    // Validates the complete call before publishing output or mutable state. Output
    // must have exactly expected_output_frame_count(input.size()) frames.
    void process(std::span<const SixChannelResamplerFrame> input,
                 std::span<SixChannelResamplerFrame> output);

    [[nodiscard]] std::uint64_t distance_to_next_output() const noexcept {
        return distance_to_next_output_;
    }

  private:
    using ChannelHistory = std::array<double, tap_count>;

    std::array<ChannelHistory, channel_count> histories_{};
    std::size_t oldest_history_frame_ = 0;
    std::uint64_t distance_to_next_output_ = 0;
};

} // namespace engine_sim_offline::dsp
