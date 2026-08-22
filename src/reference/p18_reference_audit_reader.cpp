#include "reference/p18_reference_audit_reader.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace crankwave::reference {
namespace {

constexpr std::size_t kRecordSampleIndexOffset = 0;
constexpr std::size_t kRecordStepEndOffset = 8;
constexpr std::size_t kRecordValuesOffset = 16;
constexpr std::size_t kRecordValueCount = 14;
constexpr std::size_t kRecordBusValueStart = 12;

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::digits == 53);
static_assert(std::numeric_limits<double>::max_exponent == 1024);

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] double read_f64_le(std::span<const std::byte> bytes,
                                 std::size_t offset) noexcept {
    return std::bit_cast<double>(read_u64_le(bytes, offset));
}

[[nodiscard]] P18ReferenceAuditDecodeError
error(P18ReferenceAuditDecodeErrorCode code, std::size_t byte_offset,
      std::uint64_t record_index = kP18ReferenceAuditNoRecordIndex) noexcept {
    return {code, byte_offset, record_index};
}

[[nodiscard]] bool has_magic(std::span<const std::byte> bytes) noexcept {
    constexpr char kMagic[] = "ESOAUD01";
    for (std::size_t index = 0; index < 8; ++index) {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(kMagic[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

P18ReferenceAuditDecodeResult
decode_p18_reference_audit(std::span<const std::byte> bytes) {
    if (bytes.size() < kP18ReferenceAuditHeaderBytes) {
        return error(P18ReferenceAuditDecodeErrorCode::truncated_header, bytes.size());
    }
    if (!has_magic(bytes)) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_magic, 0);
    }
    if (read_u32_le(bytes, 8) != 1U) {
        return error(P18ReferenceAuditDecodeErrorCode::unsupported_version, 8);
    }
    if (read_u32_le(bytes, 12) != kP18ReferenceAuditHeaderBytes) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_header_size, 12);
    }
    if (read_u32_le(bytes, 16) != kP18ReferenceAuditCylinderCount) {
        return error(P18ReferenceAuditDecodeErrorCode::unsupported_cylinder_count, 16);
    }
    if (read_u32_le(bytes, 20) != kP18ReferenceAuditBusCount) {
        return error(P18ReferenceAuditDecodeErrorCode::unsupported_bus_count, 20);
    }
    if (read_u32_le(bytes, 24) != kP18ReferenceAuditRecordBytes) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_record_size, 24);
    }
    if (read_u32_le(bytes, 28) != kP18ReferenceAuditSampleRateHz) {
        return error(P18ReferenceAuditDecodeErrorCode::unsupported_sample_rate, 28);
    }
    if (read_u64_le(bytes, 32) != kP18ReferenceAuditRecordCount) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_record_count, 32);
    }
    if (read_u64_le(bytes, 40) != kP18ReferenceAuditIntervalStart) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_interval_start, 40);
    }
    if (read_u64_le(bytes, 48) != kP18ReferenceAuditIntervalEndExclusive) {
        return error(P18ReferenceAuditDecodeErrorCode::invalid_interval_end, 48);
    }
    if (read_u64_le(bytes, 56) != 0U) {
        return error(P18ReferenceAuditDecodeErrorCode::nonzero_reserved_header, 56);
    }
    if (bytes.size() < kP18ReferenceAuditByteCount) {
        return error(P18ReferenceAuditDecodeErrorCode::truncated_payload, bytes.size());
    }
    if (bytes.size() > kP18ReferenceAuditByteCount) {
        return error(P18ReferenceAuditDecodeErrorCode::trailing_bytes,
                     kP18ReferenceAuditByteCount);
    }

    P18DecodedReferenceAudit decoded;
    decoded.frames.reserve(static_cast<std::size_t>(kP18ReferenceAuditRecordCount));
    for (std::uint64_t record_index = 0; record_index < kP18ReferenceAuditRecordCount;
         ++record_index) {
        const std::size_t record_offset =
            kP18ReferenceAuditHeaderBytes +
            static_cast<std::size_t>(record_index) * kP18ReferenceAuditRecordBytes;
        if (read_u64_le(bytes, record_offset + kRecordSampleIndexOffset) !=
            record_index) {
            return error(P18ReferenceAuditDecodeErrorCode::invalid_sample_index,
                         record_offset + kRecordSampleIndexOffset, record_index);
        }
        if (read_u64_le(bytes, record_offset + kRecordStepEndOffset) !=
            record_index + 1U) {
            return error(P18ReferenceAuditDecodeErrorCode::invalid_step_end,
                         record_offset + kRecordStepEndOffset, record_index);
        }

        P18ReferenceAuditBusFrame frame;
        for (std::size_t value_index = 0; value_index < kRecordValueCount;
             ++value_index) {
            const std::size_t value_offset =
                record_offset + kRecordValuesOffset + value_index * sizeof(double);
            const double value = read_f64_le(bytes, value_offset);
            if (!std::isfinite(value)) {
                return error(P18ReferenceAuditDecodeErrorCode::non_finite_record_value,
                             value_offset, record_index);
            }
            if (value_index >= kRecordBusValueStart) {
                frame.pre_dsp_buses[value_index - kRecordBusValueStart] = value;
            }
        }
        decoded.frames.push_back(frame);
    }

    return decoded;
}

} // namespace crankwave::reference
