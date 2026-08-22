#include "reference/reference_parity_v1_reader.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace crankwave::reference;
using Bytes = std::vector<std::byte>;

constexpr std::size_t kFirstRecordOffset = kReferenceParityV1HeaderBytes;
constexpr std::size_t kRecordTimeOffset = 16;
constexpr std::size_t kRecordDtOffset = 24;
constexpr std::size_t kRecordRpmOffset = 32;
constexpr std::size_t kRecordFilteredRpmOffset = 40;
constexpr std::size_t kRecordCrankAngleOffset = 48;
constexpr std::size_t kRecordRequestedThrottleOffset = 56;
constexpr std::size_t kRecordResolvedThrottleOffset = 64;
constexpr std::size_t kRecordIgnitionOffset = 72;
constexpr std::size_t kRecordReservedOffset = 76;
constexpr std::size_t kRecordCylinderValuesOffset = 80;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t read_u64(const Bytes &bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

void write_u32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] =
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)};
    }
}

void write_u64(Bytes &bytes, std::size_t offset, std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        bytes[offset + shift / 8U] =
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)};
    }
}

const DecodedReferenceParityV1 &
expect_decoded(const ReferenceParityV1DecodeResult &result, const char *message) {
    const auto *decoded = std::get_if<DecodedReferenceParityV1>(&result);
    expect(decoded != nullptr, message);
    return *decoded;
}

void expect_error(std::span<const std::byte> bytes,
                  ReferenceParityV1DecodeErrorCode expected_code,
                  std::size_t expected_offset, const char *message,
                  std::uint64_t expected_record = kReferenceParityV1NoRecordIndex) {
    const auto result = decode_reference_parity_v1(bytes);
    const auto *error = std::get_if<ReferenceParityV1DecodeError>(&result);
    expect(error != nullptr && error->code == expected_code &&
               error->byte_offset == expected_offset &&
               error->record_index == expected_record,
           message);
}

void expect_u32_mutation(
    Bytes &bytes, std::size_t offset, std::uint32_t invalid_value,
    ReferenceParityV1DecodeErrorCode expected_code, const char *message,
    std::uint64_t expected_record = kReferenceParityV1NoRecordIndex) {
    const std::uint32_t original =
        static_cast<std::uint32_t>(read_u64(bytes, offset) & UINT32_MAX);
    write_u32(bytes, offset, invalid_value);
    expect_error(bytes, expected_code, offset, message, expected_record);
    write_u32(bytes, offset, original);
}

void expect_u64_mutation(
    Bytes &bytes, std::size_t offset, std::uint64_t invalid_value,
    ReferenceParityV1DecodeErrorCode expected_code, const char *message,
    std::uint64_t expected_record = kReferenceParityV1NoRecordIndex) {
    const auto original = read_u64(bytes, offset);
    write_u64(bytes, offset, invalid_value);
    expect_error(bytes, expected_code, offset, message, expected_record);
    write_u64(bytes, offset, original);
}

void expect_f64_mutation(
    Bytes &bytes, std::size_t offset, double invalid_value,
    ReferenceParityV1DecodeErrorCode expected_code, const char *message,
    std::uint64_t expected_record = kReferenceParityV1NoRecordIndex) {
    expect_u64_mutation(bytes, offset, bits(invalid_value), expected_code, message,
                        expected_record);
}

Bytes read_exact_fixture(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(input.is_open(), "could not open pinned reference-parity.bin");
    const auto size = input.tellg();
    expect(size == static_cast<std::streamoff>(kReferenceParityV1ByteCount),
           "pinned reference-parity.bin has unexpected size");
    input.seekg(0);
    Bytes bytes(kReferenceParityV1ByteCount);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "pinned reference-parity.bin read was incomplete");
    return bytes;
}

void expect_control(const ReferenceParityV1Frame &frame, std::uint64_t requested_bits,
                    std::uint64_t resolved_bits, bool ignition, bool fuel, bool starter,
                    bool dyno, const char *message) {
    expect(bits(frame.controls.requested_throttle_01) == requested_bits &&
               bits(frame.controls.resolved_intake_throttle_01) == resolved_bits &&
               frame.controls.ignition_enabled == ignition &&
               frame.controls.fuel_enabled == fuel &&
               frame.controls.starter_enabled == starter &&
               frame.controls.dyno_enabled == dyno,
           message);
}

