#pragma once

#include <cstdint>
#include <string_view>

namespace crankwave::responsive {

// Delivery profiles describe the immutable presentation-side contract. They
// do not change the 10 kHz authoring/physics capture or the phase-domain grid.
inline constexpr std::string_view kResponsiveCanonicalDeliveryProfileV1 =
    "canonical-192k-v1";
inline constexpr std::string_view kResponsiveGameDeliveryProfileV1 =
    "game-48k-v1";

struct ResponsiveDeliveryProfileV1 {
    std::string_view id;
    std::uint32_t sample_rate_hz;
    std::uint32_t block_frames;
    std::uint32_t fft_size;
    std::uint32_t coefficient_count;
};

inline constexpr ResponsiveDeliveryProfileV1 kResponsiveCanonicalDeliveryProfile{
    kResponsiveCanonicalDeliveryProfileV1, 192'000U, 3'840U, 65'536U, 30'071U};
inline constexpr ResponsiveDeliveryProfileV1 kResponsiveGameDeliveryProfile{
    kResponsiveGameDeliveryProfileV1, 48'000U, 8'192U, 16'384U, 7'518U};

[[nodiscard]] constexpr const ResponsiveDeliveryProfileV1 *
responsive_delivery_profile(const std::string_view id) noexcept {
    if (id == kResponsiveCanonicalDeliveryProfile.id) {
        return &kResponsiveCanonicalDeliveryProfile;
    }
    if (id == kResponsiveGameDeliveryProfile.id) {
        return &kResponsiveGameDeliveryProfile;
    }
    return nullptr;
}

[[nodiscard]] constexpr bool
responsive_delivery_profile_matches(const ResponsiveDeliveryProfileV1 &profile,
                                     const std::string_view id,
                                     const std::uint32_t sample_rate_hz,
                                     const std::uint32_t block_frames,
                                     const std::uint32_t fft_size,
                                     const std::uint32_t coefficient_count) noexcept {
    return profile.id == id && profile.sample_rate_hz == sample_rate_hz &&
           profile.block_frames == block_frames && profile.fft_size == fft_size &&
           profile.coefficient_count == coefficient_count;
}

} // namespace crankwave::responsive
