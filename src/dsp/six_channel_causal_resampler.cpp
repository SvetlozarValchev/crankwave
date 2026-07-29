#include "dsp/six_channel_causal_resampler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace engine_sim_offline::dsp {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr std::size_t kCoefficientCount =
    SixChannelCausalResampler::phase_row_count * SixChannelCausalResampler::tap_count;

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::domain_error{message};
    }
}

double bessel_i0(double value) {
    require_finite(value, "six-channel resampler Bessel input was non-finite");

    const double quarter_square = 0.25 * value * value;
    double term = 1.0;
    double sum = 1.0;
    for (int order = 1; order <= 40; ++order) {
        const double divisor = static_cast<double>(order * order);
        term = term * (quarter_square / divisor);
        sum = sum + term;
    }
    require_finite(sum, "six-channel resampler Bessel result was non-finite");
    return sum;
}

double normalized_sinc(double value) {
    require_finite(value, "six-channel resampler sinc input was non-finite");
    if (std::abs(value) < 1e-15) {
        return 1.0;
    }

    const double radians = kPi * value;
    const double result = std::sin(radians) / radians;
    require_finite(result, "six-channel resampler sinc result was non-finite");
    return result;
}

class CoefficientTable {
  public:
    CoefficientTable() : coefficients_(kCoefficientCount) {
        constexpr auto tap_count = SixChannelCausalResampler::tap_count;
        constexpr auto half_width = SixChannelCausalResampler::group_delay_input_frames;
        constexpr auto phase_count = SixChannelCausalResampler::phase_interval_count;
        constexpr double beta = SixChannelCausalResampler::kaiser_beta;
        constexpr double cutoff = SixChannelCausalResampler::source_nyquist_cutoff;

        const double inverse_i0_beta = 1.0 / bessel_i0(beta);
        require_finite(inverse_i0_beta,
                       "six-channel resampler inverse Bessel result was non-finite");

        std::array<double, tap_count> window{};
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            const double normalized_position =
                (static_cast<double>(tap) - static_cast<double>(half_width)) /
                static_cast<double>(half_width);
            const double radicand =
                std::max(0.0, 1.0 - normalized_position * normalized_position);
            window[tap] = bessel_i0(beta * std::sqrt(radicand)) * inverse_i0_beta;
            require_finite(window[tap],
                           "six-channel resampler Kaiser coefficient was non-finite");
        }

        // The finite causal support closes at zero. Besides avoiding an endpoint
        // discontinuity, this makes the phase-one row an exact shift of phase zero
        // while retaining unit DC gain.
        window.front() = 0.0;
        window.back() = 0.0;

        for (std::size_t phase = 0; phase < phase_count; ++phase) {
            const double fraction =
                static_cast<double>(phase) / static_cast<double>(phase_count);
            double sum = 0.0;
            for (std::size_t tap = 0; tap < tap_count; ++tap) {
                const double offset =
                    static_cast<double>(tap) - static_cast<double>(half_width);
                const double distance = offset - fraction;
                const double value =
                    cutoff * normalized_sinc(cutoff * distance) * window[tap];
                require_finite(
                    value, "six-channel resampler table coefficient was non-finite");
                coefficients_[phase * tap_count + tap] = value;
                sum = sum + value;
            }
            require_finite(sum, "six-channel resampler table row sum was non-finite");
            if (sum == 0.0) {
                throw std::domain_error{"six-channel resampler table row sum was zero"};
            }
            for (std::size_t tap = 0; tap < tap_count; ++tap) {
                auto &value = coefficients_[phase * tap_count + tap];
                value = value / sum;
                require_finite(
                    value,
                    "normalized six-channel resampler coefficient was non-finite");
            }
        }

        const auto wrap_offset = phase_count * tap_count;
        coefficients_[wrap_offset] = 0.0;
        for (std::size_t tap = 1; tap < tap_count; ++tap) {
            coefficients_[wrap_offset + tap] = coefficients_[tap - 1];
        }
    }

    [[nodiscard]] const double *row(std::size_t phase) const {
        if (phase >= SixChannelCausalResampler::phase_row_count) {
            throw std::out_of_range{"six-channel resampler phase row was out of range"};
        }
        return coefficients_.data() + phase * SixChannelCausalResampler::tap_count;
    }

  private:
    std::vector<double> coefficients_;
};

const CoefficientTable &coefficient_table() {
    static const CoefficientTable table;
    return table;
}

