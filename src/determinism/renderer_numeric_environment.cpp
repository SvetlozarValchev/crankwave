#include "determinism/renderer_numeric_environment.hpp"
#include "engine_sim_offline_generated/renderer_numeric_policy_generated.hpp"

#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <string_view>

#if defined(__linux__) && defined(__x86_64__)
#include <asm/prctl.h>
#include <cpuid.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if ENGINE_SIM_OFFLINE_RENDERER_NUMERIC_POLICY_ADMITTED
#if !defined(__linux__) || !defined(__x86_64__) || !defined(__LP64__)
#error "numeric-policy CMake admission disagrees with the compiler target"
#endif
static_assert(sizeof(void *) == 8);
static_assert(sizeof(long) == 8);
#endif

namespace engine_sim_offline::determinism {
namespace {

class ErrnoRestore final {
  public:
    ErrnoRestore() noexcept : saved_(errno) {}
    ErrnoRestore(const ErrnoRestore &) = delete;
    ErrnoRestore &operator=(const ErrnoRestore &) = delete;
    ~ErrnoRestore() noexcept {
        errno = saved_;
    }

  private:
    int saved_ = 0;
};

template <typename T>
[[nodiscard]] constexpr BinaryFormatObservation binary_format() noexcept {
    return {
        static_cast<std::uint16_t>(std::numeric_limits<T>::radix),
        static_cast<std::uint16_t>(std::numeric_limits<T>::digits),
        static_cast<std::int32_t>(std::numeric_limits<T>::min_exponent),
        static_cast<std::uint16_t>(std::numeric_limits<T>::max_exponent),
        static_cast<std::uint16_t>(sizeof(T)),
        std::numeric_limits<T>::is_iec559,
    };
}

[[nodiscard]] RendererNumericBuildPolicy compiled_build_policy() noexcept {
    static_assert(generated::kRendererNumericBuildPolicyFlags ==
                  kRendererNumericBuildPolicyFlags);
    return generated::kRendererNumericBuildPolicyAdmitted
               ? RendererNumericBuildPolicy::x86_64_v1_strict_v1
               : RendererNumericBuildPolicy::unmarked;
}

[[nodiscard]] RendererNumericEnvironmentError
failure(RendererNumericEnvironmentErrorCode code, std::string_view component,
        std::uint64_t observed, std::uint64_t required,
        std::string_view message) noexcept {
    return {
        code, component, observed, required, message,
    };
}

[[nodiscard]] bool is_binary_format(const BinaryFormatObservation &value,
                                    std::uint16_t digits, std::int32_t minimum_exponent,
                                    std::uint16_t maximum_exponent,
                                    std::uint16_t storage_bytes) noexcept {
    return value.radix == 2 && value.significand_digits == digits &&
           value.minimum_exponent == minimum_exponent &&
           value.maximum_exponent == maximum_exponent &&
           value.storage_bytes == storage_bytes && value.is_iec559;
}

#if defined(__linux__) && defined(__x86_64__)
[[nodiscard]] std::uint32_t read_mxcsr() noexcept {
    std::uint32_t value = 0;
    __asm__ volatile("stmxcsr %0" : "=m"(value));
    return value;
}

[[nodiscard]] std::uint16_t read_x87_control_word() noexcept {
    std::uint16_t value = 0;
    __asm__ volatile("fnstcw %0" : "=m"(value));
    return value;
}
#endif

} // namespace

RendererNumericEnvironmentSnapshot
observe_current_thread_renderer_numeric_environment() noexcept {
    const ErrnoRestore restore_errno;
    RendererNumericEnvironmentSnapshot snapshot;
    snapshot.build_policy = compiled_build_policy();
    snapshot.pointer_storage_bytes = sizeof(void *);
    snapshot.long_storage_bytes = sizeof(long);
    snapshot.binary32 = binary_format<float>();
    snapshot.binary64 = binary_format<double>();
    snapshot.extended80 = binary_format<long double>();
    snapshot.float_evaluation_method = FLT_EVAL_METHOD;
    snapshot.fe_rounding_mode = std::fegetround();

#if defined(__linux__) && defined(__x86_64__)
    snapshot.is_linux_x86_64 = true;
#if !defined(__ILP32__)
    snapshot.is_sysv_lp64 =
        snapshot.pointer_storage_bytes == 8 && snapshot.long_storage_bytes == 8;
#endif
    snapshot.mxcsr = read_mxcsr();
    snapshot.x87_control_word = read_x87_control_word();

    errno = 0;
    const long cpuid_setting = ::syscall(SYS_arch_prctl, ARCH_GET_CPUID, 0UL);
    const int cpuid_errno = errno;
    if (cpuid_setting < 0) {
        snapshot.arch_get_cpuid_errno = cpuid_errno;
        return snapshot;
    }

    snapshot.arch_get_cpuid_observed = true;
    snapshot.cpuid_enabled = cpuid_setting == 1;
    if (!snapshot.cpuid_enabled) {
        return snapshot;
    }

    unsigned int vendor_signature = 0;
    snapshot.maximum_basic_cpuid_leaf = __get_cpuid_max(0, &vendor_signature);
    if (snapshot.maximum_basic_cpuid_leaf < 1) {
        return snapshot;
    }

    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    __cpuid(1, eax, ebx, ecx, edx);
    snapshot.cpuid_leaf1_observed = true;
    snapshot.cpuid_leaf1_edx = edx;
#endif

    return snapshot;
}

RendererNumericEnvironmentResult validate_renderer_numeric_environment(
    const RendererNumericEnvironmentSnapshot &snapshot) noexcept {
    if (!snapshot.is_linux_x86_64) {
        return failure(RendererNumericEnvironmentErrorCode::unsupported_platform,
                       "platform", 0, 1,
                       "renderer numeric policy requires Linux x86-64");
    }
    if (!snapshot.is_sysv_lp64 || snapshot.pointer_storage_bytes != 8 ||
        snapshot.long_storage_bytes != 8) {
        const auto observed = static_cast<std::uint64_t>(
            (static_cast<std::uint16_t>(snapshot.pointer_storage_bytes) << 8U) |
            snapshot.long_storage_bytes);
        return failure(RendererNumericEnvironmentErrorCode::unsupported_abi, "abi",
                       observed, 0x0808U,
                       "renderer numeric policy requires the SysV LP64 ABI, not "
                       "x32 or another data model");
    }
    if (snapshot.build_policy != RendererNumericBuildPolicy::x86_64_v1_strict_v1) {
        return failure(
            RendererNumericEnvironmentErrorCode::build_policy_unmarked, "build_policy",
            static_cast<std::uint64_t>(snapshot.build_policy),
            static_cast<std::uint64_t>(RendererNumericBuildPolicy::x86_64_v1_strict_v1),
            "build did not attest the exact strict numeric flag policy");
    }
    if (!snapshot.arch_get_cpuid_observed) {
        return failure(RendererNumericEnvironmentErrorCode::cpuid_query_unavailable,
                       "ARCH_GET_CPUID",
                       static_cast<std::uint32_t>(snapshot.arch_get_cpuid_errno), 0,
                       "calling-thread CPUID state could not be observed");
    }
    if (!snapshot.cpuid_enabled) {
        return failure(RendererNumericEnvironmentErrorCode::cpuid_disabled,
                       "ARCH_GET_CPUID", 0, 1,
                       "CPUID is disabled for the calling thread");
    }
    if (!snapshot.cpuid_leaf1_observed || snapshot.maximum_basic_cpuid_leaf < 1) {
        return failure(RendererNumericEnvironmentErrorCode::cpuid_leaf_unavailable,
                       "CPUID.01H", snapshot.maximum_basic_cpuid_leaf, 1,
                       "required CPUID feature leaf is unavailable");
    }
    const auto admitted_features =
        snapshot.cpuid_leaf1_edx & detail::kRequiredCpuidLeaf1Edx;
    if (admitted_features != detail::kRequiredCpuidLeaf1Edx) {
        return failure(
            RendererNumericEnvironmentErrorCode::cpu_capability_missing,
            "CPUID.01H:EDX", admitted_features, detail::kRequiredCpuidLeaf1Edx,
            "CPU lacks one or more x86-64-v1 capabilities required by the build");
    }

    if (!is_binary_format(snapshot.binary32, 24, -125, 128, 4)) {
        return failure(
            RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
            "float", snapshot.binary32.significand_digits, 24,
            "float is not the required IEEE-754 binary32 format");
    }
    if (!is_binary_format(snapshot.binary64, 53, -1021, 1024, 8)) {
        return failure(
            RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
            "double", snapshot.binary64.significand_digits, 53,
            "double is not the required IEEE-754 binary64 format");
    }
    if (!is_binary_format(snapshot.extended80, 64, -16381, 16384, 16)) {
        return failure(
            RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
            "long_double", snapshot.extended80.significand_digits, 64,
            "long double is not the required SysV x87 extended format");
    }
    if (snapshot.float_evaluation_method != 0) {
        return failure(
            RendererNumericEnvironmentErrorCode::floating_point_evaluation_unsupported,
            "FLT_EVAL_METHOD",
            static_cast<std::uint32_t>(snapshot.float_evaluation_method), 0,
            "implicit excess-precision evaluation is not admitted");
    }
    if (snapshot.fe_rounding_mode != FE_TONEAREST) {
        return failure(RendererNumericEnvironmentErrorCode::rounding_mode_mismatch,
                       "fegetround",
                       static_cast<std::uint32_t>(snapshot.fe_rounding_mode),
                       static_cast<std::uint32_t>(FE_TONEAREST),
                       "calling thread is not round-to-nearest/ties-to-even");
    }

    const auto mxcsr_control = snapshot.mxcsr & detail::kMxcsrControlMask;
    if (mxcsr_control != detail::kRequiredMxcsrControl) {
        return failure(RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                       "MXCSR", mxcsr_control, detail::kRequiredMxcsrControl,
                       "MXCSR must use nearest rounding, masked exceptions, and "
                       "disable FTZ, DAZ, and AMD MM mode");
    }
    const auto x87_control =
        static_cast<std::uint16_t>(snapshot.x87_control_word & detail::kX87ControlMask);
    if (x87_control != detail::kRequiredX87Control) {
        return failure(RendererNumericEnvironmentErrorCode::x87_control_mismatch,
                       "x87_control_word", x87_control, detail::kRequiredX87Control,
                       "x87 must use extended precision, nearest rounding, and "
                       "masked exceptions");
    }

    return RendererNumericEnvironment{
        RendererNumericBuildPolicy::x86_64_v1_strict_v1,
        kRendererNumericBuildPolicyId,
        kRendererInstructionSetProfile,
        kRendererFloatingPointFormat,
        kRendererRoundingMode,
        false,
        false,
        false,
    };
}

RendererNumericEnvironmentResult renderer_numeric_environment() noexcept {
    return validate_renderer_numeric_environment(
        observe_current_thread_renderer_numeric_environment());
}

} // namespace engine_sim_offline::determinism
