#pragma once

#include "crankwave/contract/parity_model.hpp"

#include <string_view>

namespace crankwave::contract::profile_support {

inline constexpr std::string_view kLowOrderOperatingPointV1Root =
    "engine.physics.low-order-operating-point-v1";

[[nodiscard]] constexpr std::string_view
root(const AuthoredLowOrderOperatingPointV1Profile &) noexcept {
    return kLowOrderOperatingPointV1Root;
}

[[nodiscard]] constexpr std::string_view
root(const LowOrderOperatingPointV1Profile &) noexcept {
    return kLowOrderOperatingPointV1Root;
}

} // namespace crankwave::contract::profile_support
