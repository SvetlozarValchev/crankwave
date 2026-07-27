#include "execution/linux_execution_facts.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#if defined(__linux__)
#include <time.h>
#endif

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
#include <asm/prctl.h>
#include <cpuid.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::execution {
namespace {

constexpr std::size_t kEntropyBytes = 16;
constexpr std::size_t kCpuBrandBytes = 48;
constexpr std::size_t kMaximumProcessStatusBytes = 64U * 1024U;
constexpr unsigned int kMaximumInterruptedRetries = 16;

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

[[nodiscard]] LinuxExecutionFactsError make_error(LinuxExecutionFactsErrorCode code,
                                                  std::string component,
                                                  int system_error,
                                                  std::string message) {
    return {code, std::move(component), system_error, std::move(message)};
}

[[nodiscard]] bool printable_ascii(std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char character) {
        return character >= 0x20U && character <= 0x7eU;
    });
}

[[nodiscard]] bool valid_clock_sample(const detail::LinuxClockSample &sample) noexcept {
    return sample.nanoseconds >= 0 && sample.nanoseconds < INT64_C(1000000000);
}

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)

using ClockObservationResult =
    std::variant<detail::LinuxClockSample, LinuxExecutionFactsError>;

[[nodiscard]] ClockObservationResult
observe_clock(clockid_t clock, LinuxExecutionFactsErrorCode error_code,
              std::string_view component) {
    struct timespec value{};
    if (::clock_gettime(clock, &value) == -1) {
        const auto error_number = errno;
        return make_error(error_code, std::string(component), error_number,
                          "Linux clock observation failed");
    }
    if (!std::in_range<std::int64_t>(value.tv_sec) ||
        !std::in_range<std::int64_t>(value.tv_nsec)) {
        return make_error(error_code, std::string(component), 0,
                          "Linux clock value does not fit the execution contract");
    }
    detail::LinuxClockSample result{
        static_cast<std::int64_t>(value.tv_sec),
        static_cast<std::int64_t>(value.tv_nsec),
    };
    if (!valid_clock_sample(result)) {
        return make_error(error_code, std::string(component), 0,
                          "Linux clock returned a noncanonical timespec");
    }
    return result;
}

using EntropyObservationResult =
    std::variant<std::array<std::uint8_t, kEntropyBytes>, LinuxExecutionFactsError>;

[[nodiscard]] EntropyObservationResult observe_entropy() {
    std::array<std::uint8_t, kEntropyBytes> entropy{};
    std::size_t obtained = 0;
    unsigned int interrupted = 0;
    while (obtained < entropy.size()) {
        const auto count =
            ::getrandom(entropy.data() + obtained, entropy.size() - obtained, 0);
        if (count > 0) {
            obtained += static_cast<std::size_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR &&
            interrupted++ < kMaximumInterruptedRetries) {
            continue;
        }
        const auto error_number = count == -1 ? errno : 0;
        const auto code = count == 0 || error_number == EINTR
                              ? LinuxExecutionFactsErrorCode::random_source_short_read
                              : LinuxExecutionFactsErrorCode::random_source_unavailable;
        return make_error(code, "run_id", error_number,
                          "Linux getrandom did not provide a complete run identity");
    }
    return entropy;
}

template <std::size_t Size>
[[nodiscard]] std::variant<std::string_view, LinuxExecutionFactsError>
bounded_uts_field(const char (&field)[Size], std::string_view component) {
    const auto end = std::find(field, field + Size, '\0');
    if (end == field + Size) {
        return make_error(LinuxExecutionFactsErrorCode::host_identity_invalid,
                          std::string(component), 0,
                          "Linux uname field is not NUL terminated");
    }
    return std::string_view(field, static_cast<std::size_t>(end - field));
}

