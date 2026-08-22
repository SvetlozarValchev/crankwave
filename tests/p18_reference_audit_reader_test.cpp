#include "reference/p18_reference_audit_reader.hpp"

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

constexpr std::size_t kFirstRecordOffset = kP18ReferenceAuditHeaderBytes;
constexpr std::size_t kRecordBusOffset = 112;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
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

void write_f64(Bytes &bytes, std::size_t offset, double value) {
    write_u64(bytes, offset, std::bit_cast<std::uint64_t>(value));
}

Bytes canonical_synthetic_audit() {
    Bytes bytes(kP18ReferenceAuditByteCount);
    constexpr char magic[] = "ESOAUD01";
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = std::byte{static_cast<unsigned char>(magic[index])};
    }
    write_u32(bytes, 8, 1);
    write_u32(bytes, 12, static_cast<std::uint32_t>(kP18ReferenceAuditHeaderBytes));
    write_u32(bytes, 16, kP18ReferenceAuditCylinderCount);
    write_u32(bytes, 20, kP18ReferenceAuditBusCount);
    write_u32(bytes, 24, static_cast<std::uint32_t>(kP18ReferenceAuditRecordBytes));
    write_u32(bytes, 28, kP18ReferenceAuditSampleRateHz);
    write_u64(bytes, 32, kP18ReferenceAuditRecordCount);
    write_u64(bytes, 40, kP18ReferenceAuditIntervalStart);
    write_u64(bytes, 48, kP18ReferenceAuditIntervalEndExclusive);
    write_u64(bytes, 56, 0);

    for (std::uint64_t record_index = 0; record_index < kP18ReferenceAuditRecordCount;
         ++record_index) {
        const std::size_t offset =
            kP18ReferenceAuditHeaderBytes +
            static_cast<std::size_t>(record_index) * kP18ReferenceAuditRecordBytes;
        write_u64(bytes, offset, record_index);
        write_u64(bytes, offset + 8, record_index + 1U);
    }

    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset, 1.25);
    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset + 8, -2.5);
    const std::size_t last_record_offset =
        kP18ReferenceAuditHeaderBytes +
        static_cast<std::size_t>(kP18ReferenceAuditRecordCount - 1U) *
            kP18ReferenceAuditRecordBytes;
    write_f64(bytes, last_record_offset + kRecordBusOffset, 3.75);
    write_f64(bytes, last_record_offset + kRecordBusOffset + 8, -4.5);
    return bytes;
}

const P18DecodedReferenceAudit &
expect_decoded(const P18ReferenceAuditDecodeResult &result, const char *message) {
    const auto *decoded = std::get_if<P18DecodedReferenceAudit>(&result);
    expect(decoded != nullptr, message);
    return *decoded;
}

void expect_error(const Bytes &bytes, P18ReferenceAuditDecodeErrorCode expected_code,
                  std::size_t expected_offset, const char *message,
                  std::uint64_t expected_record = kP18ReferenceAuditNoRecordIndex) {
    const auto result = decode_p18_reference_audit(bytes);
    const auto *error = std::get_if<P18ReferenceAuditDecodeError>(&result);
    expect(error != nullptr && error->code == expected_code &&
               error->byte_offset == expected_offset &&
               error->record_index == expected_record,
           message);
}

void test_exact_decode_and_owned_bus_lanes(Bytes &bytes) {
    auto result = decode_p18_reference_audit(bytes);
    const auto &decoded = expect_decoded(result, "canonical audit bytes were rejected");
    expect(decoded.frames.size() == kP18ReferenceAuditRecordCount,
           "canonical audit frame count changed");
    expect(decoded.frames.front().pre_dsp_buses[0] == 1.25 &&
               decoded.frames.front().pre_dsp_buses[1] == -2.5 &&
               decoded.frames.back().pre_dsp_buses[0] == 3.75 &&
               decoded.frames.back().pre_dsp_buses[1] == -4.5,
           "audit decoder did not retain only the ordered pre-DSP buses");

    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset, 0.0);
    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset + 8, 0.0);
    expect(std::get<P18DecodedReferenceAudit>(result).frames.front().pre_dsp_buses ==
               std::array{1.25, -2.5},
           "audit decoder retained borrowed input storage");
    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset, 1.25);
    write_f64(bytes, kFirstRecordOffset + kRecordBusOffset + 8, -2.5);
}

