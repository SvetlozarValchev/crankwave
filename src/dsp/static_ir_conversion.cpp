#include "dsp/static_ir_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::dsp {
namespace {

constexpr double kStaticIrPi = 3.141592653589793238462643383279502884;
constexpr double kPcm16PositiveMaximum = 32767.0;
constexpr std::int32_t kMeaningfulMagnitudeThreshold = 100;
constexpr long double kMinimumRetainedWeight = static_cast<long double>(1e-8);

using Limits = StaticIrConversionLimits;

void require_extended_precision_environment() {
    if constexpr (std::numeric_limits<long double>::radix != 2 ||
                  std::numeric_limits<long double>::digits != 64 ||
                  std::numeric_limits<long double>::max_exponent != 16384) {
        throw std::runtime_error{
            "static IR conversion requires x86-extended long double semantics"};
    }
}

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

void require_finite(long double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

[[nodiscard]] std::int32_t pcm16_magnitude(std::int16_t sample) noexcept {
    const auto widened = static_cast<std::int32_t>(sample);
    return widened < 0 ? -widened : widened;
}

void validate_source(std::span<const std::int16_t> decoded_pcm16,
                     std::size_t meaningful_support_frame_count,
                     double configured_gain) {
    if (decoded_pcm16.empty()) {
        throw std::invalid_argument{"static IR source must not be empty"};
    }
    if (decoded_pcm16.size() > Limits::maximum_source_frame_count) {
        throw std::length_error{
            "static IR source exceeds the configured source-frame envelope"};
    }
    if (meaningful_support_frame_count == 0 ||
        meaningful_support_frame_count > decoded_pcm16.size()) {
        throw std::invalid_argument{
            "static IR meaningful support must be inside the decoded source"};
    }
    if (!std::isfinite(configured_gain) || configured_gain < 0.0) {
        throw std::invalid_argument{
            "static IR configured gain must be nonnegative and finite"};
    }

    std::size_t detected_support = 0;
    for (std::size_t index = 0; index < decoded_pcm16.size(); ++index) {
        if (pcm16_magnitude(decoded_pcm16[index]) > kMeaningfulMagnitudeThreshold) {
            detected_support = index + 1;
        }
    }
    if (detected_support != meaningful_support_frame_count) {
        throw std::invalid_argument{
            "static IR meaningful support does not match decoded PCM16"};
    }
}

[[nodiscard]] double sinc(double value) {
    require_finite(value, "static IR sinc input was non-finite");
    if (std::abs(value) < 1e-12) {
        return 1.0;
    }
    const double radians = kStaticIrPi * value;
    const double result = std::sin(radians) / radians;
    require_finite(result, "static IR sinc result was non-finite");
    return result;
}

class StaticIrWeightTable {
  public:
    StaticIrWeightTable() : weights_(Limits::table_row_count * Limits::tap_count) {
        const double rate_ratio = static_cast<double>(Limits::target_rate_hz) /
                                  static_cast<double>(Limits::source_rate_hz);
        const double cutoff = std::min(1.0, rate_ratio);

        for (std::size_t phase = 0; phase <= Limits::phase_count; ++phase) {
            const double fraction =
                static_cast<double>(phase) / static_cast<double>(Limits::phase_count);
            for (std::size_t tap = 0; tap < Limits::tap_count; ++tap) {
                const auto offset = static_cast<std::int32_t>(tap) -
                                    static_cast<std::int32_t>(Limits::half_width) + 1;
                const double distance = static_cast<double>(offset) - fraction;
                const double normalized_distance =
                    distance / static_cast<double>(Limits::half_width);

                double window = 0.0;
                if (std::abs(normalized_distance) < 1.0) {
                    window =
                        0.42 + 0.5 * std::cos(kStaticIrPi * normalized_distance) +
                        0.08 * std::cos(2.0 * kStaticIrPi * normalized_distance);
                }
                const double weight = cutoff * sinc(cutoff * distance) * window;
                require_finite(weight, "static IR table weight was non-finite");
                weights_[phase * Limits::tap_count + tap] = weight;
            }
        }
    }

    [[nodiscard]] double at(std::size_t phase, std::size_t tap) const noexcept {
        return weights_[phase * Limits::tap_count + tap];
    }

  private:
    std::vector<double> weights_;
};

struct RationalSourcePosition {
    std::uint64_t center = 0;
    std::uint64_t fraction = 0;

    void advance() noexcept {
        fraction = fraction + Limits::source_rate_hz;
        center = center + fraction / Limits::target_rate_hz;
        fraction = fraction % Limits::target_rate_hz;
    }
};

struct InterpolatedPhase {
    std::size_t phase0 = 0;
    std::size_t phase1 = 0;
    double mix = 0.0;
};

[[nodiscard]] InterpolatedPhase
interpolated_phase(const RationalSourcePosition &position) {
    const std::uint64_t scaled_phase =
        position.fraction * static_cast<std::uint64_t>(Limits::phase_count);
    const auto phase0 = static_cast<std::size_t>(scaled_phase / Limits::target_rate_hz);
    const auto phase1 = std::min(Limits::phase_count, phase0 + 1);
    const double mix = static_cast<double>(scaled_phase % Limits::target_rate_hz) /
                       static_cast<double>(Limits::target_rate_hz);
    return {phase0, phase1, mix};
}

[[nodiscard]] bool source_index_for_tap(const RationalSourcePosition &position,
                                        std::size_t tap,
                                        std::size_t support_frame_count,
                                        std::size_t &source_index) noexcept {
    const auto candidate = static_cast<std::int64_t>(position.center) +
                           static_cast<std::int64_t>(tap) - 11;
    if (candidate < 0 || static_cast<std::uint64_t>(candidate) >= support_frame_count) {
        return false;
    }
    source_index = static_cast<std::size_t>(candidate);
    return true;
}

[[nodiscard]] double interpolated_weight(const StaticIrWeightTable &table,
                                         const InterpolatedPhase &phase,
                                         std::size_t tap) {
    const double weight0 = table.at(phase.phase0, tap);
    const double weight1 = table.at(phase.phase1, tap);
    const double weight = weight0 + (weight1 - weight0) * phase.mix;
    require_finite(weight, "static IR interpolated weight was non-finite");
    return weight;
}

[[nodiscard]] std::size_t nearest_target_index(std::size_t source_index) {
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(source_index) * Limits::target_rate_hz;
    return static_cast<std::size_t>((numerator + Limits::source_rate_hz / 2) /
                                    Limits::source_rate_hz);
}

} // namespace

std::size_t static_ir_target_count(std::size_t meaningful_support_frame_count) {
    if (meaningful_support_frame_count == 0 ||
        meaningful_support_frame_count > Limits::maximum_source_frame_count) {
        throw std::invalid_argument{
            "static IR support is outside the configured source-frame envelope"};
    }

    const std::uint64_t numerator =
        static_cast<std::uint64_t>(meaningful_support_frame_count) *
        Limits::target_rate_hz;
    return static_cast<std::size_t>((numerator + Limits::source_rate_hz / 2) /
                                    Limits::source_rate_hz);
}

std::vector<double> convert_static_ir(std::span<const std::int16_t> decoded_pcm16,
                                      std::size_t meaningful_support_frame_count,
                                      double configured_gain) {
    require_extended_precision_environment();
    validate_source(decoded_pcm16, meaningful_support_frame_count, configured_gain);

    const std::size_t target_count =
        static_ir_target_count(meaningful_support_frame_count);
    const StaticIrWeightTable table;
    std::vector<long double> source_weight_sums(meaningful_support_frame_count, 0.0L);

    // Normalizing columns preserves each 44.1 kHz source sample's discrete area.
    // Sampling that area on the 192 kHz grid produces source_area_gain; applying
    // that ratio again here would double-scale the kernel.
    RationalSourcePosition position;
    for (std::size_t target = 0; target < target_count; ++target) {
        const InterpolatedPhase phase = interpolated_phase(position);
        for (std::size_t tap = 0; tap < Limits::tap_count; ++tap) {
            std::size_t source = 0;
            if (!source_index_for_tap(position, tap, meaningful_support_frame_count,
                                      source)) {
                continue;
            }
            const double weight = interpolated_weight(table, phase, tap);
            source_weight_sums[source] =
                source_weight_sums[source] + static_cast<long double>(weight);
            require_finite(source_weight_sums[source],
                           "static IR source weight sum was non-finite");
        }
        position.advance();
    }

    std::vector<long double> target_accumulators(target_count, 0.0L);

    // The record numbers fallback assignment before normal matrix accumulation.
    // Keep that order explicit because both may contribute to the same target.
    for (std::size_t source = 0; source < meaningful_support_frame_count; ++source) {
        if (source_weight_sums[source] <= kMinimumRetainedWeight) {
            const std::size_t target = nearest_target_index(source);
            if (target >= target_count) {
                throw std::logic_error{
                    "static IR fallback target escaped the output kernel"};
            }
            target_accumulators[target] =
                target_accumulators[target] +
                static_cast<long double>(decoded_pcm16[source]);
            require_finite(target_accumulators[target],
                           "static IR fallback accumulation was non-finite");
        }
    }

    position = {};
    for (std::size_t target = 0; target < target_count; ++target) {
        const InterpolatedPhase phase = interpolated_phase(position);
        for (std::size_t tap = 0; tap < Limits::tap_count; ++tap) {
            std::size_t source = 0;
            if (!source_index_for_tap(position, tap, meaningful_support_frame_count,
                                      source)) {
                continue;
            }
            if (source_weight_sums[source] <= kMinimumRetainedWeight) {
                continue;
            }
            const double weight = interpolated_weight(table, phase, tap);
            const long double contribution =
                static_cast<long double>(decoded_pcm16[source]) *
                static_cast<long double>(weight) / source_weight_sums[source];
            require_finite(contribution, "static IR contribution was non-finite");
            target_accumulators[target] = target_accumulators[target] + contribution;
            require_finite(target_accumulators[target],
                           "static IR target accumulation was non-finite");
        }
        position.advance();
    }

    const double coefficient_scale = configured_gain / kPcm16PositiveMaximum;
    require_finite(coefficient_scale,
                   "static IR coefficient scale was non-finite");
    const long double extended_coefficient_scale =
        static_cast<long double>(coefficient_scale);

    std::vector<double> coefficients(target_count);
    for (std::size_t target = 0; target < target_count; ++target) {
        const long double scaled =
            target_accumulators[target] * extended_coefficient_scale;
        require_finite(scaled, "static IR extended coefficient was non-finite");
        coefficients[target] = static_cast<double>(scaled);
        require_finite(coefficients[target],
                       "static IR coefficient was non-finite");
    }
    return coefficients;
}

} // namespace engine_sim_offline::dsp
