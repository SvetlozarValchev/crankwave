#include "reference/reference_parity_v1_reader.hpp"

#include "engine_sim_offline/contract/common.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace engine_sim_offline::reference {
namespace {

constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kHeaderBytesOffset = 12;
constexpr std::size_t kCylinderCountOffset = 16;
constexpr std::size_t kRouteCountOffset = 20;
constexpr std::size_t kRecordBytesOffset = 24;
constexpr std::size_t kSampleRateOffset = 28;
constexpr std::size_t kRecordCountOffset = 32;
constexpr std::size_t kAudibleStartOffset = 40;
constexpr std::size_t kIntervalEndOffset = 48;
constexpr std::size_t kReferenceParametersOffset = 56;
constexpr std::size_t kCylinderDescriptorsOffset = 128;

constexpr std::size_t kRecordSampleIndexOffset = 0;
constexpr std::size_t kRecordStepEndOffset = 8;
constexpr std::size_t kRecordTimeOffset = 16;
constexpr std::size_t kRecordDtOffset = 24;
constexpr std::size_t kRecordRpmOffset = 32;
constexpr std::size_t kRecordFilteredRpmOffset = 40;
constexpr std::size_t kRecordCrankAngleOffset = 48;
constexpr std::size_t kRecordRequestedThrottleOffset = 56;
constexpr std::size_t kRecordResolvedThrottleOffset = 64;
constexpr std::size_t kRecordIgnitionOffset = 72;
constexpr std::size_t kRecordFuelOffset = 73;
constexpr std::size_t kRecordStarterOffset = 74;
constexpr std::size_t kRecordDynoOffset = 75;
constexpr std::size_t kRecordReservedOffset = 76;
constexpr std::size_t kRecordCylinderValuesOffset = 80;
constexpr std::size_t kRecordCylinderValuesBytes = 24;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kStepSeconds = 1.0 / 10000.0;
constexpr double kMaximumWrappedCrankAngle = 4.0 * kLegacyPi;

constexpr engine_sim_offline::contract::Sha256Digest kExpectedContentSha256{{
    0x19, 0xd3, 0x51, 0xb5, 0x4c, 0x8e, 0xb8, 0xb5, 0x09, 0xcd, 0x72,
    0xea, 0x03, 0x06, 0x1b, 0x01, 0xf9, 0x27, 0x22, 0xcb, 0xfa, 0x48,
    0xd2, 0x7a, 0x23, 0x42, 0xca, 0x72, 0x03, 0xff, 0xa9, 0x4c,
}};

struct ExpectedControl {
    double requested_throttle_01;
    double resolved_intake_throttle_01;
    std::uint8_t ignition;
    std::uint8_t fuel;
    std::uint8_t starter;
    std::uint8_t dyno;
};

constexpr std::array<double, 9> kExpectedReferenceParameters{
    101325.0, 343.0, 1600.0, 40.0, 1.0, 0.1, 0.1, 6.0, 2.0,
};

constexpr std::array<ReferenceParityV1CylinderDescriptor,
                     kReferenceParityV1CylinderCount>
    kExpectedDescriptors{{
        {0, 1, 0, 1, 180, 0.0, 0.0, 6.167266379343297, 6.167266379343297, 0.508, 1.0,
         1.0},
        {1, 2, 4, 0, 180, 8.377580409573333, 0.0, 6.167266379343297, 6.167266379343297,
         0.508, 1.0, 0.5},
        {2, 3, 2, 1, 180, 4.188790204786667, 0.0, 6.167266379343297, 6.167266379343297,
         0.508, 1.0, 1.0},
        {3, 4, 5, 0, 180, 10.471975511966667, 0.0, 6.167266379343297, 6.167266379343297,
         0.508, 1.0, 0.5},
        {4, 5, 1, 1, 180, 2.0943951023933334, 0.0, 6.167266379343297, 6.167266379343297,
         0.508, 1.0, 1.0},
        {5, 6, 3, 0, 180, 6.28318530718, 0.0, 6.167266379343297, 6.167266379343297,
         0.508, 1.0, 0.5},
    }};

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::digits == 53);
static_assert(std::numeric_limits<double>::max_exponent == 1024);