void test_exact_decode_and_owned_evidence(Bytes &bytes) {
    auto result = decode_reference_parity_v1(bytes);
    const auto &decoded =
        expect_decoded(result, "pinned reference-parity.bin failed strict decode");

    expect(decoded.metadata ==
               ReferenceParityV1Metadata{
                   1,
                   kReferenceParityV1SampleRateHz,
                   kReferenceParityV1RecordCount,
                   kReferenceParityV1AudibleStartRecord,
                   kReferenceParityV1IntervalEndExclusive,
                   101325.0,
                   343.0,
                   1600.0,
                   40.0,
                   1.0,
                   0.1,
                   0.1,
                   6.0,
                   2.0,
               },
           "decoded parity metadata changed");
    expect(decoded.frames.size() == kReferenceParityV1RecordCount,
           "decoded parity frame count changed");

    constexpr std::array<std::uint32_t, kReferenceParityV1CylinderCount> firing_ranks{
        0, 4, 2, 5, 1, 3};
    constexpr std::array<std::uint32_t, kReferenceParityV1CylinderCount> routes{
        1, 0, 1, 0, 1, 0};
    constexpr std::array<std::uint64_t, kReferenceParityV1CylinderCount>
        firing_angle_bits{
            UINT64_C(0x0000000000000000), UINT64_C(0x4020c152382d749c),
            UINT64_C(0x4010c152382d749c), UINT64_C(0x4024f1a6c638d1c3),
            UINT64_C(0x4000c152382d749c), UINT64_C(0x401921fb54442eea),
        };
    for (std::size_t index = 0; index < decoded.cylinders.size(); ++index) {
        const auto &descriptor = decoded.cylinders[index];
        expect(
            descriptor.runtime_index == index && descriptor.stable_id == index + 1U &&
                descriptor.firing_rank == firing_ranks[index] &&
                descriptor.route_index == routes[index] &&
                descriptor.delay_samples == 180U &&
                bits(descriptor.firing_angle_rad) == firing_angle_bits[index] &&
                bits(descriptor.header_primary_length_m) == UINT64_C(0) &&
                bits(descriptor.exhaust_system_length_m) ==
                    UINT64_C(0x4018ab47e0b3ffc9) &&
                bits(descriptor.total_audio_length_m) == UINT64_C(0x4018ab47e0b3ffc9) &&
                bits(descriptor.gas_primary_tube_length_m) ==
                    UINT64_C(0x3fe04189374bc6a8) &&
                bits(descriptor.sound_attenuation_linear) ==
                    UINT64_C(0x3ff0000000000000) &&
                bits(descriptor.route_audio_volume_linear) ==
                    (routes[index] == 1U ? UINT64_C(0x3ff0000000000000)
                                         : UINT64_C(0x3fe0000000000000)),
            "decoded frozen cylinder descriptor changed");
    }

    const auto &first = decoded.frames.front();
    expect(first.sample_index == 0 && first.step_end == 1 &&
               bits(first.fixture_time_s) == UINT64_C(0x3f1a36e2eb1c432d) &&
               bits(first.dt_s) == UINT64_C(0x3f1a36e2eb1c432d) &&
               bits(first.engine_speed_rpm) == UINT64_C(0x3fc2920e683e275a) &&
               bits(first.filtered_engine_speed_rpm) == UINT64_C(0x3fc2920d30ae6788) &&
               bits(first.crank_angle_rad) == UINT64_C(0x4000c15304182439),
           "decoded first parity record changed");
    expect(bits(first.cylinders[0].static_pressure_pa_abs) ==
                   UINT64_C(0x40f8bcdbac57b92f) &&
               bits(first.cylinders[0].dynamic_pressure_forward_pa) == UINT64_C(0) &&
               bits(first.cylinders[0].dynamic_pressure_reverse_pa) == UINT64_C(0),
           "decoded first cylinder pressure evidence changed");

    const auto &last = decoded.frames.back();
    expect(last.sample_index == kReferenceParityV1RecordCount - 1U &&
               last.step_end == kReferenceParityV1RecordCount &&
               bits(last.fixture_time_s) == UINT64_C(0x4030ffffffffe31b) &&
               bits(last.dt_s) == UINT64_C(0x3f1a36e2eb1c432d) &&
               bits(last.engine_speed_rpm) == UINT64_C(0x40b95d5555555556) &&
               bits(last.filtered_engine_speed_rpm) == UINT64_C(0x40b95d5555555556) &&
               bits(last.crank_angle_rad) == UINT64_C(0x401de7c41b49585f),
           "decoded last parity record changed");

    constexpr auto requested_018 = UINT64_C(0x3fc70a3d70a3d70a);
    constexpr auto requested_012 = UINT64_C(0x3fbeb851eb851eb8);
    constexpr auto requested_085 = UINT64_C(0x3feb333333333333);
    constexpr auto resolved_09676 = UINT64_C(0x3feef694467381d8);
    constexpr auto resolved_09856 = UINT64_C(0x3fef8a0902de00d2);
    constexpr auto resolved_02775 = UINT64_C(0x3fd1c28f5c28f5c4);
    expect_control(decoded.frames[0], requested_018, resolved_09676, false, true, true,
                   false, "initial control tuple changed");
    expect_control(decoded.frames[7999], requested_018, resolved_09676, false, true,
                   true, false, "direction-acquisition control interval changed");
    expect_control(decoded.frames[8000], requested_018, resolved_09676, false, true,
                   true, true, "direction-lock control edge changed");
    expect_control(decoded.frames[8999], requested_018, resolved_09676, false, true,
                   true, true, "direction-lock control interval changed");
    expect_control(decoded.frames[9000], requested_012, resolved_09856, true, true,
                   false, true, "ignition-handoff control edge changed");
    expect_control(decoded.frames[9999], requested_012, resolved_09856, true, true,
                   false, true, "ignition-handoff control interval changed");
    expect_control(decoded.frames[10000], requested_085, resolved_02775, true, true,
                   false, true, "loaded-pre-roll control edge changed");
    expect_control(last, requested_085, resolved_02775, true, true, false, true,
                   "final audible control tuple changed");

    const auto owned_first_rpm = first.engine_speed_rpm;
    const auto original_first_rpm =
        read_u64(bytes, kFirstRecordOffset + kRecordRpmOffset);
    write_u64(bytes, kFirstRecordOffset + kRecordRpmOffset, 0);
    expect(bits(std::get<DecodedReferenceParityV1>(result)
                    .frames.front()
                    .engine_speed_rpm) == bits(owned_first_rpm),
           "decoded parity evidence retained borrowed input storage");
    write_u64(bytes, kFirstRecordOffset + kRecordRpmOffset, original_first_rpm);
}

