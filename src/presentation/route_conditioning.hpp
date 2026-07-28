#pragma once

#include "dsp/pcg32.hpp"
#include "dsp/source_conditioning_primitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine_sim_offline::presentation {

struct Pcg32Seed {
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const Pcg32Seed &, const Pcg32Seed &) = default;
};

struct ConditioningResult {
    double jittered_engine_sim_source_unit = 0.0;
    double filtered_air_noise = 0.0;
    double conditioned_engine_sim_source_unit = 0.0;

    friend bool operator==(const ConditioningResult &,
                           const ConditioningResult &) = default;
};

// Exact route-owned jitter, DC/derivative, and filtered-air conditioning.
// One instance owns one route's continuous 192 kHz state and random streams.
class RouteConditioner {
  public:
    RouteConditioner(Pcg32Seed jitter_seed, Pcg32Seed air_noise_seed);

    [[nodiscard]] ConditioningResult
    process(double reconstructed_engine_sim_source_unit);

    [[nodiscard]] std::uint64_t jitter_rng_state() const noexcept;
    [[nodiscard]] std::uint64_t air_noise_rng_state() const noexcept;

  private:
    static constexpr std::size_t kJitterHistoryLength = 41;

    std::array<double, kJitterHistoryLength> jitter_history_{};
    std::size_t jitter_write_offset_ = 0;
    dsp::Pcg32 jitter_rng_;
    dsp::FourthOrderLowPass jitter_modulation_filter_;
    dsp::DcRemoval dc_removal_;
    dsp::BackwardDerivative derivative_;
    dsp::Pcg32 air_noise_rng_;
    dsp::FourthOrderLowPass air_noise_filter_;
};

} // namespace engine_sim_offline::presentation
