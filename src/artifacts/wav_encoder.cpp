#include "engine_sim_offline/artifacts/wav_encoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);

constexpr std::uint16_t kPcmFormatTag = 1;
constexpr std::uint16_t kIeeeFloatFormatTag = 3;
constexpr std::uint64_t kPcmHeaderByteCount = 44;
constexpr std::uint64_t kFloatHeaderByteCount = 58;

WavEncodingError error(WavEncodingErrorCode code, std::string path,
                       std::string message) {
    return {code, std::move(path), std::move(message)};
}

bool checked_multiply(std::uint64_t lhs, std::uint64_t rhs,
                      std::uint64_t &result) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

class ByteEmitter {
  public:
    ByteEmitter(std::size_t maximum_chunk_bytes, std::uint64_t initial_offset,
                const WavChunkConsumer &consumer)
        : buffer_(maximum_chunk_bytes), offset_(initial_offset), consumer_(consumer) {}

    bool append_byte(std::byte value) {
        buffer_[used_++] = value;
        return used_ != buffer_.size() || flush();
    }

    bool append(std::span<const std::byte> bytes) {
        while (!bytes.empty()) {
            const auto available = buffer_.size() - used_;
            const auto count = std::min(available, bytes.size());
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

    bool append_u16(std::uint16_t value) {
        return append_byte(static_cast<std::byte>(value & 0xffU)) &&
               append_byte(static_cast<std::byte>((value >> 8U) & 0xffU));
    }

    bool append_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            if (!append_byte(static_cast<std::byte>((value >> shift) & 0xffU))) {
                return false;
            }
        }
        return true;
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
    std::uint64_t offset_;
    const WavChunkConsumer &consumer_;
};

bool append_fourcc(ByteEmitter &emitter, const std::array<char, 4> &value) {
    for (const auto character : value) {
        if (!emitter.append_byte(
                static_cast<std::byte>(static_cast<unsigned char>(character)))) {
            return false;
        }
    }
    return true;
}

} // namespace

WavEncoder::WavEncoder(contract::AudioContract contract, std::uint16_t channel_count,
                       std::uint16_t format_tag, std::uint16_t bits_per_sample,
                       std::uint16_t block_align, std::uint32_t sample_rate,
                       std::uint32_t byte_rate, std::uint32_t data_byte_count,
                       std::uint32_t riff_chunk_size, std::uint64_t expected_byte_count,
                       std::size_t maximum_chunk_bytes) noexcept
    : contract_(std::move(contract)), channel_count_(channel_count),
      format_tag_(format_tag), bits_per_sample_(bits_per_sample),
      block_align_(block_align), sample_rate_(sample_rate), byte_rate_(byte_rate),
      data_byte_count_(data_byte_count), riff_chunk_size_(riff_chunk_size),
      expected_byte_count_(expected_byte_count),
      maximum_chunk_bytes_(maximum_chunk_bytes) {}

WavEncodingStatus WavEncoder::fail(WavEncodingError value) noexcept {
    state_ = State::failed;
    return value;
}

