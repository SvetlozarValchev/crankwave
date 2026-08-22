#include "crankwave/responsive/lifecycle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crankwave::responsive {
namespace {

constexpr double kFourStrokeCycleRevolutions = 2.0;
constexpr std::size_t kSignatureSize = 512U;

[[nodiscard]] LifecycleError error(const LifecycleErrorCode code,
                                   std::string detail_code, std::string path,
                                   std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] LifecycleError cancelled() {
    return error(LifecycleErrorCode::cancelled, "cancelled", {},
                 "responsive lifecycle operation was cancelled");
}

[[nodiscard]] bool finite(const double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool rate_equals(const contract::RationalRateHz &rate,
                               const std::uint32_t expected) noexcept {
    return rate.denominator != 0U &&
           rate.numerator == static_cast<std::uint64_t>(expected) * rate.denominator;
}

[[nodiscard]] std::optional<double> event_seconds(const LifecycleScenarioSpec &spec,
                                                  const std::string_view id) {
    const auto event =
        std::find_if(spec.scenario.events.begin(), spec.scenario.events.end(),
                     [&](const auto &candidate) { return candidate.id.value == id; });
    if (event == spec.scenario.events.end() || event->time.unit != "s" ||
        !finite(event->time.value)) {
        return std::nullopt;
    }
    return event->time.value;
}

[[nodiscard]] std::optional<std::uint64_t>
js_round_nonnegative(const double value) noexcept {
    if (!finite(value) || value < 0.0 ||
        value > static_cast<double>(std::numeric_limits<std::uint64_t>::max() - 1U)) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(std::floor(value + 0.5));
}

[[nodiscard]] std::optional<std::uint64_t>
event_frame(const LifecycleScenarioSpec &spec, const double seconds) noexcept {
    return js_round_nonnegative((seconds - spec.audible_start_seconds) *
                                static_cast<double>(kLifecycleDeliveryRateHz));
}

[[nodiscard]] std::optional<LifecycleError>
validate_capture_evidence(const LifecycleCaptureEvidence &capture) {
    if (capture.engine_id.empty() || capture.scenario_id.empty() ||
        capture.bus_id.empty() || capture.physics_rate_hz != kLifecyclePhysicsRateHz ||
        capture.delivery_rate_hz != kLifecycleDeliveryRateHz ||
        capture.pcm.size() != capture.audible_frame_count || capture.pcm.size() < 2U ||
        capture.points.empty() || capture.total_block_count == 0U ||
        capture.preparation_block_count > capture.total_block_count ||
        capture.final_physics_frame == 0U || capture.final_delivery_frame == 0U) {
        return error(LifecycleErrorCode::invalid_capture, "invalid-capture-shape",
                     "capture",
                     "lifecycle capture shape, rates, or horizons are invalid");
    }
    for (std::size_t index = 0U; index < capture.pcm.size(); ++index) {
        if (!std::isfinite(capture.pcm[index])) {
            return error(LifecycleErrorCode::invalid_capture, "nonfinite-pcm",
                         "capture.pcm", "lifecycle PCM contains a non-finite sample");
        }
    }
    std::uint64_t prior_frame = 0U;
    for (std::size_t index = 0U; index < capture.points.size(); ++index) {
        const auto &point = capture.points[index];
        if (point.source_frame >= capture.pcm.size() ||
            (index != 0U && point.source_frame <= prior_frame) ||
            !finite(point.simulation_seconds) || !finite(point.rpm) ||
            !finite(point.indicated_gas_torque_nm) ||
            point.simulation_seconds !=
                static_cast<double>(point.physics_step_end) /
                    static_cast<double>(kLifecyclePhysicsRateHz)) {
            return error(LifecycleErrorCode::invalid_capture, "invalid-endpoint",
                         "capture.points",
                         "lifecycle endpoint ordering or numeric data is invalid");
        }
        prior_frame = point.source_frame;
    }
    std::uint64_t prior_end = 0U;
    for (std::size_t index = 0U; index < capture.completed_cycles.size(); ++index) {
        const auto &cycle = capture.completed_cycles[index];
        if (cycle.start_frame >= cycle.end_frame ||
            cycle.end_frame > capture.pcm.size() || !finite(cycle.mean_rpm) ||
            cycle.mean_rpm < 0.0 || (index != 0U && cycle.end_frame <= prior_end)) {
            return error(LifecycleErrorCode::invalid_capture, "invalid-cycle",
                         "capture.completed_cycles",
                         "lifecycle cycle evidence is malformed or reordered");
        }
        prior_end = cycle.end_frame;
    }
    return std::nullopt;
}

[[nodiscard]] LifecycleResult<double>
interpolate_rpm(const std::span<const LifecycleCapturePoint> points,
                const std::uint64_t source_frame) {
    if (points.empty()) {
        return error(LifecycleErrorCode::invalid_capture, "missing-endpoints",
                     "capture.points", "capture has no telemetry endpoints");
    }
    auto upper = std::find_if(points.begin(), points.end(), [&](const auto &point) {
        return point.source_frame >= source_frame;
    });
    if (upper == points.end()) {
        upper = points.end() - 1;
    }
    const auto lower = upper == points.begin() ? upper : upper - 1;
    double amount = 0.0;
    if (upper->source_frame > lower->source_frame) {
        amount = static_cast<double>(source_frame - lower->source_frame) /
                 static_cast<double>(upper->source_frame - lower->source_frame);
    }
    amount = std::clamp(amount, 0.0, 1.0);
    const double result = lower->rpm + (upper->rpm - lower->rpm) * amount;
    if (!finite(result)) {
        return error(LifecycleErrorCode::invalid_capture, "nonfinite-interpolated-rpm",
                     "capture.points", "interpolated lifecycle RPM is non-finite");
    }
    return result;
}

[[nodiscard]] LifecycleResult<std::vector<double>>
normalized_signature(const std::span<const float> pcm, const std::uint64_t start_frame,
                     const std::uint64_t end_frame,
                     const std::stop_token cancellation) {
    if (start_frame >= end_frame || end_frame > pcm.size()) {
        return error(LifecycleErrorCode::invalid_capture,
                     "signature-window-out-of-range", "pcm",
                     "seam signature window is outside PCM");
    }
    std::vector<double> signature(kSignatureSize, 0.0);
    double mean = 0.0;
    for (std::size_t index = 0U; index < signature.size(); ++index) {
        if ((index & 63U) == 0U && cancellation.stop_requested()) {
            return cancelled();
        }
        const double position =
            static_cast<double>(start_frame) +
            static_cast<double>(end_frame - start_frame - 1U) *
                static_cast<double>(index) /
                static_cast<double>(std::max<std::size_t>(1U, signature.size() - 1U));
        const auto left = static_cast<std::uint64_t>(std::floor(position));
        const auto right = std::min(end_frame - 1U, left + 1U);
        const double amount = position - static_cast<double>(left);
        const double left_value = static_cast<double>(pcm[left]);
        const double right_value = static_cast<double>(pcm[right]);
        const double value = left_value + (right_value - left_value) * amount;
        signature[index] = value;
        mean += value;
    }
    mean /= static_cast<double>(signature.size());
    double energy = 0.0;
    for (double &value : signature) {
        value -= mean;
        energy += value * value;
    }
    if (!(energy > 1.0e-20) || !finite(energy)) {
        return error(LifecycleErrorCode::no_admissible_seam, "silent-signature", "pcm",
                     "seam signature has no finite audible energy");
    }
    const double scale = 1.0 / std::sqrt(energy);
    for (double &value : signature) {
        value *= scale;
    }
    return signature;
}

[[nodiscard]] LifecycleResult<double> window_rms(const std::span<const float> pcm,
                                                 const std::uint64_t start_frame,
                                                 const std::uint64_t end_frame,
                                                 const std::stop_token cancellation) {
    if (start_frame > end_frame || end_frame > pcm.size()) {
        return error(LifecycleErrorCode::invalid_capture, "rms-window-out-of-range",
                     "pcm", "RMS window is outside PCM");
    }
    double energy = 0.0;
    for (std::uint64_t frame = start_frame; frame < end_frame; ++frame) {
        if ((frame & 4095U) == 0U && cancellation.stop_requested()) {
            return cancelled();
        }
        const double sample = static_cast<double>(pcm[frame]);
        energy += sample * sample;
    }
    const double result =
        end_frame > start_frame
            ? std::sqrt(energy / static_cast<double>(end_frame - start_frame))
            : 0.0;
    if (!finite(result)) {
        return error(LifecycleErrorCode::invalid_capture, "nonfinite-rms", "pcm",
                     "PCM RMS is non-finite");
    }
    return result;
}

struct ScoredSeam {
    LifecycleSeam seam;
    double score = 0.0;
};

[[nodiscard]] LifecycleResult<LifecycleSeam>
choose_event_seam(const std::string_view label, const std::span<const float> source_pcm,
                  const std::span<const LifecycleCaptureCycle> source_cycles,
                  const std::span<const float> target_pcm,
                  const std::span<const LifecycleCaptureCycle> target_cycles,
                  const std::stop_token cancellation) {
    std::optional<ScoredSeam> best;
    for (const auto &source : source_cycles) {
        for (const auto &target : target_cycles) {
            if (cancellation.stop_requested()) {
                return cancelled();
            }
            const auto crossfade = static_cast<std::uint32_t>(
                std::min({source.end_frame - source.start_frame,
                          target.end_frame - target.start_frame,
                          static_cast<std::uint64_t>(kLifecycleMaximumSeamFrames)}));
            if (crossfade < 2U) {
                continue;
            }
            auto source_signature =
                normalized_signature(source_pcm, source.start_frame,
                                     source.start_frame + crossfade, cancellation);
            if (std::holds_alternative<LifecycleError>(source_signature)) {
                const auto &failure = std::get<LifecycleError>(source_signature);
                if (failure.code == LifecycleErrorCode::cancelled) {
                    return failure;
                }
                continue;
            }
            auto target_signature =
                normalized_signature(target_pcm, target.start_frame,
                                     target.start_frame + crossfade, cancellation);
            if (std::holds_alternative<LifecycleError>(target_signature)) {
                const auto &failure = std::get<LifecycleError>(target_signature);
                if (failure.code == LifecycleErrorCode::cancelled) {
                    return failure;
                }
                continue;
            }
            const auto &left = std::get<std::vector<double>>(source_signature);
            const auto &right = std::get<std::vector<double>>(target_signature);
            double correlation = 0.0;
            for (std::size_t index = 0U; index < left.size(); ++index) {
                correlation += left[index] * right[index];
            }
            auto source_rms = window_rms(source_pcm, source.start_frame,
                                         source.start_frame + crossfade, cancellation);
            if (const auto *failure = std::get_if<LifecycleError>(&source_rms)) {
                return *failure;
            }
            auto target_rms = window_rms(target_pcm, target.start_frame,
                                         target.start_frame + crossfade, cancellation);
            if (const auto *failure = std::get_if<LifecycleError>(&target_rms)) {
                return *failure;
            }
            const double source_level = std::get<double>(source_rms);
            const double target_level = std::get<double>(target_rms);
            if (source_level <= 1.0e-8 || target_level <= 1.0e-8) {
                continue;
            }
            const double level_penalty =
                std::abs(std::log(source_level / target_level));
            const double rpm_penalty = std::abs(std::log(
                std::max(1.0, source.mean_rpm) / std::max(1.0, target.mean_rpm)));
            const double score = correlation - level_penalty * 0.15 - rpm_penalty * 0.5;
            if (!finite(score)) {
                continue;
            }
            ScoredSeam choice{{source.start_frame,
                               crossfade,
                               source.mean_rpm,
                               target.start_frame,
                               target.mean_rpm,
                               std::clamp(correlation, -1.0, 1.0),
                               {},
                               {}},
                              score};
            if (!best || choice.score > best->score + 1.0e-12 ||
                (std::abs(choice.score - best->score) <= 1.0e-12 &&
                 choice.seam.source_frame < best->seam.source_frame)) {
                best = std::move(choice);
            }
        }
    }
    if (!best) {
        return error(LifecycleErrorCode::no_admissible_seam, "no-finite-audible-seam",
                     std::string{label},
                     std::string{label} + " has no finite audible seam pair");
    }
    return std::move(best->seam);
}

[[nodiscard]] LifecycleResult<LifecycleSeam> choose_pre_event_seam(
    const std::string_view label, const LifecycleCaptureEvidence &source,
    const std::uint64_t event_source_frame, const LifecycleCaptureEvidence &target,
    const std::span<const LifecycleCaptureCycle> target_cycles,
    const std::stop_token cancellation) {
    if (event_source_frame < kLifecycleEventLeadFrames) {
        return error(LifecycleErrorCode::no_admissible_seam, "missing-pre-event-window",
                     std::string{label},
                     std::string{label} + " lacks its 170 ms pre-event seam window");
    }
    const std::uint64_t source_frame = event_source_frame - kLifecycleEventLeadFrames;
    if (source_frame + kLifecycleMaximumSeamFrames > event_source_frame ||
        source_frame + kLifecycleMaximumSeamFrames > source.pcm.size()) {
        return error(LifecycleErrorCode::no_admissible_seam, "missing-pre-event-window",
                     std::string{label},
                     std::string{label} + " lacks its 170 ms pre-event seam window");
    }
    auto rpm = interpolate_rpm(source.points, source_frame);
    if (const auto *failure = std::get_if<LifecycleError>(&rpm)) {
        return *failure;
    }
    const LifecycleCaptureCycle source_cycle{source_frame,
                                             source_frame + kLifecycleMaximumSeamFrames,
                                             std::get<double>(rpm)};
    return choose_event_seam(label, source.pcm, std::span{&source_cycle, 1U},
                             target.pcm, target_cycles, cancellation);
}

[[nodiscard]] LifecycleResult<LifecycleCheckpoint>
checkpoint(std::string kind, const std::uint64_t frame,
           const LifecycleCaptureEvidence &capture, std::string precision,
           std::string method) {
    if (frame >= capture.pcm.size()) {
        return error(LifecycleErrorCode::invalid_capture, "checkpoint-out-of-range",
                     "checkpoint.frame", "lifecycle checkpoint is outside PCM");
    }
    auto rpm = interpolate_rpm(capture.points, frame);
    if (const auto *failure = std::get_if<LifecycleError>(&rpm)) {
        return *failure;
    }
    const double value = std::get<double>(rpm);
    if (value < 0.0) {
        return error(LifecycleErrorCode::invalid_capture, "negative-checkpoint-rpm",
                     "checkpoint.rpm", "lifecycle checkpoint RPM is negative");
    }
    return LifecycleCheckpoint{std::move(kind), frame, value, std::move(precision),
                               std::move(method)};
}

[[nodiscard]] LifecycleResult<LifecycleShutdownPresentation>
cook_shutdown_impl(const LifecycleScenarioSpec &spec,
                   const LifecycleCaptureEvidence &capture, const bool elevated,
                   const std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
        return cancelled();
    }
    if (const auto failure = validate_capture_evidence(capture)) {
        return *failure;
    }
    const auto keyoff_seconds = event_seconds(spec, "key-off");
    const auto keyoff_frame =
        keyoff_seconds ? event_frame(spec, *keyoff_seconds) : std::nullopt;
    if (!keyoff_frame || *keyoff_frame >= capture.pcm.size()) {
        return error(LifecycleErrorCode::invalid_request, "invalid-keyoff-event",
                     "scenario.events.key-off",
                     "key-off event is absent or outside audible PCM");
    }

    std::vector<LifecycleCaptureCycle> running_cycles;
    for (const auto &cycle : capture.completed_cycles) {
        if (cycle.end_frame <= *keyoff_frame) {
            running_cycles.push_back(cycle);
        }
    }
    LifecycleSeam entry;
    if (elevated) {
        if (running_cycles.empty()) {
            return error(LifecycleErrorCode::no_admissible_seam,
                         "missing-pre-keyoff-cycle", "shutdown-elevated",
                         "elevated shutdown has no complete pre-keyoff running cycle");
        }
        const auto &cycle = running_cycles.front();
        entry = {cycle.start_frame,
                 static_cast<std::uint32_t>(std::max<std::uint64_t>(
                     1U, std::min<std::uint64_t>(cycle.end_frame - cycle.start_frame,
                                                 kLifecyclePresentationFadeFrames))),
                 cycle.mean_rpm,
                 cycle.start_frame,
                 cycle.mean_rpm,
                 1.0,
                 "running",
                 "shutdown-high-rpm-pre-keyoff-cycle-reference"};
    } else {
        auto selected = choose_pre_event_seam("shutdown entry", capture, *keyoff_frame,
                                              capture, running_cycles, cancellation);
        if (const auto *failure = std::get_if<LifecycleError>(&selected)) {
            return *failure;
        }
        entry = std::get<LifecycleSeam>(std::move(selected));
        entry.target = "running";
        entry.target_reference = "shutdown-pre-keyoff-cycle-reference";
    }

    const auto stopped = std::find_if(
        capture.points.begin(), capture.points.end(), [&](const auto &point) {
            return point.source_frame >= *keyoff_frame && !point.ignition_enabled &&
                   (elevated ? point.fuel_enabled : !point.fuel_enabled) &&
                   std::abs(point.rpm) <= 1.0;
        });
    if (stopped == capture.points.end()) {
        return error(LifecycleErrorCode::no_admissible_event, "engine-did-not-stop",
                     "capture.points",
                     elevated ? "elevated shutdown does not reach <= 1 RPM"
                              : "shutdown capture does not reach <= 1 RPM");
    }
    const std::uint64_t stopped_frame = stopped->source_frame;
    auto pre_event_rms =
        window_rms(capture.pcm, entry.source_frame, *keyoff_frame, cancellation);
    if (const auto *failure = std::get_if<LifecycleError>(&pre_event_rms)) {
        return *failure;
    }
    const double quiet_peak_threshold =
        std::max(4.0 / 32768.0, std::get<double>(pre_event_rms) * 0.01);
    const double quiet_rms_threshold =
        std::max(2.0 / 32768.0, std::get<double>(pre_event_rms) * 0.0032);
    std::uint64_t last_above_quiet = stopped_frame - 1U;
    for (std::uint64_t frame = stopped_frame; frame < capture.pcm.size(); ++frame) {
        if ((frame & 4095U) == 0U && cancellation.stop_requested()) {
            return cancelled();
        }
        if (std::abs(static_cast<double>(capture.pcm[frame])) > quiet_peak_threshold) {
            last_above_quiet = frame;
        }
    }
    const std::uint64_t silence_frame = std::max(stopped_frame, last_above_quiet + 1U);
    const std::uint64_t quiet_tail = capture.pcm.size() - silence_frame;
    if (quiet_tail < kLifecycleQuietTailFrames) {
        return error(LifecycleErrorCode::insufficient_quiet_tail,
                     "insufficient-verified-quiet-tail", "capture.pcm",
                     "shutdown does not retain the required 300 ms quiet tail");
    }
    auto tail_rms =
        window_rms(capture.pcm, silence_frame, capture.pcm.size(), cancellation);
    if (const auto *failure = std::get_if<LifecycleError>(&tail_rms)) {
        return *failure;
    }
    if (std::get<double>(tail_rms) > quiet_rms_threshold) {
        return error(LifecycleErrorCode::insufficient_quiet_tail,
                     "quiet-tail-rms-exceeded", "capture.pcm",
                     "shutdown post-roll exceeds its relative quiet-RMS threshold");
    }

    std::vector<LifecycleCheckpoint> checkpoints;
    checkpoints.reserve(3U);
    auto settled =
        checkpoint(elevated ? "settled-running" : "settled-idle", entry.source_frame,
                   capture, "cycle-window",
                   elevated ? "captured-high-rpm-pre-key-off-cycle"
                            : "pre-key-off-window-correlated-to-captured-running-idle");
    auto ignition = checkpoint("ignition-off", *keyoff_frame, capture, "exact",
                               "authored-operating-state-event");
    auto stopped_checkpoint =
        checkpoint("engine-stopped", stopped_frame, capture, "advance-block",
                   "first-observed-absolute-engine-speed-at-or-below-1-rpm");
    for (auto *candidate : {&settled, &ignition, &stopped_checkpoint}) {
        if (const auto *failure = std::get_if<LifecycleError>(candidate)) {
            return *failure;
        }
        checkpoints.push_back(std::get<LifecycleCheckpoint>(std::move(*candidate)));
    }
    if (entry.source_frame + entry.crossfade_frames > *keyoff_frame ||
        *keyoff_frame > stopped_frame || stopped_frame > silence_frame ||
        silence_frame >= capture.pcm.size()) {
        return error(LifecycleErrorCode::invalid_capture, "invalid-shutdown-order",
                     "shutdown",
                     "shutdown checkpoints, seam, or quiet tail are not ordered");
    }
    return LifecycleShutdownPresentation{std::move(checkpoints),
                                         entry,
                                         silence_frame,
                                         kLifecyclePresentationFadeFrames,
                                         quiet_tail,
                                         quiet_peak_threshold,
                                         quiet_rms_threshold};
}

[[nodiscard]] bool role_matches(const LifecycleCaptureEvidence &capture,
                                const LifecycleScenarioRole role) noexcept {
    return capture.role == role;
}

} // namespace

