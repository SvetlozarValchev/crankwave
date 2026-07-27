#pragma once

#include "engine_sim_offline/contract/source_matrix.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::size_t kMaximumArtifactEncoderChunkBytes = 64U * 1024U;

enum class WavEncodingErrorCode : std::uint8_t {
    invalid_contract,
    unsupported_encoding,
    unsupported_channel_layout,
    unrepresentable_rate,
    size_overflow,
    invalid_state,
    frame_count_mismatch,
    non_finite_sample,
    sample_out_of_range,
    callback_rejected,
};

struct WavEncodingError {
    WavEncodingErrorCode code = WavEncodingErrorCode::invalid_contract;
    std::string path;
    std::string message;

    friend bool operator==(const WavEncodingError &,
                           const WavEncodingError &) = default;
};

using WavEncodingStatus = std::optional<WavEncodingError>;

// The view is callback-scoped. `byte_offset` is contiguous from zero and the byte
// span never exceeds the configured maximum chunk size. Returning false rejects the
// chunk and permanently fails the encoder so the surrounding artifact transaction can
// abort.
using WavChunkConsumer =
    std::function<bool(std::uint64_t byte_offset, std::span<const std::byte> bytes)>;

struct WavEncoderOptions {
    std::size_t maximum_chunk_bytes = 16U * 1024U;
};

class WavEncoder {
  public:
    [[nodiscard]] WavEncodingStatus begin(const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus
    write_float32_interleaved(std::span<const float> samples,
                              const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus
    write_pcm_s24_interleaved(std::span<const std::int32_t> samples,
                              const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus finish(const WavChunkConsumer &consumer);

    [[nodiscard]] const contract::AudioContract &contract() const noexcept;
    [[nodiscard]] std::uint16_t channel_count() const noexcept;
    [[nodiscard]] std::uint64_t frames_written() const noexcept;
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept;
    [[nodiscard]] std::uint64_t expected_byte_count() const noexcept;
    [[nodiscard]] std::size_t maximum_chunk_bytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

  private:
    enum class State : std::uint8_t {
        ready,
        begun,
        finished,
        failed,
    };

    WavEncoder(contract::AudioContract contract, std::uint16_t channel_count,
               std::uint16_t format_tag, std::uint16_t bits_per_sample,
               std::uint16_t block_align, std::uint32_t sample_rate,
               std::uint32_t byte_rate, std::uint32_t data_byte_count,
               std::uint32_t riff_chunk_size, std::uint64_t expected_byte_count,
               std::size_t maximum_chunk_bytes) noexcept;

    [[nodiscard]] WavEncodingStatus fail(WavEncodingError error) noexcept;

    contract::AudioContract contract_;
    std::uint16_t channel_count_ = 0;
    std::uint16_t format_tag_ = 0;
    std::uint16_t bits_per_sample_ = 0;
    std::uint16_t block_align_ = 0;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t byte_rate_ = 0;
    std::uint32_t data_byte_count_ = 0;
    std::uint32_t riff_chunk_size_ = 0;
    std::uint64_t expected_byte_count_ = 0;
    std::size_t maximum_chunk_bytes_ = 0;
    std::uint64_t frames_written_ = 0;
    std::uint64_t bytes_emitted_ = 0;
    State state_ = State::ready;

    friend std::variant<WavEncoder, WavEncodingError>
    make_wav_encoder(const contract::AudioContract &, WavEncoderOptions);
};

using WavEncoderResult = std::variant<WavEncoder, WavEncodingError>;

// The current data contract defines only the "mono" channel-layout semantics. Other
// semantic IDs are rejected until the contract supplies an unambiguous channel count
// and, where needed, a WAVE speaker mask.
[[nodiscard]] WavEncoderResult make_wav_encoder(const contract::AudioContract &contract,
                                                WavEncoderOptions options = {});

} // namespace engine_sim_offline::artifacts