void test_header_rejections(Bytes &bytes) {
    expect_error(
        std::span<const std::byte>{bytes}.first(kReferenceParityV1HeaderBytes - 1U),
        ReferenceParityV1DecodeErrorCode::truncated_header,
        kReferenceParityV1HeaderBytes - 1U, "truncated parity header was accepted");

    const auto original_magic = bytes[0];
    bytes[0] = std::byte{'X'};
    expect_error(bytes, ReferenceParityV1DecodeErrorCode::invalid_magic, 0,
                 "invalid parity magic was accepted");
    bytes[0] = original_magic;

    struct HeaderMutation {
        std::size_t offset;
        bool field_is_u64;
        std::uint64_t invalid_value;
        ReferenceParityV1DecodeErrorCode code;
    };
    constexpr HeaderMutation mutations[]{
        {8, false, 2, ReferenceParityV1DecodeErrorCode::unsupported_version},
        {12, false, 607, ReferenceParityV1DecodeErrorCode::invalid_header_size},
        {16, false, 5, ReferenceParityV1DecodeErrorCode::unsupported_cylinder_count},
        {20, false, 3, ReferenceParityV1DecodeErrorCode::unsupported_route_count},
        {24, false, 223, ReferenceParityV1DecodeErrorCode::invalid_record_size},
        {28, false, 192000, ReferenceParityV1DecodeErrorCode::unsupported_sample_rate},
        {32, true, 169999, ReferenceParityV1DecodeErrorCode::invalid_record_count},
        {40, true, 19999, ReferenceParityV1DecodeErrorCode::invalid_audible_start},
        {48, true, 169999, ReferenceParityV1DecodeErrorCode::invalid_interval_end},
    };
    for (const auto &mutation : mutations) {
        if (mutation.field_is_u64) {
            expect_u64_mutation(bytes, mutation.offset, mutation.invalid_value,
                                mutation.code,
                                "invalid exact parity header field was accepted");
        } else {
            expect_u32_mutation(bytes, mutation.offset,
                                static_cast<std::uint32_t>(mutation.invalid_value),
                                mutation.code,
                                "invalid exact parity header field was accepted");
        }
    }

    expect_f64_mutation(bytes, 56, 101324.0,
                        ReferenceParityV1DecodeErrorCode::invalid_reference_parameter,
                        "invalid parity reference parameter was accepted");
    expect_u32_mutation(bytes, 128 + 12, 0,
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_descriptor,
                        "invalid parity cylinder descriptor was accepted");
    expect_u32_mutation(bytes, 128 + 20, 1,
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_descriptor,
                        "nonzero parity descriptor reserved field was accepted");
    expect_u64_mutation(bytes, 128 + 32, UINT64_C(0x8000000000000000),
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_descriptor,
                        "noncanonical signed-zero descriptor field was accepted");
}

