#include "presentation/pcm16_ir_decoder.hpp"

#include <array>

namespace engine_sim_offline::presentation {
namespace {

constexpr std::size_t kRiffHeaderSize = 12;
constexpr std::size_t kChunkHeaderSize = 8;
constexpr std::uint16_t kPcmFormatTag = 1;
constexpr std::uint16_t kChannelCount = 1;
constexpr std::uint16_t kBytesPerFrame = 2;
constexpr std::uint16_t kBitsPerSample = 16;
constexpr std::uint32_t kByteRate = kConfiguredIrSampleRateHz * kBytesPerFrame;

using FourCc = std::array<char, 4>;

[[nodiscard]] constexpr FourCc four_cc(char a, char b, char c, char d) noexcept {
    return {a, b, c, d};
}

[[nodiscard]] FourCc read_four_cc(std::span<const std::byte> bytes,
                                  std::size_t offset) noexcept {
    return {
        static_cast<char>(std::to_integer<unsigned char>(bytes[offset])),
        static_cast<char>(std::to_integer<unsigned char>(bytes[offset + 1])),
        static_cast<char>(std::to_integer<unsigned char>(bytes[offset + 2])),
        static_cast<char>(std::to_integer<unsigned char>(bytes[offset + 3])),
    };
}

[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset]) |
        (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] Pcm16IrDecodeError error(Pcm16IrDecodeErrorCode code, std::size_t offset,
                                       FourCc id = {}) noexcept {
    return {code, offset, id};
}

[[nodiscard]] std::int16_t decode_sample(std::span<const std::byte> bytes,
                                         std::size_t offset) noexcept {
    const std::uint16_t raw = read_u16_le(bytes, offset);
    const std::int32_t value = raw <= 0x7fffU ? static_cast<std::int32_t>(raw)
                                              : static_cast<std::int32_t>(raw) - 65536;
    return static_cast<std::int16_t>(value);
}

[[nodiscard]] std::int32_t decode_sample_v2(std::span<const std::byte> bytes,
                                            std::size_t offset,
                                            std::uint16_t bits_per_sample) noexcept {
    if (bits_per_sample == 16U) {
        return decode_sample(bytes, offset);
    }
    const std::uint32_t raw =
        std::to_integer<std::uint32_t>(bytes[offset]) |
        (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U);
    return raw <= 0x7fffffU ? static_cast<std::int32_t>(raw)
                            : static_cast<std::int32_t>(raw) - 16777216;
}

} // namespace

std::size_t meaningful_pcm16_support(std::span<const std::int16_t> samples) noexcept {
    std::size_t support = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const std::int32_t value = samples[index];
        const std::int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > kMeaningfulSupportThreshold) {
            support = index + 1;
        }
    }
    return support;
}

