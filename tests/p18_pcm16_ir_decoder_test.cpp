#include "presentation/p18_pcm16_ir_decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::presentation;
using Bytes = std::vector<std::byte>;
using FourCc = std::array<char, 4>;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void append_four_cc(Bytes &bytes, FourCc value) {
    for (const auto character : value) {
        bytes.push_back(std::byte{static_cast<unsigned char>(character)});
    }
}

void append_u16(Bytes &bytes, std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xffU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)});
}

void append_u32(Bytes &bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)});
    }
}

void write_u16(Bytes &bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = std::byte{static_cast<unsigned char>(value & 0xffU)};
    bytes[offset + 1] = std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)};
}

void write_u32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8] =
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)};
    }
}

void append_chunk(Bytes &bytes, FourCc id, std::span<const std::byte> payload,
                  std::byte padding = std::byte{0}) {
    append_four_cc(bytes, id);
    append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if ((payload.size() & 1U) != 0U) {
        bytes.push_back(padding);
    }
}

Bytes pcm_payload(std::span<const std::int16_t> samples) {
    Bytes payload;
    payload.reserve(samples.size() * 2);
    for (const auto sample : samples) {
        append_u16(payload, static_cast<std::uint16_t>(sample));
    }
    return payload;
}

Bytes pcm_format_payload() {
    Bytes format;
    append_u16(format, 1);
    append_u16(format, 1);
    append_u32(format, kP18ConfiguredIrSampleRateHz);
    append_u32(format, kP18ConfiguredIrSampleRateHz * 2U);
    append_u16(format, 2);
    append_u16(format, 16);
    return format;
}

Bytes canonical_wave(std::span<const std::int16_t> samples,
                     std::span<const FourCc> ancillary = {}) {
    Bytes bytes;
    append_four_cc(bytes, {'R', 'I', 'F', 'F'});
    append_u32(bytes, 0);
    append_four_cc(bytes, {'W', 'A', 'V', 'E'});

    const auto format = pcm_format_payload();
    append_chunk(bytes, {'f', 'm', 't', ' '}, format);

    const std::array odd_payload{std::byte{0x5a}};
    for (const auto id : ancillary) {
        append_chunk(bytes, id, odd_payload);
    }
    const auto data = pcm_payload(samples);
    append_chunk(bytes, {'d', 'a', 't', 'a'}, data);
    write_u32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
    return bytes;
}

const P18DecodedPcm16Ir &expect_decoded(const P18Pcm16IrDecodeResult &result,
                                        const char *message) {
    const auto *decoded = std::get_if<P18DecodedPcm16Ir>(&result);
    expect(decoded != nullptr, message);
    return *decoded;
}

void expect_error(const Bytes &bytes, P18Pcm16IrDecodeErrorCode expected,
                  const char *message) {
    const auto result = decode_p18_pcm16_ir_wave(bytes);
    const auto *error = std::get_if<P18Pcm16IrDecodeError>(&result);
    expect(error != nullptr && error->code == expected, message);
}

void test_canonical_decode_support_and_owned_result() {
    constexpr std::array<std::int16_t, 8> samples{
        0, 100, -100, 101, -101, 32767, -32768, 1,
    };
    constexpr std::array ancillary{
        FourCc{'L', 'I', 'S', 'T'},
        FourCc{'b', 'e', 'x', 't'},
        FourCc{'i', 'X', 'M', 'L'},
        FourCc{'_', 'P', 'M', 'X'},
    };
    auto bytes = canonical_wave(samples, ancillary);
    auto result = decode_p18_pcm16_ir_wave(bytes);
    const auto &decoded = expect_decoded(
        result, "canonical P1.8 PCM16 WAVE with known metadata was rejected");
    expect(decoded.samples ==
                   std::vector<std::int16_t>(samples.begin(), samples.end()) &&
               decoded.meaningful_support_frames == 7,
           "P1.8 PCM16 samples or strict support were decoded incorrectly");

    bytes.clear();
    expect(std::get<P18DecodedPcm16Ir>(result).samples.front() == 0 &&
               std::get<P18DecodedPcm16Ir>(result).samples.back() == 1,
           "P1.8 PCM16 decoder retained borrowed input storage");

    constexpr std::array threshold_probe{
        std::int16_t{100},  std::int16_t{-100}, std::int16_t{101},
        std::int16_t{-101}, std::int16_t{0},    std::int16_t{-32768},
    };
    expect(p18_meaningful_pcm16_support(threshold_probe) == threshold_probe.size(),
           "P1.8 strict support or INT16_MIN magnitude handling changed");
}

