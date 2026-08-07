#include "presentation/pcm16_ir_decoder.hpp"

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
    append_u32(format, kConfiguredIrSampleRateHz);
    append_u32(format, kConfiguredIrSampleRateHz * 2U);
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

Bytes canonical_pcm24_wave(std::span<const std::int32_t> samples) {
    Bytes bytes;
    append_four_cc(bytes, {'R', 'I', 'F', 'F'});
    append_u32(bytes, 0);
    append_four_cc(bytes, {'W', 'A', 'V', 'E'});
    Bytes format;
    append_u16(format, 1);
    append_u16(format, 1);
    append_u32(format, kConfiguredIrSampleRateHz);
    append_u32(format, kConfiguredIrSampleRateHz * 3U);
    append_u16(format, 3);
    append_u16(format, 24);
    append_chunk(bytes, {'f', 'm', 't', ' '}, format);
    Bytes data;
    for (const auto sample : samples) {
        const auto raw = static_cast<std::uint32_t>(sample) & 0x00ffffffU;
        data.push_back(std::byte{static_cast<unsigned char>(raw & 0xffU)});
        data.push_back(std::byte{static_cast<unsigned char>((raw >> 8U) & 0xffU)});
        data.push_back(std::byte{static_cast<unsigned char>((raw >> 16U) & 0xffU)});
    }
    append_chunk(bytes, {'d', 'a', 't', 'a'}, data);
    write_u32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
    return bytes;
}

const DecodedPcm16Ir &expect_decoded(const Pcm16IrDecodeResult &result,
                                     const char *message) {
    const auto *decoded = std::get_if<DecodedPcm16Ir>(&result);
    expect(decoded != nullptr, message);
    return *decoded;
}

void expect_error(const Bytes &bytes, Pcm16IrDecodeErrorCode expected,
                  const char *message) {
    const auto result = decode_pcm16_ir_wave(bytes);
    const auto *error = std::get_if<Pcm16IrDecodeError>(&result);
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
    auto result = decode_pcm16_ir_wave(bytes);
    const auto &decoded =
        expect_decoded(result, "canonical PCM16 WAVE with known metadata was rejected");
    expect(decoded.samples ==
                   std::vector<std::int16_t>(samples.begin(), samples.end()) &&
               decoded.meaningful_support_frames == 7,
           "PCM16 samples or strict support were decoded incorrectly");

    bytes.clear();
    expect(std::get<DecodedPcm16Ir>(result).samples.front() == 0 &&
               std::get<DecodedPcm16Ir>(result).samples.back() == 1,
           "PCM16 decoder retained borrowed input storage");

    constexpr std::array threshold_probe{
        std::int16_t{100},  std::int16_t{-100}, std::int16_t{101},
        std::int16_t{-101}, std::int16_t{0},    std::int16_t{-32768},
    };
    expect(meaningful_pcm16_support(threshold_probe) == threshold_probe.size(),
           "strict support or INT16_MIN magnitude handling changed");
}

