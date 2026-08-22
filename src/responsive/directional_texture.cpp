#include "crankwave/responsive/directional_texture.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace crankwave::responsive {
namespace {

constexpr double kSeamBlendCycleFraction = 1.0 / 8.0;
constexpr double kTinyRms = 1.0e-30;

[[nodiscard]] constexpr std::uint32_t
presentation_state_mask(const EngineCycleStateFlagMask session_flags) noexcept {
    std::uint32_t result = 0U;
    if ((session_flags &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled)) != 0U) {
        result |= 1U << 0U;
    }
    if ((session_flags &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled)) != 0U) {
        result |= 1U << 1U;
    }
    if ((session_flags &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::starter_enabled)) != 0U) {
        result |= 1U << 2U;
    }
    // Dyno is deliberately not part of the established responsive playback mask.
    if ((session_flags &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_enabled)) != 0U) {
        result |= 1U << 3U;
    }
    if ((session_flags & engine_cycle_state_flag_mask(
                             EngineCycleStateFlag::limiter_cut_active)) != 0U) {
        result |= 1U << 4U;
    }
    return result;
}

[[nodiscard]] DirectionalTransformError error(std::string code, std::string message) {
    return DirectionalTransformError{std::move(code), std::move(message)};
}

[[nodiscard]] bool finite_endpoint(const ResponsiveCaptureEndpoint &value) {
    return std::isfinite(value.engine_speed_rpm) &&
           std::isfinite(value.mean_intake_manifold_pressure_pa_abs) &&
           std::isfinite(value.requested_throttle_01) &&
           std::isfinite(value.resolved_engine_throttle_01) &&
           std::isfinite(value.unwrapped_crank_revolutions);
}

[[nodiscard]] std::variant<std::monostate, DirectionalTransformError>
validate_capture(const DirectionalCaptureView &capture) {
    if (capture.audible_pcm.empty()) {
        return error("directional-empty-pcm",
                     "directional capture contains no audible PCM");
    }
    if (capture.endpoints.size() < 2U) {
        return error("directional-endpoints-incomplete",
                     "directional capture requires at least two endpoints");
    }
    for (const auto sample : capture.audible_pcm) {
        if (!std::isfinite(sample)) {
            return error("directional-non-finite-pcm",
                         "directional capture contains non-finite PCM");
        }
    }
    for (std::size_t index = 0; index < capture.endpoints.size(); ++index) {
        const auto &endpoint = capture.endpoints[index];
        if (!finite_endpoint(endpoint) || !(endpoint.engine_speed_rpm > 0.0) ||
            !(endpoint.mean_intake_manifold_pressure_pa_abs > 0.0)) {
            return error("directional-invalid-endpoint",
                         "directional capture endpoint is not finite and physical");
        }
        if (index == 0U) {
            continue;
        }
        const auto &prior = capture.endpoints[index - 1U];
        if (!(endpoint.delivery_frame > prior.delivery_frame) ||
            !(endpoint.unwrapped_crank_revolutions >
              prior.unwrapped_crank_revolutions)) {
            return error("directional-endpoint-order",
                         "directional capture endpoints are not strictly ordered");
        }
    }
    if (capture.audible_first_delivery_frame >=
        std::numeric_limits<std::uint64_t>::max() - capture.audible_pcm.size()) {
        return error("directional-frame-overflow",
                     "directional audible frame horizon overflows uint64");
    }
    return std::monostate{};
}

[[nodiscard]] double interpolate(const double left, const double right,
                                 const double amount) {
    return left + (right - left) * amount;
}

[[nodiscard]] std::variant<double, DirectionalTransformError>
crossing_revolutions(const DirectionalCaptureView &capture, const double rpm,
                     const DirectionalSweepDirection direction) {
    for (std::size_t index = 1U; index < capture.endpoints.size(); ++index) {
        const auto &left = capture.endpoints[index - 1U];
        const auto &right = capture.endpoints[index];
        if (right.delivery_frame < capture.audible_first_delivery_frame) {
            continue;
        }
        const auto crossed =
            direction == DirectionalSweepDirection::rising
                ? left.engine_speed_rpm <= rpm && right.engine_speed_rpm >= rpm &&
                      right.engine_speed_rpm > left.engine_speed_rpm
                : left.engine_speed_rpm >= rpm && right.engine_speed_rpm <= rpm &&
                      right.engine_speed_rpm < left.engine_speed_rpm;
        if (!crossed) {
            continue;
        }
        const auto amount = (rpm - left.engine_speed_rpm) /
                            (right.engine_speed_rpm - left.engine_speed_rpm);
        return interpolate(left.unwrapped_crank_revolutions,
                           right.unwrapped_crank_revolutions, amount);
    }
    return error("directional-rpm-not-crossed",
                 "directional capture does not cross the requested RPM");
}

