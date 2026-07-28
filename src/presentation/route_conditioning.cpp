#include "presentation/route_conditioning.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr double kJitterMaximumOffset = 40.0;
constexpr double kJitterMeanOffset = 20.0;
constexpr double kJitterAmount = 0.5;
constexpr double kNoiseExcitationScale =
    std::bit_cast<double>(UINT64_C(0x3ff6a09e667f3bcd));
constexpr double kDerivativeMix = std::bit_cast<double>(UINT64_C(0x3f847ae140000000));

} // namespace

RouteConditioner::RouteConditioner(Pcg32Seed jitter_seed, Pcg32Seed air_noise_seed)
    : jitter_rng_(jitter_seed.initial_state, jitter_seed.stream),
      jitter_modulation_filter_(10000.0, dsp::kConditionedSourceRateHz),
      dc_removal_(dsp::kConditionedSourceTimeStepS, dsp::kDcRemovalTimeConstantS),
      derivative_(dsp::kConditionedSourceTimeStepS),
      air_noise_rng_(air_noise_seed.initial_state, air_noise_seed.stream),
      air_noise_filter_(2000.0, dsp::kConditionedSourceRateHz) {}

ConditioningResult
RouteConditioner::process(double reconstructed_engine_sim_source_unit) {
    if (!std::isfinite(reconstructed_engine_sim_source_unit)) {
        throw std::domain_error{"conditioning input was non-finite"};
    }

    jitter_history_[jitter_write_offset_] = reconstructed_engine_sim_source_unit;
    ++jitter_write_offset_;
    if (jitter_write_offset_ == kJitterHistoryLength) {
        jitter_write_offset_ = 0;
    }

    const double random_offset = jitter_rng_.uniform_double() * kJitterMaximumOffset;
    const double rate_normalized_offset =
        kJitterMeanOffset + (random_offset - kJitterMeanOffset) * kNoiseExcitationScale;
    const double filtered_offset =
        jitter_modulation_filter_.process(rate_normalized_offset * kJitterAmount);
    const double clamped_offset =
        std::clamp(filtered_offset, 0.0, kJitterMaximumOffset);
    const auto lower_offset = static_cast<std::size_t>(std::floor(clamped_offset));
    const auto upper_offset = static_cast<std::size_t>(std::ceil(clamped_offset));
    const double fraction = clamped_offset - static_cast<double>(lower_offset);

    const auto lower_index =
        (jitter_write_offset_ + lower_offset) % kJitterHistoryLength;
    const auto upper_index =
        (jitter_write_offset_ + upper_offset) % kJitterHistoryLength;
    const double lower = jitter_history_[lower_index];
    const double upper = jitter_history_[upper_index];
    const double jittered = lower + (upper - lower) * fraction;

    const double dc_removed = dc_removal_.process(jittered);
    const double derivative = derivative_.process(jittered);

    const double noise = air_noise_rng_.uniform_signed_double() * kNoiseExcitationScale;
    const double filtered_air_noise = air_noise_filter_.process(noise);
    const double noise_mix = 1.0 * filtered_air_noise + (1.0 - 1.0);

    const double conditioned = dsp::cleanup_conditioned_sample(
        derivative * kDerivativeMix + dc_removed * noise_mix * (1.0 - kDerivativeMix));
    return {
        jittered,
        filtered_air_noise,
        conditioned,
    };
}

std::uint64_t RouteConditioner::jitter_rng_state() const noexcept {
    return jitter_rng_.state();
}

std::uint64_t RouteConditioner::air_noise_rng_state() const noexcept {
    return air_noise_rng_.state();
}

} // namespace engine_sim_offline::presentation
