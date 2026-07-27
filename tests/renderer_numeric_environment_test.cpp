#include "determinism/renderer_numeric_environment.hpp"

#include <array>
#include <asm/prctl.h>
#include <cerrno>
#include <cfenv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/syscall.h>
#include <unistd.h>
#include <variant>

namespace {

using namespace engine_sim_offline::determinism;

static_assert(noexcept(observe_current_thread_renderer_numeric_environment()));
static_assert(noexcept(
    validate_renderer_numeric_environment(RendererNumericEnvironmentSnapshot{})));
static_assert(noexcept(renderer_numeric_environment()));

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string(message)};
    }
}

[[nodiscard]] const RendererNumericEnvironmentError *
error_from(const RendererNumericEnvironmentResult &result) {
    return std::get_if<RendererNumericEnvironmentError>(&result);
}

void expect_error(const RendererNumericEnvironmentResult &result,
                  RendererNumericEnvironmentErrorCode code, std::string_view message) {
    const auto *error = error_from(result);
    expect(error != nullptr && error->code == code, message);
}

[[nodiscard]] std::uint32_t read_mxcsr() noexcept {
    std::uint32_t value = 0;
    __asm__ volatile("stmxcsr %0" : "=m"(value));
    return value;
}

void write_mxcsr(std::uint32_t value) noexcept {
    __asm__ volatile("ldmxcsr %0" : : "m"(value));
}

[[nodiscard]] std::uint16_t read_x87_control_word() noexcept {
    std::uint16_t value = 0;
    __asm__ volatile("fnstcw %0" : "=m"(value));
    return value;
}

[[nodiscard]] std::uint16_t read_x87_status_word() noexcept {
    std::uint16_t value = 0;
    __asm__ volatile("fnstsw %0" : "=am"(value));
    return value;
}

void write_x87_control_word(std::uint16_t value) noexcept {
    __asm__ volatile("fldcw %0" : : "m"(value));
}

struct LiveThreadState {
    std::int32_t fe_rounding_mode = -1;
    std::uint32_t mxcsr = 0;
    std::uint16_t x87_control_word = 0;
    std::uint16_t x87_status_word = 0;
    long cpuid_setting = -1;
    int saved_errno = 0;

    friend bool operator==(const LiveThreadState &, const LiveThreadState &) = default;
};

[[nodiscard]] LiveThreadState live_thread_state() noexcept {
    const int entry_errno = errno;
    const LiveThreadState state{
        std::fegetround(),
        read_mxcsr(),
        read_x87_control_word(),
        read_x87_status_word(),
        ::syscall(SYS_arch_prctl, ARCH_GET_CPUID, 0UL),
        entry_errno,
    };
    errno = entry_errno;
    return state;
}

void expect_live_rejection_preserved(const LiveThreadState &before,
                                     const RendererNumericEnvironmentResult &result,
                                     const LiveThreadState &after,
                                     RendererNumericEnvironmentErrorCode code,
                                     std::string_view rejection_message,
                                     std::string_view mutation_message) {
    expect_error(result, code, rejection_message);
    expect(after == before, mutation_message);
}

class ScopedNumericState final {
  public:
    ScopedNumericState()
        : saved_errno_(errno), mxcsr_(read_mxcsr()),
          x87_control_(read_x87_control_word()) {
        if (std::fegetenv(&environment_) != 0) {
            throw std::runtime_error{"failed to save floating-point environment"};
        }
    }

    ScopedNumericState(const ScopedNumericState &) = delete;
    ScopedNumericState &operator=(const ScopedNumericState &) = delete;

    ~ScopedNumericState() noexcept {
        (void)std::fesetenv(&environment_);
        write_x87_control_word(x87_control_);
        write_mxcsr(mxcsr_);
        errno = saved_errno_;
    }

  private:
    int saved_errno_ = 0;
    std::fenv_t environment_{};
    std::uint32_t mxcsr_ = 0;
    std::uint16_t x87_control_ = 0;
};

class ScopedCpuidDisable final {
  public:
    ScopedCpuidDisable() noexcept : saved_errno_(errno) {
        const long setting = ::syscall(SYS_arch_prctl, ARCH_GET_CPUID, 0UL);
        if (setting == 1 && ::syscall(SYS_arch_prctl, ARCH_SET_CPUID, 0UL) == 0) {
            disabled_ = true;
        }
        errno = saved_errno_;
    }

    ScopedCpuidDisable(const ScopedCpuidDisable &) = delete;
    ScopedCpuidDisable &operator=(const ScopedCpuidDisable &) = delete;

    ~ScopedCpuidDisable() noexcept {
        if (disabled_) {
            (void)::syscall(SYS_arch_prctl, ARCH_SET_CPUID, 1UL);
        }
        errno = saved_errno_;
    }

    [[nodiscard]] bool disabled() const noexcept {
        return disabled_;
    }

  private:
    int saved_errno_ = 0;
    bool disabled_ = false;
};