[[nodiscard]] std::variant<double, DirectionalTransformError>
frame_at_revolutions(const DirectionalCaptureView &capture, const double revolutions) {
    if (revolutions < capture.endpoints.front().unwrapped_crank_revolutions ||
        revolutions > capture.endpoints.back().unwrapped_crank_revolutions) {
        return error("directional-phase-outside-capture",
                     "requested crank phase is outside captured telemetry");
    }
    std::size_t lower = 0U;
    std::size_t upper = capture.endpoints.size() - 1U;
    while (upper - lower > 1U) {
        const auto middle = (lower + upper) / 2U;
        if (capture.endpoints[middle].unwrapped_crank_revolutions <= revolutions) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    const auto &left = capture.endpoints[lower];
    const auto &right = capture.endpoints[upper];
    const auto denominator =
        right.unwrapped_crank_revolutions - left.unwrapped_crank_revolutions;
    if (!(denominator > 0.0)) {
        return error("directional-phase-degenerate",
                     "captured crank interval has no positive extent");
    }
    const auto amount = (revolutions - left.unwrapped_crank_revolutions) / denominator;
    return interpolate(static_cast<double>(left.delivery_frame),
                       static_cast<double>(right.delivery_frame), amount);
}

[[nodiscard]] std::variant<float, DirectionalTransformError>
sample_at_revolutions(const DirectionalCaptureView &capture, const double revolutions) {
    const auto frame_result = frame_at_revolutions(capture, revolutions);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&frame_result)) {
        return *failure;
    }
    const auto position = std::get<double>(frame_result) -
                          static_cast<double>(capture.audible_first_delivery_frame);
    if (!std::isfinite(position) || position < 0.0) {
        return error("directional-pcm-phase-outside-capture",
                     "requested crank phase precedes audible PCM");
    }
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left + 1U >= capture.audible_pcm.size()) {
        return error("directional-pcm-phase-outside-capture",
                     "requested crank phase exceeds audible PCM");
    }
    const auto amount = position - static_cast<double>(left);
    return static_cast<float>(
        interpolate(static_cast<double>(capture.audible_pcm[left]),
                    static_cast<double>(capture.audible_pcm[left + 1U]), amount));
}

[[nodiscard]] std::variant<DirectionalTelemetrySummary, DirectionalTransformError>
summarize_telemetry(const DirectionalCaptureView &capture,
                    const double begin_revolutions, const double end_revolutions) {
    const auto begin_result = frame_at_revolutions(capture, begin_revolutions);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&begin_result)) {
        return *failure;
    }
    const auto end_result = frame_at_revolutions(capture, end_revolutions);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&end_result)) {
        return *failure;
    }
    const auto begin = std::get<double>(begin_result);
    const auto end = std::get<double>(end_result);
    DirectionalTelemetrySummary result;
    for (const auto &endpoint : capture.endpoints) {
        const auto frame = static_cast<double>(endpoint.delivery_frame);
        if (frame < begin || frame > end) {
            continue;
        }
        result.mean_manifold_pressure_pa_abs +=
            endpoint.mean_intake_manifold_pressure_pa_abs;
        result.mean_engine_speed_rpm += endpoint.engine_speed_rpm;
        result.mean_requested_throttle_01 += endpoint.requested_throttle_01;
        result.mean_resolved_engine_throttle_01 += endpoint.resolved_engine_throttle_01;
        const auto responsive_mask = presentation_state_mask(endpoint.state_flags);
        if (std::find(result.state_masks.begin(), result.state_masks.end(),
                      responsive_mask) == result.state_masks.end()) {
            result.state_masks.push_back(responsive_mask);
        }
        ++result.endpoint_count;
    }
    if (result.endpoint_count == 0U) {
        return error("directional-empty-telemetry-interval",
                     "directional source interval has no telemetry endpoints");
    }
    const auto count = static_cast<double>(result.endpoint_count);
    result.mean_manifold_pressure_pa_abs /= count;
    result.mean_engine_speed_rpm /= count;
    result.mean_requested_throttle_01 /= count;
    result.mean_resolved_engine_throttle_01 /= count;
    return result;
}

