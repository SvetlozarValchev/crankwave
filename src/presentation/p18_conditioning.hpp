#pragma once

#include "dsp/p18_pcg32.hpp"
#include "dsp/p18_primitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine_sim_offline::presentation {

struct P18Pcg32Seed {
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const P18Pcg32Seed &, const P18Pcg32Seed &) = default;
};

struct P18ConditioningResult {
    double jittered_engine_sim_source_unit = 0.0;
    double filtered_air_noise = 0.0;
    double conditioned_engine_sim_source_unit = 0.0;

    friend bool operator==(const P18ConditioningResult &,
                           const P18ConditioningResult &) = default;
};

// Exact route-owned P1.8 jitter, DC/derivative, and filtered-air conditioning.
// One instance owns one route's continuous 192 kHz state and random streams.
class P18RouteConditioner {
  public:
    P18RouteConditioner(P18Pcg32Seed jitter_seed, P18Pcg32Seed air_noise_seed);

    [[nodiscard]] P18ConditioningResult
    process(double reconstructed_engine_sim_source_unit);

    [[nodiscard]] std::uint64_t jitter_rng_state() const noexcept;
    [[nodiscard]] std::uint64_t air_noise_rng_state() const noexcept;

  private:
    static constexpr std::size_t kJitterHistoryLength = 41;

    std::array<double, kJitterHistoryLength> jitter_history_{};
    std::size_t jitter_write_offset_ = 0;
    dsp::P18Pcg32 jitter_rng_;
    dsp::P18FourthOrderLowPass jitter_modulation_filter_;
    dsp::P18DcRemoval dc_removal_;
    dsp::P18BackwardDerivative derivative_;
    dsp::P18Pcg32 air_noise_rng_;
    dsp::P18FourthOrderLowPass air_noise_filter_;
};

} // namespace engine_sim_offline::presentation