LifecycleResult<LifecycleCaptureEvidence>
map_lifecycle_capture(const LifecycleScenarioSpec &spec,
                      const FiniteResponsiveCapture &capture,
                      const std::size_t bus_index, const std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
        return cancelled();
    }
    if (const auto failure = validate_finite_responsive_capture(capture)) {
        return error(LifecycleErrorCode::invalid_capture, "invalid-finite-capture",
                     "capture", failure->detail_code + ": " + failure->message);
    }
    if (capture.engine_id != spec.scenario.engine.value ||
        capture.scenario_id != spec.id ||
        capture.total_block_count != spec.total_block_count ||
        capture.preparation_block_count != spec.preparation_block_count ||
        !rate_equals(capture.physics_rate, kLifecyclePhysicsRateHz) ||
        !rate_equals(capture.delivery_rate, kLifecycleDeliveryRateHz) ||
        capture.delivery_frames_per_block != kLifecycleFramesPerBlock) {
        return error(LifecycleErrorCode::capture_mismatch, "scenario-capture-mismatch",
                     "capture",
                     "finite capture does not match its lifecycle scenario spec");
    }
    if (bus_index >= capture.buses.size()) {
        return error(LifecycleErrorCode::invalid_request, "bus-index-out-of-range",
                     "bus_index", "lifecycle bus index is outside the finite capture");
    }
    const auto &bus = capture.buses[bus_index];
    if (bus.descriptor.channel_count != 1U ||
        !rate_equals(bus.descriptor.sample_rate, kLifecycleDeliveryRateHz) ||
        bus.audible_interleaved_samples.size() !=
            capture.audible_delivery_frame_count) {
        return error(LifecycleErrorCode::capture_mismatch, "invalid-lifecycle-bus",
                     "capture.buses",
                     "lifecycle capture requires one 192 kHz mono audible bus");
    }

    LifecycleCaptureEvidence result;
    result.role = spec.role;
    result.canonical_source_id = spec.canonical_source_id;
    result.engine_id = capture.engine_id;
    result.scenario_id = capture.scenario_id;
    result.bus_id = bus.descriptor.id;
    result.physics_rate_hz = kLifecyclePhysicsRateHz;
    result.delivery_rate_hz = kLifecycleDeliveryRateHz;
    result.audible_first_delivery_frame = capture.audible_first_delivery_frame;
    result.audible_frame_count = capture.audible_delivery_frame_count;
    result.final_physics_frame = capture.completion.physics_frame_count;
    result.final_delivery_frame = capture.completion.delivery_frame_count;
    result.preparation_block_count = capture.preparation_block_count;
    result.total_block_count = capture.total_block_count;
    result.pcm = bus.audible_interleaved_samples;
    result.scenario = spec.scenario;
    result.scenario_spec_sha256 = spec.identity_sha256;
    result.engine_provenance = capture.engine_provenance;
    result.scenario_provenance = capture.scenario_provenance;
    result.points.reserve(capture.total_block_count - capture.preparation_block_count);

    for (const auto &block : capture.blocks) {
        if (cancellation.stop_requested()) {
            return cancelled();
        }
        if (block.phase != EngineSessionBlockPhase::audible) {
            continue;
        }
        const std::uint64_t end =
            block.first_delivery_frame + block.delivery_frame_count;
        if (end <= capture.audible_first_delivery_frame || result.pcm.empty()) {
            return error(LifecycleErrorCode::invalid_capture,
                         "invalid-audible-block-horizon", "capture.blocks",
                         "audible lifecycle block precedes the audible horizon");
        }
        const std::uint64_t source_frame = std::min<std::uint64_t>(
            result.pcm.size() - 1U, end - capture.audible_first_delivery_frame - 1U);
        const auto &engine = block.telemetry.engine;
        result.points.push_back(
            {source_frame, block.telemetry.physics_step_end,
             static_cast<double>(block.telemetry.physics_step_end) /
                 static_cast<double>(kLifecyclePhysicsRateHz),
             engine.engine_speed_rpm, engine.ignition_enabled, engine.fuel_enabled,
             engine.starter_enabled, engine.torque.instantaneous_indicated_gas.value_nm,
             engine.torque.instantaneous_indicated_gas.availability ==
                     contract::Availability::available
                 ? LifecycleCapturePoint::PublishedAvailability::available
                 : LifecycleCapturePoint::PublishedAvailability::unavailable});
    }

    for (const auto &block : capture.blocks) {
        for (const auto &cycle : block.completed_cycles) {
            if (cancellation.stop_requested()) {
                return cancelled();
            }
            const double relative_start =
                cycle.start_boundary.delivery_frame -
                static_cast<double>(capture.audible_first_delivery_frame);
            const double relative_end =
                cycle.end_boundary.delivery_frame -
                static_cast<double>(capture.audible_first_delivery_frame);
            // JavaScript Math.round(x) is floor(x + .5), including for negative
            // cycle boundaries. Negative starts are discarded before conversion.
            const double rounded_start = std::floor(relative_start + 0.5);
            const double rounded_end = std::floor(relative_end + 0.5);
            if (!finite(rounded_start) || !finite(rounded_end) || rounded_start < 0.0 ||
                rounded_end <= rounded_start ||
                rounded_end > static_cast<double>(result.pcm.size())) {
                continue;
            }
            result.completed_cycles.push_back(
                {static_cast<std::uint64_t>(rounded_start),
                 static_cast<std::uint64_t>(rounded_end), cycle.mean_engine_speed_rpm});
        }
    }
    if (const auto failure = validate_capture_evidence(result)) {
        return *failure;
    }
    return result;
}

