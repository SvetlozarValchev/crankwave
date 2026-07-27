#include "p18_audition_wav_encoder.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

class PrefixBuilder {
  public:
    constexpr void append_byte(std::byte value) {
        bytes_[offset_++] = value;
    }

    constexpr void append_ascii(std::string_view value) {
        for (const char character : value) {
            append_byte(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
    }

    constexpr void append_u16(std::uint16_t value) {
        append_byte(static_cast<std::byte>(value & UINT16_C(0xff)));
        append_byte(static_cast<std::byte>((value >> 8U) & UINT16_C(0xff)));
    }

    constexpr void append_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            append_byte(static_cast<std::byte>((value >> shift) & UINT32_C(0xff)));
        }
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return offset_;
    }

    [[nodiscard]] constexpr auto finish() const noexcept {
        return bytes_;
    }

  private:
    std::array<std::byte, kP18AuditionWavePrefixByteCount> bytes_{};
    std::size_t offset_ = 0;
};

consteval auto make_prefix() {
    PrefixBuilder builder;
    builder.append_ascii("RIFF");
    builder.append_u32(8'640'294);
    builder.append_ascii("WAVEfmt ");
    builder.append_u32(40);
    builder.append_u16(0xfffe);
    builder.append_u16(1);
    builder.append_u32(192'000);
    builder.append_u32(576'000);
    builder.append_u16(3);
    builder.append_u16(24);
    builder.append_u16(22);
    builder.append_u16(24);
    builder.append_u32(4);
    constexpr std::array pcm_guid{
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00},
        std::byte{0x80}, std::byte{0x00}, std::byte{0x00}, std::byte{0xaa},
        std::byte{0x00}, std::byte{0x38}, std::byte{0x9b}, std::byte{0x71},
    };
    for (const auto byte : pcm_guid) {
        builder.append_byte(byte);
    }
    builder.append_ascii("LIST");
    builder.append_u32(226);
    builder.append_ascii("INFOICMT");
    builder.append_u32(140);
    builder.append_ascii("1500-6500 RPM over 15 s; 85% effort; coherent sum of two "
                         "linear wet/dry exhaust buses; fixed x128 monitoring gain; "
                         "no limiter or compressor");
    builder.append_byte(std::byte{0});
    builder.append_ascii("INAM");
    builder.append_u32(44);
    builder.append_ascii("BMW M52B28 fifth-gear-equivalent dyno sweep");
    builder.append_byte(std::byte{0});
    builder.append_ascii("ISFT");
    builder.append_u32(14);
    builder.append_ascii("Lavf60.16.100");
    builder.append_byte(std::byte{0});
    builder.append_ascii("data");
    builder.append_u32(8'640'000);
    if (builder.size() != kP18AuditionWavePrefixByteCount) {
        throw "P1.8 audition prefix length mismatch";
    }
    return builder.finish();
}

inline constexpr auto kPrefix = make_prefix();

[[nodiscard]] WavEncodingError encoding_error(WavEncodingErrorCode code,
                                              std::string path, std::string message) {
    return {code, std::move(path), std::move(message)};
}

class BoundedEmitter {
  public:
    BoundedEmitter(std::size_t maximum_chunk_bytes, std::uint64_t initial_offset,
                   const WavChunkConsumer &consumer)
        : buffer_(maximum_chunk_bytes), offset_(initial_offset), consumer_(consumer) {}

    bool append(std::span<const std::byte> bytes) {
        while (!bytes.empty()) {
            const auto count = std::min(buffer_.size() - used_, bytes.size());
            std::copy_n(bytes.begin(), count,
                        buffer_.begin() + static_cast<std::ptrdiff_t>(used_));
            used_ += count;
            bytes = bytes.subspan(count);
            if (used_ == buffer_.size() && !flush()) {
                return false;
            }
        }
        return true;
    }

    bool append_pcm24(std::int32_t sample) {
        const auto bits = static_cast<std::uint32_t>(sample);
        const std::array bytes{
            static_cast<std::byte>(bits & UINT32_C(0xff)),
            static_cast<std::byte>((bits >> 8U) & UINT32_C(0xff)),
            static_cast<std::byte>((bits >> 16U) & UINT32_C(0xff)),
        };
        return append(bytes);
    }

    bool finish() {
        return flush();
    }

    [[nodiscard]] std::uint64_t offset() const noexcept {
        return offset_;
    }

  private:
    bool flush() {
        if (used_ == 0) {
            return true;
        }
        bool accepted = false;
        try {
            accepted =
                consumer_(offset_, std::span<const std::byte>{buffer_.data(), used_});
        } catch (...) {
            accepted = false;
        }
        if (!accepted) {
            return false;
        }
        offset_ += used_;
        used_ = 0;
        return true;
    }

    std::vector<std::byte> buffer_;
    std::size_t used_ = 0;
    std::uint64_t offset_ = 0;
    const WavChunkConsumer &consumer_;
};

} // namespace