Pcm16IrDecodeResult decode_pcm16_ir_wave(std::span<const std::byte> wave_bytes) {
    if (wave_bytes.size() < kRiffHeaderSize) {
        return error(Pcm16IrDecodeErrorCode::truncated_riff_header, 0);
    }
    if (read_four_cc(wave_bytes, 0) != four_cc('R', 'I', 'F', 'F')) {
        return error(Pcm16IrDecodeErrorCode::invalid_riff_signature, 0,
                     read_four_cc(wave_bytes, 0));
    }
    if (read_four_cc(wave_bytes, 8) != four_cc('W', 'A', 'V', 'E')) {
        return error(Pcm16IrDecodeErrorCode::invalid_wave_signature, 8,
                     read_four_cc(wave_bytes, 8));
    }

    const std::uint64_t declared_size =
        static_cast<std::uint64_t>(read_u32_le(wave_bytes, 4)) + 8U;
    if (declared_size > wave_bytes.size()) {
        return error(Pcm16IrDecodeErrorCode::truncated_riff_payload, 4);
    }
    if (declared_size < wave_bytes.size()) {
        return error(Pcm16IrDecodeErrorCode::trailing_bytes,
                     static_cast<std::size_t>(declared_size));
    }

    bool seen_format = false;
    bool seen_data = false;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::size_t cursor = kRiffHeaderSize;

    while (cursor < wave_bytes.size()) {
        const std::size_t remaining = wave_bytes.size() - cursor;
        if (remaining < kChunkHeaderSize) {
            return error(Pcm16IrDecodeErrorCode::truncated_chunk_header, cursor);
        }

        const FourCc id = read_four_cc(wave_bytes, cursor);
        const std::size_t payload_offset = cursor + kChunkHeaderSize;
        const std::size_t payload_size = read_u32_le(wave_bytes, cursor + 4);
        if (payload_size > wave_bytes.size() - payload_offset) {
            return error(Pcm16IrDecodeErrorCode::truncated_chunk_payload, cursor, id);
        }
        const std::size_t payload_end = payload_offset + payload_size;
        const bool requires_padding = (payload_size & 1U) != 0U;
        if (requires_padding && payload_end == wave_bytes.size()) {
            return error(Pcm16IrDecodeErrorCode::missing_chunk_padding, cursor, id);
        }

        if (id == four_cc('f', 'm', 't', ' ')) {
            if (seen_format) {
                return error(Pcm16IrDecodeErrorCode::duplicate_chunk, cursor, id);
            }
            seen_format = true;
            if (payload_size != 16U) {
                return error(Pcm16IrDecodeErrorCode::unsupported_format_chunk_size,
                             cursor, id);
            }
            if (read_u16_le(wave_bytes, payload_offset) != kPcmFormatTag) {
                return error(Pcm16IrDecodeErrorCode::unsupported_audio_format,
                             payload_offset, id);
            }
            if (read_u16_le(wave_bytes, payload_offset + 2) != kChannelCount) {
                return error(Pcm16IrDecodeErrorCode::unsupported_channel_count,
                             payload_offset + 2, id);
            }
            if (read_u32_le(wave_bytes, payload_offset + 4) !=
                kConfiguredIrSampleRateHz) {
                return error(Pcm16IrDecodeErrorCode::unsupported_sample_rate,
                             payload_offset + 4, id);
            }
            if (read_u32_le(wave_bytes, payload_offset + 8) != kByteRate) {
                return error(Pcm16IrDecodeErrorCode::inconsistent_byte_rate,
                             payload_offset + 8, id);
            }
            if (read_u16_le(wave_bytes, payload_offset + 12) != kBytesPerFrame) {
                return error(Pcm16IrDecodeErrorCode::inconsistent_block_alignment,
                             payload_offset + 12, id);
            }
            if (read_u16_le(wave_bytes, payload_offset + 14) != kBitsPerSample) {
                return error(Pcm16IrDecodeErrorCode::unsupported_bits_per_sample,
                             payload_offset + 14, id);
            }
        } else if (id == four_cc('d', 'a', 't', 'a')) {
            if (seen_data) {
                return error(Pcm16IrDecodeErrorCode::duplicate_chunk, cursor, id);
            }
            seen_data = true;
            if ((payload_size % kBytesPerFrame) != 0U) {
                return error(Pcm16IrDecodeErrorCode::misaligned_data_size, cursor, id);
            }
            if ((payload_size / kBytesPerFrame) > kMaximumConfiguredIrFrameCount) {
                return error(Pcm16IrDecodeErrorCode::data_frame_count_exceeds_limit,
                             cursor, id);
            }
            data_offset = payload_offset;
            data_size = payload_size;
        }

        cursor = payload_end + static_cast<std::size_t>(requires_padding);
    }

    if (!seen_format) {
        return error(Pcm16IrDecodeErrorCode::missing_format_chunk, wave_bytes.size());
    }
    if (!seen_data) {
        return error(Pcm16IrDecodeErrorCode::missing_data_chunk, wave_bytes.size());
    }

    DecodedPcm16Ir decoded;
    const std::size_t sample_count = data_size / kBytesPerFrame;
    decoded.samples.reserve(sample_count);
    for (std::size_t offset = data_offset; offset < data_offset + data_size;
         offset += kBytesPerFrame) {
        decoded.samples.push_back(decode_sample(wave_bytes, offset));
    }
    decoded.meaningful_support_frames = meaningful_pcm16_support(decoded.samples);
    return decoded;
}