LifecycleResult<LifecycleDynamicStarterRelease>
choose_dynamic_starter_release(const LifecycleCaptureEvidence &startup_probe,
                               const double running_floor_rpm,
                               const std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
        return cancelled();
    }
    if (!role_matches(startup_probe, LifecycleScenarioRole::startup_probe) ||
        !finite(running_floor_rpm) || running_floor_rpm <= 0.0) {
        return error(
            LifecycleErrorCode::invalid_request, "invalid-startup-probe-request",
            "startup_probe",
            "dynamic release requires a startup probe and positive running floor");
    }
    if (const auto failure = validate_capture_evidence(startup_probe)) {
        return *failure;
    }
    const auto ignition_seconds_value = std::find_if(
        startup_probe.scenario.events.begin(), startup_probe.scenario.events.end(),
        [](const auto &event) { return event.id.value == "ignition-on"; });
    if (ignition_seconds_value == startup_probe.scenario.events.end() ||
        ignition_seconds_value->time.unit != "s") {
        return error(LifecycleErrorCode::invalid_request, "missing-ignition-event",
                     "startup_probe.scenario",
                     "startup probe has no ignition-on event");
    }
    const auto ignition =
        js_round_nonnegative((ignition_seconds_value->time.value -
                              startup_probe.scenario.audible_start.value) *
                             static_cast<double>(kLifecycleDeliveryRateHz));
    if (!ignition) {
        return error(LifecycleErrorCode::invalid_request, "invalid-ignition-event",
                     "startup_probe.scenario",
                     "startup probe ignition event is outside audible time");
    }
    const auto first_fire = std::find_if(
        startup_probe.points.begin(), startup_probe.points.end(),
        [&](const auto &point) {
            return point.source_frame >= *ignition && point.ignition_enabled &&
                   point.fuel_enabled &&
                   point.indicated_gas_availability ==
                       LifecycleCapturePoint::PublishedAvailability::available &&
                   point.indicated_gas_torque_nm > 0.0;
        });
    if (first_fire == startup_probe.points.end()) {
        return error(
            LifecycleErrorCode::no_admissible_event, "startup-probe-never-fires",
            "startup_probe.points",
            "startup probe never exposes positive available combustion torque");
    }
    const auto floor =
        std::find_if(first_fire, startup_probe.points.end(), [&](const auto &point) {
            return std::abs(point.rpm) >= running_floor_rpm;
        });
    if (floor == startup_probe.points.end()) {
        return error(LifecycleErrorCode::no_admissible_event,
                     "startup-probe-never-reaches-running-floor",
                     "startup_probe.points",
                     "startup probe never reaches the responsive running floor");
    }
    const auto release_cycle =
        std::find_if(startup_probe.completed_cycles.begin(),
                     startup_probe.completed_cycles.end(), [&](const auto &cycle) {
                         return cycle.start_frame >= floor->source_frame &&
                                cycle.end_frame < startup_probe.pcm.size();
                     });
    if (release_cycle == startup_probe.completed_cycles.end()) {
        return error(LifecycleErrorCode::no_admissible_event,
                     "missing-post-floor-cycle", "startup_probe.completed_cycles",
                     "startup probe has no complete post-floor crank cycle");
    }
    const std::uint64_t complete_seconds =
        release_cycle->end_frame / kLifecycleDeliveryRateHz;
    const std::uint64_t remaining_frames =
        release_cycle->end_frame % kLifecycleDeliveryRateHz;
    const std::uint64_t physics_tick =
        complete_seconds * kLifecyclePhysicsRateHz +
        (remaining_frames * kLifecyclePhysicsRateHz + kLifecycleDeliveryRateHz - 1U) /
            kLifecycleDeliveryRateHz;
    if (physics_tick == 0U) {
        return error(LifecycleErrorCode::internal_error, "zero-starter-release",
                     "startup_probe", "dynamic starter release resolved to zero");
    }
    return LifecycleDynamicStarterRelease{
        physics_tick,
        static_cast<double>(physics_tick) /
            static_cast<double>(kLifecyclePhysicsRateHz),
        *first_fire, *floor, *release_cycle};
}