void test_container_boundaries_fail_closed() {
    constexpr std::array<std::int16_t, 2> samples{1, -1};
    const auto valid = canonical_wave(samples);

    Bytes truncated_header(valid.begin(), valid.begin() + 11);
    expect_error(truncated_header, Pcm16IrDecodeErrorCode::truncated_riff_header,
                 "truncated RIFF header was accepted");

    auto bad_riff = valid;
    bad_riff[0] = std::byte{'X'};
    expect_error(bad_riff, Pcm16IrDecodeErrorCode::invalid_riff_signature,
                 "non-RIFF input was accepted");
    auto bad_wave = valid;
    bad_wave[8] = std::byte{'X'};
    expect_error(bad_wave, Pcm16IrDecodeErrorCode::invalid_wave_signature,
                 "non-WAVE RIFF was accepted");

    auto truncated_payload = valid;
    write_u32(truncated_payload, 4,
              static_cast<std::uint32_t>(truncated_payload.size() - 7));
    expect_error(truncated_payload, Pcm16IrDecodeErrorCode::truncated_riff_payload,
                 "truncated declared RIFF payload was accepted");
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    expect_error(trailing, Pcm16IrDecodeErrorCode::trailing_bytes,
                 "bytes outside the RIFF boundary were accepted");

    Bytes short_chunk(valid.begin(), valid.begin() + 12);
    short_chunk.push_back(std::byte{'f'});
    write_u32(short_chunk, 4, static_cast<std::uint32_t>(short_chunk.size() - 8));
    expect_error(short_chunk, Pcm16IrDecodeErrorCode::truncated_chunk_header,
                 "truncated chunk header was accepted");

    Bytes oversized_chunk(valid.begin(), valid.begin() + 12);
    append_four_cc(oversized_chunk, {'f', 'm', 't', ' '});
    append_u32(oversized_chunk, 64);
    write_u32(oversized_chunk, 4,
              static_cast<std::uint32_t>(oversized_chunk.size() - 8));
    expect_error(oversized_chunk, Pcm16IrDecodeErrorCode::truncated_chunk_payload,
                 "truncated chunk payload was accepted");

    Bytes missing_padding(valid.begin(), valid.begin() + 36);
    append_four_cc(missing_padding, {'J', 'U', 'N', 'K'});
    append_u32(missing_padding, 1);
    missing_padding.push_back(std::byte{1});
    write_u32(missing_padding, 4,
              static_cast<std::uint32_t>(missing_padding.size() - 8));
    expect_error(missing_padding, Pcm16IrDecodeErrorCode::missing_chunk_padding,
                 "missing final RIFF chunk padding was accepted");
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
    const auto repeated_result = decode_pcm16_ir_wave(repeated);
    const auto &repeated_decoded =
        expect_decoded(repeated_result,
                       "bounded arbitrary or repeated ancillary chunks were rejected");
    expect(repeated_decoded.samples ==
               std::vector<std::int16_t>(samples.begin(), samples.end()),
           "ancillary chunks affected decoded PCM samples");

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
    const auto data_first_result = decode_pcm16_ir_wave(data_first);
    const auto &data_first_decoded = expect_decoded(
        data_first_result,
        "bounded data-before-format layout or nonzero RIFF pad was rejected");
    expect(data_first_decoded.samples ==
               std::vector<std::int16_t>(samples.begin(), samples.end()),
           "chunk order affected decoded PCM samples");
}

void test_required_chunks_and_media_shape_rejections() {
    constexpr std::array<std::int16_t, 2> samples{101, 0};
    const auto valid = canonical_wave(samples);

    auto duplicate_format = valid;
    const auto format = pcm_format_payload();
    append_chunk(duplicate_format, {'f', 'm', 't', ' '}, format);
    write_u32(duplicate_format, 4,
              static_cast<std::uint32_t>(duplicate_format.size() - 8));
    expect_error(duplicate_format, Pcm16IrDecodeErrorCode::duplicate_chunk,
                 "duplicate format chunk was accepted");

    auto duplicate_data = valid;
    const auto data = pcm_payload(samples);
    append_chunk(duplicate_data, {'d', 'a', 't', 'a'}, data);
    write_u32(duplicate_data, 4, static_cast<std::uint32_t>(duplicate_data.size() - 8));
    expect_error(duplicate_data, Pcm16IrDecodeErrorCode::duplicate_chunk,
                 "duplicate data chunk was accepted");

    Bytes missing_format(valid.begin(), valid.begin() + 12);
    append_chunk(missing_format, {'d', 'a', 't', 'a'}, data);
    write_u32(missing_format, 4, static_cast<std::uint32_t>(missing_format.size() - 8));
    expect_error(missing_format, Pcm16IrDecodeErrorCode::missing_format_chunk,
                 "WAVE without a format chunk was accepted");

    Bytes missing_data(valid.begin(), valid.begin() + 36);
    write_u32(missing_data, 4, static_cast<std::uint32_t>(missing_data.size() - 8));
    expect_error(missing_data, Pcm16IrDecodeErrorCode::missing_data_chunk,
                 "WAVE without a data chunk was accepted");

    Bytes extended_format(valid.begin(), valid.begin() + 12);
    auto oversized_format = format;
    oversized_format.push_back(std::byte{0});
    oversized_format.push_back(std::byte{0});
    append_chunk(extended_format, {'f', 'm', 't', ' '}, oversized_format);
    append_chunk(extended_format, {'d', 'a', 't', 'a'}, data);
    write_u32(extended_format, 4,
              static_cast<std::uint32_t>(extended_format.size() - 8));
    expect_error(extended_format, Pcm16IrDecodeErrorCode::unsupported_format_chunk_size,
                 "noncanonical PCM format chunk size was accepted");

    struct FieldMutation {
        std::size_t offset;
        std::uint32_t value;
        bool is_u16;
        Pcm16IrDecodeErrorCode code;
    };
    constexpr std::array mutations{
        FieldMutation{20, 3, true, Pcm16IrDecodeErrorCode::unsupported_audio_format},
        FieldMutation{22, 2, true, Pcm16IrDecodeErrorCode::unsupported_channel_count},
        FieldMutation{24, 48000, false,
                      Pcm16IrDecodeErrorCode::unsupported_sample_rate},
        FieldMutation{28, 1, false, Pcm16IrDecodeErrorCode::inconsistent_byte_rate},
        FieldMutation{32, 4, true,
                      Pcm16IrDecodeErrorCode::inconsistent_block_alignment},
        FieldMutation{34, 24, true,
                      Pcm16IrDecodeErrorCode::unsupported_bits_per_sample},
    };
    for (const auto &mutation : mutations) {
        auto changed = valid;
        if (mutation.is_u16) {
            write_u16(changed, mutation.offset,
                      static_cast<std::uint16_t>(mutation.value));
        } else {
            write_u32(changed, mutation.offset, mutation.value);
        }
        expect_error(changed, mutation.code, "invalid PCM16 media field was accepted");
    }

    auto odd_data = valid;
    write_u32(odd_data, 40, 3);
    expect_error(odd_data, Pcm16IrDecodeErrorCode::misaligned_data_size,
                 "odd-sized PCM16 data was accepted");

    std::vector<std::int16_t> too_many(kMaximumConfiguredIrFrameCount + 1);
    const auto oversized = canonical_wave(too_many);
    expect_error(oversized, Pcm16IrDecodeErrorCode::data_frame_count_exceeds_limit,
                 "oversized PCM16 IR was accepted");
}