void test_container_boundaries_fail_closed() {
    constexpr std::array<std::int16_t, 2> samples{1, -1};
    const auto valid = canonical_wave(samples);

    Bytes truncated_header(valid.begin(), valid.begin() + 11);
    expect_error(truncated_header, P18Pcm16IrDecodeErrorCode::truncated_riff_header,
                 "truncated P1.8 RIFF header was accepted");

    auto bad_riff = valid;
    bad_riff[0] = std::byte{'X'};
    expect_error(bad_riff, P18Pcm16IrDecodeErrorCode::invalid_riff_signature,
                 "non-RIFF P1.8 input was accepted");
    auto bad_wave = valid;
    bad_wave[8] = std::byte{'X'};
    expect_error(bad_wave, P18Pcm16IrDecodeErrorCode::invalid_wave_signature,
                 "non-WAVE P1.8 RIFF was accepted");

    auto truncated_payload = valid;
    write_u32(truncated_payload, 4,
              static_cast<std::uint32_t>(truncated_payload.size() - 7));
    expect_error(truncated_payload, P18Pcm16IrDecodeErrorCode::truncated_riff_payload,
                 "truncated declared P1.8 RIFF payload was accepted");
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    expect_error(trailing, P18Pcm16IrDecodeErrorCode::trailing_bytes,
                 "bytes outside the P1.8 RIFF boundary were accepted");

    Bytes short_chunk(valid.begin(), valid.begin() + 12);
    short_chunk.push_back(std::byte{'f'});
    write_u32(short_chunk, 4, static_cast<std::uint32_t>(short_chunk.size() - 8));
    expect_error(short_chunk, P18Pcm16IrDecodeErrorCode::truncated_chunk_header,
                 "truncated P1.8 chunk header was accepted");

    Bytes oversized_chunk(valid.begin(), valid.begin() + 12);
    append_four_cc(oversized_chunk, {'f', 'm', 't', ' '});
    append_u32(oversized_chunk, 64);
    write_u32(oversized_chunk, 4,
              static_cast<std::uint32_t>(oversized_chunk.size() - 8));
    expect_error(oversized_chunk, P18Pcm16IrDecodeErrorCode::truncated_chunk_payload,
                 "truncated P1.8 chunk payload was accepted");

    Bytes missing_padding(valid.begin(), valid.begin() + 36);
    append_four_cc(missing_padding, {'J', 'U', 'N', 'K'});
    append_u32(missing_padding, 1);
    missing_padding.push_back(std::byte{1});
    write_u32(missing_padding, 4,
              static_cast<std::uint32_t>(missing_padding.size() - 8));
    expect_error(missing_padding, P18Pcm16IrDecodeErrorCode::missing_chunk_padding,
                 "missing final P1.8 RIFF chunk padding was accepted");
}

void test_ancillary_chunks_and_chunk_order_are_layout_neutral() {
    constexpr std::array<std::int16_t, 2> samples{101, 0};
    constexpr std::array repeated_ancillary{
        FourCc{'J', 'U', 'N', 'K'},
        FourCc{'J', 'U', 'N', 'K'},
        FourCc{'L', 'I', 'S', 'T'},
        FourCc{'?', '?', '?', '?'},
    };
    const auto repeated = canonical_wave(samples, repeated_ancillary);
    const auto repeated_result = decode_p18_pcm16_ir_wave(repeated);
    const auto &repeated_decoded = expect_decoded(
        repeated_result,
        "bounded arbitrary or repeated P1.8 ancillary chunks were rejected");
    expect(repeated_decoded.samples ==
               std::vector<std::int16_t>(samples.begin(), samples.end()),
           "ancillary P1.8 chunks affected decoded PCM samples");

    Bytes data_first;
    append_four_cc(data_first, {'R', 'I', 'F', 'F'});
    append_u32(data_first, 0);
    append_four_cc(data_first, {'W', 'A', 'V', 'E'});
    const auto data = pcm_payload(samples);
    append_chunk(data_first, {'d', 'a', 't', 'a'}, data);
    const std::array odd_metadata{std::byte{0x5a}};
    append_chunk(data_first, {'c', 'u', 'e', ' '}, odd_metadata, std::byte{0xff});
    const auto format = pcm_format_payload();
    append_chunk(data_first, {'f', 'm', 't', ' '}, format);
    write_u32(data_first, 4, static_cast<std::uint32_t>(data_first.size() - 8));
    const auto data_first_result = decode_p18_pcm16_ir_wave(data_first);
    const auto &data_first_decoded = expect_decoded(
        data_first_result,
        "bounded P1.8 data-before-format layout or nonzero RIFF pad was rejected");
    expect(data_first_decoded.samples ==
               std::vector<std::int16_t>(samples.begin(), samples.end()),
           "P1.8 chunk order affected decoded PCM samples");
}