WavEncodingStatus WavEncoder::begin(const WavChunkConsumer &consumer) {
    if (state_ != State::ready) {
        return fail(error(WavEncodingErrorCode::invalid_state, "state",
                          "WAVE header may be emitted exactly once"));
    }

    ByteEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    const auto common_header = append_fourcc(emitter, {'R', 'I', 'F', 'F'}) &&
                               emitter.append_u32(riff_chunk_size_) &&
                               append_fourcc(emitter, {'W', 'A', 'V', 'E'}) &&
                               append_fourcc(emitter, {'f', 'm', 't', ' '});

    bool emitted = common_header;
    if (format_tag_ == kIeeeFloatFormatTag) {
        emitted =
            emitted && emitter.append_u32(18) && emitter.append_u16(format_tag_) &&
            emitter.append_u16(channel_count_) && emitter.append_u32(sample_rate_) &&
            emitter.append_u32(byte_rate_) && emitter.append_u16(block_align_) &&
            emitter.append_u16(bits_per_sample_) && emitter.append_u16(0) &&
            append_fourcc(emitter, {'f', 'a', 'c', 't'}) && emitter.append_u32(4) &&
            emitter.append_u32(static_cast<std::uint32_t>(contract_.frame_count));
    } else {
        emitted =
            emitted && emitter.append_u32(16) && emitter.append_u16(format_tag_) &&
            emitter.append_u16(channel_count_) && emitter.append_u32(sample_rate_) &&
            emitter.append_u32(byte_rate_) && emitter.append_u16(block_align_) &&
            emitter.append_u16(bits_per_sample_);
    }
    emitted = emitted && append_fourcc(emitter, {'d', 'a', 't', 'a'}) &&
              emitter.append_u32(data_byte_count_) && emitter.finish();
    bytes_emitted_ = emitter.offset();

    if (!emitted) {
        return fail(error(WavEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected the WAVE header"));
    }

    const auto expected_header = format_tag_ == kIeeeFloatFormatTag
                                     ? kFloatHeaderByteCount
                                     : kPcmHeaderByteCount;
    if (bytes_emitted_ != expected_header) {
        return fail(error(WavEncodingErrorCode::size_overflow, "header",
                          "internal WAVE header size did not match its format"));
    }
    state_ = State::begun;
    return std::nullopt;
}

WavEncodingStatus
WavEncoder::write_float32_interleaved(std::span<const float> samples,
                                      const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(WavEncodingErrorCode::invalid_state, "state",
                          "audio frames require an emitted WAVE header"));
    }
    if (format_tag_ != kIeeeFloatFormatTag) {
        return fail(error(WavEncodingErrorCode::unsupported_encoding,
                          "audio.sample_encoding_id",
                          "Float32 input requires a float32le WAVE contract"));
    }
    if (samples.size() % channel_count_ != 0) {
        return fail(
            error(WavEncodingErrorCode::frame_count_mismatch, "samples",
                  "interleaved sample count is not divisible by the channel count"));
    }

    const auto frame_count =
        static_cast<std::uint64_t>(samples.size() / channel_count_);
    std::uint64_t next_frame_count = 0;
    if (!checked_add(frames_written_, frame_count, next_frame_count) ||
        next_frame_count > contract_.frame_count) {
        return fail(error(WavEncodingErrorCode::frame_count_mismatch, "samples",
                          "audio input exceeds the declared frame count"));
    }

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample = samples[index];
        if (!std::isfinite(sample)) {
            return fail(error(WavEncodingErrorCode::non_finite_sample,
                              "samples[" + std::to_string(index) + "]",
                              "WAVE serialization rejects non-finite samples"));
        }
    }

    ByteEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    bool emitted = true;
    for (const auto sample : samples) {
        emitted = emitter.append_u32(std::bit_cast<std::uint32_t>(sample));
        if (!emitted) {
            break;
        }
    }
    emitted = emitted && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        return fail(error(WavEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected WAVE sample bytes"));
    }

    frames_written_ = next_frame_count;
    return std::nullopt;
}