LifecycleResult<LifecycleCookedStarter>
cook_lifecycle_starter(const LifecycleCaptureEvidence &capture,
                       const std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
        return cancelled();
    }
    if (!role_matches(capture, LifecycleScenarioRole::starter)) {
        return error(LifecycleErrorCode::invalid_request, "wrong-starter-role",
                     "capture.role",
                     "starter cooker requires the starter capture role");
    }
    if (const auto failure = validate_capture_evidence(capture)) {
        return *failure;
    }
    const auto stable_start = std::min<std::uint64_t>(
        capture.pcm.size() - 2U,
        std::max<std::uint64_t>(
            1U, *js_round_nonnegative(
                    std::max(0.3 * static_cast<double>(kLifecycleDeliveryRateHz),
                             static_cast<double>(capture.pcm.size()) * 0.3))));
    std::vector<LifecycleCaptureCycle> cycles;
    for (const auto &cycle : capture.completed_cycles) {
        if (cycle.start_frame >= stable_start &&
            cycle.end_frame <= capture.pcm.size()) {
            cycles.push_back(cycle);
        }
    }
    if (cycles.size() < 2U) {
        return error(
            LifecycleErrorCode::no_admissible_seam,
            "insufficient-stable-starter-cycles", "capture.completed_cycles",
            "starter capture has fewer than two complete post-settle crank cycles");
    }
    const std::uint64_t loop_start = cycles.front().start_frame;
    const std::uint64_t loop_end = cycles.back().end_frame;
    const std::uint32_t crossfade = static_cast<std::uint32_t>(std::max<std::uint64_t>(
        1U, std::min({cycles.front().end_frame - cycles.front().start_frame,
                      cycles.back().end_frame - cycles.back().start_frame,
                      (loop_end - loop_start - 1U) / 2U})));
    const double rpm = static_cast<double>(cycles.size()) *
                       kFourStrokeCycleRevolutions * 60.0 *
                       static_cast<double>(kLifecycleDeliveryRateHz) /
                       static_cast<double>(loop_end - loop_start);
    if (!finite(rpm) || rpm <= 0.0 || crossfade * 2U > loop_end - loop_start) {
        return error(LifecycleErrorCode::invalid_capture, "invalid-starter-loop",
                     "starter", "starter loop geometry or mean crank RPM is invalid");
    }
    return LifecycleCookedStarter{{rpm, rpm, loop_start, loop_end, crossfade,
                                   kLifecyclePresentationFadeFrames,
                                   kLifecyclePresentationFadeFrames},
                                  std::move(cycles)};
}