PcmIrV2DecodeResult decode_pcm_ir_wave_v2(std::span<const std::byte> wave_bytes) {
    if (wave_bytes.size() < kRiffHeaderSize) {
        return error(Pcm16IrDecodeErrorCode::truncated_riff_header, 0);
    }
    if (read_four_cc(wave_bytes, 0) != four_cc('R', 'I', 'F', 'F')) {
        return error(Pcm16IrDecodeErrorCode::invalid_riff_signature, 0,
                     read_four_cc(wave_bytes, 0));
    }
    if (read_four_cc(wave_bytes, 8) != four_cc('W', 'A', 'V', 'E')) {
        return error(Pcm16IrDecodeErrorCode::invalid_wave_signature, 8,
                     read_four_cc(wave_bytes, 8));
    }

    const std::uint64_t declared_size =
        static_cast<std::uint64_t>(read_u32_le(wave_bytes, 4)) + 8U;
    if (declared_size > wave_bytes.size()) {
        return error(Pcm16IrDecodeErrorCode::truncated_riff_payload, 4);
    }
    if (declared_size < wave_bytes.size()) {
        return error(Pcm16IrDecodeErrorCode::trailing_bytes,
                     static_cast<std::size_t>(declared_size));
    }

    bool seen_format = false;
    bool seen_data = false;
    std::uint16_t bits_per_sample = 0;
    std::uint16_t bytes_per_frame = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::size_t cursor = kRiffHeaderSize;

    while (cursor < wave_bytes.size()) {
        const std::size_t remaining = wave_bytes.size() - cursor;
        if (remaining < kChunkHeaderSize) {
            return error(Pcm16IrDecodeErrorCode::truncated_chunk_header, cursor);
        }

        const FourCc id = read_four_cc(wave_bytes, cursor);
        const std::size_t payload_offset = cursor + kChunkHeaderSize;
        const std::size_t payload_size = read_u32_le(wave_bytes, cursor + 4);
        if (payload_size > wave_bytes.size() - payload_offset) {
            return error(Pcm16IrDecodeErrorCode::truncated_chunk_payload, cursor, id);
        }
        const std::size_t payload_end = payload_offset + payload_size;
        const bool requires_padding = (payload_size & 1U) != 0U;
        if (requires_padding && payload_end == wave_bytes.size()) {
            return error(Pcm16IrDecodeErrorCode::missing_chunk_padding, cursor, id);
        }

        if (id == four_cc('f', 'm', 't', ' ')) {
            if (seen_format) {
                return error(Pcm16IrDecodeErrorCode::duplicate_chunk, cursor, id);
            }
            seen_format = true;
            if (payload_size != 16U) {
                return error(Pcm16IrDecodeErrorCode::unsupported_format_chunk_size,
                             cursor, id);
            }
            if (read_u16_le(wave_bytes, payload_offset) != kPcmFormatTag) {
                return error(Pcm16IrDecodeErrorCode::unsupported_audio_format,
                             payload_offset, id);
            }
            if (read_u16_le(wave_bytes, payload_offset + 2) != kChannelCount) {
                return error(Pcm16IrDecodeErrorCode::unsupported_channel_count,
                             payload_offset + 2, id);
            }
            if (read_u32_le(wave_bytes, payload_offset + 4) !=
                kConfiguredIrSampleRateHz) {
                return error(Pcm16IrDecodeErrorCode::unsupported_sample_rate,
                             payload_offset + 4, id);
            }
            bits_per_sample = read_u16_le(wave_bytes, payload_offset + 14);
            if (bits_per_sample != 16U && bits_per_sample != 24U) {
                return error(Pcm16IrDecodeErrorCode::unsupported_bits_per_sample,
                             payload_offset + 14, id);
            }
            bytes_per_frame = static_cast<std::uint16_t>(bits_per_sample / 8U);
            if (read_u32_le(wave_bytes, payload_offset + 8) !=
                kConfiguredIrSampleRateHz * bytes_per_frame) {
                return error(Pcm16IrDecodeErrorCode::inconsistent_byte_rate,
                             payload_offset + 8, id);
            }
            if (read_u16_le(wave_bytes, payload_offset + 12) != bytes_per_frame) {
                return error(Pcm16IrDecodeErrorCode::inconsistent_block_alignment,
                             payload_offset + 12, id);
            }
        } else if (id == four_cc('d', 'a', 't', 'a')) {
            if (seen_data) {
                return error(Pcm16IrDecodeErrorCode::duplicate_chunk, cursor, id);
            }
            seen_data = true;
            data_offset = payload_offset;
            data_size = payload_size;
        }

        cursor = payload_end + static_cast<std::size_t>(requires_padding);
    }

    if (!seen_format) {
        return error(Pcm16IrDecodeErrorCode::missing_format_chunk, wave_bytes.size());
    }
    if (!seen_data) {
        return error(Pcm16IrDecodeErrorCode::missing_data_chunk, wave_bytes.size());
    }
    if ((data_size % bytes_per_frame) != 0U) {
        return error(Pcm16IrDecodeErrorCode::misaligned_data_size,
                     data_offset - kChunkHeaderSize, four_cc('d', 'a', 't', 'a'));
    }
    const std::size_t frame_count = data_size / bytes_per_frame;
    if (frame_count > kMaximumExtendedConfiguredIrFrameCount) {
        return error(Pcm16IrDecodeErrorCode::data_frame_count_exceeds_limit,
                     data_offset - kChunkHeaderSize, four_cc('d', 'a', 't', 'a'));
    }

    DecodedPcmIrV2 decoded;
    decoded.bits_per_sample = bits_per_sample;
    decoded.samples.reserve(frame_count);
    const std::int32_t threshold =
        kMeaningfulSupportThreshold << (bits_per_sample - 16U);
    for (std::size_t offset = data_offset; offset < data_offset + data_size;
         offset += bytes_per_frame) {
        const std::int32_t sample =
            decode_sample_v2(wave_bytes, offset, bits_per_sample);
        decoded.samples.push_back(sample);
        const std::int64_t widened = sample;
        const std::int64_t magnitude = widened < 0 ? -widened : widened;
        if (magnitude > threshold) {
            decoded.meaningful_support_frames = decoded.samples.size();
        }
    }
    return decoded;
}

} // namespace engine_sim_offline::presentation
