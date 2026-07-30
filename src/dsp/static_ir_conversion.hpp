#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine_sim_offline::dsp {

// Fixed method limits for configured impulse-response conversion. The converter
// accepts the complete decoded PCM16 data so that it can verify the caller's
// meaningful-support result before using only that support prefix.
struct StaticIrConversionLimits {
    static constexpr std::size_t half_width = 12;
    static constexpr std::size_t tap_count = 24;
    static constexpr std::size_t phase_count = 4096;
    static constexpr std::size_t table_row_count = phase_count + 1;
    static constexpr std::uint64_t source_rate_hz = 44100;
    static constexpr std::uint64_t target_rate_hz = 192000;
    static constexpr double source_area_gain = 44100.0 / 192000.0;
    static constexpr std::size_t maximum_source_frame_count = 33705;
    static constexpr std::size_t maximum_target_coefficient_count =
        (maximum_source_frame_count * target_rate_hz + source_rate_hz / 2) /
        source_rate_hz;
};

// Returns the positive, half-up-rounded coefficient count for the fixed
// 44.1 kHz-to-192 kHz conversion. The source count is bounded by the frozen
// source-frame envelope above.
[[nodiscard]] std::size_t
static_ir_target_count(std::size_t meaningful_support_frame_count);

// Applies the configured static-IR conversion to a decoded mono PCM16
// signal. meaningful_support_frame_count must equal one plus the last index in
// decoded_pcm16 whose absolute integer magnitude is strictly greater than 100.
// The configured gain must be nonnegative and finite. The returned coefficients
// are binary64 at 192 kHz; this function performs no file I/O or serialization.
//
// Exact execution uses the compile target's identified extended accumulator:
// SysV x87 extended on the admitted native target and IEEE binary128 on wasm32.
// Unsupported or mismatched floating-point formats are rejected before allocation.
[[nodiscard]] std::vector<double>
convert_static_ir(std::span<const std::int16_t> decoded_pcm16,
                  std::size_t meaningful_support_frame_count,
                  double configured_gain);

} // namespace engine_sim_offline::dsp
