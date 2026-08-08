#pragma once

#include "engine_sim_offline/responsive/finite_capture.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

inline constexpr std::size_t kDirectionalSamplesPerCycle = 4096U;
inline constexpr std::size_t kDirectionalSourceCycleCount = 3U;
inline constexpr double kDirectionalCycleRevolutions = 2.0;

enum class DirectionalSweepDirection : std::uint8_t {
    rising,
    falling,
};

struct DirectionalTransformError {
    std::string code;
    std::string message;

    friend bool operator==(const DirectionalTransformError &,
                           const DirectionalTransformError &) = default;
};

struct DirectionalCaptureView {
    std::uint64_t audible_first_delivery_frame = 0;
    std::span<const float> audible_pcm;
    std::span<const ResponsiveCaptureEndpoint> endpoints;
};

struct DirectionalTelemetrySummary {
    double mean_manifold_pressure_pa_abs = 0.0;
    double mean_engine_speed_rpm = 0.0;
    double mean_requested_throttle_01 = 0.0;
    double mean_resolved_engine_throttle_01 = 0.0;
    // Established responsive playback mask: ignition/fuel/starter/limiter/cut
    // occupy bits 0..4. This is not EngineCycleStateFlagMask; dyno is omitted.
    std::vector<std::uint32_t> state_masks;
    std::size_t endpoint_count = 0;

    friend bool operator==(const DirectionalTelemetrySummary &,
                           const DirectionalTelemetrySummary &) = default;
};

struct DirectionalClosureMetrics {
    double signal_rms = 0.0;
    double source_adjacent_derivative_rms = 0.0;
    double seam_absolute_delta = 0.0;
    double seam_over_source_adjacent_derivative_rms = 0.0;
    double maximum_adjacent_delta_over_source_adjacent_derivative_rms = 0.0;
    double correction_rms_over_source_rms = 0.0;

    friend bool operator==(const DirectionalClosureMetrics &,
                           const DirectionalClosureMetrics &) = default;
};

struct DirectionalTextureTransform {
    double crossing_revolutions = 0.0;
    double source_cycle_begin_revolutions = 0.0;
    double source_cycle_end_revolutions = 0.0;
    std::vector<float> source;
    std::vector<float> seam_closed;
    DirectionalTelemetrySummary telemetry;
    DirectionalClosureMetrics closure;
};

[[nodiscard]] std::variant<DirectionalTextureTransform,
                           DirectionalTransformError>
transform_directional_capture(const DirectionalCaptureView &capture,
                              double target_rpm,
                              DirectionalSweepDirection direction);

[[nodiscard]] std::variant<std::vector<float>, DirectionalTransformError>
close_directional_three_cycle_seam(std::span<const float> source);

[[nodiscard]] std::variant<DirectionalClosureMetrics,
                           DirectionalTransformError>
measure_directional_closure(std::span<const float> source,
                            std::span<const float> candidate);

} // namespace engine_sim_offline::responsive
