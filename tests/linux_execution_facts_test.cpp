#include "execution/linux_execution_facts.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

using namespace crankwave;
using namespace crankwave::execution;

static_assert(!std::is_default_constructible_v<LinuxExecutionFactsObservation>);
static_assert(!std::is_copy_constructible_v<LinuxExecutionFactsObservation>);
static_assert(std::is_move_constructible_v<LinuxExecutionFactsObservation>);
static_assert(!std::is_move_assignable_v<LinuxExecutionFactsObservation>);
static_assert(!std::is_default_constructible_v<ObservedExecutionFacts>);
static_assert(
    !std::is_constructible_v<ObservedExecutionFacts, contract::ExecutionFacts>);
static_assert(std::is_copy_constructible_v<ObservedExecutionFacts>);
static_assert(std::is_move_constructible_v<ObservedExecutionFacts>);
static_assert(!std::is_copy_assignable_v<ObservedExecutionFacts>);
static_assert(!std::is_move_assignable_v<ObservedExecutionFacts>);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Value, class... Alternatives>
const Value &require_value(const std::variant<Alternatives...> &result,
                           std::string_view message) {
    const auto *value = std::get_if<Value>(&result);
    expect(value != nullptr, message);
    return *value;
}

template <class... Alternatives>
void expect_error(const std::variant<Alternatives...> &result,
                  LinuxExecutionFactsErrorCode code, std::string_view message) {
    const auto *error = std::get_if<LinuxExecutionFactsError>(&result);
    expect(error != nullptr && error->code == code, message);
}

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
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
            static_cast<void>(::syscall(SYS_arch_prctl, ARCH_SET_CPUID, 1UL));
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
#endif

