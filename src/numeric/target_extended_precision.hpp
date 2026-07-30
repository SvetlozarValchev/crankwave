#pragma once

#include <cstddef>
#include <limits>
#include <string_view>

namespace engine_sim_offline::numeric {

// The written presentation and clock-resolution algorithms use long double for
// their extended accumulators. Their meaning is selected by the compile target,
// never by an authored option or a runtime compatibility mode.
#if defined(__wasm32__)
inline constexpr bool kTargetExtendedPrecisionIsSupported = true;
inline constexpr std::string_view kTargetExtendedPrecisionIdentity =
    "wasm32-ieee754-binary128-strict-v1";
inline constexpr int kTargetLongDoubleRadix = 2;
inline constexpr int kTargetLongDoubleDigits = 113;
inline constexpr int kTargetLongDoubleMinExponent = -16381;
inline constexpr int kTargetLongDoubleMaxExponent = 16384;
inline constexpr std::size_t kTargetLongDoubleBytes = 16;
#elif defined(__linux__) && defined(__x86_64__)
inline constexpr bool kTargetExtendedPrecisionIsSupported = true;
inline constexpr std::string_view kTargetExtendedPrecisionIdentity =
    "linux-x86-64-sysv-x87-extended-strict-v1";
inline constexpr int kTargetLongDoubleRadix = 2;
inline constexpr int kTargetLongDoubleDigits = 64;
inline constexpr int kTargetLongDoubleMinExponent = -16381;
inline constexpr int kTargetLongDoubleMaxExponent = 16384;
inline constexpr std::size_t kTargetLongDoubleBytes = 16;
#else
inline constexpr bool kTargetExtendedPrecisionIsSupported = false;
inline constexpr std::string_view kTargetExtendedPrecisionIdentity =
    "unsupported-extended-precision-target";
inline constexpr int kTargetLongDoubleRadix = 0;
inline constexpr int kTargetLongDoubleDigits = 0;
inline constexpr int kTargetLongDoubleMinExponent = 0;
inline constexpr int kTargetLongDoubleMaxExponent = 0;
inline constexpr std::size_t kTargetLongDoubleBytes = 0;
#endif

[[nodiscard]] constexpr bool
target_extended_precision_format_is_admitted() noexcept {
    return kTargetExtendedPrecisionIsSupported &&
           std::numeric_limits<long double>::is_iec559 &&
           std::numeric_limits<long double>::has_infinity &&
           std::numeric_limits<long double>::has_quiet_NaN &&
           std::numeric_limits<long double>::round_style ==
               std::round_to_nearest &&
           std::numeric_limits<long double>::radix == kTargetLongDoubleRadix &&
           std::numeric_limits<long double>::digits == kTargetLongDoubleDigits &&
           std::numeric_limits<long double>::min_exponent ==
               kTargetLongDoubleMinExponent &&
           std::numeric_limits<long double>::max_exponent ==
               kTargetLongDoubleMaxExponent &&
           sizeof(long double) == kTargetLongDoubleBytes;
}

} // namespace engine_sim_offline::numeric
