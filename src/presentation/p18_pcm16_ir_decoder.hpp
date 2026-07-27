#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr std::uint32_t kP18ConfiguredIrSampleRateHz = 44100;
inline constexpr std::int32_t kP18MeaningfulSupportThreshold = 100;
inline constexpr std::size_t kP18MaximumConfiguredIrFrameCount = 33705;

enum class P18Pcm16IrDecodeErrorCode : std::uint8_t {
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

struct P18Pcm16IrDecodeError {
    P18Pcm16IrDecodeErrorCode code = P18Pcm16IrDecodeErrorCode::truncated_riff_header;
    std::size_t byte_offset = 0;
    std::array<char, 4> chunk_id{};

    friend bool operator==(const P18Pcm16IrDecodeError &,
                           const P18Pcm16IrDecodeError &) = default;
};

struct P18DecodedPcm16Ir {
    std::vector<std::int16_t> samples;
    std::size_t meaningful_support_frames = 0;

    friend bool operator==(const P18DecodedPcm16Ir &,
                           const P18DecodedPcm16Ir &) = default;
};

using P18Pcm16IrDecodeResult = std::variant<P18DecodedPcm16Ir, P18Pcm16IrDecodeError>;

// Returns zero when no sample crosses the strict threshold. INT16_MIN is handled
// without signed overflow.
[[nodiscard]] std::size_t
p18_meaningful_pcm16_support(std::span<const std::int16_t> samples) noexcept;

// Fixture-free decoder for the canonical P1.8 configured-IR media shape. The input
// is borrowed only for this call. It requires exactly one 16-byte PCM `fmt ` chunk
// and one PCM16 mono 44.1 kHz `data` chunk. Structurally bounded ancillary chunks
// are skipped without overfitting their IDs, order, repetition, or pad-byte value.
// Asset identity, expected frame count, and expected support are deliberately checked
// by the later isolated integration boundary, not by this byte decoder.
[[nodiscard]] P18Pcm16IrDecodeResult
decode_p18_pcm16_ir_wave(std::span<const std::byte> wave_bytes);

} // namespace engine_sim_offline::presentation