LifecycleResult<LifecycleStartupPresentation> cook_lifecycle_startup(
    const LifecycleScenarioSpec &spec, const LifecycleCaptureEvidence &capture,
    const LifecycleCaptureEvidence &starter_capture,
    const LifecycleCookedStarter &starter, const std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
        return cancelled();
    }
    if (spec.role != LifecycleScenarioRole::startup ||
        !role_matches(capture, LifecycleScenarioRole::startup) ||
        !role_matches(starter_capture, LifecycleScenarioRole::starter) ||
        starter.stable_cycles.size() < 2U) {
        return error(LifecycleErrorCode::invalid_request, "startup-role-mismatch",
                     "startup",
                     "startup cooker requires matched startup and starter inputs");
    }
    if (const auto failure = validate_capture_evidence(capture)) {
        return *failure;
    }
    if (const auto failure = validate_capture_evidence(starter_capture)) {
        return *failure;
    }
    const auto ignition_seconds_value = event_seconds(spec, "ignition-on");
    const auto release_seconds_value = event_seconds(spec, "starter-release");
    const auto ignition_frame_value = ignition_seconds_value
                                          ? event_frame(spec, *ignition_seconds_value)
                                          : std::nullopt;
    const auto release_frame_value = release_seconds_value
                                         ? event_frame(spec, *release_seconds_value)
                                         : std::nullopt;
    if (!ignition_frame_value || !release_frame_value ||
        *release_frame_value >= capture.pcm.size()) {
        return error(LifecycleErrorCode::invalid_request, "invalid-startup-events",
                     "scenario.events",
                     "startup ignition or starter-release event is invalid");
    }
    const auto first_combustion = std::find_if(
        capture.points.begin(), capture.points.end(), [&](const auto &point) {
            return point.source_frame >= *ignition_frame_value &&
                   point.ignition_enabled && point.fuel_enabled &&
                   point.indicated_gas_availability ==
                       LifecycleCapturePoint::PublishedAvailability::available &&
                   point.indicated_gas_torque_nm > 0.0;
        });
    if (first_combustion == capture.points.end()) {
        return error(LifecycleErrorCode::no_admissible_event,
                     "startup-no-positive-combustion", "capture.points",
                     "startup exposes no positive indicated-gas torque after ignition");
    }
    auto entry_result =
        choose_pre_event_seam("startup entry", capture, first_combustion->source_frame,
                              starter_capture, starter.stable_cycles, cancellation);
    if (const auto *failure = std::get_if<LifecycleError>(&entry_result)) {
        return *failure;
    }
    auto entry = std::get<LifecycleSeam>(std::move(entry_result));
    entry.target = "starter";
    entry.target_reference = "starter-loop";

    const std::uint64_t stable_tail_start =
        *release_frame_value + kLifecycleDeliveryRateHz;
    std::vector<LifecycleCaptureCycle> source_candidates;
    for (const auto &cycle : capture.completed_cycles) {
        if (cycle.start_frame >= stable_tail_start &&
            cycle.end_frame <= capture.pcm.size()) {
            source_candidates.push_back(cycle);
        }
    }
    auto exit_result = choose_event_seam("startup exit", capture.pcm, source_candidates,
                                         capture.pcm, source_candidates, cancellation);
    if (const auto *failure = std::get_if<LifecycleError>(&exit_result)) {
        return *failure;
    }
    auto exit = std::get<LifecycleSeam>(std::move(exit_result));
    exit.target = "running";
    exit.target_reference = "startup-post-release-cycle-reference";

    std::vector<LifecycleCheckpoint> checkpoints;
    checkpoints.reserve(4U);
    auto ignition = checkpoint("ignition-on", *ignition_frame_value, capture, "exact",
                               "authored-operating-state-event");
    auto combustion =
        checkpoint("first-combustion", first_combustion->source_frame, capture,
                   "advance-block", "first-positive-indicated-gas-torque-proxy");
    auto release = checkpoint("starter-release", *release_frame_value, capture, "exact",
                              "authored-operating-state-event");
    auto running =
        checkpoint("running-floor", exit.source_frame, capture, "cycle-boundary",
                   "post-release-cycle-correlated-to-captured-running-idle");
    for (auto *candidate : {&ignition, &combustion, &release, &running}) {
        if (const auto *failure = std::get_if<LifecycleError>(candidate)) {
            return *failure;
        }
        checkpoints.push_back(std::get<LifecycleCheckpoint>(std::move(*candidate)));
    }
    if (entry.source_frame + entry.crossfade_frames > *ignition_frame_value ||
        *ignition_frame_value > first_combustion->source_frame ||
        first_combustion->source_frame > *release_frame_value ||
        *release_frame_value > exit.source_frame) {
        return error(LifecycleErrorCode::invalid_capture, "invalid-startup-order",
                     "startup", "startup checkpoints and seams are not ordered");
    }
    return LifecycleStartupPresentation{std::move(checkpoints), std::move(entry),
                                        std::move(exit)};
}