std::span<const std::byte, kP18AuditionWavePrefixByteCount>
p18_audition_wave_prefix() noexcept {
    return std::span<const std::byte, kP18AuditionWavePrefixByteCount>{kPrefix};
}

P18AuditionWaveEncoder::P18AuditionWaveEncoder(std::size_t maximum_chunk_bytes) noexcept
    : maximum_chunk_bytes_(maximum_chunk_bytes) {}

WavEncodingStatus P18AuditionWaveEncoder::fail(WavEncodingError error) noexcept {
    state_ = State::failed;
    return error;
}

WavEncodingStatus P18AuditionWaveEncoder::begin(const WavChunkConsumer &consumer) {
    if (state_ != State::ready) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "P1.8 audition prefix may be emitted once"));
    }

    BoundedEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    const auto emitted = emitter.append(kPrefix) && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        return fail(encoding_error(WavEncodingErrorCode::callback_rejected, "consumer",
                                   "artifact consumer rejected the P1.8 audition "
                                   "WAVE prefix"));
    }
    if (bytes_emitted_ != kP18AuditionWavePrefixByteCount) {
        return fail(encoding_error(WavEncodingErrorCode::size_overflow, "header",
                                   "P1.8 audition WAVE prefix length changed"));
    }
    state_ = State::begun;
    return std::nullopt;
}

WavEncodingStatus
P18AuditionWaveEncoder::write_pcm24(std::span<const std::int32_t> samples,
                                    const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "P1.8 audition samples require an emitted prefix"));
    }
    if (samples.size() > kP18AuditionWaveFrameCount - frames_written_) {
        return fail(encoding_error(WavEncodingErrorCode::frame_count_mismatch,
                                   "samples",
                                   "P1.8 audition input exceeds 2,880,000 frames"));
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (samples[index] < -8'388'608 || samples[index] > 8'388'607) {
            return fail(encoding_error(
                WavEncodingErrorCode::sample_out_of_range,
                "samples[" + std::to_string(index) + "]",
                "P1.8 audition PCM24 code is outside the signed 24-bit range"));
        }
    }

    BoundedEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    bool emitted = true;
    for (const auto sample : samples) {
        if (!emitter.append_pcm24(sample)) {
            emitted = false;
            break;
        }
    }
    emitted = emitted && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        return fail(encoding_error(WavEncodingErrorCode::callback_rejected, "consumer",
                                   "artifact consumer rejected P1.8 audition "
                                   "sample bytes"));
    }
    frames_written_ += samples.size();
    return std::nullopt;
}

WavEncodingStatus P18AuditionWaveEncoder::finish(const WavChunkConsumer &) {
    if (state_ != State::begun) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "only a begun P1.8 audition stream can finish"));
    }
    if (frames_written_ != kP18AuditionWaveFrameCount) {
        return fail(encoding_error(WavEncodingErrorCode::frame_count_mismatch,
                                   "frames_written",
                                   "P1.8 audition stream requires exactly "
                                   "2,880,000 frames"));
    }
    if (bytes_emitted_ != kP18AuditionWaveByteCount) {
        return fail(encoding_error(WavEncodingErrorCode::size_overflow, "byte_count",
                                   "P1.8 audition stream length differs from its "
                                   "declared RIFF size"));
    }
    state_ = State::finished;
    return std::nullopt;
}

std::uint64_t P18AuditionWaveEncoder::frames_written() const noexcept {
    return frames_written_;
}

std::uint64_t P18AuditionWaveEncoder::bytes_emitted() const noexcept {
    return bytes_emitted_;
}

std::size_t P18AuditionWaveEncoder::maximum_chunk_bytes() const noexcept {
    return maximum_chunk_bytes_;
}

bool P18AuditionWaveEncoder::failed() const noexcept {
    return state_ == State::failed;
}

bool P18AuditionWaveEncoder::finished() const noexcept {
    return state_ == State::finished;
}

P18AuditionWaveEncoderResult make_p18_audition_wave_encoder(WavEncoderOptions options) {
    if (options.maximum_chunk_bytes == 0 ||
        options.maximum_chunk_bytes > kMaximumArtifactEncoderChunkBytes) {
        return encoding_error(WavEncodingErrorCode::invalid_contract,
                              "options.maximum_chunk_bytes",
                              "artifact chunk size must be in [1, 65536]");
    }
    return P18AuditionWaveEncoder{options.maximum_chunk_bytes};
}

} // namespace engine_sim_offline::artifacts