[[nodiscard]] detail::CanonicalStringResult observe_host_os() {
    struct utsname identity{};
    if (::uname(&identity) == -1) {
        const auto error_number = errno;
        return make_error(LinuxExecutionFactsErrorCode::host_identity_unavailable,
                          "host_os", error_number, "Linux uname observation failed");
    }
    auto sysname = bounded_uts_field(identity.sysname, "host_os.sysname");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&sysname)) {
        return std::move(*error);
    }
    auto release = bounded_uts_field(identity.release, "host_os.release");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&release)) {
        return std::move(*error);
    }
    auto machine = bounded_uts_field(identity.machine, "host_os.machine");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&machine)) {
        return std::move(*error);
    }
    return detail::canonical_host_os(std::get<std::string_view>(sysname),
                                     std::get<std::string_view>(release),
                                     std::get<std::string_view>(machine));
}

[[nodiscard]] detail::CanonicalStringResult observe_cpu_model() {
    errno = 0;
    const long cpuid_setting = ::syscall(SYS_arch_prctl, ARCH_GET_CPUID, 0UL);
    const auto error_number = errno;
    if (cpuid_setting < 0) {
        return make_error(LinuxExecutionFactsErrorCode::cpu_identity_unavailable,
                          "cpu_model", error_number,
                          "Linux calling-thread CPUID state is unavailable");
    }
    if (cpuid_setting != 1) {
        return make_error(LinuxExecutionFactsErrorCode::cpu_identity_unavailable,
                          "cpu_model", 0,
                          "CPUID is disabled for the Linux calling thread");
    }

    unsigned int signature = 0;
    if (__get_cpuid_max(UINT32_C(0x80000000), &signature) < UINT32_C(0x80000004)) {
        return make_error(LinuxExecutionFactsErrorCode::cpu_identity_unavailable,
                          "cpu_model", 0,
                          "extended CPUID brand leaves are unavailable");
    }

    std::array<char, kCpuBrandBytes> brand{};
    for (unsigned int index = 0; index < 3; ++index) {
        unsigned int eax = 0;
        unsigned int ebx = 0;
        unsigned int ecx = 0;
        unsigned int edx = 0;
        __cpuid(UINT32_C(0x80000002) + index, eax, ebx, ecx, edx);
        const std::array words{eax, ebx, ecx, edx};
        std::memcpy(brand.data() + index * 16U, words.data(), 16U);
    }
    return detail::canonical_cpu_model(brand);
}

[[nodiscard]] detail::PositiveU32Result observe_logical_cpu_count() {
    errno = 0;
    const auto observed = ::sysconf(_SC_NPROCESSORS_ONLN);
    const auto error_number = errno;
    if (observed == -1) {
        return make_error(LinuxExecutionFactsErrorCode::logical_cpu_count_unavailable,
                          "logical_cpu_count", error_number,
                          "Linux online logical CPU count is unavailable");
    }
    return detail::logical_cpu_count_from_sysconf(observed);
}

class FileDescriptor final {
  public:
    explicit FileDescriptor(int descriptor) noexcept : descriptor_(descriptor) {}
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    ~FileDescriptor() {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
        }
    }

    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

  private:
    int descriptor_ = -1;
};

[[nodiscard]] detail::PositiveU32Result observe_process_thread_count() {
    FileDescriptor status(
        ::open("/proc/self/status", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (status.get() < 0) {
        const auto error_number = errno;
        return make_error(LinuxExecutionFactsErrorCode::process_status_open_failed,
                          "observed_process_threads", error_number,
                          "could not open bounded Linux process status");
    }

    std::array<char, kMaximumProcessStatusBytes + 1U> bytes{};
    std::size_t size = 0;
    unsigned int interrupted = 0;
    while (size < bytes.size()) {
        const auto count =
            ::read(status.get(), bytes.data() + size, bytes.size() - size);
        if (count > 0) {
            size += static_cast<std::size_t>(count);
            if (size > kMaximumProcessStatusBytes) {
                return make_error(
                    LinuxExecutionFactsErrorCode::process_status_too_large,
                    "observed_process_threads", 0,
                    "Linux process status exceeded the fixed read bound");
            }
            continue;
        }
        if (count == 0) {
            return detail::parse_process_thread_count(
                std::string_view(bytes.data(), size));
        }
        if (errno == EINTR && interrupted++ < kMaximumInterruptedRetries) {
            continue;
        }
        const auto error_number = errno;
        return make_error(LinuxExecutionFactsErrorCode::process_status_read_failed,
                          "observed_process_threads", error_number,
                          "could not read bounded Linux process status");
    }
    return make_error(LinuxExecutionFactsErrorCode::process_status_too_large,
                      "observed_process_threads", 0,
                      "Linux process status exceeded the fixed read bound");
}

[[nodiscard]] detail::PeakResidentBytesResult observe_peak_resident_bytes() {
    struct rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) == -1) {
        return std::optional<std::uint64_t>{};
    }
    return detail::peak_resident_bytes_from_linux_kib(usage.ru_maxrss);
}