LifecycleResult<LifecycleShutdownPresentation>
cook_lifecycle_shutdown(const LifecycleScenarioSpec &spec,
                        const LifecycleCaptureEvidence &capture,
                        const std::stop_token cancellation) {
    if (spec.role != LifecycleScenarioRole::shutdown ||
        !role_matches(capture, LifecycleScenarioRole::shutdown)) {
        return error(LifecycleErrorCode::invalid_request, "shutdown-role-mismatch",
                     "shutdown", "shutdown cooker requires the shutdown capture role");
    }
    return cook_shutdown_impl(spec, capture, false, cancellation);
}

LifecycleResult<LifecycleShutdownPresentation>
cook_lifecycle_elevated_shutdown(const LifecycleScenarioSpec &spec,
                                 const LifecycleCaptureEvidence &capture,
                                 const std::stop_token cancellation) {
    if (spec.role != LifecycleScenarioRole::shutdown_elevated ||
        !role_matches(capture, LifecycleScenarioRole::shutdown_elevated)) {
        return error(LifecycleErrorCode::invalid_request,
                     "elevated-shutdown-role-mismatch", "shutdown-elevated",
                     "elevated shutdown cooker requires the elevated role");
    }
    return cook_shutdown_impl(spec, capture, true, cancellation);
}

