#pragma once

#include "dsp/causal_reconstruction_table.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kExcitationFramesPerMethodBlock = 200;
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
    static constexpr std::uint64_t kPhysicsRate = 10000;
    static constexpr std::uint64_t kSourceRate = 192000;

    explicit CausalReconstruction(std::size_t route_count);

    [[nodiscard]] static ReconstructionPhase
    resolve_phase(std::uint64_t source_interval_offset);

    [[nodiscard]] std::size_t
    expected_output_frame_count(std::size_t input_frame_count) const;

    void process(std::span<const double> input_frame_major,
                 std::size_t input_frame_count, std::span<double> output_frame_major);

    [[nodiscard]] std::size_t route_count() const noexcept {
        return route_count_;
    }

    [[nodiscard]] std::uint64_t distance_to_next_output() const noexcept {
        return distance_to_next_output_;
    }

  private:
    dsp::CausalReconstructionTable table_;
    std::size_t route_count_ = 0;
    std::vector<double> histories_;
    std::size_t oldest_history_frame_ = 0;
    std::uint64_t distance_to_next_output_ = 0;
};

} // namespace engine_sim_offline::presentation
