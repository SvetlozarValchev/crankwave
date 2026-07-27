#pragma once

#include "dsp/p18_reconstruction_table.hpp"
#include "presentation/exhaust_excitation_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kP18PhysicsFramesPerMethodBlock = 200;
inline constexpr std::size_t kP18SourceFramesPerMethodBlock = 3840;

struct P18SourceFrame {
    std::array<double, kP18ExhaustRouteCount> route_values_engine_sim_source_unit{};

    friend bool operator==(const P18SourceFrame &, const P18SourceFrame &) = default;
};

struct P18ReconstructionPhase {
    std::uint16_t phase0 = 0;
    std::uint64_t remainder = 0;
    double mix = 0.0;

    friend bool operator==(const P18ReconstructionPhase &,
                           const P18ReconstructionPhase &) = default;
};

// Exact shared-clock, two-route causal P1.8 reconstruction. The current input
// frame is committed only after every output in its source interval.
class P18CausalReconstruction {
  public:
    static constexpr std::uint64_t kPhysicsRate = 10000;
    static constexpr std::uint64_t kSourceRate = 192000;

    P18CausalReconstruction() = default;

    [[nodiscard]] static P18ReconstructionPhase
    resolve_phase(std::uint64_t source_interval_offset);

    [[nodiscard]] std::size_t
    expected_output_frame_count(std::size_t input_frame_count) const;

    void process(std::span<const ExhaustExcitationFrame> input,
                 std::span<P18SourceFrame> output);

    [[nodiscard]] std::uint64_t distance_to_next_output() const noexcept {
        return distance_to_next_output_;
    }

  private:
    dsp::P18ReconstructionTable table_;
    std::array<std::array<double, dsp::P18ReconstructionTable::tap_count>,
               kP18ExhaustRouteCount>
        histories_{};
    std::size_t oldest_history_frame_ = 0;
    std::uint64_t distance_to_next_output_ = 0;
};

} // namespace engine_sim_offline::presentation