void test_v2_preserves_pcm16_and_adds_pcm24_and_long_media() {
    constexpr std::array<std::int16_t, 8> pcm16{
        0, 100, -100, 101, -101, 32767, -32768, 1,
    };
    const auto pcm16_bytes = canonical_wave(pcm16);
    const auto legacy_result = decode_pcm16_ir_wave(pcm16_bytes);
    const auto extended_result = decode_pcm_ir_wave_v2(pcm16_bytes);
    const auto *legacy = std::get_if<DecodedPcm16Ir>(&legacy_result);
    const auto *extended = std::get_if<DecodedPcmIrV2>(&extended_result);
    expect(legacy != nullptr && extended != nullptr &&
               extended->bits_per_sample == 16U &&
               extended->meaningful_support_frames ==
                   legacy->meaningful_support_frames &&
               extended->samples == std::vector<std::int32_t>(
                                        legacy->samples.begin(),
                                        legacy->samples.end()),
           "v2 PCM16 decode changed legacy integer samples or support");

    constexpr std::array<std::int32_t, 9> pcm24{
        0, 25600, -25600, 25601, -25601, 8388607, -8388608, 1, 0,
    };
    const auto pcm24_result = decode_pcm_ir_wave_v2(canonical_pcm24_wave(pcm24));
    const auto *decoded_pcm24 = std::get_if<DecodedPcmIrV2>(&pcm24_result);
    expect(decoded_pcm24 != nullptr && decoded_pcm24->bits_per_sample == 24U &&
               decoded_pcm24->samples ==
                   std::vector<std::int32_t>(pcm24.begin(), pcm24.end()) &&
               decoded_pcm24->meaningful_support_frames == 7U,
           "v2 PCM24 decode changed signed values or scaled support threshold");

    std::vector<std::int16_t> long_pcm16(kMaximumConfiguredIrFrameCount + 1U, 0);
    long_pcm16.back() = 101;
    const auto long_bytes = canonical_wave(long_pcm16);
    const auto legacy_long = decode_pcm16_ir_wave(long_bytes);
    const auto extended_long = decode_pcm_ir_wave_v2(long_bytes);
    expect(std::holds_alternative<Pcm16IrDecodeError>(legacy_long) &&
               std::holds_alternative<DecodedPcmIrV2>(extended_long) &&
               std::get<DecodedPcmIrV2>(extended_long)
                       .meaningful_support_frames == long_pcm16.size(),
           "v2 did not add long PCM16 media without widening v1");
}

void run_tests() {
    test_canonical_decode_support_and_owned_result();
    test_container_boundaries_fail_closed();
    test_ancillary_chunks_and_chunk_order_are_layout_neutral();
    test_required_chunks_and_media_shape_rejections();
    test_v2_preserves_pcm16_and_adds_pcm24_and_long_media();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "PCM16 IR decoder test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