[[nodiscard]] std::uint8_t read_u8(std::span<const std::byte> bytes,
                                   std::size_t offset) noexcept {
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

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

[[nodiscard]] bool same_binary64(double lhs, double rhs) noexcept {
    return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
}

[[nodiscard]] bool has_magic(std::span<const std::byte> bytes) noexcept {
    constexpr char kMagic[] = "ESOPAR01";
    for (std::size_t index = 0; index < 8; ++index) {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(kMagic[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ReferenceParityV1DecodeError
error(ReferenceParityV1DecodeErrorCode code, std::size_t byte_offset,
      std::uint64_t record_index = kReferenceParityV1NoRecordIndex) noexcept {
    return {code, byte_offset, record_index};
}

[[nodiscard]] ExpectedControl expected_control(std::uint64_t record_index) noexcept {
    if (record_index < UINT64_C(8000)) {
        return {0.18, 0.9676, 0, 1, 1, 0};
    }
    if (record_index < UINT64_C(9000)) {
        return {0.18, 0.9676, 0, 1, 1, 1};
    }
    if (record_index < UINT64_C(10000)) {
        return {0.12, 0.9856, 1, 1, 0, 1};
    }
    return {0.85, 0.2775000000000001, 1, 1, 0, 1};
}

[[nodiscard]] ReferenceParityV1CylinderDescriptor
read_descriptor(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return {
        read_u32_le(bytes, offset),      read_u32_le(bytes, offset + 4),
        read_u32_le(bytes, offset + 8),  read_u32_le(bytes, offset + 12),
        read_u32_le(bytes, offset + 16), read_f64_le(bytes, offset + 24),
        read_f64_le(bytes, offset + 32), read_f64_le(bytes, offset + 40),
        read_f64_le(bytes, offset + 48), read_f64_le(bytes, offset + 56),
        read_f64_le(bytes, offset + 64), read_f64_le(bytes, offset + 72),
    };
}

[[nodiscard]] std::optional<std::size_t>
descriptor_mismatch_offset(const ReferenceParityV1CylinderDescriptor &actual,
                           const ReferenceParityV1CylinderDescriptor &expected,
                           std::size_t descriptor_offset) noexcept {
    const std::array<std::pair<bool, std::size_t>, 12> comparisons{{
        {actual.runtime_index == expected.runtime_index, 0},
        {actual.stable_id == expected.stable_id, 4},
        {actual.firing_rank == expected.firing_rank, 8},
        {actual.route_index == expected.route_index, 12},
        {actual.delay_samples == expected.delay_samples, 16},
        {same_binary64(actual.firing_angle_rad, expected.firing_angle_rad), 24},
        {same_binary64(actual.header_primary_length_m,
                       expected.header_primary_length_m),
         32},
        {same_binary64(actual.exhaust_system_length_m,
                       expected.exhaust_system_length_m),
         40},
        {same_binary64(actual.total_audio_length_m, expected.total_audio_length_m), 48},
        {same_binary64(actual.gas_primary_tube_length_m,
                       expected.gas_primary_tube_length_m),
         56},
        {same_binary64(actual.sound_attenuation_linear,
                       expected.sound_attenuation_linear),
         64},
        {same_binary64(actual.route_audio_volume_linear,
                       expected.route_audio_volume_linear),
         72},
    }};
    for (const auto &[matches, field_offset] : comparisons) {
        if (!matches) {
            return descriptor_offset + field_offset;
        }
    }
    return std::nullopt;
}

} // namespace

ReferenceParityV1DecodeResult
decode_reference_parity_v1(std::span<const std::byte> bytes) {
    if (bytes.size() < kReferenceParityV1HeaderBytes) {
        return error(ReferenceParityV1DecodeErrorCode::truncated_header, bytes.size());
    }
    if (!has_magic(bytes)) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_magic, 0);
    }
    if (read_u32_le(bytes, kVersionOffset) != 1U) {
        return error(ReferenceParityV1DecodeErrorCode::unsupported_version,
                     kVersionOffset);
    }
    if (read_u32_le(bytes, kHeaderBytesOffset) != kReferenceParityV1HeaderBytes) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_header_size,
                     kHeaderBytesOffset);
    }
    if (read_u32_le(bytes, kCylinderCountOffset) != kReferenceParityV1CylinderCount) {
        return error(ReferenceParityV1DecodeErrorCode::unsupported_cylinder_count,
                     kCylinderCountOffset);
    }
    if (read_u32_le(bytes, kRouteCountOffset) != kReferenceParityV1RouteCount) {
        return error(ReferenceParityV1DecodeErrorCode::unsupported_route_count,
                     kRouteCountOffset);
    }
    if (read_u32_le(bytes, kRecordBytesOffset) != kReferenceParityV1RecordBytes) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_record_size,
                     kRecordBytesOffset);
    }
    if (read_u32_le(bytes, kSampleRateOffset) != kReferenceParityV1SampleRateHz) {
        return error(ReferenceParityV1DecodeErrorCode::unsupported_sample_rate,
                     kSampleRateOffset);
    }
    if (read_u64_le(bytes, kRecordCountOffset) != kReferenceParityV1RecordCount) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_record_count,
                     kRecordCountOffset);
    }
    if (read_u64_le(bytes, kAudibleStartOffset) !=
        kReferenceParityV1AudibleStartRecord) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_audible_start,
                     kAudibleStartOffset);
    }
    if (read_u64_le(bytes, kIntervalEndOffset) !=
        kReferenceParityV1IntervalEndExclusive) {
        return error(ReferenceParityV1DecodeErrorCode::invalid_interval_end,
                     kIntervalEndOffset);
    }

    for (std::size_t index = 0; index < kExpectedReferenceParameters.size(); ++index) {
        const auto offset = kReferenceParametersOffset + index * sizeof(double);
        if (!same_binary64(read_f64_le(bytes, offset),
                           kExpectedReferenceParameters[index])) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_reference_parameter,
                         offset);
        }
    }

    DecodedReferenceParityV1 decoded;
    decoded.metadata = {
        1,
        kReferenceParityV1SampleRateHz,
        kReferenceParityV1RecordCount,
        kReferenceParityV1AudibleStartRecord,
        kReferenceParityV1IntervalEndExclusive,
        kExpectedReferenceParameters[0],
        kExpectedReferenceParameters[1],
        kExpectedReferenceParameters[2],
        kExpectedReferenceParameters[3],
        kExpectedReferenceParameters[4],
        kExpectedReferenceParameters[5],
        kExpectedReferenceParameters[6],
        kExpectedReferenceParameters[7],
        kExpectedReferenceParameters[8],
    };

    for (std::size_t index = 0; index < decoded.cylinders.size(); ++index) {
        const auto descriptor_offset =
            kCylinderDescriptorsOffset +
            index * kReferenceParityV1CylinderDescriptorBytes;
        const auto descriptor = read_descriptor(bytes, descriptor_offset);
        if (read_u32_le(bytes, descriptor_offset + 20) != 0U) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_cylinder_descriptor,
                         descriptor_offset + 20);
        }
        if (const auto mismatch = descriptor_mismatch_offset(
                descriptor, kExpectedDescriptors[index], descriptor_offset);
            mismatch.has_value()) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_cylinder_descriptor,
                         *mismatch);
        }
        decoded.cylinders[index] = descriptor;
    }

    if (bytes.size() < kReferenceParityV1ByteCount) {
        return error(ReferenceParityV1DecodeErrorCode::truncated_payload, bytes.size());
    }
    if (bytes.size() > kReferenceParityV1ByteCount) {
        return error(ReferenceParityV1DecodeErrorCode::trailing_bytes,
                     kReferenceParityV1ByteCount);
    }

    decoded.frames.reserve(static_cast<std::size_t>(kReferenceParityV1RecordCount));
    double expected_fixture_time = 0.0;
    double previous_filtered_rpm = 0.0;
    const double filter_alpha = kStepSeconds / (100.0 + kStepSeconds);
    for (std::uint64_t record_index = 0; record_index < kReferenceParityV1RecordCount;
         ++record_index) {
        const auto record_offset =
            kReferenceParityV1HeaderBytes +
            static_cast<std::size_t>(record_index) * kReferenceParityV1RecordBytes;
        if (read_u64_le(bytes, record_offset + kRecordSampleIndexOffset) !=
            record_index) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_sample_index,
                         record_offset + kRecordSampleIndexOffset, record_index);
        }
        if (read_u64_le(bytes, record_offset + kRecordStepEndOffset) !=
            record_index + 1U) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_step_end,
                         record_offset + kRecordStepEndOffset, record_index);
        }

        expected_fixture_time += kStepSeconds;
        const double fixture_time =
            read_f64_le(bytes, record_offset + kRecordTimeOffset);
        if (!std::isfinite(fixture_time) ||
            !same_binary64(fixture_time, expected_fixture_time)) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_fixture_time,
                         record_offset + kRecordTimeOffset, record_index);
        }
        const double dt = read_f64_le(bytes, record_offset + kRecordDtOffset);
        if (!same_binary64(dt, kStepSeconds)) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_step_duration,
                         record_offset + kRecordDtOffset, record_index);
        }

        const double rpm = read_f64_le(bytes, record_offset + kRecordRpmOffset);
        if (!std::isfinite(rpm) || rpm < 0.0) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_engine_speed,
                         record_offset + kRecordRpmOffset, record_index);
        }
        const double filtered_rpm =
            read_f64_le(bytes, record_offset + kRecordFilteredRpmOffset);
        if (!std::isfinite(filtered_rpm) || filtered_rpm < 0.0) {
            return error(
                ReferenceParityV1DecodeErrorCode::invalid_filtered_engine_speed,
                record_offset + kRecordFilteredRpmOffset, record_index);
        }
        const double expected_filtered_rpm =
            filter_alpha * previous_filtered_rpm + (1.0 - filter_alpha) * rpm;
        if (!same_binary64(filtered_rpm, expected_filtered_rpm)) {
            return error(
                ReferenceParityV1DecodeErrorCode::filtered_engine_speed_mismatch,
                record_offset + kRecordFilteredRpmOffset, record_index);
        }
        previous_filtered_rpm = filtered_rpm;

        const double crank_angle =
            read_f64_le(bytes, record_offset + kRecordCrankAngleOffset);
        if (!std::isfinite(crank_angle) || crank_angle < 0.0 ||
            crank_angle >= kMaximumWrappedCrankAngle) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_crank_angle,
                         record_offset + kRecordCrankAngleOffset, record_index);
        }

        const auto expected = expected_control(record_index);
        const double requested_throttle =
            read_f64_le(bytes, record_offset + kRecordRequestedThrottleOffset);
        const double resolved_throttle =
            read_f64_le(bytes, record_offset + kRecordResolvedThrottleOffset);
        const std::array<std::uint8_t, 4> actual_flags{
            read_u8(bytes, record_offset + kRecordIgnitionOffset),
            read_u8(bytes, record_offset + kRecordFuelOffset),
            read_u8(bytes, record_offset + kRecordStarterOffset),
            read_u8(bytes, record_offset + kRecordDynoOffset),
        };
        const std::array<std::uint8_t, 4> expected_flags{
            expected.ignition,
            expected.fuel,
            expected.starter,
            expected.dyno,
        };
        if (!same_binary64(requested_throttle, expected.requested_throttle_01)) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                         record_offset + kRecordRequestedThrottleOffset, record_index);
        }
        if (!same_binary64(resolved_throttle, expected.resolved_intake_throttle_01)) {
            return error(ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                         record_offset + kRecordResolvedThrottleOffset, record_index);
        }
        for (std::size_t flag = 0; flag < actual_flags.size(); ++flag) {
            if (actual_flags[flag] != expected_flags[flag]) {
                return error(ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                             record_offset + kRecordIgnitionOffset + flag,
                             record_index);
            }
        }
        if (read_u32_le(bytes, record_offset + kRecordReservedOffset) != 0U) {
            return error(ReferenceParityV1DecodeErrorCode::nonzero_reserved_record,
                         record_offset + kRecordReservedOffset, record_index);
        }

        ReferenceParityV1Frame frame{
            record_index,
            record_index + 1U,
            fixture_time,
            dt,
            rpm,
            filtered_rpm,
            crank_angle,
            {
                requested_throttle,
                resolved_throttle,
                actual_flags[0] != 0U,
                actual_flags[1] != 0U,
                actual_flags[2] != 0U,
                actual_flags[3] != 0U,
            },
            {},
        };
        for (std::size_t cylinder = 0; cylinder < frame.cylinders.size(); ++cylinder) {
            const auto pressure_offset = record_offset + kRecordCylinderValuesOffset +
                                         cylinder * kRecordCylinderValuesBytes;
            const ReferenceParityV1CylinderPressure pressure{
                read_f64_le(bytes, pressure_offset),
                read_f64_le(bytes, pressure_offset + 8),
                read_f64_le(bytes, pressure_offset + 16),
            };
            if (!std::isfinite(pressure.static_pressure_pa_abs) ||
                pressure.static_pressure_pa_abs <= 0.0) {
                return error(
                    ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                    pressure_offset, record_index);
            }
            if (!std::isfinite(pressure.dynamic_pressure_forward_pa) ||
                pressure.dynamic_pressure_forward_pa < 0.0) {
                return error(
                    ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                    pressure_offset + 8U, record_index);
            }
            if (!std::isfinite(pressure.dynamic_pressure_reverse_pa) ||
                pressure.dynamic_pressure_reverse_pa < 0.0) {
                return error(
                    ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                    pressure_offset + 16U, record_index);
            }
            frame.cylinders[cylinder] = pressure;
        }
        decoded.frames.push_back(frame);
    }

    if (engine_sim_offline::contract::sha256(bytes) != kExpectedContentSha256) {
        return error(ReferenceParityV1DecodeErrorCode::content_digest_mismatch, 0);
    }

    return decoded;
}

} // namespace engine_sim_offline::reference