void test_header_rejections(Bytes &bytes) {
    Bytes short_header(bytes.begin(),
                       bytes.begin() + kP18ReferenceAuditHeaderBytes - 1U);
    expect_error(short_header, P18ReferenceAuditDecodeErrorCode::truncated_header,
                 short_header.size(), "truncated audit header was accepted");

    const auto original_magic = bytes[0];
    bytes[0] = std::byte{'X'};
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::invalid_magic, 0,
                 "invalid audit magic was accepted");
    bytes[0] = original_magic;

    struct HeaderMutation {
        std::size_t offset;
        bool field_is_u64;
        std::uint64_t invalid_value;
        std::uint64_t valid_value;
        P18ReferenceAuditDecodeErrorCode code;
    };
    constexpr HeaderMutation mutations[]{
        {8, false, 2, 1, P18ReferenceAuditDecodeErrorCode::unsupported_version},
        {12, false, 63, kP18ReferenceAuditHeaderBytes,
         P18ReferenceAuditDecodeErrorCode::invalid_header_size},
        {16, false, 5, kP18ReferenceAuditCylinderCount,
         P18ReferenceAuditDecodeErrorCode::unsupported_cylinder_count},
        {20, false, 3, kP18ReferenceAuditBusCount,
         P18ReferenceAuditDecodeErrorCode::unsupported_bus_count},
        {24, false, 120, kP18ReferenceAuditRecordBytes,
         P18ReferenceAuditDecodeErrorCode::invalid_record_size},
        {28, false, 192000, kP18ReferenceAuditSampleRateHz,
         P18ReferenceAuditDecodeErrorCode::unsupported_sample_rate},
        {32, true, 169999, kP18ReferenceAuditRecordCount,
         P18ReferenceAuditDecodeErrorCode::invalid_record_count},
        {40, true, 1, kP18ReferenceAuditIntervalStart,
         P18ReferenceAuditDecodeErrorCode::invalid_interval_start},
        {48, true, 169999, kP18ReferenceAuditIntervalEndExclusive,
         P18ReferenceAuditDecodeErrorCode::invalid_interval_end},
        {56, true, 1, 0, P18ReferenceAuditDecodeErrorCode::nonzero_reserved_header},
    };

    for (const auto &mutation : mutations) {
        if (mutation.field_is_u64) {
            write_u64(bytes, mutation.offset, mutation.invalid_value);
        } else {
            write_u32(bytes, mutation.offset,
                      static_cast<std::uint32_t>(mutation.invalid_value));
        }
        expect_error(bytes, mutation.code, mutation.offset,
                     "invalid exact audit header field was accepted");
        if (mutation.field_is_u64) {
            write_u64(bytes, mutation.offset, mutation.valid_value);
        } else {
            write_u32(bytes, mutation.offset,
                      static_cast<std::uint32_t>(mutation.valid_value));
        }
    }
}

void test_payload_and_record_rejections(Bytes &bytes) {
    const std::byte final_byte = bytes.back();
    bytes.pop_back();
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::truncated_payload,
                 bytes.size(), "truncated exact audit payload was accepted");
    bytes.push_back(final_byte);

    bytes.push_back(std::byte{0});
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::trailing_bytes,
                 kP18ReferenceAuditByteCount,
                 "trailing bytes after exact audit payload were accepted");
    bytes.pop_back();

    write_u64(bytes, kFirstRecordOffset, 1);
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::invalid_sample_index,
                 kFirstRecordOffset, "invalid audit sample index was accepted", 0);
    write_u64(bytes, kFirstRecordOffset, 0);

    write_u64(bytes, kFirstRecordOffset + 8, 2);
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::invalid_step_end,
                 kFirstRecordOffset + 8, "invalid audit step_end was accepted", 0);
    write_u64(bytes, kFirstRecordOffset + 8, 1);

    constexpr std::size_t intermediate_value_offset = kFirstRecordOffset + 16;
    write_f64(bytes, intermediate_value_offset,
              std::numeric_limits<double>::infinity());
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::non_finite_record_value,
                 intermediate_value_offset,
                 "non-finite intermediate audit value was accepted", 0);
    write_f64(bytes, intermediate_value_offset, 0.0);

    const std::size_t bus_value_offset = kFirstRecordOffset + kRecordBusOffset;
    write_f64(bytes, bus_value_offset, std::numeric_limits<double>::quiet_NaN());
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::non_finite_record_value,
                 bus_value_offset, "non-finite audit bus was accepted", 0);
    write_f64(bytes, bus_value_offset, 1.25);

    const std::uint64_t last_index = kP18ReferenceAuditRecordCount - 1U;
    const std::size_t last_step_offset =
        kP18ReferenceAuditHeaderBytes +
        static_cast<std::size_t>(last_index) * kP18ReferenceAuditRecordBytes + 8U;
    write_u64(bytes, last_step_offset, last_index);
    expect_error(bytes, P18ReferenceAuditDecodeErrorCode::invalid_step_end,
                 last_step_offset, "late invalid audit step_end was not scanned",
                 last_index);
    write_u64(bytes, last_step_offset, last_index + 1U);
}

Bytes read_exact_fixture(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(input.is_open(), "could not open pinned reference-audit.bin");
    const auto size = input.tellg();
    expect(size == static_cast<std::streamoff>(kP18ReferenceAuditByteCount),
           "pinned reference-audit.bin has unexpected size");
    input.seekg(0);
    Bytes bytes(kP18ReferenceAuditByteCount);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "pinned reference-audit.bin read was incomplete");
    return bytes;
}

void test_pinned_fixture(const std::string &path) {
    const auto bytes = read_exact_fixture(path);
    const auto result = decode_p18_reference_audit(bytes);
    const auto &decoded =
        expect_decoded(result, "pinned reference-audit.bin failed strict decode");
    expect(decoded.frames.size() == kP18ReferenceAuditRecordCount,
           "pinned reference-audit.bin decoded an unexpected frame count");
    expect(std::bit_cast<std::uint64_t>(decoded.frames.front().pre_dsp_buses[0]) ==
                   UINT64_C(0) &&
               std::bit_cast<std::uint64_t>(decoded.frames.front().pre_dsp_buses[1]) ==
                   UINT64_C(0) &&
               std::bit_cast<std::uint64_t>(decoded.frames.back().pre_dsp_buses[0]) ==
                   UINT64_C(0xc0ab9eac34b14179) &&
               std::bit_cast<std::uint64_t>(decoded.frames.back().pre_dsp_buses[1]) ==
                   UINT64_C(0x411f21959890664d),
           "pinned reference-audit.bin bus offsets or route order changed");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2,
               "usage: p18_reference_audit_reader_test <reference-audit.bin>");
        auto synthetic = canonical_synthetic_audit();
        test_exact_decode_and_owned_bus_lanes(synthetic);
        test_header_rejections(synthetic);
        test_payload_and_record_rejections(synthetic);
        test_pinned_fixture(argv[1]);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
