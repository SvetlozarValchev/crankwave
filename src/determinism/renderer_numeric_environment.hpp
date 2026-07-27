#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace engine_sim_offline::determinism {

// This marker is an assertion made by the build system after it has attached the exact
// numeric flag tail. C++ cannot recover all effective code-generation flags from a
// binary, so the runtime observer deliberately does not pretend otherwise.
enum class RendererNumericBuildPolicy : std::uint8_t {
    unmarked,
    x86_64_v1_strict_v1,
};

// This versioned ID owns the exact compiled flags plus the admitted binary32,
// binary64, x87-extended, FE, MXCSR, and x87 control model. The manifest's primary
// floating-point format remains the existing ieee754_binary64 value.
inline constexpr std::string_view kRendererNumericBuildPolicyId =
    "x86-64-v1-binary64-x87-extended-strict-v1";
inline constexpr std::string_view kRendererNumericBuildPolicyFlags =
    "-march=x86-64 -mtune=generic -mfpmath=sse -mno-avx -mno-avx2 "
    "-mno-fma -fno-lto -fexcess-precision=standard -fno-fast-math "
    "-ffp-contract=off";
inline constexpr std::string_view kRendererInstructionSetProfile = "x86-64-v1";
inline constexpr std::string_view kRendererFloatingPointFormat = "ieee754_binary64";
inline constexpr std::string_view kRendererRoundingMode = "nearest_ties_to_even";

struct BinaryFormatObservation {
    std::uint16_t radix = 0;
    std::uint16_t significand_digits = 0;
    std::int32_t minimum_exponent = 0;
    std::uint16_t maximum_exponent = 0;
    std::uint16_t storage_bytes = 0;
    bool is_iec559 = false;

    friend bool operator==(const BinaryFormatObservation &,
                           const BinaryFormatObservation &) = default;
};

// Raw facts for one calling thread. Sticky exception status bits are retained in the
// raw registers but are excluded from admission because they do not change results.
struct RendererNumericEnvironmentSnapshot {
    RendererNumericBuildPolicy build_policy = RendererNumericBuildPolicy::unmarked;
    bool is_linux_x86_64 = false;
    bool is_sysv_lp64 = false;
    std::uint8_t pointer_storage_bytes = 0;
    std::uint8_t long_storage_bytes = 0;

    bool arch_get_cpuid_observed = false;
    std::int32_t arch_get_cpuid_errno = 0;
    bool cpuid_enabled = false;
    bool cpuid_leaf1_observed = false;
    std::uint32_t maximum_basic_cpuid_leaf = 0;
    std::uint32_t cpuid_leaf1_edx = 0;

    BinaryFormatObservation binary32;
    BinaryFormatObservation binary64;
    BinaryFormatObservation extended80;
    std::int32_t float_evaluation_method = -1;

    std::int32_t fe_rounding_mode = -1;
    std::uint32_t mxcsr = 0;
    std::uint16_t x87_control_word = 0;
    std::uint16_t x87_status_word = 0;

    friend bool operator==(const RendererNumericEnvironmentSnapshot &,
                           const RendererNumericEnvironmentSnapshot &) = default;
};

// Canonical content identity produced only after all machine-specific observations
// have been admitted. Extra host capabilities and sticky flags cannot change it.
struct RendererNumericEnvironment {
    RendererNumericBuildPolicy build_policy = RendererNumericBuildPolicy::unmarked;
    std::string_view build_policy_id;
    std::string_view instruction_set_profile;
    std::string_view floating_point_format;
    std::string_view rounding;
    bool fma_contraction = false;
    bool flush_to_zero = false;
    bool denormals_are_zero = false;

    friend bool operator==(const RendererNumericEnvironment &,
                           const RendererNumericEnvironment &) = default;
};

enum class RendererNumericEnvironmentErrorCode : std::uint8_t {
    unsupported_platform,
    unsupported_abi,
    build_policy_unmarked,
    cpuid_query_unavailable,
    cpuid_disabled,
    cpuid_leaf_unavailable,
    cpu_capability_missing,
    floating_point_format_unsupported,
    floating_point_evaluation_unsupported,
    rounding_mode_mismatch,
    mxcsr_control_mismatch,
    x87_control_mismatch,
};

struct RendererNumericEnvironmentError {
    RendererNumericEnvironmentErrorCode code =
        RendererNumericEnvironmentErrorCode::unsupported_platform;
    std::string_view component;
    std::uint64_t observed = 0;
    std::uint64_t required = 0;
    std::string_view message;

    friend bool operator==(const RendererNumericEnvironmentError &,
                           const RendererNumericEnvironmentError &) = default;
};

using RendererNumericEnvironmentResult =
    std::variant<RendererNumericEnvironment, RendererNumericEnvironmentError>;

// Reads only the calling thread. It performs ARCH_GET_CPUID before executing CPUID
// and never changes the floating-point environment or the CPUID setting.
[[nodiscard]] RendererNumericEnvironmentSnapshot
observe_current_thread_renderer_numeric_environment() noexcept;

// Pure admission seam. No host state is read and the supplied snapshot is unchanged.
[[nodiscard]] RendererNumericEnvironmentResult validate_renderer_numeric_environment(
    const RendererNumericEnvironmentSnapshot &snapshot) noexcept;

[[nodiscard]] RendererNumericEnvironmentResult renderer_numeric_environment() noexcept;

namespace detail {

inline constexpr std::uint32_t kRequiredCpuidLeaf1Edx = (1U << 0U) |  // x87 FPU
                                                        (1U << 8U) |  // CMPXCHG8B
                                                        (1U << 15U) | // CMOV
                                                        (1U << 23U) | // MMX
                                                        (1U << 24U) | // FXSAVE/FXRSTOR
                                                        (1U << 25U) | // SSE
                                                        (1U << 26U);  // SSE2

inline constexpr std::uint32_t kMxcsrControlMask = 0x2ffc0U;
inline constexpr std::uint32_t kRequiredMxcsrControl = 0x1f80U;
inline constexpr std::uint16_t kX87ControlMask = 0x0f3fU;
inline constexpr std::uint16_t kRequiredX87Control = 0x033fU;

} // namespace detail
} // namespace engine_sim_offline::determinism
