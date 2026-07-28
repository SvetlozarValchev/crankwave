#include "artifacts/audition_wav_encoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::artifacts {
namespace {

constexpr std::uint64_t kBytesPerPcm24Sample = 3;
constexpr std::uint64_t kRiffHeaderByteCount = 12;
constexpr std::uint64_t kExtensibleFormatChunkByteCount = 48;
constexpr std::uint64_t kChunkHeaderByteCount = 8;
constexpr std::uint64_t kInfoTypeByteCount = 4;

[[nodiscard]] WavEncodingError encoding_error(WavEncodingErrorCode code,
                                              std::string path, std::string message) {
    return {code, std::move(path), std::move(message)};
}

[[nodiscard]] bool checked_add(std::uint64_t left, std::uint64_t right,
                               std::uint64_t &result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t &result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

class PrefixBuilder {
  public:
    explicit PrefixBuilder(std::size_t expected_size) {
        bytes_.reserve(expected_size);
    }

    void append_byte(std::byte value) {
        bytes_.push_back(value);
    }

    void append_ascii(std::string_view value) {
        for (const char character : value) {
            append_byte(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
    }

    void append_u16(std::uint16_t value) {
        append_byte(static_cast<std::byte>(value & UINT16_C(0xff)));
        append_byte(static_cast<std::byte>((value >> 8U) & UINT16_C(0xff)));
    }

    void append_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            append_byte(static_cast<std::byte>((value >> shift) & UINT32_C(0xff)));
        }
    }

    void append_info_chunk(std::string_view id, std::string_view value) {
        const auto payload_size = static_cast<std::uint32_t>(value.size() + 1U);
        append_ascii(id);
        append_u32(payload_size);
        append_ascii(value);
        append_byte(std::byte{0});
        if ((payload_size & 1U) != 0U) {
            append_byte(std::byte{0});
        }
    }

    [[nodiscard]] std::vector<std::byte> finish() && {
        return std::move(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
};

[[nodiscard]] std::vector<std::byte>
make_prefix(std::uint32_t sample_rate, std::uint32_t byte_rate,
            std::uint32_t data_byte_count, std::uint32_t riff_chunk_size,
            std::uint32_t list_payload_size, std::size_t prefix_byte_count,
            const AuditionWaveMetadata &metadata) {
    PrefixBuilder builder{prefix_byte_count};
    builder.append_ascii("RIFF");
    builder.append_u32(riff_chunk_size);
    builder.append_ascii("WAVEfmt ");
    builder.append_u32(40);
    builder.append_u16(0xfffe);
    builder.append_u16(1);
    builder.append_u32(sample_rate);
    builder.append_u32(byte_rate);
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
    builder.append_u32(list_payload_size);
    builder.append_ascii("INFO");
    builder.append_info_chunk("ICMT", metadata.comment);
    builder.append_info_chunk("INAM", metadata.title);
    builder.append_info_chunk("ISFT", metadata.software);
    builder.append_ascii("data");
    builder.append_u32(data_byte_count);
    return std::move(builder).finish();
}

[[nodiscard]] bool valid_metadata_text(std::string_view value) noexcept {
    return !value.empty() && value.find('\0') == std::string_view::npos &&
           value.size() <= kMaximumAuditionMetadataFieldBytes;
}

[[nodiscard]] std::uint64_t info_chunk_byte_count(std::string_view value) noexcept {
    const auto payload_size = static_cast<std::uint64_t>(value.size()) + 1U;
    return kChunkHeaderByteCount + payload_size + (payload_size & 1U);
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

AuditionWaveEncoder::AuditionWaveEncoder(contract::AudioContract audio,
                                         AuditionWaveMetadata metadata,
                                         std::vector<std::byte> prefix,
                                         std::uint64_t data_byte_count,
                                         std::uint64_t expected_byte_count,
                                         std::size_t maximum_chunk_bytes) noexcept
    : contract_(std::move(audio)), metadata_(std::move(metadata)),
      prefix_(std::move(prefix)), data_byte_count_(data_byte_count),
      expected_byte_count_(expected_byte_count),
      maximum_chunk_bytes_(maximum_chunk_bytes) {}

WavEncodingStatus AuditionWaveEncoder::fail(WavEncodingError error) noexcept {
    state_ = State::failed;
    return error;
}

WavEncodingStatus AuditionWaveEncoder::begin(const WavChunkConsumer &consumer) {
    if (state_ != State::ready) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "audition prefix may be emitted once"));
    }

    BoundedEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
    const auto emitted = emitter.append(prefix_) && emitter.finish();
    bytes_emitted_ = emitter.offset();
    if (!emitted) {
        return fail(encoding_error(WavEncodingErrorCode::callback_rejected, "consumer",
                                   "artifact consumer rejected the audition "
                                   "WAVE prefix"));
    }
    if (bytes_emitted_ != prefix_.size()) {
        return fail(encoding_error(WavEncodingErrorCode::size_overflow, "header",
                                   "audition WAVE prefix length changed"));
    }
    state_ = State::begun;
    return std::nullopt;
}

WavEncodingStatus
AuditionWaveEncoder::write_pcm24(std::span<const std::int32_t> samples,
                                 const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "audition samples require an emitted prefix"));
    }
    if (samples.size() > contract_.frame_count - frames_written_) {
        return fail(encoding_error(WavEncodingErrorCode::frame_count_mismatch,
                                   "samples",
                                   "audition input exceeds its declared frame count"));
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (samples[index] < -8'388'608 || samples[index] > 8'388'607) {
            return fail(encoding_error(
                WavEncodingErrorCode::sample_out_of_range,
                "samples[" + std::to_string(index) + "]",
                "audition PCM24 code is outside the signed 24-bit range"));
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
                                   "artifact consumer rejected audition "
                                   "sample bytes"));
    }
    frames_written_ += static_cast<std::uint64_t>(samples.size());
    return std::nullopt;
}

