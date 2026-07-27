#pragma once

#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/p18_causal_reconstruction.hpp"
#include "presentation/p18_conditioning.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

inline constexpr contract::RationalRateHz kP18ExcitationRateHz{10000, 1};
inline constexpr std::array<contract::RouteId, kP18ExhaustRouteCount>
    kP18ReferenceRouteIds{contract::RouteId{1}, contract::RouteId{2}};

struct P18RouteConditioningSeeds {
    P18Pcg32Seed jitter;
    P18Pcg32Seed air_noise;

    friend bool operator==(const P18RouteConditioningSeeds &,
                           const P18RouteConditioningSeeds &) = default;
};

struct P18ConditionedSourceFrame {
    std::array<double, kP18ExhaustRouteCount> route_values_engine_sim_source_unit{};

    friend bool operator==(const P18ConditionedSourceFrame &,
                           const P18ConditionedSourceFrame &) = default;
};

struct P18SourceBlockExtent {
    std::uint64_t first_input_frame_index = 0;
    std::uint64_t first_source_frame_index = 0;
    std::size_t input_frame_count = 0;
    std::size_t source_frame_count = 0;

    friend bool operator==(const P18SourceBlockExtent &,
                           const P18SourceBlockExtent &) = default;
};

// Fixture-free coordinator for one exact P1.8 source-stage session. Each call
// admits one complete 200-frame method block and produces 3,840 conditioned
// source frames. Arithmetic failure is terminal because route state may already
// have advanced; structural validation happens before mutation.
class P18SourceStage {
  public:
    explicit P18SourceStage(
        std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount> seeds);

    [[nodiscard]] P18SourceBlockExtent
    process(ExhaustExcitationBlockView input,
            std::span<P18ConditionedSourceFrame> output);

    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept;
    [[nodiscard]] bool terminal_failed() const noexcept;

    [[nodiscard]] std::uint64_t jitter_rng_state(std::size_t route) const;
    [[nodiscard]] std::uint64_t air_noise_rng_state(std::size_t route) const;

  private:
    static std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount>
    validate_seeds(std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount> seeds);

    std::array<P18RouteConditioningSeeds, kP18ExhaustRouteCount> seeds_;
    P18CausalReconstruction reconstruction_;
    std::array<P18RouteConditioner, kP18ExhaustRouteCount> conditioners_;
    std::array<P18SourceFrame, kP18SourceFramesPerMethodBlock> reconstructed_scratch_{};
    std::uint64_t next_input_frame_index_ = 0;
    std::uint64_t next_source_frame_index_ = 0;
    bool terminal_failed_ = false;
};

} // namespace engine_sim_offline::presentation
