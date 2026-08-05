#include "presentation/mastering.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

static_assert(sizeof(float) == 4);
static_assert(sizeof(double) == 8);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);

constexpr double kPi = std::bit_cast<double>(UINT64_C(0x400921fb54442d18));

[[nodiscard]] std::int32_t round_float_to_s32_ties_even(float value) {
    const double magnitude = std::abs(static_cast<double>(value));
    const double lower_as_double = std::floor(magnitude);
    const double fraction = magnitude - lower_as_double;
    auto rounded_magnitude = static_cast<std::uint32_t>(lower_as_double);
    if (fraction > 0.5 ||
        (fraction == 0.5 && (rounded_magnitude & UINT32_C(1)) != 0U)) {
        ++rounded_magnitude;
    }

    if (std::signbit(value)) {
        return -static_cast<std::int32_t>(rounded_magnitude);
    }
    return static_cast<std::int32_t>(rounded_magnitude);
}

[[nodiscard]] std::int32_t floor_s32_over_256(std::int32_t value) noexcept {
    auto quotient = value / 256;
    if (value < 0 && value % 256 != 0) {
        --quotient;
    }
    return quotient;
}

} // namespace

MasteringSettings::MasteringSettings(std::uint64_t audible_frame_count,
                                     std::uint64_t fade_in_frame_count,
                                     std::uint64_t fade_out_frame_count,
                                     float volume_linear)
    : audible_frame_count_(audible_frame_count),
      fade_in_frame_count_(fade_in_frame_count),
      fade_out_frame_count_(fade_out_frame_count),
      volume_linear_(volume_linear) {
    if (audible_frame_count_ == 0) {
        throw std::invalid_argument{"mastering audible frame count must be positive"};
    }
    if (fade_in_frame_count_ > audible_frame_count_ ||
        fade_out_frame_count_ > audible_frame_count_ - fade_in_frame_count_) {
        throw std::invalid_argument{
            "mastering fade frame counts must fit inside the audible interval"};
    }
    if (!std::isfinite(volume_linear_) || volume_linear_ <= 0.0F) {
        throw std::invalid_argument{
            "mastering listening volume must be finite and positive"};
    }
}

std::uint64_t MasteringSettings::audible_frame_count() const noexcept {
    return audible_frame_count_;
}

std::uint64_t MasteringSettings::fade_in_frame_count() const noexcept {
    return fade_in_frame_count_;
}

std::uint64_t MasteringSettings::fade_out_frame_count() const noexcept {
    return fade_out_frame_count_;
}

float MasteringSettings::volume_linear() const noexcept {
    return volume_linear_;
}

double quarter_sine_gain(std::uint64_t k, std::uint64_t fade_frame_count) {
    if (fade_frame_count == 0) {
        throw std::invalid_argument{"quarter-sine fade frame count must be positive"};
    }
    if (k > fade_frame_count) {
        throw std::out_of_range{"quarter-sine index exceeds its fade interval"};
    }
    const double ratio = static_cast<double>(k) / static_cast<double>(fade_frame_count);
    const double angle = (ratio * kPi) / 2.0;
    return std::sin(angle);
}

double audition_fade_gain(std::uint64_t frame_index,
                          const MasteringSettings &settings) {
    if (frame_index >= settings.audible_frame_count()) {
        throw std::out_of_range{"audition frame exceeds the audible interval"};
    }
    if (frame_index < settings.fade_in_frame_count()) {
        return quarter_sine_gain(frame_index, settings.fade_in_frame_count());
    }
    const auto fade_out_start =
        settings.audible_frame_count() - settings.fade_out_frame_count();
    if (frame_index <= fade_out_start) {
        return 1.0;
    }
    return quarter_sine_gain(settings.audible_frame_count() - frame_index,
                             settings.fade_out_frame_count());
}

Pcm24Quantization quantize_pcm24(float faded_sample) {
    if (!std::isfinite(faded_sample)) {
        throw std::domain_error{"PCM24 quantizer input was non-finite"};
    }

    std::int32_t s32 = 0;
    bool saturated = false;
    if (faded_sample >= 1.0F) {
        s32 = std::numeric_limits<std::int32_t>::max();
        saturated = true;
    } else if (faded_sample <= -1.0F) {
        s32 = std::numeric_limits<std::int32_t>::min();
        saturated = true;
    } else {
        const float scaled = faded_sample * 0x1p31F;
        s32 = round_float_to_s32_ties_even(scaled);
    }
    return {s32, floor_s32_over_256(s32), saturated};
}

std::array<std::byte, 3> serialize_pcm24le(std::int32_t pcm24_sample) {
    if (pcm24_sample < -8'388'608 || pcm24_sample > 8'388'607) {
        throw std::out_of_range{"PCM24 code is outside the signed 24-bit range"};
    }
    const auto bits = static_cast<std::uint32_t>(pcm24_sample);
    return {
        static_cast<std::byte>(bits & UINT32_C(0xff)),
        static_cast<std::byte>((bits >> 8U) & UINT32_C(0xff)),
        static_cast<std::byte>((bits >> 16U) & UINT32_C(0xff)),
    };
}

} // namespace engine_sim_offline::presentation
