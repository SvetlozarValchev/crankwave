#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace crankwave::presentation {

// Immutable frame-domain settings for one audition render. Validation happens once
// at construction so every subsequent frame and block call can share the same exact
// horizon, fade geometry, and Float32 listening volume.
class MasteringSettings final {
  public:
    MasteringSettings(std::uint64_t audible_frame_count,
                      std::uint64_t fade_in_frame_count,
                      std::uint64_t fade_out_frame_count, float volume_linear);

    [[nodiscard]] std::uint64_t audible_frame_count() const noexcept;
    [[nodiscard]] std::uint64_t fade_in_frame_count() const noexcept;
    [[nodiscard]] std::uint64_t fade_out_frame_count() const noexcept;
    [[nodiscard]] float volume_linear() const noexcept;

    friend bool operator==(const MasteringSettings &,
                           const MasteringSettings &) = default;

  private:
    std::uint64_t audible_frame_count_ = 0;
    std::uint64_t fade_in_frame_count_ = 0;
    std::uint64_t fade_out_frame_count_ = 0;
    float volume_linear_ = 0.0F;
};

struct Pcm24Quantization {
    std::int32_t s32 = 0;
    std::int32_t pcm24 = 0;
    bool saturated = false;

    friend bool operator==(const Pcm24Quantization &,
                           const Pcm24Quantization &) = default;
};

// Returns sin(((binary64(k) / binary64(fade_frame_count)) * pi) / 2) in the
// normative operation order. fade_frame_count must be positive and `k` must be
// in [0, fade_frame_count].
[[nodiscard]] double quarter_sine_gain(std::uint64_t k, std::uint64_t fade_frame_count);

// Returns the frame-indexed fade gain for the settings' audible interval.
[[nodiscard]] double audition_fade_gain(std::uint64_t frame_index,
                                        const MasteringSettings &settings);

// Quantizes one finite faded Float32 sample using the exact Float32-to-S32 and
// floor(S32 / 256) path. Saturation is reported, not treated as a fallback.
[[nodiscard]] Pcm24Quantization quantize_pcm24(float faded_sample);

// Serializes the low 24 bits of a validated PCM24 code. Codes outside the signed
// 24-bit range are rejected.
[[nodiscard]] std::array<std::byte, 3> serialize_pcm24le(std::int32_t pcm24_sample);

} // namespace crankwave::presentation
