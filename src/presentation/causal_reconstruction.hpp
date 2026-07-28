#pragma once

#include "dsp/causal_reconstruction_table.hpp"
#include "presentation/exhaust_excitation_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kExcitationFramesPerMethodBlock = 200;
inline constexpr std::size_t kSourceFramesPerMethodBlock = 3840;

struct ReconstructedSourceFrame {
    std::array<double, kExhaustExcitationRouteCount>
        route_values_engine_sim_source_unit{};

    friend bool operator==(const ReconstructedSourceFrame &,
                           const ReconstructedSourceFrame &) = default;
};

struct ReconstructionPhase {
    std::uint16_t phase0 = 0;
    std::uint64_t remainder = 0;
    double mix = 0.0;

    friend bool operator==(const ReconstructionPhase &,
                           const ReconstructionPhase &) = default;
};

// Exact shared-clock, two-route causal reconstruction. The current input
// frame is committed only after every output in its source interval.
class CausalReconstruction {
  public:
    static constexpr std::uint64_t kPhysicsRate = 10000;
    static constexpr std::uint64_t kSourceRate = 192000;

    CausalReconstruction() = default;

    [[nodiscard]] static ReconstructionPhase
    resolve_phase(std::uint64_t source_interval_offset);

    [[nodiscard]] std::size_t
    expected_output_frame_count(std::size_t input_frame_count) const;

    void process(std::span<const ExhaustExcitationFrame> input,
                 std::span<ReconstructedSourceFrame> output);

    [[nodiscard]] std::uint64_t distance_to_next_output() const noexcept {
        return distance_to_next_output_;
    }

  private:
    dsp::CausalReconstructionTable table_;
    std::array<std::array<double, dsp::CausalReconstructionTable::tap_count>,
               kExhaustExcitationRouteCount>
        histories_{};
    std::size_t oldest_history_frame_ = 0;
    std::uint64_t distance_to_next_output_ = 0;
};

} // namespace engine_sim_offline::presentation
