#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::presentation {

inline constexpr std::uint64_t kP18AudibleFrameCount = 2'880'000;
inline constexpr std::uint64_t kP18FadeFrameCount = 3'840;
inline constexpr std::uint64_t kP18FadeOutStartFrame =
    kP18AudibleFrameCount - kP18FadeFrameCount;

struct P18Pcm24Quantization {
    std::int32_t s32 = 0;
    std::int32_t pcm24 = 0;
    bool saturated = false;

    friend bool operator==(const P18Pcm24Quantization &,
                           const P18Pcm24Quantization &) = default;
};

// Every arithmetic boundary required to audit the P1.8 reference master. Keeping
// both integer representations here prevents an integration from silently replacing
// the observed Float32 -> S32 -> PCM24 path with direct 24-bit quantization.
struct P18MasteredFrame {
    float raw = 0.0F;
    float monitor = 0.0F;
    double fade_gain = 0.0;
    float faded = 0.0F;
    std::int32_t s32 = 0;
    std::int32_t pcm24 = 0;
    bool saturated = false;

    friend bool operator==(const P18MasteredFrame &,
                           const P18MasteredFrame &) = default;
};

// Returns sin(((binary64(k) / 3840) * pi) / 2) in the normative operation order.
// `k` must be in [0, 3840].
[[nodiscard]] double p18_quarter_sine_gain(std::uint64_t k);

// Returns the frame-indexed fade gain for one of exactly 2,880,000 audible frames.
[[nodiscard]] double p18_audition_fade_gain(std::uint64_t frame_index);

// Quantizes one finite faded Float32 sample using the exact Float32-to-S32 and
// floor(S32 / 256) path. Saturation is reported, not treated as a fallback.
[[nodiscard]] P18Pcm24Quantization p18_quantize_pcm24(float faded_sample);

// Serializes the low 24 bits of a validated PCM24 code. Codes outside the signed
// 24-bit range are rejected.
[[nodiscard]] std::array<std::byte, 3> p18_serialize_pcm24le(std::int32_t pcm24_sample);

// Route addition is one Float32 operation in route-0, route-1 order. Every result is
// finite or the call fails before returning a frame.
[[nodiscard]] P18MasteredFrame p18_master_reference_frame(float route_0_selected,
                                                          float route_1_selected,
                                                          std::uint64_t frame_index);

// Exact chunk-independent counterpart to p18_master_reference_frame. Span lengths
// must match and the addressed frame interval must fit the fixed audible interval.
// The block is staged so validation/arithmetic failure leaves caller output unchanged.
void p18_master_reference_block(std::span<const float> route_0_selected,
                                std::span<const float> route_1_selected,
                                std::uint64_t first_frame_index,
                                std::span<P18MasteredFrame> output);

} // namespace engine_sim_offline::presentation
