#include "presentation/master_dynamics.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr float kPeakRetentionPerFrame = std::bit_cast<float>(UINT32_C(0x3f7ffef2));
constexpr float kGainRetentionPerFrame =
    std::bit_cast<float>(UINT32_C(0x3f7fe1df));
constexpr float kGainBlendPerFrame = std::bit_cast<float>(UINT32_C(0x39f10800));

[[nodiscard]] float require_volume(double volume_linear) {
    const float result = static_cast<float>(volume_linear);
    if (!std::isfinite(volume_linear) || volume_linear <= 0.0 ||
        !std::isfinite(result) || result <= 0.0F) {
        throw std::invalid_argument{"master-dynamics volume must be finite, positive, "
                                    "and Float32 representable"};
    }
    return result;
}

} // namespace

MasterDynamics::MasterDynamics(double volume_linear)
    : volume_linear_(require_volume(volume_linear)),
      peak_retention_per_frame_(kPeakRetentionPerFrame),
      gain_retention_per_frame_(kGainRetentionPerFrame) {}

float MasterDynamics::process(float source_unit_mix) {
    if (!std::isfinite(source_unit_mix)) {
        throw std::domain_error{"master-dynamics input was non-finite"};
    }

    peak_source_units_ = peak_retention_per_frame_ * peak_source_units_;
    const float magnitude = std::abs(source_unit_mix);
    if (magnitude > peak_source_units_) {
        peak_source_units_ = magnitude;
    }
    if (!std::isfinite(peak_source_units_) || peak_source_units_ <= 0.0F) {
        throw std::domain_error{"master-dynamics peak state was invalid"};
    }

    const float requested_gain =
        std::clamp(kTargetPeakSourceUnits / peak_source_units_,
                   kMinimumGainLinear, kMaximumGainLinear);
    gain_linear_ = gain_retention_per_frame_ * gain_linear_ +
                   kGainBlendPerFrame * requested_gain;
    if (!std::isfinite(gain_linear_)) {
        throw std::domain_error{"master-dynamics gain state was non-finite"};
    }

    const float leveled = source_unit_mix * gain_linear_;
    const float volume_applied = leveled * volume_linear_;
    const float normalized = static_cast<float>(volume_applied / kPcm16FullScale);
    const float softened = static_cast<float>(std::tanh(normalized));
    if (!std::isfinite(leveled) || !std::isfinite(volume_applied) ||
        !std::isfinite(normalized) || !std::isfinite(softened)) {
        throw std::domain_error{"master-dynamics output was non-finite"};
    }
    const float normalized_bound = std::nextafter(1.0F, 0.0F);
    return std::clamp(softened, -normalized_bound, normalized_bound);
}

float MasterDynamics::volume_linear() const noexcept {
    return volume_linear_;
}

float MasterDynamics::peak_source_units() const noexcept {
    return peak_source_units_;
}

float MasterDynamics::gain_linear() const noexcept {
    return gain_linear_;
}

float MasterDynamics::peak_retention_per_frame() const noexcept {
    return peak_retention_per_frame_;
}

float MasterDynamics::gain_retention_per_frame() const noexcept {
    return gain_retention_per_frame_;
}

} // namespace engine_sim_offline::presentation