WavEncodingStatus AuditionWaveEncoder::finish(const WavChunkConsumer &consumer) {
    if (state_ != State::begun) {
        return fail(encoding_error(WavEncodingErrorCode::invalid_state, "state",
                                   "only a begun audition stream can finish"));
    }
    if (frames_written_ != contract_.frame_count) {
        return fail(encoding_error(WavEncodingErrorCode::frame_count_mismatch,
                                   "frames_written",
                                   "audition stream did not receive exactly its "
                                   "declared frame count"));
    }
    if ((data_byte_count_ & 1U) != 0U) {
        const std::array padding{std::byte{0}};
        BoundedEmitter emitter{maximum_chunk_bytes_, bytes_emitted_, consumer};
        const auto emitted = emitter.append(padding) && emitter.finish();
        bytes_emitted_ = emitter.offset();
        if (!emitted) {
            return fail(encoding_error(WavEncodingErrorCode::callback_rejected,
                                       "consumer",
                                       "artifact consumer rejected the audition "
                                       "WAVE pad byte"));
        }
    }
    if (bytes_emitted_ != expected_byte_count_) {
        return fail(encoding_error(WavEncodingErrorCode::size_overflow, "byte_count",
                                   "audition stream length differs from its "
                                   "declared RIFF size"));
    }
    state_ = State::finished;
    return std::nullopt;
}

std::uint64_t AuditionWaveEncoder::frames_written() const noexcept {
    return frames_written_;
}

std::uint64_t AuditionWaveEncoder::bytes_emitted() const noexcept {
    return bytes_emitted_;
}

std::uint64_t AuditionWaveEncoder::expected_byte_count() const noexcept {
    return expected_byte_count_;
}

const contract::AudioContract &AuditionWaveEncoder::contract() const noexcept {
    return contract_;
}

const AuditionWaveMetadata &AuditionWaveEncoder::metadata() const noexcept {
    return metadata_;
}

std::span<const std::byte> AuditionWaveEncoder::prefix() const noexcept {
    return prefix_;
}

std::size_t AuditionWaveEncoder::maximum_chunk_bytes() const noexcept {
    return maximum_chunk_bytes_;
}

bool AuditionWaveEncoder::failed() const noexcept {
    return state_ == State::failed;
}

bool AuditionWaveEncoder::finished() const noexcept {
    return state_ == State::finished;
}