LifecycleResult<LifecycleStartupAdmissionSeed>
make_lifecycle_startup_admission_seed(const ResponsiveBakeProfile &profile,
                                      const LifecycleDynamicStarterRelease &release) {
    const auto report = validate_responsive_bake_profile(profile);
    if (!report.ok() || release.physics_tick == 0U || !finite(release.seconds) ||
        release.seconds <= 0.0) {
        return error(LifecycleErrorCode::invalid_request,
                     "invalid-startup-admission-input", "startup_admission",
                     "startup admission requires a valid profile and dynamic release");
    }
    LifecycleStartupAdmissionSeed result;
    result.running_floor_rpm = profile.rpm.outer_minimum_rpm;
    result.held_anchor_floor_rpm = profile.rpm.anchors.front();
    result.release = release;
    for (const auto &lane : profile.capture.load_lanes) {
        double gain = 0.0;
        if (lane.id == "coast") {
            gain = 0.5761944116355173;
        } else if (lane.id == "mid") {
            gain = 0.7179832867135557;
        } else if (lane.id == "power") {
            gain = 0.7940403012442127;
        } else {
            return error(LifecycleErrorCode::invalid_request,
                         "unsupported-admission-lane", "profile.capture.load_lanes",
                         "startup admission has no accepted gain for a profile lane");
        }
        result.lanes.push_back({lane.id, lane.throttle_01, gain});
    }
    return result;
}