#endif

} // namespace

namespace detail {

std::string run_id_from_entropy(std::span<const std::uint8_t, 16> entropy) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result = "render-run-";
    result.reserve(result.size() + entropy.size() * 2U);
    for (const auto byte : entropy) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

CanonicalStringResult canonical_utc_timestamp(const LinuxClockSample &sample) {
    if (!valid_clock_sample(sample)) {
        return make_error(LinuxExecutionFactsErrorCode::utc_timestamp_invalid,
                          "started_utc", 0,
                          "realtime clock returned invalid nanoseconds");
    }
#if defined(__linux__)
    if (!std::in_range<time_t>(sample.seconds)) {
        return make_error(LinuxExecutionFactsErrorCode::utc_timestamp_out_of_range,
                          "started_utc", 0,
                          "realtime seconds do not fit the Linux UTC conversion range");
    }
    const auto seconds = static_cast<time_t>(sample.seconds);
    struct tm utc{};
    errno = 0;
    if (::gmtime_r(&seconds, &utc) == nullptr) {
        const auto error_number = errno;
        return make_error(LinuxExecutionFactsErrorCode::utc_timestamp_out_of_range,
                          "started_utc", error_number,
                          "Linux UTC conversion rejected the realtime sample");
    }
    const auto year = static_cast<long long>(utc.tm_year) + 1900LL;
    if (year < 0 || year > 9999 || utc.tm_mon < 0 || utc.tm_mon > 11 ||
        utc.tm_mday < 1 || utc.tm_mday > 31 || utc.tm_hour < 0 || utc.tm_hour > 23 ||
        utc.tm_min < 0 || utc.tm_min > 59 || utc.tm_sec < 0 || utc.tm_sec > 59) {
        return make_error(LinuxExecutionFactsErrorCode::utc_timestamp_out_of_range,
                          "started_utc", 0,
                          "Linux UTC conversion is outside canonical manifest syntax");
    }

    std::array<char, 32> text{};
    const auto count = std::snprintf(
        text.data(), text.size(), "%04lld-%02d-%02dT%02d:%02d:%02d.%09lldZ", year,
        utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
        static_cast<long long>(sample.nanoseconds));
    if (count != 30) {
        return make_error(
            LinuxExecutionFactsErrorCode::utc_timestamp_out_of_range, "started_utc", 0,
            "Linux UTC conversion did not produce the canonical fixed width");
    }
    return std::string(text.data(), static_cast<std::size_t>(count));
#else
    return make_error(LinuxExecutionFactsErrorCode::unsupported_platform, "started_utc",
                      0, "UTC execution observation is implemented only on Linux");
#endif
}

ElapsedNanosecondsResult checked_elapsed(const LinuxClockSample &started,
                                         const LinuxClockSample &finished) {
    if (!valid_clock_sample(started) || !valid_clock_sample(finished)) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_overflow,
                          "wall_elapsed", 0,
                          "elapsed clock sample contains invalid nanoseconds");
    }
    if (finished.seconds < started.seconds ||
        (finished.seconds == started.seconds &&
         finished.nanoseconds < started.nanoseconds)) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_clock_regressed,
                          "wall_elapsed", 0, "Linux elapsed clock regressed");
    }
    if (finished == started) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_not_positive,
                          "wall_elapsed", 0,
                          "completed execution elapsed time is zero");
    }

    constexpr auto billion = INT64_C(1000000000);
    if (started.seconds < 0 &&
        finished.seconds > std::numeric_limits<std::int64_t>::max() + started.seconds) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_overflow,
                          "wall_elapsed", 0, "elapsed seconds overflow int64");
    }
    auto seconds = finished.seconds - started.seconds;
    auto nanoseconds = finished.nanoseconds - started.nanoseconds;
    if (nanoseconds < 0) {
        --seconds;
        nanoseconds += billion;
    }
    if (seconds < 0 || seconds > std::numeric_limits<std::int64_t>::max() / billion) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_overflow,
                          "wall_elapsed", 0, "elapsed nanoseconds overflow int64");
    }
    const auto seconds_ns = seconds * billion;
    if (nanoseconds > std::numeric_limits<std::int64_t>::max() - seconds_ns) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_overflow,
                          "wall_elapsed", 0, "elapsed nanoseconds overflow int64");
    }
    const auto total = seconds_ns + nanoseconds;
    if (total <= 0) {
        return make_error(LinuxExecutionFactsErrorCode::elapsed_not_positive,
                          "wall_elapsed", 0,
                          "completed execution elapsed time is not positive");
    }
    return std::chrono::nanoseconds{total};
}

