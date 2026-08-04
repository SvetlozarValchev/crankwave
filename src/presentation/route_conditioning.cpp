#include "presentation/route_conditioning.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr double kJitterMaximumOffset = 40.0;
constexpr double kJitterMeanOffset = 20.0;
constexpr double kNoiseExcitationScale =
    std::bit_cast<double>(UINT64_C(0x3ff6a09e667f3bcd));

[[nodiscard]] RouteConditioningCalibration
require_valid_calibration(RouteConditioningCalibration calibration) {
    if (!valid_route_conditioning_calibration(calibration)) {
        throw std::invalid_argument{
            "route conditioning calibration is outside the executable domain"};
    }
    return calibration;
}

} // namespace

bool valid_route_conditioning_calibration(
    const RouteConditioningCalibration &calibration) noexcept {
    const auto canonical_nonnegative = [](double value) {
        return std::isfinite(value) && value >= 0.0 &&
               (value != 0.0 || !std::signbit(value));
    };
    const auto unit_interval = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0 &&
               (value != 0.0 || !std::signbit(value));
    };
    const auto valid_cutoff = [](double value) {
        return std::isfinite(value) && value > 0.0 &&
               value < dsp::kConditionedSourceRateHz / 2.0;
    };
    return canonical_nonnegative(calibration.jitter_scale) &&
           valid_cutoff(calibration.jitter_modulation_cutoff_hz) &&
           unit_interval(calibration.derivative_mix_01) &&
           unit_interval(calibration.air_noise_mix_01) &&
           valid_cutoff(calibration.air_noise_cutoff_hz);
}

RouteConditioner::RouteConditioner(Pcg32Seed jitter_seed, Pcg32Seed air_noise_seed,
                                   RouteConditioningCalibration calibration)
    : calibration_(require_valid_calibration(calibration)),
      jitter_rng_(jitter_seed.initial_state, jitter_seed.stream),
      jitter_modulation_filter_(calibration_.jitter_modulation_cutoff_hz,
                                dsp::kConditionedSourceRateHz),
      dc_removal_(dsp::kConditionedSourceTimeStepS, dsp::kDcRemovalTimeConstantS),
      derivative_(dsp::kConditionedSourceTimeStepS),
      air_noise_rng_(air_noise_seed.initial_state, air_noise_seed.stream),
      air_noise_filter_(calibration_.air_noise_cutoff_hz,
                        dsp::kConditionedSourceRateHz) {}

ConditioningResult
RouteConditioner::process(double reconstructed_engine_sim_source_unit,
                          double exhaust_flow_activity_01) {
    if (!std::isfinite(reconstructed_engine_sim_source_unit) ||
        !std::isfinite(exhaust_flow_activity_01) || exhaust_flow_activity_01 < 0.0 ||
        exhaust_flow_activity_01 > 1.0) {
        throw std::domain_error{
            "conditioning input or exhaust-flow activity was invalid"};
    }
    if (exhaust_flow_activity_01 == 0.0 && std::signbit(exhaust_flow_activity_01)) {
        throw std::domain_error{"conditioning exhaust-flow activity was negative zero"};
    }

    jitter_history_[jitter_write_offset_] = reconstructed_engine_sim_source_unit;
    ++jitter_write_offset_;
    if (jitter_write_offset_ == kJitterHistoryLength) {
        jitter_write_offset_ = 0;
    }

    const double random_offset = jitter_rng_.uniform_double() * kJitterMaximumOffset;
    const double rate_normalized_offset =
        kJitterMeanOffset + (random_offset - kJitterMeanOffset) * kNoiseExcitationScale;
    const double filtered_offset = jitter_modulation_filter_.process(
        rate_normalized_offset * calibration_.jitter_scale);
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
    const double flow_coupled_air_noise = exhaust_flow_activity_01 * filtered_air_noise;
    const double noise_mix = calibration_.air_noise_mix_01 * flow_coupled_air_noise +
                             (1.0 - calibration_.air_noise_mix_01);

    const double conditioned = dsp::cleanup_conditioned_sample(
        derivative * calibration_.derivative_mix_01 +
        dc_removed * noise_mix * (1.0 - calibration_.derivative_mix_01));
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