WavEncodingStatus
WavEncoder::write_pcm_s24_interleaved(std::span<const std::int32_t> samples,
                                      const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(WavEncodingErrorCode::invalid_state, "state",
                          "audio frames require an emitted WAVE header"));
    }
    if (format_tag_ != kPcmFormatTag) {
        return fail(error(WavEncodingErrorCode::unsupported_encoding,
                          "audio.sample_encoding_id",
                          "PCM24 input requires a pcm_s24le WAVE contract"));
    }
    if (samples.size() % channel_count_ != 0) {
        return fail(
            error(WavEncodingErrorCode::frame_count_mismatch, "samples",
                  "interleaved sample count is not divisible by the channel count"));
    }

    const auto frame_count =
        static_cast<std::uint64_t>(samples.size() / channel_count_);
    std::uint64_t next_frame_count = 0;
    if (!checked_add(frames_written_, frame_count, next_frame_count) ||
        next_frame_count > contract_.frame_count) {
        return fail(error(WavEncodingErrorCode::frame_count_mismatch, "samples",
                          "audio input exceeds the declared frame count"));
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (samples[index] < -8388608 || samples[index] > 8388607) {
            return fail(error(
                WavEncodingErrorCode::sample_out_of_range,
                "samples[" + std::to_string(index) + "]",
                "PCM24 input must already be quantized into [-8388608, 8388607]"));
        }
    }

    ByteEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    bool emitted = true;
    for (const auto sample : samples) {
        const auto bits = static_cast<std::uint32_t>(sample);
        emitted = emitter.append_byte(static_cast<std::byte>(bits & 0xffU)) &&
                  emitter.append_byte(static_cast<std::byte>((bits >> 8U) & 0xffU)) &&
                  emitter.append_byte(static_cast<std::byte>((bits >> 16U) & 0xffU));
        if (!emitted) {
            break;
        }
    }
    emitted = emitted && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        return fail(error(WavEncodingErrorCode::callback_rejected, "consumer",
                          "artifact consumer rejected WAVE sample bytes"));
    }

    frames_written_ = next_frame_count;
    return std::nullopt;
}

WavEncodingStatus WavEncoder::finish(const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(error(WavEncodingErrorCode::invalid_state, "state",
                          "only a begun WAVE stream can be finished"));
    }
    if (frames_written_ != contract_.frame_count) {
        return fail(
            error(WavEncodingErrorCode::frame_count_mismatch, "frames_written",
                  "WAVE stream did not receive exactly its declared frame count"));
    }

    if ((data_byte_count_ & 1U) != 0U) {
        ByteEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
        const auto emitted = emitter.append_byte(std::byte{0}) && emitter.finish();
        bytes_emitted_ = emitter.offset();
        if (!emitted) {
            return fail(error(WavEncodingErrorCode::callback_rejected, "consumer",
                              "artifact consumer rejected WAVE pad byte"));
        }
    }
    if (bytes_emitted_ != expected_byte_count_) {
        return fail(error(WavEncodingErrorCode::size_overflow, "byte_count",
                          "emitted WAVE length differs from its declared RIFF size"));
    }
    state_ = State::finished;
    return std::nullopt;
}

const contract::AudioContract &WavEncoder::contract() const noexcept {
    return contract_;
}

std::uint16_t WavEncoder::channel_count() const noexcept {
    return channel_count_;
}

std::uint64_t WavEncoder::frames_written() const noexcept {
    return frames_written_;
}

std::uint64_t WavEncoder::bytes_emitted() const noexcept {
    return bytes_emitted_;
}

std::uint64_t WavEncoder::expected_byte_count() const noexcept {
    return expected_byte_count_;
}

std::size_t WavEncoder::maximum_chunk_bytes() const noexcept {
    return maximum_chunk_bytes_;
}

bool WavEncoder::failed() const noexcept {
    return state_ == State::failed;
}

bool WavEncoder::finished() const noexcept {
    return state_ == State::finished;
}