AuditionWaveEncoderResult
make_audition_wave_encoder(const contract::AudioContract &audio,
                           AuditionWaveMetadata metadata, WavEncoderOptions options) {
    const auto rate_report = contract::validate(audio.sample_rate);
    if (!rate_report.ok() || audio.frame_count == 0 ||
        !contract::is_valid_semantic_id(audio.channel_layout_id) ||
        !contract::is_valid_semantic_id(audio.sample_encoding_id)) {
        return encoding_error(WavEncodingErrorCode::invalid_contract, "audio",
                              "audition WAVE media contract is structurally invalid");
    }
    if (audio.channel_layout_id != "mono") {
        return encoding_error(WavEncodingErrorCode::unsupported_channel_layout,
                              "audio.channel_layout_id",
                              "audition WAVE output requires mono audio");
    }
    if (audio.sample_encoding_id != "pcm_s24le") {
        return encoding_error(WavEncodingErrorCode::unsupported_encoding,
                              "audio.sample_encoding_id",
                              "audition WAVE output requires pcm_s24le audio");
    }
    if (options.maximum_chunk_bytes == 0 ||
        options.maximum_chunk_bytes > kMaximumArtifactEncoderChunkBytes) {
        return encoding_error(WavEncodingErrorCode::invalid_contract,
                              "options.maximum_chunk_bytes",
                              "artifact chunk size must be in [1, 65536]");
    }
    const std::array metadata_fields{
        std::pair{std::string_view{"metadata.comment"},
                  std::string_view{metadata.comment}},
        std::pair{std::string_view{"metadata.title"}, std::string_view{metadata.title}},
        std::pair{std::string_view{"metadata.software"},
                  std::string_view{metadata.software}},
    };
    for (const auto &[path, value] : metadata_fields) {
        if (!valid_metadata_text(value)) {
            return encoding_error(
                WavEncodingErrorCode::invalid_contract, std::string{path},
                "audition WAVE INFO text must be nonempty, NUL-free, and at most "
                "4096 bytes");
        }
    }
    if (audio.sample_rate.denominator == 0 ||
        audio.sample_rate.numerator % audio.sample_rate.denominator != 0) {
        return encoding_error(
            WavEncodingErrorCode::unrepresentable_rate, "audio.sample_rate",
            "audition WAVE cannot represent a non-integral rational sample rate");
    }
    const auto sample_rate_u64 =
        audio.sample_rate.numerator / audio.sample_rate.denominator;
    std::uint64_t byte_rate_u64 = 0;
    if (sample_rate_u64 == 0 ||
        sample_rate_u64 > std::numeric_limits<std::uint32_t>::max() ||
        !checked_multiply(sample_rate_u64, kBytesPerPcm24Sample, byte_rate_u64) ||
        byte_rate_u64 > std::numeric_limits<std::uint32_t>::max()) {
        return encoding_error(WavEncodingErrorCode::unrepresentable_rate,
                              "audio.sample_rate",
                              "audition WAVE sample or byte rate does not fit uint32");
    }

    std::uint64_t data_byte_count = 0;
    if (!checked_multiply(audio.frame_count, kBytesPerPcm24Sample, data_byte_count) ||
        data_byte_count > std::numeric_limits<std::uint32_t>::max()) {
        return encoding_error(WavEncodingErrorCode::size_overflow, "audio.frame_count",
                              "audition WAVE data chunk exceeds the RIFF limit");
    }

    std::uint64_t list_payload_size = kInfoTypeByteCount;
    for (const auto &[path, value] : metadata_fields) {
        static_cast<void>(path);
        if (!checked_add(list_payload_size, info_chunk_byte_count(value),
                         list_payload_size)) {
            return encoding_error(WavEncodingErrorCode::size_overflow, "metadata",
                                  "audition WAVE INFO list size overflowed");
        }
    }
    if (list_payload_size > std::numeric_limits<std::uint32_t>::max()) {
        return encoding_error(WavEncodingErrorCode::size_overflow, "metadata",
                              "audition WAVE INFO list exceeds the RIFF limit");
    }

    std::uint64_t prefix_byte_count = kRiffHeaderByteCount;
    if (!checked_add(prefix_byte_count, kExtensibleFormatChunkByteCount,
                     prefix_byte_count) ||
        !checked_add(prefix_byte_count, kChunkHeaderByteCount + list_payload_size,
                     prefix_byte_count) ||
        !checked_add(prefix_byte_count, kChunkHeaderByteCount, prefix_byte_count) ||
        prefix_byte_count > std::numeric_limits<std::size_t>::max()) {
        return encoding_error(WavEncodingErrorCode::size_overflow, "metadata",
                              "audition WAVE prefix size overflowed");
    }

    const auto data_padding = data_byte_count & 1U;
    std::uint64_t expected_byte_count = prefix_byte_count;
    if (!checked_add(expected_byte_count, data_byte_count, expected_byte_count) ||
        !checked_add(expected_byte_count, data_padding, expected_byte_count) ||
        expected_byte_count < 8U ||
        expected_byte_count - 8U > std::numeric_limits<std::uint32_t>::max()) {
        return encoding_error(WavEncodingErrorCode::size_overflow, "audio.frame_count",
                              "complete audition WAVE exceeds the RIFF limit");
    }
    const auto riff_chunk_size = expected_byte_count - 8U;
    auto prefix = make_prefix(static_cast<std::uint32_t>(sample_rate_u64),
                              static_cast<std::uint32_t>(byte_rate_u64),
                              static_cast<std::uint32_t>(data_byte_count),
                              static_cast<std::uint32_t>(riff_chunk_size),
                              static_cast<std::uint32_t>(list_payload_size),
                              static_cast<std::size_t>(prefix_byte_count), metadata);
    if (prefix.size() != prefix_byte_count) {
        return encoding_error(WavEncodingErrorCode::size_overflow, "metadata",
                              "audition WAVE prefix construction changed size");
    }
    return AuditionWaveEncoder{
        audio,           std::move(metadata), std::move(prefix),
        data_byte_count, expected_byte_count, options.maximum_chunk_bytes,
    };
}

} // namespace engine_sim_offline::artifacts
