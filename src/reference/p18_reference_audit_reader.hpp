#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::reference {

inline constexpr std::size_t kP18ReferenceAuditHeaderBytes = 64;
inline constexpr std::size_t kP18ReferenceAuditRecordBytes = 128;
inline constexpr std::uint32_t kP18ReferenceAuditCylinderCount = 6;
inline constexpr std::uint32_t kP18ReferenceAuditBusCount = 2;
inline constexpr std::uint32_t kP18ReferenceAuditSampleRateHz = 10000;
inline constexpr std::uint64_t kP18ReferenceAuditRecordCount = 170000;
inline constexpr std::uint64_t kP18ReferenceAuditIntervalStart = 0;
inline constexpr std::uint64_t kP18ReferenceAuditIntervalEndExclusive = 170000;
inline constexpr std::size_t kP18ReferenceAuditByteCount =
    kP18ReferenceAuditHeaderBytes +
    static_cast<std::size_t>(kP18ReferenceAuditRecordCount) *
        kP18ReferenceAuditRecordBytes;
inline constexpr std::uint64_t kP18ReferenceAuditNoRecordIndex =
    std::numeric_limits<std::uint64_t>::max();

enum class P18ReferenceAuditDecodeErrorCode : std::uint8_t {
    truncated_header,
    invalid_magic,
    unsupported_version,
    invalid_header_size,
    unsupported_cylinder_count,
    unsupported_bus_count,
    invalid_record_size,
    unsupported_sample_rate,
    invalid_record_count,
    invalid_interval_start,
    invalid_interval_end,
    nonzero_reserved_header,
    truncated_payload,
    trailing_bytes,
    invalid_sample_index,
    invalid_step_end,
    non_finite_record_value,
};

struct P18ReferenceAuditDecodeError {
    P18ReferenceAuditDecodeErrorCode code =
        P18ReferenceAuditDecodeErrorCode::truncated_header;
    std::size_t byte_offset = 0;
    std::uint64_t record_index = kP18ReferenceAuditNoRecordIndex;

    friend bool operator==(const P18ReferenceAuditDecodeError &,
                           const P18ReferenceAuditDecodeError &) = default;
};

// Reference-only raw lanes. The values remain uncalibrated
// engine_sim_source_unit and are ordered exactly as fixture bus 0 then bus 1.
// The separately identified M2 adapter owns conversion to presentation types.
struct P18ReferenceAuditBusFrame {
    std::array<double, kP18ReferenceAuditBusCount> pre_dsp_buses{};

    friend bool operator==(const P18ReferenceAuditBusFrame &,
                           const P18ReferenceAuditBusFrame &) = default;
};

struct P18DecodedReferenceAudit {
    std::vector<P18ReferenceAuditBusFrame> frames;

    friend bool operator==(const P18DecodedReferenceAudit &,
                           const P18DecodedReferenceAudit &) = default;
};

using P18ReferenceAuditDecodeResult =
    std::variant<P18DecodedReferenceAudit, P18ReferenceAuditDecodeError>;

// Strict, path-free decoder for the frozen ESOAUD01 v1 P1.8 audit shape. Input
// storage is borrowed only for this call. Every record value is checked for
// finiteness, but only the two final pre-DSP buses are retained in owned output.
[[nodiscard]] P18ReferenceAuditDecodeResult
decode_p18_reference_audit(std::span<const std::byte> bytes);

} // namespace engine_sim_offline::reference
