#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

// Immutable frame-domain settings for one audition render. Validation happens once
// at construction so every subsequent frame and block call can share the same exact
// horizon, fade geometry, and Float32 monitoring gain.
class MasteringSettings final {
  public:
    MasteringSettings(std::uint64_t audible_frame_count,
                      std::uint64_t fade_in_frame_count,
                      std::uint64_t fade_out_frame_count, float monitoring_gain_linear);

    [[nodiscard]] std::uint64_t audible_frame_count() const noexcept;
    [[nodiscard]] std::uint64_t fade_in_frame_count() const noexcept;
    [[nodiscard]] std::uint64_t fade_out_frame_count() const noexcept;
    [[nodiscard]] float monitoring_gain_linear() const noexcept;

    friend bool operator==(const MasteringSettings &,
                           const MasteringSettings &) = default;

  private:
    std::uint64_t audible_frame_count_ = 0;
    std::uint64_t fade_in_frame_count_ = 0;
    std::uint64_t fade_out_frame_count_ = 0;
    float monitoring_gain_linear_ = 0.0F;
};

struct Pcm24Quantization {
    std::int32_t s32 = 0;
    std::int32_t pcm24 = 0;
    bool saturated = false;

    friend bool operator==(const Pcm24Quantization &,
                           const Pcm24Quantization &) = default;
};

// Every arithmetic boundary required to audit the accepted master. Keeping
// both integer representations here prevents an integration from silently replacing
// the observed Float32 -> S32 -> PCM24 path with direct 24-bit quantization.
struct MasteredFrame {
    float raw = 0.0F;
    float monitor = 0.0F;
    double fade_gain = 0.0;
    float faded = 0.0F;
    std::int32_t s32 = 0;
    std::int32_t pcm24 = 0;
    bool saturated = false;

    friend bool operator==(const MasteredFrame &, const MasteredFrame &) = default;
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

// Route addition is one Float32 operation in route-0, route-1 order. Every result is
// finite or the call fails before returning a frame.
[[nodiscard]] MasteredFrame master_frame(float route_0_selected, float route_1_selected,
                                         std::uint64_t frame_index,
                                         const MasteringSettings &settings);

// Exact chunk-independent counterpart to master_frame. Span lengths
// must match and the addressed frame interval must fit the settings' audible interval.
// The block is staged so validation/arithmetic failure leaves caller output unchanged.
void master_block(std::span<const float> route_0_selected,
                  std::span<const float> route_1_selected,
                  std::uint64_t first_frame_index, const MasteringSettings &settings,
                  std::span<MasteredFrame> output);

} // namespace engine_sim_offline::presentation