[[nodiscard]] double smoothstep(const double amount) {
    return amount * amount * (3.0 - 2.0 * amount);
}

[[nodiscard]] double signal_rms(const std::span<const float> samples) {
    double square_sum = 0.0;
    for (const auto sample : samples) {
        square_sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return std::sqrt(square_sum / static_cast<double>(samples.size()));
}

[[nodiscard]] double derivative_rms(const std::span<const float> samples) {
    double square_sum = 0.0;
    for (std::size_t index = 1U; index < samples.size(); ++index) {
        const auto delta = static_cast<double>(samples[index]) -
                           static_cast<double>(samples[index - 1U]);
        square_sum += delta * delta;
    }
    return std::sqrt(square_sum / static_cast<double>(samples.size() - 1U));
}

} // namespace

std::variant<std::vector<float>, DirectionalTransformError>
close_directional_three_cycle_seam(const std::span<const float> source) {
    constexpr auto expected =
        kDirectionalSamplesPerCycle * kDirectionalSourceCycleCount;
    if (source.size() != expected) {
        return error("directional-seam-source-size",
                     "seam closure requires exactly three phase cycles");
    }
    for (const auto sample : source) {
        if (!std::isfinite(sample)) {
            return error("directional-seam-non-finite",
                         "seam closure source contains non-finite PCM");
        }
    }

    std::vector<double> phase_template(kDirectionalSamplesPerCycle);
    for (std::size_t phase = 0U; phase < kDirectionalSamplesPerCycle; ++phase) {
        phase_template[phase] =
            (static_cast<double>(source[phase]) +
             static_cast<double>(source[kDirectionalSamplesPerCycle + phase]) +
             static_cast<double>(source[2U * kDirectionalSamplesPerCycle + phase])) /
            static_cast<double>(kDirectionalSourceCycleCount);
    }
    const auto target_boundary_delta =
        0.5 * ((phase_template[1U] - phase_template[0U]) +
               (phase_template.back() - phase_template[phase_template.size() - 2U]));
    const auto correction =
        phase_template.front() - phase_template.back() - target_boundary_delta;
    for (std::size_t phase = 0U; phase < phase_template.size(); ++phase) {
        phase_template[phase] +=
            correction * (static_cast<double>(phase) /
                          static_cast<double>(phase_template.size() - 1U));
    }

    const auto blend_frames = static_cast<std::size_t>(std::llround(
        static_cast<double>(kDirectionalSamplesPerCycle) * kSeamBlendCycleFraction));
    std::vector<float> output(source.begin(), source.end());
    const auto final_blend_begin = output.size() - blend_frames;
    for (std::size_t index = 0U; index < blend_frames; ++index) {
        const auto amount =
            static_cast<double>(index) / static_cast<double>(blend_frames - 1U);
        const auto start_weight = smoothstep(1.0 - amount);
        const auto end_weight = smoothstep(amount);
        const auto end_index = final_blend_begin + index;
        const auto end_phase = end_index % kDirectionalSamplesPerCycle;
        output[index] = static_cast<float>(
            static_cast<double>(source[index]) +
            (phase_template[index] - static_cast<double>(source[index])) *
                start_weight);
        output[end_index] = static_cast<float>(
            static_cast<double>(source[end_index]) +
            (phase_template[end_phase] - static_cast<double>(source[end_index])) *
                end_weight);
    }
    return output;
}