WavEncoderResult make_wav_encoder(const contract::AudioContract &audio,
                                  WavEncoderOptions options) {
    const auto rate_report = contract::validate(audio.sample_rate);
    if (!rate_report.ok() || audio.frame_count == 0 ||
        !contract::is_valid_semantic_id(audio.channel_layout_id) ||
        !contract::is_valid_semantic_id(audio.sample_encoding_id)) {
        return error(WavEncodingErrorCode::invalid_contract, "audio",
                     "WAVE media contract is structurally invalid");
    }
    if (options.maximum_chunk_bytes == 0 ||
        options.maximum_chunk_bytes > kMaximumArtifactEncoderChunkBytes) {
        return error(WavEncodingErrorCode::invalid_contract,
                     "options.maximum_chunk_bytes",
                     "artifact chunk size must be in [1, 65536]");
    }
    if (audio.channel_layout_id != "mono") {
        return error(WavEncodingErrorCode::unsupported_channel_layout,
                     "audio.channel_layout_id",
                     "only mono is resolvable from the current audio contract");
    }
    constexpr std::uint16_t channel_count = 1;

    std::uint16_t format_tag = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint64_t header_byte_count = 0;
    std::uint64_t riff_fixed_byte_count = 0;
    if (audio.sample_encoding_id == "float32le") {
        format_tag = kIeeeFloatFormatTag;
        bits_per_sample = 32;
        header_byte_count = kFloatHeaderByteCount;
        riff_fixed_byte_count = 50;
    } else if (audio.sample_encoding_id == "pcm_s24le") {
        format_tag = kPcmFormatTag;
        bits_per_sample = 24;
        header_byte_count = kPcmHeaderByteCount;
        riff_fixed_byte_count = 36;
    } else {
        return error(WavEncodingErrorCode::unsupported_encoding,
                     "audio.sample_encoding_id",
                     "WAVE encoder supports float32le and pcm_s24le");
    }

    if (audio.sample_rate.denominator == 0 ||
        audio.sample_rate.numerator % audio.sample_rate.denominator != 0) {
        return error(
            WavEncodingErrorCode::unrepresentable_rate, "audio.sample_rate",
            "classic WAVE cannot represent a non-integral rational sample rate");
    }
    const auto sample_rate_u64 =
        audio.sample_rate.numerator / audio.sample_rate.denominator;
    if (sample_rate_u64 == 0 ||
        sample_rate_u64 > std::numeric_limits<std::uint32_t>::max()) {
        return error(WavEncodingErrorCode::unrepresentable_rate, "audio.sample_rate",
                     "WAVE sample rate does not fit its uint32 field");
    }

    const auto bytes_per_sample = bits_per_sample / 8U;
    const auto block_align_u64 =
        static_cast<std::uint64_t>(channel_count) * bytes_per_sample;
    const auto byte_rate_u64 = sample_rate_u64 * block_align_u64;
    if (block_align_u64 > std::numeric_limits<std::uint16_t>::max() ||
        byte_rate_u64 > std::numeric_limits<std::uint32_t>::max()) {
        return error(WavEncodingErrorCode::size_overflow, "audio",
                     "WAVE block alignment or byte rate overflows its field");
    }

    std::uint64_t data_byte_count = 0;
    if (!checked_multiply(audio.frame_count, block_align_u64, data_byte_count) ||
        data_byte_count > std::numeric_limits<std::uint32_t>::max()) {
        return error(WavEncodingErrorCode::size_overflow, "audio.frame_count",
                     "WAVE data chunk exceeds the classic RIFF limit");
    }
    const auto pad_byte_count = data_byte_count & 1U;
    std::uint64_t riff_chunk_size = 0;
    if (!checked_add(riff_fixed_byte_count, data_byte_count, riff_chunk_size) ||
        !checked_add(riff_chunk_size, pad_byte_count, riff_chunk_size) ||
        riff_chunk_size > std::numeric_limits<std::uint32_t>::max()) {
        return error(WavEncodingErrorCode::size_overflow, "audio.frame_count",
                     "complete WAVE payload exceeds the classic RIFF limit");
    }
    std::uint64_t expected_byte_count = 0;
    if (!checked_add(header_byte_count, data_byte_count, expected_byte_count) ||
        !checked_add(expected_byte_count, pad_byte_count, expected_byte_count)) {
        return error(WavEncodingErrorCode::size_overflow, "audio.frame_count",
                     "complete WAVE byte count overflowed uint64");
    }

    return WavEncoder{
        audio,
        channel_count,
        format_tag,
        bits_per_sample,
        static_cast<std::uint16_t>(block_align_u64),
        static_cast<std::uint32_t>(sample_rate_u64),
        static_cast<std::uint32_t>(byte_rate_u64),
        static_cast<std::uint32_t>(data_byte_count),
        static_cast<std::uint32_t>(riff_chunk_size),
        expected_byte_count,
        options.maximum_chunk_bytes,
    };
}

} // namespace engine_sim_offline::artifacts