CanonicalStringResult canonical_host_os(std::string_view sysname,
                                        std::string_view release,
                                        std::string_view machine) {
    constexpr std::size_t maximum_field_bytes = 256;
    if (sysname.size() > maximum_field_bytes || release.size() > maximum_field_bytes ||
        machine.size() > maximum_field_bytes || !printable_ascii(sysname) ||
        !printable_ascii(release) || !printable_ascii(machine)) {
        return make_error(LinuxExecutionFactsErrorCode::host_identity_invalid,
                          "host_os", 0,
                          "Linux uname identity must be bounded printable ASCII");
    }
    std::string result;
    result.reserve(sysname.size() + release.size() + machine.size() + 2U);
    result.append(sysname);
    result.push_back(' ');
    result.append(release);
    result.push_back(' ');
    result.append(machine);
    return result;
}

CanonicalStringResult canonical_cpu_model(std::span<const char, 48> brand) {
    std::size_t begin = 0;
    while (begin < brand.size() && (brand[begin] == ' ' || brand[begin] == '\0')) {
        ++begin;
    }
    std::size_t end = brand.size();
    while (end > begin && (brand[end - 1] == ' ' || brand[end - 1] == '\0')) {
        --end;
    }
    if (begin == end) {
        return make_error(LinuxExecutionFactsErrorCode::cpu_identity_invalid,
                          "cpu_model", 0, "CPUID brand string is empty");
    }
    const std::string_view model(brand.data() + begin, end - begin);
    if (!printable_ascii(model)) {
        return make_error(LinuxExecutionFactsErrorCode::cpu_identity_invalid,
                          "cpu_model", 0, "CPUID brand string is not printable ASCII");
    }
    return std::string(model);
}

PositiveU32Result logical_cpu_count_from_sysconf(long value) {
    if (value <= 0 ||
        static_cast<unsigned long>(value) > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(LinuxExecutionFactsErrorCode::logical_cpu_count_out_of_range,
                          "logical_cpu_count", 0,
                          "Linux online logical CPU count does not fit positive u32");
    }
    return static_cast<std::uint32_t>(value);
}

