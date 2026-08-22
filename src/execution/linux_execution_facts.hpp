#pragma once

#include "crankwave/contract/render_manifest.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace crankwave::execution {

enum class LinuxExecutionFactsErrorCode : std::uint8_t {
    unsupported_platform,
    random_source_unavailable,
    random_source_short_read,
    realtime_clock_unavailable,
    elapsed_clock_unavailable,
    utc_timestamp_invalid,
    utc_timestamp_out_of_range,
    host_identity_unavailable,
    host_identity_invalid,
    cpu_identity_unavailable,
    cpu_identity_invalid,
    logical_cpu_count_unavailable,
    logical_cpu_count_out_of_range,
    process_status_open_failed,
    process_status_read_failed,
    process_status_too_large,
    process_thread_count_missing,
    process_thread_count_malformed,
    process_thread_count_out_of_range,
    peak_resident_memory_invalid,
    peak_resident_memory_overflow,
    elapsed_clock_regressed,
    elapsed_not_positive,
    elapsed_overflow,
    observation_already_finished,
    contract_validation_failed,
};

struct LinuxExecutionFactsError {
    LinuxExecutionFactsErrorCode code =
        LinuxExecutionFactsErrorCode::unsupported_platform;
    std::string component;
    int system_error = 0;
    std::string message;

    friend bool operator==(const LinuxExecutionFactsError &,
                           const LinuxExecutionFactsError &) = default;
};

class LinuxExecutionFactsObservation;

// A complete current-process observation. Its constructor is private, and only the
// zero-argument Linux observer below can manufacture this wrapper. Pure parsing seams
// return raw scalar values and cannot acquire this type.
class ObservedExecutionFacts final {
  public:
    ObservedExecutionFacts(const ObservedExecutionFacts &) = default;
    ObservedExecutionFacts(ObservedExecutionFacts &&) noexcept = default;
    ObservedExecutionFacts &operator=(const ObservedExecutionFacts &) = delete;
    ObservedExecutionFacts &operator=(ObservedExecutionFacts &&) = delete;

    [[nodiscard]] const contract::ExecutionFacts &facts() const noexcept;

  private:
    friend class LinuxExecutionFactsObservation;

    explicit ObservedExecutionFacts(contract::ExecutionFacts facts);

    contract::ExecutionFacts facts_;
};

// A begun single-job observation. It owns the immutable start evidence and may be
// finished exactly once. A failed finish is terminal because retrying would change
// the measured interval.
class LinuxExecutionFactsObservation final {
  public:
    ~LinuxExecutionFactsObservation();
    LinuxExecutionFactsObservation(LinuxExecutionFactsObservation &&) noexcept;
    LinuxExecutionFactsObservation &
    operator=(LinuxExecutionFactsObservation &&) = delete;
    LinuxExecutionFactsObservation(const LinuxExecutionFactsObservation &) = delete;
    LinuxExecutionFactsObservation &
    operator=(const LinuxExecutionFactsObservation &) = delete;

  private:
    struct Implementation;

    friend std::variant<LinuxExecutionFactsObservation, LinuxExecutionFactsError>
    begin_single_job_linux_execution();
    friend std::variant<ObservedExecutionFacts, LinuxExecutionFactsError>
    finish_single_job_linux_execution(LinuxExecutionFactsObservation &&observation);

    explicit LinuxExecutionFactsObservation(
        std::unique_ptr<Implementation> implementation) noexcept;
    [[nodiscard]] static ObservedExecutionFacts
    make_observed(contract::ExecutionFacts facts);

    std::unique_ptr<Implementation> implementation_;
};

using BeginSingleJobLinuxExecutionResult =
    std::variant<LinuxExecutionFactsObservation, LinuxExecutionFactsError>;
using FinishSingleJobLinuxExecutionResult =
    std::variant<ObservedExecutionFacts, LinuxExecutionFactsError>;

// The sole production entry point. The concurrent-render-job count is fixed to one
// by this API rather than accepted as a caller assertion.
[[nodiscard]] BeginSingleJobLinuxExecutionResult begin_single_job_linux_execution();

[[nodiscard]] FinishSingleJobLinuxExecutionResult
finish_single_job_linux_execution(LinuxExecutionFactsObservation &&observation);

namespace detail {

// Pure formatting/parsing seams shared by production collection and focused tests.
// Their synthetic outputs are ordinary values and carry no production authority.
struct LinuxClockSample {
    std::int64_t seconds = 0;
    std::int64_t nanoseconds = 0;

    friend bool operator==(const LinuxClockSample &,
                           const LinuxClockSample &) = default;
};

using CanonicalStringResult = std::variant<std::string, LinuxExecutionFactsError>;
using ElapsedNanosecondsResult =
    std::variant<std::chrono::nanoseconds, LinuxExecutionFactsError>;
using PositiveU32Result = std::variant<std::uint32_t, LinuxExecutionFactsError>;
using PeakResidentBytesResult =
    std::variant<std::optional<std::uint64_t>, LinuxExecutionFactsError>;

[[nodiscard]] std::string
run_id_from_entropy(std::span<const std::uint8_t, 16> entropy);

[[nodiscard]] CanonicalStringResult
canonical_utc_timestamp(const LinuxClockSample &sample);

[[nodiscard]] ElapsedNanosecondsResult
checked_elapsed(const LinuxClockSample &started, const LinuxClockSample &finished);

[[nodiscard]] CanonicalStringResult canonical_host_os(std::string_view sysname,
                                                      std::string_view release,
                                                      std::string_view machine);

[[nodiscard]] CanonicalStringResult
canonical_cpu_model(std::span<const char, 48> brand);

[[nodiscard]] PositiveU32Result logical_cpu_count_from_sysconf(long value);

[[nodiscard]] PositiveU32Result
parse_process_thread_count(std::string_view process_status,
                           bool input_exceeded_bound = false);

[[nodiscard]] PeakResidentBytesResult
peak_resident_bytes_from_linux_kib(long maximum_resident_kib);

} // namespace detail
} // namespace crankwave::execution
