#pragma once

#include "crankwave/artifacts/wav_encoder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace crankwave::artifacts {

inline constexpr std::size_t kMaximumAuditionMetadataFieldBytes = 4096;

// Per-artifact RIFF INFO annotations. Values are emitted as NUL-terminated ICMT,
// INAM, and ISFT payloads in this order. They are retained as exact render evidence,
// but only a later admitted job compiler may establish their semantic truth. Embedded
// NULs, empty values, and values above the fixed allocation bound are rejected.
struct AuditionWaveMetadata {
    std::string comment;
    std::string title;
    std::string software;

    friend bool operator==(const AuditionWaveMetadata &,
                           const AuditionWaveMetadata &) = default;
};

class AuditionWaveEncoder {
  public:
    [[nodiscard]] WavEncodingStatus begin(const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus write_pcm24(std::span<const std::int32_t> samples,
                                                const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus finish(const WavChunkConsumer &consumer);

    [[nodiscard]] std::uint64_t frames_written() const noexcept;
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept;
    [[nodiscard]] std::uint64_t expected_byte_count() const noexcept;
    [[nodiscard]] const contract::AudioContract &contract() const noexcept;
    [[nodiscard]] const AuditionWaveMetadata &metadata() const noexcept;
    [[nodiscard]] std::span<const std::byte> prefix() const noexcept;
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

    AuditionWaveEncoder(contract::AudioContract audio, AuditionWaveMetadata metadata,
                        std::vector<std::byte> prefix, std::uint64_t data_byte_count,
                        std::uint64_t expected_byte_count,
                        std::size_t maximum_chunk_bytes) noexcept;
    [[nodiscard]] WavEncodingStatus fail(WavEncodingError error) noexcept;

    contract::AudioContract contract_;
    AuditionWaveMetadata metadata_;
    std::vector<std::byte> prefix_;
    std::uint64_t data_byte_count_ = 0;
    std::uint64_t expected_byte_count_ = 0;
    std::size_t maximum_chunk_bytes_ = 0;
    std::uint64_t frames_written_ = 0;
    std::uint64_t bytes_emitted_ = 0;
    State state_ = State::ready;

    friend std::variant<AuditionWaveEncoder, WavEncodingError>
    make_audition_wave_encoder(const contract::AudioContract &, AuditionWaveMetadata,
                               WavEncoderOptions);
};

using AuditionWaveEncoderResult = std::variant<AuditionWaveEncoder, WavEncodingError>;

[[nodiscard]] AuditionWaveEncoderResult
make_audition_wave_encoder(const contract::AudioContract &audio,
                           AuditionWaveMetadata metadata,
                           WavEncoderOptions options = {});

} // namespace crankwave::artifacts