PositiveU32Result parse_process_thread_count(std::string_view process_status,
                                             bool input_exceeded_bound) {
    if (input_exceeded_bound || process_status.size() > kMaximumProcessStatusBytes) {
        return make_error(LinuxExecutionFactsErrorCode::process_status_too_large,
                          "observed_process_threads", 0,
                          "Linux process status exceeded the fixed read bound");
    }

    bool found = false;
    std::uint32_t result = 0;
    std::size_t begin = 0;
    while (begin <= process_status.size()) {
        const auto end = process_status.find('\n', begin);
        const auto line = process_status.substr(begin, end - begin);
        constexpr std::string_view prefix = "Threads:";
        if (line.starts_with(prefix)) {
            if (found) {
                return make_error(
                    LinuxExecutionFactsErrorCode::process_thread_count_malformed,
                    "observed_process_threads", 0,
                    "Linux process status contains duplicate Threads fields");
            }
            found = true;
            auto value = line.substr(prefix.size());
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            std::uint64_t parsed = 0;
            std::size_t digits = 0;
            while (digits < value.size() && value[digits] >= '0' &&
                   value[digits] <= '9') {
                const auto digit = static_cast<std::uint64_t>(value[digits] - '0');
                if (parsed >
                    (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
                    return make_error(
                        LinuxExecutionFactsErrorCode::process_thread_count_out_of_range,
                        "observed_process_threads", 0,
                        "Linux process thread count exceeds u32");
                }
                parsed = parsed * 10U + digit;
                ++digits;
            }
            auto trailing = value.substr(digits);
            while (!trailing.empty() &&
                   (trailing.front() == ' ' || trailing.front() == '\t')) {
                trailing.remove_prefix(1);
            }
            if (digits == 0 || !trailing.empty()) {
                return make_error(
                    LinuxExecutionFactsErrorCode::process_thread_count_malformed,
                    "observed_process_threads", 0,
                    "Linux process Threads field is malformed");
            }
            if (parsed == 0) {
                return make_error(
                    LinuxExecutionFactsErrorCode::process_thread_count_out_of_range,
                    "observed_process_threads", 0,
                    "Linux process thread count is zero");
            }
            result = static_cast<std::uint32_t>(parsed);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    if (!found) {
        return make_error(LinuxExecutionFactsErrorCode::process_thread_count_missing,
                          "observed_process_threads", 0,
                          "Linux process status has no Threads field");
    }
    return result;
}

PeakResidentBytesResult peak_resident_bytes_from_linux_kib(long maximum_resident_kib) {
    if (maximum_resident_kib < 0) {
        return make_error(LinuxExecutionFactsErrorCode::peak_resident_memory_invalid,
                          "peak_resident_bytes", 0,
                          "Linux peak resident memory is negative");
    }
    if (maximum_resident_kib == 0) {
        return std::optional<std::uint64_t>{};
    }
    const auto value = static_cast<std::uint64_t>(maximum_resident_kib);
    if (value > std::numeric_limits<std::uint64_t>::max() / UINT64_C(1024)) {
        return make_error(LinuxExecutionFactsErrorCode::peak_resident_memory_overflow,
                          "peak_resident_bytes", 0,
                          "Linux peak resident memory does not fit bytes");
    }
    return std::optional<std::uint64_t>{value * UINT64_C(1024)};
}

} // namespace detail

struct LinuxExecutionFactsObservation::Implementation {
    detail::LinuxClockSample started;
    std::string run_id;
    std::string started_utc;
    std::string host_os;
    std::string cpu_model;
    std::uint32_t logical_cpu_count = 0;
    bool finished = false;
};

ObservedExecutionFacts::ObservedExecutionFacts(contract::ExecutionFacts facts)
    : facts_(std::move(facts)) {}

const contract::ExecutionFacts &ObservedExecutionFacts::facts() const noexcept {
    return facts_;
}

LinuxExecutionFactsObservation::LinuxExecutionFactsObservation(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}

LinuxExecutionFactsObservation::~LinuxExecutionFactsObservation() = default;

LinuxExecutionFactsObservation::LinuxExecutionFactsObservation(
    LinuxExecutionFactsObservation &&) noexcept = default;

ObservedExecutionFacts
LinuxExecutionFactsObservation::make_observed(contract::ExecutionFacts facts) {
    return ObservedExecutionFacts{std::move(facts)};
}

BeginSingleJobLinuxExecutionResult begin_single_job_linux_execution() {
    const ErrnoRestore restore_errno;
#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
    auto entropy = observe_entropy();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&entropy)) {
        return std::move(*error);
    }
    const auto run_id = detail::run_id_from_entropy(
        std::get<std::array<std::uint8_t, kEntropyBytes>>(entropy));

    auto host_os = observe_host_os();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&host_os)) {
        return std::move(*error);
    }
    auto cpu_model = observe_cpu_model();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&cpu_model)) {
        return std::move(*error);
    }
    auto logical_cpu_count = observe_logical_cpu_count();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&logical_cpu_count)) {
        return std::move(*error);
    }

    auto implementation =
        std::make_unique<LinuxExecutionFactsObservation::Implementation>();
    implementation->run_id = run_id;
    implementation->host_os = std::get<std::string>(std::move(host_os));
    implementation->cpu_model = std::get<std::string>(std::move(cpu_model));
    implementation->logical_cpu_count = std::get<std::uint32_t>(logical_cpu_count);

    auto realtime = observe_clock(
        CLOCK_REALTIME, LinuxExecutionFactsErrorCode::realtime_clock_unavailable,
        "started_utc");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&realtime)) {
        return std::move(*error);
    }
    auto started_utc =
        detail::canonical_utc_timestamp(std::get<detail::LinuxClockSample>(realtime));
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&started_utc)) {
        return std::move(*error);
    }
    auto elapsed_start = observe_clock(
        CLOCK_BOOTTIME, LinuxExecutionFactsErrorCode::elapsed_clock_unavailable,
        "wall_elapsed");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&elapsed_start)) {
        return std::move(*error);
    }
    implementation->started = std::get<detail::LinuxClockSample>(elapsed_start);
    implementation->started_utc = std::get<std::string>(std::move(started_utc));
    return LinuxExecutionFactsObservation{std::move(implementation)};
