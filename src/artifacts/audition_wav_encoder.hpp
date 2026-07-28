#pragma once

#include "engine_sim_offline/artifacts/wav_encoder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::uint64_t kAuditionWaveFrameCount = 2'880'000;
inline constexpr std::uint64_t kAuditionWavePrefixByteCount = 302;
inline constexpr std::uint64_t kAuditionWaveDataByteCount = 8'640'000;
inline constexpr std::uint64_t kAuditionWaveByteCount = 8'640'302;

// The immutable, oracle-compatible WAVE_FORMAT_EXTENSIBLE/INFO prefix. Its data-size
// field commits the stream to exactly kAuditionWaveFrameCount mono PCM24 frames.
[[nodiscard]] std::span<const std::byte, kAuditionWavePrefixByteCount>
audition_wave_prefix() noexcept;

class AuditionWaveEncoder {
  public:
    [[nodiscard]] WavEncodingStatus begin(const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus write_pcm24(std::span<const std::int32_t> samples,
                                                const WavChunkConsumer &consumer);
    [[nodiscard]] WavEncodingStatus finish(const WavChunkConsumer &consumer);

    [[nodiscard]] std::uint64_t frames_written() const noexcept;
    [[nodiscard]] std::uint64_t bytes_emitted() const noexcept;
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

    explicit AuditionWaveEncoder(std::size_t maximum_chunk_bytes) noexcept;
    [[nodiscard]] WavEncodingStatus fail(WavEncodingError error) noexcept;

    std::size_t maximum_chunk_bytes_ = 0;
    std::uint64_t frames_written_ = 0;
    std::uint64_t bytes_emitted_ = 0;
    State state_ = State::ready;

    friend std::variant<AuditionWaveEncoder, WavEncodingError>
        make_audition_wave_encoder(WavEncoderOptions);
};

using AuditionWaveEncoderResult =
    std::variant<AuditionWaveEncoder, WavEncodingError>;

[[nodiscard]] AuditionWaveEncoderResult
make_audition_wave_encoder(WavEncoderOptions options = {});

} // namespace engine_sim_offline::artifacts
