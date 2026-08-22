#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace crankwave::presentation {

inline constexpr std::uint32_t kConfiguredIrSampleRateHz = 44100;
inline constexpr std::int32_t kMeaningfulSupportThreshold = 100;
inline constexpr std::size_t kMaximumConfiguredIrFrameCount = 33705;
// The additive v2 media envelope is deliberately wider than every approved
// engine-sim sound-library IR.  The v1 decoder above keeps its original bound;
// callers only enter this envelope through the explicitly identified v2 method.
inline constexpr std::size_t kMaximumExtendedConfiguredIrFrameCount = 131072;

enum class Pcm16IrDecodeErrorCode : std::uint8_t {
    truncated_riff_header,
    invalid_riff_signature,
    invalid_wave_signature,
    truncated_riff_payload,
    trailing_bytes,
    truncated_chunk_header,
    truncated_chunk_payload,
    missing_chunk_padding,
    duplicate_chunk,
    unsupported_format_chunk_size,
    unsupported_audio_format,
    unsupported_channel_count,
    unsupported_sample_rate,
    inconsistent_byte_rate,
    inconsistent_block_alignment,
    unsupported_bits_per_sample,
    misaligned_data_size,
    data_frame_count_exceeds_limit,
    missing_format_chunk,
    missing_data_chunk,
};

struct Pcm16IrDecodeError {
    Pcm16IrDecodeErrorCode code = Pcm16IrDecodeErrorCode::truncated_riff_header;
    std::size_t byte_offset = 0;
    std::array<char, 4> chunk_id{};

    friend bool operator==(const Pcm16IrDecodeError &,
                           const Pcm16IrDecodeError &) = default;
};

struct DecodedPcm16Ir {
    std::vector<std::int16_t> samples;
    std::size_t meaningful_support_frames = 0;

    friend bool operator==(const DecodedPcm16Ir &, const DecodedPcm16Ir &) = default;
};

using Pcm16IrDecodeResult = std::variant<DecodedPcm16Ir, Pcm16IrDecodeError>;

struct DecodedPcmIrV2 {
    // Exact signed integer sample values. PCM16 occupies the signed 16-bit
    // range and PCM24 occupies the signed 24-bit range; no quantization occurs
    // while decoding.
    std::vector<std::int32_t> samples;
    std::uint16_t bits_per_sample = 0;
    std::size_t meaningful_support_frames = 0;

    friend bool operator==(const DecodedPcmIrV2 &,
                           const DecodedPcmIrV2 &) = default;
};

using PcmIrV2DecodeResult = std::variant<DecodedPcmIrV2, Pcm16IrDecodeError>;

// Returns zero when no sample crosses the strict threshold. INT16_MIN is handled
// without signed overflow.
[[nodiscard]] std::size_t
meaningful_pcm16_support(std::span<const std::int16_t> samples) noexcept;

// Fixture-free decoder for the accepted configured-IR media shape. The input
// is borrowed only for this call. It requires exactly one 16-byte PCM `fmt ` chunk
// and one PCM16 mono 44.1 kHz `data` chunk. Structurally bounded ancillary chunks
// are skipped without overfitting their IDs, order, repetition, or pad-byte value.
// Asset identity, expected frame count, and expected support are deliberately checked
// by the later isolated integration boundary, not by this byte decoder.
[[nodiscard]] Pcm16IrDecodeResult
decode_pcm16_ir_wave(std::span<const std::byte> wave_bytes);

// Decoder for the versioned extended configured-IR path. Container rules stay
// identical to v1, while the admitted data shape is mono PCM16 or PCM24 at
// 44.1 kHz and up to kMaximumExtendedConfiguredIrFrameCount frames. Meaningful
// support uses the v1 threshold scaled exactly into the source integer domain
// (100 for PCM16, 25,600 for PCM24).
[[nodiscard]] PcmIrV2DecodeResult
decode_pcm_ir_wave_v2(std::span<const std::byte> wave_bytes);

} // namespace crankwave::presentation
