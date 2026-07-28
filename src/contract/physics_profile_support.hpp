#pragma once

#include "engine_sim_offline/contract/parity_model.hpp"

#include <string_view>

namespace engine_sim_offline::contract::profile_support {

inline constexpr std::string_view kLegacyLowOrderV1Root =
    "engine.physics.legacy-low-order-v1";

[[nodiscard]] constexpr std::string_view
root(const AuthoredLegacyLowOrderV1Profile &) noexcept {
    return kLegacyLowOrderV1Root;
}

[[nodiscard]] constexpr std::string_view
root(const LegacyLowOrderV1Profile &) noexcept {
    return kLegacyLowOrderV1Root;
}

} // namespace engine_sim_offline::contract::profile_support