void test_run_id_and_utc_formatting() {
    std::array<std::uint8_t, 16> entropy{};
    for (std::size_t index = 0; index < entropy.size(); ++index) {
        entropy[index] = static_cast<std::uint8_t>(index);
    }
    expect(detail::run_id_from_entropy(entropy) ==
               "render-run-000102030405060708090a0b0c0d0e0f",
           "entropy-to-run-ID encoding changed");

    auto timestamp = detail::canonical_utc_timestamp({0, 7});
    expect(require_value<std::string>(timestamp, "Unix epoch UTC was rejected") ==
               "1970-01-01T00:00:00.000000007Z",
           "canonical UTC epoch formatting changed");

    timestamp = detail::canonical_utc_timestamp({951'782'400, 123'456'789});
    expect(require_value<std::string>(timestamp, "leap-day UTC was rejected") ==
               "2000-02-29T00:00:00.123456789Z",
           "canonical leap-day UTC formatting changed");

    expect_error(detail::canonical_utc_timestamp({0, -1}),
                 LinuxExecutionFactsErrorCode::utc_timestamp_invalid,
                 "negative realtime nanoseconds were accepted");
    expect_error(detail::canonical_utc_timestamp({0, 1'000'000'000}),
                 LinuxExecutionFactsErrorCode::utc_timestamp_invalid,
                 "one-billion realtime nanoseconds were accepted");
    expect_error(
        detail::canonical_utc_timestamp({std::numeric_limits<std::int64_t>::max(), 0}),
        LinuxExecutionFactsErrorCode::utc_timestamp_out_of_range,
        "unrepresentable UTC year was accepted");
}

void test_elapsed_arithmetic() {
    auto elapsed = detail::checked_elapsed({10, 900'000'000}, {12, 100'000'000});
    expect(require_value<std::chrono::nanoseconds>(
               elapsed, "positive elapsed interval was rejected") ==
               std::chrono::milliseconds{1'200},
           "timespec borrow changed elapsed duration");

    expect_error(detail::checked_elapsed({10, 0}, {10, 0}),
                 LinuxExecutionFactsErrorCode::elapsed_not_positive,
                 "zero elapsed interval was accepted");
    expect_error(detail::checked_elapsed({10, 1}, {10, 0}),
                 LinuxExecutionFactsErrorCode::elapsed_clock_regressed,
                 "regressing elapsed clock was accepted");
    expect_error(detail::checked_elapsed({10, -1}, {11, 0}),
                 LinuxExecutionFactsErrorCode::elapsed_overflow,
                 "invalid start timespec was accepted");
    expect_error(detail::checked_elapsed(
                     {0, 0}, {std::numeric_limits<std::int64_t>::max(), 999'999'999}),
                 LinuxExecutionFactsErrorCode::elapsed_overflow,
                 "elapsed nanosecond overflow was accepted");
}

void test_host_cpu_and_count_normalization() {
    auto host = detail::canonical_host_os("Linux", "7.0.0", "x86_64");
    expect(require_value<std::string>(host, "valid uname tuple was rejected") ==
               "Linux 7.0.0 x86_64",
           "uname tuple formatting changed");
    expect_error(detail::canonical_host_os("", "7.0.0", "x86_64"),
                 LinuxExecutionFactsErrorCode::host_identity_invalid,
                 "empty uname sysname was accepted");
    expect_error(detail::canonical_host_os("Linux", "bad\nrelease", "x86_64"),
                 LinuxExecutionFactsErrorCode::host_identity_invalid,
                 "control byte in uname tuple was accepted");
    expect_error(detail::canonical_host_os(std::string(257, 'x'), "7.0.0", "x86_64"),
                 LinuxExecutionFactsErrorCode::host_identity_invalid,
                 "oversized uname field was accepted");

    std::array<char, 48> brand{};
    brand.fill(' ');
    constexpr std::string_view model = "Example CPU 123";
    std::ranges::copy(model, brand.begin() + 3);
    auto cpu = detail::canonical_cpu_model(brand);
    expect(require_value<std::string>(cpu, "valid CPUID brand was rejected") == model,
           "CPUID padding normalization changed");
    brand.fill(' ');
    expect_error(detail::canonical_cpu_model(brand),
                 LinuxExecutionFactsErrorCode::cpu_identity_invalid,
                 "empty CPUID brand was accepted");
    brand.fill(' ');
    brand[1] = '\n';
    expect_error(detail::canonical_cpu_model(brand),
                 LinuxExecutionFactsErrorCode::cpu_identity_invalid,
                 "control byte in CPUID brand was accepted");

    auto logical = detail::logical_cpu_count_from_sysconf(32);
    expect(require_value<std::uint32_t>(
               logical, "positive logical CPU count was rejected") == 32,
           "logical CPU count conversion changed");
    expect_error(detail::logical_cpu_count_from_sysconf(0),
                 LinuxExecutionFactsErrorCode::logical_cpu_count_out_of_range,
                 "zero logical CPU count was accepted");
    expect_error(detail::logical_cpu_count_from_sysconf(-1),
                 LinuxExecutionFactsErrorCode::logical_cpu_count_out_of_range,
                 "negative logical CPU count was accepted");
    if constexpr (std::numeric_limits<unsigned long>::max() >
                  std::numeric_limits<std::uint32_t>::max()) {
        expect_error(
            detail::logical_cpu_count_from_sysconf(static_cast<long>(
                static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()) +
                1UL)),
            LinuxExecutionFactsErrorCode::logical_cpu_count_out_of_range,
            "logical CPU count above u32 was accepted");
    }
}

void test_thread_and_resident_memory_parsing() {
    auto threads = detail::parse_process_thread_count("Name:\ttest\nThreads:\t7\n");
    expect(require_value<std::uint32_t>(threads,
                                        "valid process thread count was rejected") == 7,
           "process thread count parsing changed");
    expect_error(detail::parse_process_thread_count("Name:\ttest\n"),
                 LinuxExecutionFactsErrorCode::process_thread_count_missing,
                 "missing process thread count was accepted");
    expect_error(detail::parse_process_thread_count("Threads:\t1\nThreads:\t2\n"),
                 LinuxExecutionFactsErrorCode::process_thread_count_malformed,
                 "duplicate process thread count was accepted");
    for (const auto invalid : {"Threads:\t\n", "Threads:\t-1\n", "Threads:\t1x\n"}) {
        expect_error(detail::parse_process_thread_count(invalid),
                     LinuxExecutionFactsErrorCode::process_thread_count_malformed,
                     "malformed process thread count was accepted");
    }
    expect_error(detail::parse_process_thread_count("Threads:\t0\n"),
                 LinuxExecutionFactsErrorCode::process_thread_count_out_of_range,
                 "zero process thread count was accepted");
    expect_error(detail::parse_process_thread_count("Threads:\t4294967296\n"),
                 LinuxExecutionFactsErrorCode::process_thread_count_out_of_range,
                 "process thread count above u32 was accepted");
    expect_error(detail::parse_process_thread_count("Threads:\t1\n", true),
                 LinuxExecutionFactsErrorCode::process_status_too_large,
                 "truncated process status was accepted");
    expect_error(detail::parse_process_thread_count(std::string(64U * 1024U + 1U, 'x')),
                 LinuxExecutionFactsErrorCode::process_status_too_large,
                 "oversized process status was accepted");

    auto resident = detail::peak_resident_bytes_from_linux_kib(123);
    const auto &resident_value = require_value<std::optional<std::uint64_t>>(
        resident, "positive Linux peak RSS was rejected");
    expect(resident_value == UINT64_C(125952),
           "Linux KiB-to-byte RSS conversion changed");
    resident = detail::peak_resident_bytes_from_linux_kib(0);
    expect(!require_value<std::optional<std::uint64_t>>(
                resident, "unavailable Linux peak RSS was rejected")
                .has_value(),
           "zero Linux peak RSS was not represented as unavailable");
    expect_error(detail::peak_resident_bytes_from_linux_kib(-1),
                 LinuxExecutionFactsErrorCode::peak_resident_memory_invalid,
                 "negative Linux peak RSS was accepted");
    if constexpr (std::numeric_limits<unsigned long>::max() >
                  std::numeric_limits<std::uint64_t>::max() / UINT64_C(1024)) {
        expect_error(detail::peak_resident_bytes_from_linux_kib(
                         std::numeric_limits<long>::max()),
                     LinuxExecutionFactsErrorCode::peak_resident_memory_overflow,
                     "overflowing Linux peak RSS was accepted");
    }
}

void test_live_one_shot_observation() {
    constexpr int caller_errno = EDOM;
    errno = caller_errno;
    auto begin = begin_single_job_linux_execution();
    expect(errno == caller_errno, "begin changed caller errno");
#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
    auto *observation = std::get_if<LinuxExecutionFactsObservation>(&begin);
    if (observation == nullptr) {
        const auto &error = std::get<LinuxExecutionFactsError>(begin);
        throw std::runtime_error{"live begin failed at " + error.component + ": " +
                                 error.message};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    errno = caller_errno;
    auto finish = finish_single_job_linux_execution(std::move(*observation));
    expect(errno == caller_errno, "finish changed caller errno");
    const auto *observed = std::get_if<ObservedExecutionFacts>(&finish);
    if (observed == nullptr) {
        const auto &error = std::get<LinuxExecutionFactsError>(finish);
        throw std::runtime_error{"live finish failed at " + error.component + ": " +
                                 error.message};
    }
    const auto &facts = observed->facts();
    expect(contract::validate(facts).ok(),
           "live execution facts failed their typed contract");
    expect(facts.run_id.size() == 43 && facts.run_id.starts_with("render-run-") &&
               std::ranges::all_of(std::string_view{facts.run_id}.substr(11),
                                   [](char character) {
                                       return (character >= '0' && character <= '9') ||
                                              (character >= 'a' && character <= 'f');
                                   }),
           "live run ID is not canonical 128-bit lowercase hex");
    expect(facts.started_utc.size() == 30 && facts.started_utc[19] == '.' &&
               facts.started_utc.back() == 'Z',
           "live UTC timestamp is not fixed nanosecond syntax");
    expect(facts.wall_elapsed.count() > 0 && !facts.host_os.empty() &&
               !facts.cpu_model.empty() && facts.logical_cpu_count > 0 &&
               facts.observed_process_threads > 0 && facts.concurrent_render_jobs == 1,
           "live execution observation lost a required fact");
    expect(!facts.peak_resident_bytes.has_value() || *facts.peak_resident_bytes > 0,
           "live execution observation emitted zero peak RSS");

    errno = caller_errno;
    const auto repeated = finish_single_job_linux_execution(std::move(*observation));
    expect(errno == caller_errno, "repeated finish changed caller errno");
    expect_error(repeated, LinuxExecutionFactsErrorCode::observation_already_finished,
                 "one-shot execution observation finished twice");
#else
    expect_error(begin, LinuxExecutionFactsErrorCode::unsupported_platform,
                 "unsupported platform did not reject live execution observation");
#endif
}

void test_disabled_cpuid_rejection_when_supported() {
#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
    ScopedCpuidDisable restore;
    if (!restore.disabled()) {
        return;
    }
    constexpr int caller_errno = EOVERFLOW;
    errno = caller_errno;
    const auto result = begin_single_job_linux_execution();
    expect(errno == caller_errno, "disabled-CPUID rejection changed caller errno");
    expect_error(result, LinuxExecutionFactsErrorCode::cpu_identity_unavailable,
                 "disabled CPUID was not rejected before executing CPUID");
#endif
}

} // namespace

int main() {
    try {
        test_run_id_and_utc_formatting();
        test_elapsed_arithmetic();
        test_host_cpu_and_count_normalization();
        test_thread_and_resident_memory_parsing();
        test_disabled_cpuid_rejection_when_supported();
        test_live_one_shot_observation();
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Linux execution-facts test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