[[nodiscard]] RendererNumericEnvironmentSnapshot admitted_live_snapshot() {
    const auto snapshot = observe_current_thread_renderer_numeric_environment();
    const auto result = validate_renderer_numeric_environment(snapshot);
    if (const auto *error = error_from(result)) {
        std::string message{"live numeric environment was rejected at "};
        message.append(error->component);
        message.append(": ");
        message.append(error->message);
        throw std::runtime_error{message};
    }
    return snapshot;
}

void test_exact_policy_marker() {
    expect(kRendererNumericBuildPolicyFlags ==
               "-march=x86-64 -mtune=generic -mfpmath=sse -mno-avx -mno-avx2 "
               "-mno-fma -fno-lto -fexcess-precision=standard -fno-fast-math "
               "-ffp-contract=off",
           "numeric build-policy marker changed from the preflighted flags");
}

void test_live_observation_is_admitted_and_read_only() {
    const auto before = observe_current_thread_renderer_numeric_environment();
    errno = EDOM;
    const auto result = renderer_numeric_environment();
    const int errno_after = errno;
    const auto after = observe_current_thread_renderer_numeric_environment();

    expect(before == after, "current-thread observer changed numeric state");
    expect(errno_after == EDOM, "current-thread observer changed errno");
    const auto *identity = std::get_if<RendererNumericEnvironment>(&result);
    expect(identity != nullptr, "canonical live environment was rejected");
    expect(identity->build_policy == RendererNumericBuildPolicy::x86_64_v1_strict_v1 &&
               identity->build_policy_id ==
                   "x86-64-v1-binary64-x87-extended-strict-v1" &&
               identity->build_policy_id == kRendererNumericBuildPolicyId &&
               identity->instruction_set_profile == "x86-64-v1" &&
               identity->floating_point_format == "ieee754_binary64" &&
               identity->rounding == "nearest_ties_to_even" &&
               !identity->fma_contraction && !identity->flush_to_zero &&
               !identity->denormals_are_zero,
           "admission produced the wrong canonical identity");
    expect(before.is_linux_x86_64 && before.is_sysv_lp64 &&
               before.pointer_storage_bytes == 8 && before.long_storage_bytes == 8,
           "live observer did not identify Linux x86-64 SysV LP64");
}

void test_pure_validator_rejects_each_evidence_class() {
    const auto good = admitted_live_snapshot();
    const auto good_result = validate_renderer_numeric_environment(good);
    const auto *good_identity = std::get_if<RendererNumericEnvironment>(&good_result);
    expect(good_identity != nullptr, "good snapshot did not produce an identity");

    auto candidate = good;
    candidate.is_linux_x86_64 = false;
    candidate.build_policy = RendererNumericBuildPolicy::unmarked;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::unsupported_platform,
                 "platform did not take precedence over an invalid policy");

    candidate = good;
    candidate.is_sysv_lp64 = false;
    candidate.pointer_storage_bytes = 4;
    candidate.long_storage_bytes = 4;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::unsupported_abi,
                 "Linux x86-64 x32 ABI was admitted");

    candidate = good;
    candidate.build_policy = RendererNumericBuildPolicy::unmarked;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::build_policy_unmarked,
                 "unmarked build policy was admitted");

    candidate = good;
    candidate.arch_get_cpuid_observed = false;
    candidate.arch_get_cpuid_errno = ENOSYS;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::cpuid_query_unavailable,
                 "unobservable CPUID state was admitted");

    candidate = good;
    candidate.cpuid_enabled = false;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::cpuid_disabled,
                 "disabled CPUID was admitted");

    candidate = good;
    candidate.cpuid_leaf1_observed = false;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::cpuid_leaf_unavailable,
                 "missing CPUID leaf was admitted");

    for (const auto bit : std::array<unsigned int, 7>{0, 8, 15, 23, 24, 25, 26}) {
        candidate = good;
        candidate.cpuid_leaf1_edx &= ~(1U << bit);
        expect_error(validate_renderer_numeric_environment(candidate),
                     RendererNumericEnvironmentErrorCode::cpu_capability_missing,
                     "a missing required CPUID bit was admitted");
    }

    candidate = good;
    candidate.binary32.is_iec559 = false;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
                 "non-IEC-559 binary32 was admitted");

    candidate = good;
    candidate.binary64.minimum_exponent = -1020;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
                 "wrong binary64 exponent range was admitted");

    candidate = good;
    candidate.extended80.maximum_exponent = 16383;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::floating_point_format_unsupported,
                 "wrong extended precision format was admitted");

    candidate = good;
    candidate.float_evaluation_method = 2;
    expect_error(
        validate_renderer_numeric_environment(candidate),
        RendererNumericEnvironmentErrorCode::floating_point_evaluation_unsupported,
        "implicit excess precision was admitted");

    candidate = good;
    candidate.fe_rounding_mode = FE_DOWNWARD;
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::rounding_mode_mismatch,
                 "wrong fenv rounding mode was admitted");

    candidate = good;
    candidate.mxcsr |= (1U << 15U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                 "MXCSR flush-to-zero was admitted");

    candidate = good;
    candidate.mxcsr |= (1U << 6U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                 "MXCSR denormals-are-zero was admitted");

    candidate = good;
    candidate.mxcsr |= (1U << 13U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                 "MXCSR directed rounding was admitted");

    candidate = good;
    candidate.mxcsr &= ~(1U << 7U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                 "an unmasked MXCSR exception was admitted");

    candidate = good;
    candidate.mxcsr |= (1U << 17U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
                 "AMD MXCSR MM mode was admitted");

    candidate = good;
    candidate.x87_control_word =
        static_cast<std::uint16_t>((candidate.x87_control_word & ~0x0300U) | 0x0200U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::x87_control_mismatch,
                 "x87 double precision mode was admitted");

    candidate = good;
    candidate.x87_control_word =
        static_cast<std::uint16_t>(candidate.x87_control_word | 0x0400U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::x87_control_mismatch,
                 "x87 directed rounding was admitted");

    candidate = good;
    candidate.x87_control_word =
        static_cast<std::uint16_t>(candidate.x87_control_word & ~0x0001U);
    expect_error(validate_renderer_numeric_environment(candidate),
                 RendererNumericEnvironmentErrorCode::x87_control_mismatch,
                 "an unmasked x87 exception was admitted");

    candidate = good;
    candidate.cpuid_leaf1_edx = 0xffffffffU;
    candidate.mxcsr |= 0x3fU;
    candidate.x87_control_word =
        static_cast<std::uint16_t>(candidate.x87_control_word | 0x1000U);
    const auto extra_result = validate_renderer_numeric_environment(candidate);
    const auto *extra_identity = std::get_if<RendererNumericEnvironment>(&extra_result);
    expect(extra_identity != nullptr && *extra_identity == *good_identity,
           "extra CPU bits, sticky MXCSR flags, or x87 IC changed identity");

    const auto unchanged = good;
    (void)validate_renderer_numeric_environment(good);
    expect(good == unchanged, "pure validator changed its input snapshot");
}

