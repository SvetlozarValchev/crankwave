#pragma once

#include "dsp/causal_reconstruction_table.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kExcitationFramesPerMethodBlock = 400;
inline constexpr std::size_t kSourceFramesPerMethodBlock = 3840;

struct ReconstructionPhase {
    std::uint16_t phase0 = 0;
    std::uint64_t remainder = 0;
    double mix = 0.0;

    friend bool operator==(const ReconstructionPhase &,
                           const ReconstructionPhase &) = default;
};

// Exact shared-clock, N-route causal reconstruction. Values are frame-major:
// frame * route_count() + route. The current input frame is committed only
// after every output in its source interval.
class CausalReconstruction {
  public:
    static constexpr std::uint64_t kInputRateHz = 20000;
    static constexpr std::uint64_t kSourceRateHz = 192000;

    explicit CausalReconstruction(std::size_t route_count,
                                  std::uint64_t input_rate_hz = kInputRateHz);

    [[nodiscard]] static ReconstructionPhase
    resolve_phase(std::uint64_t source_interval_offset);

    [[nodiscard]] std::size_t
    expected_output_frame_count(std::size_t input_frame_count) const;

    void process(std::span<const double> input_frame_major,
                 std::size_t input_frame_count, std::span<double> output_frame_major);

    [[nodiscard]] std::size_t route_count() const noexcept {
        return route_count_;
    }

    [[nodiscard]] std::uint64_t input_rate_hz() const noexcept {
        return input_rate_hz_;
    }

    [[nodiscard]] std::size_t input_frames_per_method_block() const noexcept {
        return input_frames_per_method_block_;
    }

    [[nodiscard]] std::uint64_t distance_to_next_output() const noexcept {
        return distance_to_next_output_;
    }

  private:
    static constexpr std::uint64_t kRationalPhaseStepHz = 4000;
    static constexpr std::size_t kRationalPhaseKernelCount =
        kSourceRateHz / kRationalPhaseStepHz;

    dsp::CausalReconstructionTable table_;
    std::size_t route_count_ = 0;
    std::uint64_t input_rate_hz_ = 0;
    std::size_t input_frames_per_method_block_ = 0;
    // The admitted 20 kHz -> 192 kHz clocks visit only 48 distinct fractional
    // phases. Store the exact interpolated 257-tap rows once so the realtime hot
    // path does not repeat phase resolution and coefficient interpolation for every
    // output frame.
    std::vector<double> rational_phase_kernels_;
    std::vector<double> histories_;
    std::size_t oldest_history_frame_ = 0;
    std::uint64_t distance_to_next_output_ = 0;
};

} // namespace engine_sim_offline::presentation
