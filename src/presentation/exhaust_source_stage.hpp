#pragma once

#include "presentation/causal_reconstruction.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/route_conditioning.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

inline constexpr contract::RationalRateHz kExcitationRateHz{10000, 1};

using ExhaustSourceRouteIds =
    std::array<contract::RouteId, kExhaustExcitationRouteCount>;

struct RouteConditioningSeeds {
    Pcg32Seed jitter;
    Pcg32Seed air_noise;

    friend bool operator==(const RouteConditioningSeeds &,
                           const RouteConditioningSeeds &) = default;
};

struct ConditionedSourceFrame {
    std::array<double, kExhaustExcitationRouteCount>
        route_values_engine_sim_source_unit{};

    friend bool operator==(const ConditionedSourceFrame &,
                           const ConditionedSourceFrame &) = default;
};

struct SourceBlockExtent {
    std::uint64_t first_input_frame_index = 0;
    std::uint64_t first_source_frame_index = 0;
    std::size_t input_frame_count = 0;
    std::size_t source_frame_count = 0;

    friend bool operator==(const SourceBlockExtent &,
                           const SourceBlockExtent &) = default;
};

// Fixture-free coordinator for one exact source-stage session. Each call
// admits one complete 200-frame method block and produces 3,840 conditioned
// source frames. Arithmetic failure is terminal because route state may already
// have advanced; structural validation happens before mutation.
class ExhaustSourceStage {
  public:
    // Route values and conditioning seeds share this exact positional order:
    // route_seeds[i] belongs to expected_route_ids[i], and every input block must
    // present the same ordered IDs before any stateful DSP work begins.
    ExhaustSourceStage(
        ExhaustSourceRouteIds expected_route_ids,
        std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> route_seeds);

    [[nodiscard]] SourceBlockExtent process(ExhaustExcitationBlockView input,
                                            std::span<ConditionedSourceFrame> output);

    [[nodiscard]] const ExhaustSourceRouteIds &expected_route_ids() const noexcept;
    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept;
    [[nodiscard]] bool terminal_failed() const noexcept;

    [[nodiscard]] std::uint64_t jitter_rng_state(std::size_t route) const;
    [[nodiscard]] std::uint64_t air_noise_rng_state(std::size_t route) const;

  private:
    static ExhaustSourceRouteIds
    validate_route_ids(ExhaustSourceRouteIds expected_route_ids);
    static std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount>
    validate_seeds(
        std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> seeds);

    ExhaustSourceRouteIds expected_route_ids_;
    std::array<RouteConditioningSeeds, kExhaustExcitationRouteCount> seeds_;
    CausalReconstruction reconstruction_;
    std::array<RouteConditioner, kExhaustExcitationRouteCount> conditioners_;
    std::array<ReconstructedSourceFrame, kSourceFramesPerMethodBlock>
        reconstructed_scratch_{};
    std::uint64_t next_input_frame_index_ = 0;
    std::uint64_t next_source_frame_index_ = 0;
    bool terminal_failed_ = false;
};

} // namespace engine_sim_offline::presentation