void test_live_rounding_rejection_restores_state() {
    {
        ScopedNumericState restore;
        expect(std::fesetround(FE_DOWNWARD) == 0,
               "could not install live downward rounding mode");
        errno = ERANGE;
        const auto before = live_thread_state();
        expect(before.fe_rounding_mode == FE_DOWNWARD,
               "downward rounding mutation did not take effect");
        const auto result = renderer_numeric_environment();
        const auto after = live_thread_state();
        expect_live_rejection_preserved(
            before, result, after,
            RendererNumericEnvironmentErrorCode::rounding_mode_mismatch,
            "live downward rounding mode was admitted",
            "rounding rejection changed live thread state before restoration");
    }
    (void)admitted_live_snapshot();
}

void test_live_mxcsr_rejection_restores_state() {
    {
        ScopedNumericState restore;
        const auto original_mxcsr = read_mxcsr();
        write_mxcsr(original_mxcsr | (1U << 15U));
        errno = EDOM;
        const auto before = live_thread_state();
        expect(before.mxcsr == (original_mxcsr | (1U << 15U)),
               "FTZ-only MXCSR mutation did not take effect exactly");
        const auto result = renderer_numeric_environment();
        const auto after = live_thread_state();
        expect_live_rejection_preserved(
            before, result, after,
            RendererNumericEnvironmentErrorCode::mxcsr_control_mismatch,
            "live FTZ mode was admitted",
            "MXCSR rejection changed live thread state before restoration");
    }
    (void)admitted_live_snapshot();
}

void test_live_x87_rejection_restores_state() {
    {
        ScopedNumericState restore;
        const auto changed =
            static_cast<std::uint16_t>((read_x87_control_word() & ~0x0300U) | 0x0200U);
        write_x87_control_word(changed);
        errno = EILSEQ;
        const auto before = live_thread_state();
        expect(before.x87_control_word == changed,
               "x87 precision mutation did not take effect");
        const auto result = renderer_numeric_environment();
        const auto after = live_thread_state();
        expect_live_rejection_preserved(
            before, result, after,
            RendererNumericEnvironmentErrorCode::x87_control_mismatch,
            "live x87 double precision mode was admitted",
            "x87 rejection changed live thread state before restoration");
    }
    (void)admitted_live_snapshot();
}

void test_live_cpuid_rejection_restores_state_when_supported() {
    bool exercised = false;
    {
        ScopedCpuidDisable restore;
        exercised = restore.disabled();
        if (exercised) {
            errno = EOVERFLOW;
            const auto before = live_thread_state();
            expect(before.cpuid_setting == 0,
                   "CPUID disable mutation did not take effect");
            const auto result = renderer_numeric_environment();
            const auto after = live_thread_state();
            expect_live_rejection_preserved(
                before, result, after,
                RendererNumericEnvironmentErrorCode::cpuid_disabled,
                "live disabled CPUID state was admitted",
                "CPUID rejection changed live thread state before restoration");
        }
    }
    (void)admitted_live_snapshot();
}

} // namespace

int main() {
    test_exact_policy_marker();
    test_live_observation_is_admitted_and_read_only();
    test_pure_validator_rejects_each_evidence_class();
    test_live_rounding_rejection_restores_state();
    test_live_mxcsr_rejection_restores_state();
    test_live_x87_rejection_restores_state();
    test_live_cpuid_rejection_restores_state_when_supported();
}