void test_required_chunks_and_media_shape_rejections() {
    constexpr std::array<std::int16_t, 2> samples{101, 0};
    const auto valid = canonical_wave(samples);

    auto duplicate_format = valid;
    const auto format = pcm_format_payload();
    append_chunk(duplicate_format, {'f', 'm', 't', ' '}, format);
    write_u32(duplicate_format, 4,
              static_cast<std::uint32_t>(duplicate_format.size() - 8));
    expect_error(duplicate_format, P18Pcm16IrDecodeErrorCode::duplicate_chunk,
                 "duplicate P1.8 format chunk was accepted");

    auto duplicate_data = valid;
    const auto data = pcm_payload(samples);
    append_chunk(duplicate_data, {'d', 'a', 't', 'a'}, data);
    write_u32(duplicate_data, 4, static_cast<std::uint32_t>(duplicate_data.size() - 8));
    expect_error(duplicate_data, P18Pcm16IrDecodeErrorCode::duplicate_chunk,
                 "duplicate P1.8 data chunk was accepted");

    Bytes missing_format(valid.begin(), valid.begin() + 12);
    append_chunk(missing_format, {'d', 'a', 't', 'a'}, data);
    write_u32(missing_format, 4, static_cast<std::uint32_t>(missing_format.size() - 8));
    expect_error(missing_format, P18Pcm16IrDecodeErrorCode::missing_format_chunk,
                 "P1.8 WAVE without a format chunk was accepted");

    Bytes missing_data(valid.begin(), valid.begin() + 36);
    write_u32(missing_data, 4, static_cast<std::uint32_t>(missing_data.size() - 8));
    expect_error(missing_data, P18Pcm16IrDecodeErrorCode::missing_data_chunk,
                 "P1.8 WAVE without a data chunk was accepted");

    Bytes extended_format(valid.begin(), valid.begin() + 12);
    auto oversized_format = format;
    oversized_format.push_back(std::byte{0});
    oversized_format.push_back(std::byte{0});
    append_chunk(extended_format, {'f', 'm', 't', ' '}, oversized_format);
    append_chunk(extended_format, {'d', 'a', 't', 'a'}, data);
    write_u32(extended_format, 4,
              static_cast<std::uint32_t>(extended_format.size() - 8));
    expect_error(extended_format,
                 P18Pcm16IrDecodeErrorCode::unsupported_format_chunk_size,
                 "noncanonical P1.8 PCM format chunk size was accepted");

    struct FieldMutation {
        std::size_t offset;
        std::uint32_t value;
        bool is_u16;
        P18Pcm16IrDecodeErrorCode code;
    };
    constexpr std::array mutations{
        FieldMutation{20, 3, true, P18Pcm16IrDecodeErrorCode::unsupported_audio_format},
        FieldMutation{22, 2, true,
                      P18Pcm16IrDecodeErrorCode::unsupported_channel_count},
        FieldMutation{24, 48000, false,
                      P18Pcm16IrDecodeErrorCode::unsupported_sample_rate},
        FieldMutation{28, 1, false, P18Pcm16IrDecodeErrorCode::inconsistent_byte_rate},
        FieldMutation{32, 4, true,
                      P18Pcm16IrDecodeErrorCode::inconsistent_block_alignment},
        FieldMutation{34, 24, true,
                      P18Pcm16IrDecodeErrorCode::unsupported_bits_per_sample},
    };
    for (const auto &mutation : mutations) {
        auto changed = valid;
        if (mutation.is_u16) {
            write_u16(changed, mutation.offset,
                      static_cast<std::uint16_t>(mutation.value));
        } else {
            write_u32(changed, mutation.offset, mutation.value);
        }
        expect_error(changed, mutation.code,
                     "invalid P1.8 PCM16 media field was accepted");
    }

    auto odd_data = valid;
    write_u32(odd_data, 40, 3);
    expect_error(odd_data, P18Pcm16IrDecodeErrorCode::misaligned_data_size,
                 "odd-sized P1.8 PCM16 data was accepted");

    std::vector<std::int16_t> too_many(kP18MaximumConfiguredIrFrameCount + 1);
    const auto oversized = canonical_wave(too_many);
    expect_error(oversized, P18Pcm16IrDecodeErrorCode::data_frame_count_exceeds_limit,
                 "oversized P1.8 PCM16 IR was accepted");
}

void run_tests() {
    test_canonical_decode_support_and_owned_result();
    test_container_boundaries_fail_closed();
    test_ancillary_chunks_and_chunk_order_are_layout_neutral();
    test_required_chunks_and_media_shape_rejections();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "P1.8 PCM16 IR decoder test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