std::uint64_t output_count_for_interval(std::uint64_t distance) {
    if (distance >= SixChannelCausalResampler::output_rate_hz) {
        throw std::logic_error{"six-channel resampler clock state was out of range"};
    }
    const auto remaining = SixChannelCausalResampler::output_rate_hz - distance;
    return UINT64_C(1) +
           (remaining - UINT64_C(1)) / SixChannelCausalResampler::input_rate_hz;
}

} // namespace

SixChannelResamplerPhase
SixChannelCausalResampler::resolve_phase(std::uint64_t source_interval_offset) {
    if (source_interval_offset >= output_rate_hz) {
        throw std::invalid_argument{
            "six-channel resampler phase offset must be inside one source interval"};
    }

    const auto scaled = source_interval_offset * phase_interval_count;
    const auto row0 = scaled / output_rate_hz;
    const auto remainder = scaled % output_rate_hz;
    return {
        static_cast<std::uint16_t>(row0),
        remainder,
        static_cast<double>(remainder) / static_cast<double>(output_rate_hz),
    };
}

std::size_t SixChannelCausalResampler::expected_output_frame_count(
    std::size_t input_frame_count) const {
    if (input_frame_count > maximum_input_frames_per_call) {
        throw std::invalid_argument{
            "six-channel resampler input exceeded one source block"};
    }

    auto distance = distance_to_next_output_;
    std::size_t result = 0;
    for (std::size_t frame = 0; frame < input_frame_count; ++frame) {
        const auto output_count = output_count_for_interval(distance);
        result += static_cast<std::size_t>(output_count);
        distance = distance + output_count * input_rate_hz - output_rate_hz;
    }
    return result;
}

void SixChannelCausalResampler::process(std::span<const SixChannelResamplerFrame> input,
                                        std::span<SixChannelResamplerFrame> output) {
    const auto expected_output = expected_output_frame_count(input.size());
    if (output.size() != expected_output) {
        throw std::invalid_argument{
            "six-channel resampler output span did not match the exact clock count"};
    }
    for (const auto &frame : input) {
        for (const double sample : frame) {
            if (!std::isfinite(sample)) {
                throw std::domain_error{"six-channel resampler input was non-finite"};
            }
        }
    }

    auto candidate_histories = histories_;
    auto candidate_oldest_history_frame = oldest_history_frame_;
    auto candidate_distance_to_next_output = distance_to_next_output_;
    std::vector<SixChannelResamplerFrame> candidate_output(expected_output);

    const auto &table = coefficient_table();
    std::size_t output_index = 0;
    for (const auto &input_frame : input) {
        // Each input frame is observed at the end of its 80 kHz source interval.
        // Emit acoustic frames strictly before that endpoint from the already
        // committed history, then publish this source observation.
        const auto output_count =
            output_count_for_interval(candidate_distance_to_next_output);
        auto offset = candidate_distance_to_next_output;
        for (std::uint64_t frame = 0; frame < output_count; ++frame) {
            const auto phase = resolve_phase(offset);
            const auto *row0 = table.row(phase.row0);
            const auto *row1 =
                table.row(static_cast<std::size_t>(phase.row0) + std::size_t{1});

            SixChannelResamplerFrame samples{};
            auto history_index = candidate_oldest_history_frame;
            for (std::size_t tap = 0; tap < tap_count; ++tap) {
                const double coefficient =
                    row0[tap] + (row1[tap] - row0[tap]) * phase.row_mix;
                for (std::size_t channel = 0; channel < channel_count; ++channel) {
                    samples[channel] =
                        samples[channel] +
                        candidate_histories[channel][history_index] * coefficient;
                }
                ++history_index;
                if (history_index == tap_count) {
                    history_index = 0;
                }
            }
            for (const double sample : samples) {
                if (!std::isfinite(sample)) {
                    throw std::domain_error{
                        "six-channel resampler output was non-finite"};
                }
            }
            candidate_output[output_index] = samples;
            ++output_index;
            offset += input_rate_hz;
        }

        for (std::size_t channel = 0; channel < channel_count; ++channel) {
            candidate_histories[channel][candidate_oldest_history_frame] =
                input_frame[channel];
        }
        ++candidate_oldest_history_frame;
        if (candidate_oldest_history_frame == tap_count) {
            candidate_oldest_history_frame = 0;
        }
        candidate_distance_to_next_output = offset - output_rate_hz;
    }

    if (output_index != expected_output) {
        throw std::logic_error{
            "six-channel resampler clock produced an inconsistent frame count"};
    }

    std::copy(candidate_output.begin(), candidate_output.end(), output.begin());
    histories_ = candidate_histories;
    oldest_history_frame_ = candidate_oldest_history_frame;
    distance_to_next_output_ = candidate_distance_to_next_output;
}

} // namespace engine_sim_offline::dsp