#else
    return make_error(LinuxExecutionFactsErrorCode::unsupported_platform, "execution",
                      0, "execution facts require Linux x86-64 SysV LP64");
#endif
}

FinishSingleJobLinuxExecutionResult
finish_single_job_linux_execution(LinuxExecutionFactsObservation &&observation) {
    const ErrnoRestore restore_errno;
    if (!observation.implementation_ || observation.implementation_->finished) {
        return make_error(
            LinuxExecutionFactsErrorCode::observation_already_finished, "execution", 0,
            "single-job Linux execution observation is inactive or finished");
    }
    observation.implementation_->finished = true;

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
    auto elapsed_end = observe_clock(
        CLOCK_BOOTTIME, LinuxExecutionFactsErrorCode::elapsed_clock_unavailable,
        "wall_elapsed");
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&elapsed_end)) {
        return std::move(*error);
    }
    auto elapsed =
        detail::checked_elapsed(observation.implementation_->started,
                                std::get<detail::LinuxClockSample>(elapsed_end));
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&elapsed)) {
        return std::move(*error);
    }

    auto process_threads = observe_process_thread_count();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&process_threads)) {
        return std::move(*error);
    }
    auto peak_resident = observe_peak_resident_bytes();
    if (auto *error = std::get_if<LinuxExecutionFactsError>(&peak_resident)) {
        return std::move(*error);
    }

    contract::ExecutionFacts facts{
        observation.implementation_->run_id,
        observation.implementation_->started_utc,
        std::get<std::chrono::nanoseconds>(elapsed),
        observation.implementation_->host_os,
        observation.implementation_->cpu_model,
        observation.implementation_->logical_cpu_count,
        std::get<std::uint32_t>(process_threads),
        1,
        std::get<std::optional<std::uint64_t>>(peak_resident),
    };
    const auto validation = contract::validate(facts);
    if (!validation.ok()) {
        const auto &issue = validation.issues.front();
        return make_error(
            LinuxExecutionFactsErrorCode::contract_validation_failed, issue.path, 0,
            "observed execution facts failed their typed contract: " + issue.message);
    }
    return LinuxExecutionFactsObservation::make_observed(std::move(facts));
#else
    return make_error(LinuxExecutionFactsErrorCode::unsupported_platform, "execution",
                      0, "execution facts require Linux x86-64 SysV LP64");
#endif
}

} // namespace engine_sim_offline::execution