LifecycleResult<LifecycleStartupAdmissionFloorEvidence>
bind_lifecycle_startup_admission_floor_evidence(
    const LifecycleStartupAdmissionSeed &seed, const LifecycleHeldAtlasBinding &held) {
    if (held.manifest_path.empty() || held.manifest_sha256.is_zero() ||
        held.load_coordinate.empty() || seed.lanes.empty() ||
        !finite(seed.running_floor_rpm) || seed.running_floor_rpm <= 0.0 ||
        !finite(seed.held_anchor_floor_rpm) || seed.held_anchor_floor_rpm <= 0.0) {
        return error(LifecycleErrorCode::invalid_request, "invalid-held-atlas-binding",
                     "held", "startup admission held-atlas binding is incomplete");
    }
    LifecycleStartupAdmissionFloorEvidence result;
    result.candidate_status = seed.candidate_status;
    result.atlas_manifest = held.manifest_path;
    result.atlas_manifest_sha256 = held.manifest_sha256;
    result.atlas_load_coordinate = held.load_coordinate;
    result.running_floor_rpm = seed.running_floor_rpm;
    result.held_anchor_floor_rpm = seed.held_anchor_floor_rpm;
    result.release.seconds = seed.release.seconds;
    result.release.first_positive_combustion_frame =
        seed.release.first_positive_combustion.source_frame;
    result.release.running_floor_frame = seed.release.running_floor.source_frame;
    result.release.release_cycle = seed.release.release_cycle;
    result.lanes = seed.lanes;
    return result;
}

LifecycleResult<LifecycleStartupAdmissionPresentation>
bind_lifecycle_startup_admission_presentation(
    const LifecycleStartupAdmissionFloorEvidence &evidence,
    const LifecycleEvidenceArtifactBinding &artifact) {
    if (evidence.schema != kStartupAdmissionFloorEvidenceSchema ||
        evidence.atlas_load_coordinate.empty() ||
        evidence.atlas_manifest_sha256.is_zero() || artifact.path.empty() ||
        artifact.sha256.is_zero() || evidence.lanes.empty()) {
        return error(LifecycleErrorCode::invalid_request,
                     "invalid-admission-evidence-binding", "startup_admission",
                     "startup admission evidence artifact binding is incomplete");
    }
    LifecycleStartupAdmissionPresentation result;
    result.running_bed_load_coordinate = evidence.atlas_load_coordinate;
    result.lanes = evidence.lanes;
    result.evidence.path = artifact.path;
    result.evidence.sha256 = artifact.sha256;
    result.evidence.corrected_held_manifest_sha256 = evidence.atlas_manifest_sha256;
    return result;
}

LifecycleResult<LifecycleCookedPackage> assemble_lifecycle_package(
    LifecycleCaptureEvidence starter_capture, LifecycleCaptureEvidence startup_capture,
    LifecycleCaptureEvidence shutdown_capture,
    std::optional<LifecycleCaptureEvidence> elevated_shutdown_capture,
    const LifecycleCookedStarter &starter, LifecycleStartupPresentation startup,
    LifecycleShutdownPresentation shutdown,
    std::optional<LifecycleShutdownPresentation> elevated_shutdown,
    LifecycleStartupAdmissionSeed startup_admission_seed) {
    if (!role_matches(starter_capture, LifecycleScenarioRole::starter) ||
        !role_matches(startup_capture, LifecycleScenarioRole::startup) ||
        !role_matches(shutdown_capture, LifecycleScenarioRole::shutdown) ||
        elevated_shutdown_capture.has_value() != elevated_shutdown.has_value() ||
        (elevated_shutdown_capture &&
         !role_matches(*elevated_shutdown_capture,
                       LifecycleScenarioRole::shutdown_elevated))) {
        return error(LifecycleErrorCode::invalid_request,
                     "lifecycle-package-role-mismatch", "captures",
                     "lifecycle package captures and presentations do not match roles");
    }
    for (const auto *capture :
         {&starter_capture, &startup_capture, &shutdown_capture}) {
        if (const auto failure = validate_capture_evidence(*capture)) {
            return *failure;
        }
    }
    if (elevated_shutdown_capture) {
        if (const auto failure =
                validate_capture_evidence(*elevated_shutdown_capture)) {
            return *failure;
        }
    }
    const std::string &engine_id = starter_capture.engine_id;
    const std::string &bus_id = starter_capture.bus_id;
    const auto same_identity = [&](const LifecycleCaptureEvidence &capture) {
        return capture.engine_id == engine_id && capture.bus_id == bus_id &&
               capture.engine_provenance == starter_capture.engine_provenance;
    };
    if (!same_identity(startup_capture) || !same_identity(shutdown_capture) ||
        (elevated_shutdown_capture && !same_identity(*elevated_shutdown_capture))) {
        return error(
            LifecycleErrorCode::capture_mismatch,
            "lifecycle-package-provenance-mismatch", "captures",
            "lifecycle captures do not share engine, bus, and compiler provenance");
    }

    LifecycleCookedPackage result;
    result.id = engine_id + "-lifecycle-preview";
    result.engine_id = engine_id;
    result.bus_id = bus_id;
    result.starter = starter.presentation;
    result.startup = std::move(startup);
    result.shutdown = std::move(shutdown);
    result.shutdown_elevated = std::move(elevated_shutdown);
    result.startup_admission_seed = std::move(startup_admission_seed);
    result.captures.reserve(elevated_shutdown_capture ? 4U : 3U);
    result.captures.push_back(std::move(starter_capture));
    result.captures.push_back(std::move(startup_capture));
    result.captures.push_back(std::move(shutdown_capture));
    if (elevated_shutdown_capture) {
        result.captures.push_back(std::move(*elevated_shutdown_capture));
    }
    return result;
}

} // namespace crankwave::responsive
