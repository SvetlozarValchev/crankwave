#include "reference/p18_reference_full_audit_reader.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace engine_sim_offline::reference {
namespace {

constexpr std::size_t kRecordValuesOffset = 16U;
constexpr std::size_t kPreDelayValueStart = 0U;
constexpr std::size_t kPostDelayValueStart = 6U;
constexpr std::size_t kBusValueStart = 12U;

[[nodiscard]] std::uint64_t read_u64_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] double read_f64_le(std::span<const std::byte> bytes,
                                 std::size_t offset) noexcept {
    return std::bit_cast<double>(read_u64_le(bytes, offset));
}

} // namespace

P18FullReferenceAuditDecodeResult
decode_p18_full_reference_audit(std::span<const std::byte> bytes) {
    auto strict_result = decode_p18_reference_audit(bytes);
    if (const auto *error = std::get_if<P18ReferenceAuditDecodeError>(&strict_result)) {
        return *error;
    }

    const auto &strict = std::get<P18DecodedReferenceAudit>(strict_result);
    P18DecodedFullReferenceAudit decoded;
    decoded.frames.resize(strict.frames.size());
    for (std::size_t frame = 0U; frame < decoded.frames.size(); ++frame) {
        const std::size_t record_offset =
            kP18ReferenceAuditHeaderBytes + frame * kP18ReferenceAuditRecordBytes;
        auto &destination = decoded.frames[frame];
        for (std::size_t cylinder = 0U; cylinder < kP18ReferenceAuditCylinderCount;
             ++cylinder) {
            destination.pre_delay_cylinders[cylinder] = read_f64_le(
                bytes, record_offset + kRecordValuesOffset +
                           (kPreDelayValueStart + cylinder) * sizeof(double));
            destination.post_delay_cylinders[cylinder] = read_f64_le(
                bytes, record_offset + kRecordValuesOffset +
                           (kPostDelayValueStart + cylinder) * sizeof(double));
        }
        for (std::size_t route = 0U; route < kP18ReferenceAuditBusCount; ++route) {
            destination.pre_dsp_buses[route] =
                read_f64_le(bytes, record_offset + kRecordValuesOffset +
                                       (kBusValueStart + route) * sizeof(double));
        }
    }
    return decoded;
}

} // namespace engine_sim_offline::reference