std::variant<DirectionalClosureMetrics, DirectionalTransformError>
measure_directional_closure(const std::span<const float> source,
                            const std::span<const float> candidate) {
    if (source.size() != candidate.size() || source.size() < 2U) {
        return error("directional-closure-shape",
                     "directional closure vectors must have equal nontrivial shape");
    }
    DirectionalClosureMetrics result;
    result.signal_rms = signal_rms(source);
    result.source_adjacent_derivative_rms = derivative_rms(source);
    double correction_square_sum = 0.0;
    double maximum_adjacent_delta = 0.0;
    for (std::size_t index = 0U; index < candidate.size(); ++index) {
        if (!std::isfinite(candidate[index])) {
            return error("directional-closure-non-finite",
                         "directional closure emitted non-finite PCM");
        }
        const auto correction =
            static_cast<double>(candidate[index]) - static_cast<double>(source[index]);
        correction_square_sum += correction * correction;
        if (index != 0U) {
            maximum_adjacent_delta =
                std::max(maximum_adjacent_delta,
                         std::abs(static_cast<double>(candidate[index]) -
                                  static_cast<double>(candidate[index - 1U])));
        }
    }
    result.seam_absolute_delta = std::abs(static_cast<double>(candidate.front()) -
                                          static_cast<double>(candidate.back()));
    maximum_adjacent_delta =
        std::max(maximum_adjacent_delta, result.seam_absolute_delta);
    const auto derivative_reference =
        std::max(result.source_adjacent_derivative_rms, kTinyRms);
    result.seam_over_source_adjacent_derivative_rms =
        result.seam_absolute_delta / derivative_reference;
    result.maximum_adjacent_delta_over_source_adjacent_derivative_rms =
        maximum_adjacent_delta / derivative_reference;
    result.correction_rms_over_source_rms =
        std::sqrt(correction_square_sum / static_cast<double>(source.size())) /
        std::max(result.signal_rms, kTinyRms);
    return result;
}

std::variant<DirectionalTextureTransform, DirectionalTransformError>
transform_directional_capture(const DirectionalCaptureView &capture,
                              const double target_rpm,
                              const DirectionalSweepDirection direction) {
    const auto validation = validate_capture(capture);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&validation)) {
        return *failure;
    }
    if (!std::isfinite(target_rpm) || !(target_rpm > 0.0)) {
        return error("directional-invalid-rpm",
                     "directional target RPM must be finite and positive");
    }

    const auto crossing_result = crossing_revolutions(capture, target_rpm, direction);
    if (const auto *failure =
            std::get_if<DirectionalTransformError>(&crossing_result)) {
        return *failure;
    }
    DirectionalTextureTransform result;
    result.crossing_revolutions = std::get<double>(crossing_result);
    result.source_cycle_begin_revolutions =
        std::ceil(result.crossing_revolutions / kDirectionalCycleRevolutions) *
        kDirectionalCycleRevolutions;
    result.source_cycle_end_revolutions =
        result.source_cycle_begin_revolutions +
        static_cast<double>(kDirectionalSourceCycleCount) *
            kDirectionalCycleRevolutions;

    constexpr auto output_size =
        kDirectionalSamplesPerCycle * kDirectionalSourceCycleCount;
    result.source.resize(output_size);
    for (std::size_t index = 0U; index < output_size; ++index) {
        const auto revolutions = result.source_cycle_begin_revolutions +
                                 static_cast<double>(index) *
                                     kDirectionalCycleRevolutions /
                                     static_cast<double>(kDirectionalSamplesPerCycle);
        const auto sample = sample_at_revolutions(capture, revolutions);
        if (const auto *failure = std::get_if<DirectionalTransformError>(&sample)) {
            return *failure;
        }
        result.source[index] = std::get<float>(sample);
    }
    const auto telemetry =
        summarize_telemetry(capture, result.source_cycle_begin_revolutions,
                            result.source_cycle_end_revolutions);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&telemetry)) {
        return *failure;
    }
    result.telemetry = std::get<DirectionalTelemetrySummary>(telemetry);

    const auto closed = close_directional_three_cycle_seam(result.source);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&closed)) {
        return *failure;
    }
    result.seam_closed = std::get<std::vector<float>>(closed);
    const auto metrics = measure_directional_closure(result.source, result.seam_closed);
    if (const auto *failure = std::get_if<DirectionalTransformError>(&metrics)) {
        return *failure;
    }
    result.closure = std::get<DirectionalClosureMetrics>(metrics);
    return result;
}

} // namespace crankwave::responsive