void test_payload_and_record_rejections(Bytes &bytes) {
    expect_error(std::span<const std::byte>{bytes}.first(bytes.size() - 1U),
                 ReferenceParityV1DecodeErrorCode::truncated_payload, bytes.size() - 1U,
                 "truncated parity payload was accepted");

    bytes.push_back(std::byte{0});
    expect_error(bytes, ReferenceParityV1DecodeErrorCode::trailing_bytes,
                 kReferenceParityV1ByteCount,
                 "trailing parity payload byte was accepted");
    bytes.pop_back();

    expect_u64_mutation(bytes, kFirstRecordOffset, 1,
                        ReferenceParityV1DecodeErrorCode::invalid_sample_index,
                        "invalid parity sample index was accepted", 0);
    expect_u64_mutation(bytes, kFirstRecordOffset + 8, 2,
                        ReferenceParityV1DecodeErrorCode::invalid_step_end,
                        "invalid parity step_end was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordTimeOffset, 0.0,
                        ReferenceParityV1DecodeErrorCode::invalid_fixture_time,
                        "invalid parity fixture time was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordDtOffset, 0.001,
                        ReferenceParityV1DecodeErrorCode::invalid_step_duration,
                        "invalid parity step duration was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordRpmOffset, -1.0,
                        ReferenceParityV1DecodeErrorCode::invalid_engine_speed,
                        "negative parity engine speed was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordFilteredRpmOffset,
                        std::numeric_limits<double>::quiet_NaN(),
                        ReferenceParityV1DecodeErrorCode::invalid_filtered_engine_speed,
                        "non-finite parity filtered speed was accepted", 0);
    expect_f64_mutation(
        bytes, kFirstRecordOffset + kRecordFilteredRpmOffset, 0.0,
        ReferenceParityV1DecodeErrorCode::filtered_engine_speed_mismatch,
        "invalid parity filtered-speed recurrence was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordCrankAngleOffset,
                        4.0 * 3.14159265359,
                        ReferenceParityV1DecodeErrorCode::invalid_crank_angle,
                        "out-of-domain parity crank angle was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordRequestedThrottleOffset, 0.5,
                        ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                        "invalid parity requested throttle was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordResolvedThrottleOffset, 0.5,
                        ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                        "invalid parity resolved throttle was accepted", 0);

    const auto original_ignition = bytes[kFirstRecordOffset + kRecordIgnitionOffset];
    bytes[kFirstRecordOffset + kRecordIgnitionOffset] = std::byte{2};
    expect_error(bytes, ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                 kFirstRecordOffset + kRecordIgnitionOffset,
                 "invalid parity control flag was accepted", 0);
    bytes[kFirstRecordOffset + kRecordIgnitionOffset] = original_ignition;

    expect_u32_mutation(bytes, kFirstRecordOffset + kRecordReservedOffset, 1,
                        ReferenceParityV1DecodeErrorCode::nonzero_reserved_record,
                        "nonzero parity record reserved field was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordCylinderValuesOffset,
                        std::numeric_limits<double>::infinity(),
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                        "non-finite parity cylinder pressure was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordCylinderValuesOffset + 8U,
                        -1.0,
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                        "negative forward dynamic cylinder pressure was accepted", 0);
    expect_f64_mutation(bytes, kFirstRecordOffset + kRecordCylinderValuesOffset + 16U,
                        -1.0,
                        ReferenceParityV1DecodeErrorCode::invalid_cylinder_pressure,
                        "negative reverse dynamic cylinder pressure was accepted", 0);

    const auto finite_pressure_offset =
        kFirstRecordOffset + kRecordCylinderValuesOffset;
    const auto original_pressure = read_u64(bytes, finite_pressure_offset);
    write_u64(bytes, finite_pressure_offset, bits(101326.0));
    expect_error(bytes, ReferenceParityV1DecodeErrorCode::content_digest_mismatch, 0,
                 "domain-valid parity payload corruption escaped content identity");
    write_u64(bytes, finite_pressure_offset, original_pressure);

    constexpr std::uint64_t boundary_record = 8000;
    const std::size_t boundary_dyno_offset =
        kReferenceParityV1HeaderBytes +
        static_cast<std::size_t>(boundary_record) * kReferenceParityV1RecordBytes +
        kRecordIgnitionOffset + 3U;
    const auto original_dyno = bytes[boundary_dyno_offset];
    bytes[boundary_dyno_offset] = std::byte{0};
    expect_error(bytes, ReferenceParityV1DecodeErrorCode::invalid_control_timeline,
                 boundary_dyno_offset,
                 "invalid parity right-continuous control edge was accepted",
                 boundary_record);
    bytes[boundary_dyno_offset] = original_dyno;
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2,
               "usage: reference_parity_v1_reader_test <reference-parity.bin>");
        auto bytes = read_exact_fixture(argv[1]);
        test_exact_decode_and_owned_evidence(bytes);
        test_header_rejections(bytes);
        test_payload_and_record_rejections(bytes);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
