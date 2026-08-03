#pragma once

#include "presentation/causal_reconstruction.hpp"
#include "presentation/exhaust_excitation_block.hpp"
#include "presentation/route_conditioning.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr contract::RationalRateHz kExcitationRateHz{20000, 1};

struct RouteConditioningSeeds {
    Pcg32Seed jitter;
    Pcg32Seed air_noise;

    friend bool operator==(const RouteConditioningSeeds &,
                           const RouteConditioningSeeds &) = default;
};

struct SourceBlockExtent {
    std::uint64_t first_input_frame_index = 0;
    std::uint64_t first_source_frame_index = 0;
    std::size_t input_frame_count = 0;
    std::size_t source_frame_count = 0;

    friend bool operator==(const SourceBlockExtent &,
                           const SourceBlockExtent &) = default;
};

// Fixture-free coordinator for one exact source-stage session. Each call admits
// one complete 400-frame, 20 ms excitation block at the canonical 20 kHz input clock
// and produces 3,840 conditioned source frames at 192 kHz. Arithmetic failure is
// terminal because route state may already have advanced; structural validation
// happens before mutation.
class ExhaustSourceStage {
  public:
    // Route values and conditioning seeds share this exact positional order:
    // route_seeds[i] belongs to expected_route_ids[i], and every input block must
    // present the same ordered IDs before any stateful DSP work begins.
    ExhaustSourceStage(
        std::span<const contract::RouteId> expected_route_ids,
        std::span<const RouteConditioningSeeds> route_seeds,
        RouteConditioningCalibration conditioning,
        contract::RationalRateHz input_rate = kExcitationRateHz,
        std::size_t input_frames_per_block = kExcitationFramesPerMethodBlock);

    // Output values are frame-major: frame * route_count() + route.
    [[nodiscard]] SourceBlockExtent process(ExhaustExcitationBlockView input,
                                            std::span<double> output_frame_major);

    [[nodiscard]] std::span<const contract::RouteId>
    expected_route_ids() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] contract::RationalRateHz input_rate() const noexcept;
    [[nodiscard]] std::size_t input_frames_per_block() const noexcept;
    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept;
    [[nodiscard]] bool terminal_failed() const noexcept;

    [[nodiscard]] std::uint64_t jitter_rng_state(std::size_t route) const;
    [[nodiscard]] std::uint64_t air_noise_rng_state(std::size_t route) const;

  private:
    static std::vector<contract::RouteId>
    validate_route_ids(std::span<const contract::RouteId> expected_route_ids);
    static std::vector<RouteConditioningSeeds>
    validate_seeds(std::span<const RouteConditioningSeeds> seeds,
                   std::size_t route_count);

    std::vector<contract::RouteId> expected_route_ids_;
    std::vector<RouteConditioningSeeds> seeds_;
    RouteConditioningCalibration conditioning_;
    contract::RationalRateHz input_rate_{};
    std::size_t input_frames_per_block_ = 0;
    CausalReconstruction reconstruction_;
    std::vector<RouteConditioner> conditioners_;
    std::vector<double> reconstructed_scratch_;
    std::uint64_t next_input_frame_index_ = 0;
    std::uint64_t next_source_frame_index_ = 0;
    bool terminal_failed_ = false;
};

} // namespace engine_sim_offline::presentation
