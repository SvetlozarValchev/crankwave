#include "crankwave/responsive/held_texture.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::responsive {
namespace {

void require(contract::ValidationReport &report, const bool condition,
             const contract::ContractIssueCode code, std::string path,
             std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] contract::ValidationReport
one_issue(const contract::ContractIssueCode code, std::string path,
          std::string message) {
    contract::ValidationReport report;
    report.add(code, std::move(path), std::move(message));
    return report;
}

[[nodiscard]] bool finite(const double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] contract::ValidationReport
validate_transform_capture(const FiniteResponsiveCapture &capture) {
    using enum contract::ContractIssueCode;
    contract::ValidationReport report;
    require(report,
            capture.physics_rate.numerator == kResponsivePhysicsRateHz &&
                capture.physics_rate.denominator == 1U,
            unsupported_value, "capture.physics_rate",
            "held transform requires exact 10 kHz physics");
    require(report,
            capture.delivery_rate.numerator == 192'000U &&
                capture.delivery_rate.denominator == 1U,
            unsupported_value, "capture.delivery_rate",
            "held transform requires exact 192 kHz delivery");
    require(report, capture.delivery_frames_per_block == 3'840U, unsupported_value,
            "capture.delivery_frames_per_block",
            "held transform requires exact 20 ms delivery blocks");
    require(report,
            capture.blocks.size() == capture.total_block_count &&
                capture.preparation_block_count < capture.total_block_count,
            inconsistent_shape, "capture.blocks",
            "capture block count differs from its finite horizon");
    require(report,
            capture.audible_first_delivery_frame ==
                    capture.preparation_block_count *
                        capture.delivery_frames_per_block &&
                capture.audible_delivery_frame_count ==
                    (capture.total_block_count - capture.preparation_block_count) *
                        capture.delivery_frames_per_block,
            inconsistent_semantics, "capture.audible_horizon",
            "capture audible horizon differs from its block partition");
    require(report,
            !capture.buses.empty() &&
                capture.buses.size() == capture.selected_bus_ids.size(),
            inconsistent_shape, "capture.buses",
            "held transform requires selected capture buses");
    for (std::size_t index = 0U; index < capture.buses.size(); ++index) {
        const auto &bus = capture.buses[index];
        require(report,
                bus.descriptor.id == capture.selected_bus_ids[index] &&
                    bus.descriptor.channel_count == 1U &&
                    bus.descriptor.sample_rate == capture.delivery_rate &&
                    bus.audible_interleaved_samples.size() ==
                        capture.audible_delivery_frame_count,
                inconsistent_shape, "capture.buses[" + std::to_string(index) + "]",
                "held capture bus is not one complete mono audible tape");
        require(report,
                std::ranges::all_of(bus.audible_interleaved_samples,
                                    [](const float sample) { return finite(sample); }),
                invalid_value, "capture.buses[" + std::to_string(index) + "].samples",
                "held capture PCM contains a non-finite sample");
    }
    std::uint64_t prior_delivery = 0U;
    double prior_revolutions = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < capture.blocks.size(); ++index) {
        const auto &endpoint = capture.blocks[index].endpoint;
        const bool ordered =
            (index == 0U || endpoint.delivery_frame > prior_delivery) &&
            endpoint.unwrapped_crank_revolutions > prior_revolutions;
        require(report,
                ordered && finite(endpoint.engine_speed_rpm) &&
                    finite(endpoint.mean_intake_manifold_pressure_pa_abs) &&
                    endpoint.mean_intake_manifold_pressure_pa_abs > 0.0 &&
                    finite(endpoint.requested_throttle_01) &&
                    finite(endpoint.unwrapped_crank_revolutions),
                invalid_value, "capture.blocks[" + std::to_string(index) + "].endpoint",
                "held capture endpoints must be finite and strictly ordered");
        prior_delivery = endpoint.delivery_frame;
        prior_revolutions = endpoint.unwrapped_crank_revolutions;
    }
    return report;
}

[[nodiscard]] HeldResult<double>
frame_at_revolutions(const FiniteResponsiveCapture &capture, const double revolutions) {
    using enum contract::ContractIssueCode;
    if (!finite(revolutions) || capture.blocks.size() < 2U) {
        return one_issue(
            invalid_value, "capture.blocks",
            "phase lookup requires finite revolutions and at least two endpoints");
    }
    const auto &first = capture.blocks.front().endpoint;
    const auto &last = capture.blocks.back().endpoint;
    if (revolutions < first.unwrapped_crank_revolutions ||
        revolutions > last.unwrapped_crank_revolutions) {
        return one_issue(invalid_value, "interval",
                         "phase lookup lies outside captured telemetry");
    }
    std::size_t lower = 0U;
    std::size_t upper = capture.blocks.size() - 1U;
    while (upper - lower > 1U) {
        const std::size_t middle = (lower + upper) >> 1U;
        if (capture.blocks[middle].endpoint.unwrapped_crank_revolutions <=
            revolutions) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    const auto &left = capture.blocks[lower].endpoint;
    const auto &right = capture.blocks[upper].endpoint;
    const double revolution_delta =
        right.unwrapped_crank_revolutions - left.unwrapped_crank_revolutions;
    if (!(revolution_delta > 0.0)) {
        return one_issue(inconsistent_semantics, "capture.blocks",
                         "adjacent telemetry endpoints have no crank progress");
    }
    const double amount =
        (revolutions - left.unwrapped_crank_revolutions) / revolution_delta;
    const double frame =
        static_cast<double>(left.delivery_frame) +
        static_cast<double>(right.delivery_frame - left.delivery_frame) * amount;
    if (!finite(frame)) {
        return one_issue(invalid_value, "interval",
                         "phase lookup produced a non-finite delivery frame");
    }
    return frame;
}

[[nodiscard]] double signal_rms(const std::span<const float> samples) {
    double square_sum = 0.0;
    for (const float sample : samples) {
        const double value = static_cast<double>(sample);
        square_sum += value * value;
    }
    return std::sqrt(square_sum / static_cast<double>(samples.size()));
}

[[nodiscard]] double error_rms(const std::span<const float> reference,
                               const std::span<const float> candidate) {
    double square_sum = 0.0;
    for (std::size_t index = 0U; index < reference.size(); ++index) {
        const double error = static_cast<double>(reference[index]) -
                             static_cast<double>(candidate[index]);
        square_sum += error * error;
    }
    return std::sqrt(square_sum / static_cast<double>(reference.size()));
}

[[nodiscard]] double derivative_rms(const std::span<const float> samples) {
    double square_sum = 0.0;
    for (std::size_t index = 1U; index < samples.size(); ++index) {
        const double delta = static_cast<double>(samples[index]) -
                             static_cast<double>(samples[index - 1U]);
        square_sum += delta * delta;
    }
    return std::sqrt(square_sum / static_cast<double>(samples.size() - 1U));
}

[[nodiscard]] double smoothstep(const double amount) noexcept {
    return amount * amount * (3.0 - 2.0 * amount);
}

[[nodiscard]] contract::Sha256Digest
route_identity(const HeldStateScenarioSpec &spec,
               const FiniteResponsiveCapture &capture, const HeldCookedRoute &route) {
    detail::CanonicalIdentityBytes identity{kHeldTextureIdentityMethodId};
    identity.digest(spec.identity_sha256);
    identity.digest(capture.engine_provenance.bundle.sha256);
    identity.digest(capture.scenario_provenance.bundle.sha256);
    identity.string(route.route_id);
    identity.f64(route.source_interval.start_revolutions);
    identity.f64(route.source_interval.end_revolutions);
    identity.digest(route.mean_payload_sha256);
    identity.digest(route.residual_payload_sha256);
    identity.f64(route.telemetry.mean_manifold_pressure_pa_abs);
    identity.f64(route.telemetry.rpm_error_rms);
    identity.f64(route.telemetry.maximum_absolute_rpm_error);
    identity.u64(route.telemetry.telemetry_endpoint_count);
    identity.string(kHeldCaptureMethodId);
    identity.string(kHeldDecompositionMethodId);
    return identity.finish();
}

[[nodiscard]] contract::Sha256Digest cell_identity(const HeldStateScenarioSpec &spec,
                                                   const HeldCookedCell &cell) {
    detail::CanonicalIdentityBytes identity{kHeldStateIdentityMethodId};
    identity.digest(spec.identity_sha256);
    identity.string(cell.id);
    identity.f64(cell.rpm);
    identity.string(cell.lane_id);
    identity.f64(cell.throttle_01);
    identity.u64(cell.routes.size());
    for (const auto &route : cell.routes) {
        identity.string(route.route_id);
        identity.digest(route.identity_sha256);
    }
    return identity.finish();
}

} // namespace

HeldResult<HeldCaptureInterval>
choose_held_capture_interval(const FiniteResponsiveCapture &capture) {
    using enum contract::ContractIssueCode;
    auto report = validate_transform_capture(capture);
    if (!report.ok()) {
        return report;
    }
    const auto first_audible =
        std::ranges::find_if(capture.blocks, [&](const ResponsiveCaptureBlock &block) {
            return block.endpoint.delivery_frame >=
                   capture.audible_first_delivery_frame;
        });
    if (first_audible == capture.blocks.end()) {
        return one_issue(missing_value, "capture.blocks",
                         "capture has no audible telemetry endpoint");
    }
    const double start =
        std::ceil(
            (first_audible->endpoint.unwrapped_crank_revolutions +
             static_cast<double>(kHeldGuardCycleCountBefore) * kHeldCycleRevolutions) /
            kHeldCycleRevolutions) *
        kHeldCycleRevolutions;
    const double end =
        start + static_cast<double>(kHeldCapturedCycleCount) * kHeldCycleRevolutions;
    const double required_tail =
        end + static_cast<double>(kHeldGuardCycleCountAfter) * kHeldCycleRevolutions;
    if (required_tail > capture.blocks.back().endpoint.unwrapped_crank_revolutions) {
        return one_issue(inconsistent_semantics, "capture.blocks",
                         "capture ends before the required held-texture guard tail");
    }
    return HeldCaptureInterval{start, end};
}

HeldResult<std::vector<float>>
phase_normalize_held_capture(const FiniteResponsiveCapture &capture,
                             const std::size_t bus_index,
                             const HeldCaptureInterval &interval) {
    using enum contract::ContractIssueCode;
    auto report = validate_transform_capture(capture);
    require(report, bus_index < capture.buses.size(), invalid_value, "bus_index",
            "held capture bus index is outside the capture");
    require(report,
            finite(interval.start_revolutions) && finite(interval.end_revolutions) &&
                interval.end_revolutions > interval.start_revolutions,
            invalid_value, "interval",
            "held capture interval must be finite and increasing");
    require(report,
            interval.end_revolutions - interval.start_revolutions ==
                static_cast<double>(kHeldCapturedCycleCount) * kHeldCycleRevolutions,
            inconsistent_semantics, "interval",
            "held capture interval must contain exactly 48 cycles");
    if (!report.ok()) {
        return report;
    }
    const auto &pcm = capture.buses[bus_index].audible_interleaved_samples;
    std::vector<float> result(kHeldNormalizedSampleCount);
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const double revolutions = interval.start_revolutions +
                                   static_cast<double>(index) * kHeldCycleRevolutions /
                                       static_cast<double>(kHeldSamplesPerCycle);
        auto frame_result = frame_at_revolutions(capture, revolutions);
        if (const auto *failure =
                std::get_if<contract::ValidationReport>(&frame_result)) {
            return *failure;
        }
        const double position =
            std::get<double>(frame_result) -
            static_cast<double>(capture.audible_first_delivery_frame);
        const auto left = static_cast<std::int64_t>(std::floor(position));
        if (left < 0 || static_cast<std::uint64_t>(left + 1) >= pcm.size()) {
            return one_issue(inconsistent_semantics, "capture.buses",
                             "phase-normalized sample lies outside audible PCM");
        }
        const double left_sample =
            static_cast<double>(pcm[static_cast<std::size_t>(left)]);
        const double right_sample =
            static_cast<double>(pcm[static_cast<std::size_t>(left + 1)]);
        result[index] = static_cast<float>(left_sample +
                                           (right_sample - left_sample) *
                                               (position - static_cast<double>(left)));
    }
    return result;
}

HeldResult<HeldTextureDecomposition>
decompose_held_texture(const std::span<const float> normalized_source) {
    using enum contract::ContractIssueCode;
    if (normalized_source.size() != kHeldNormalizedSampleCount) {
        return one_issue(inconsistent_shape, "normalized_source",
                         "held texture source must contain 48 times 4096 samples");
    }
    if (!std::ranges::all_of(normalized_source,
                             [](const float sample) { return finite(sample); })) {
        return one_issue(invalid_value, "normalized_source",
                         "held texture source contains a non-finite sample");
    }

    std::vector<double> raw_mean(kHeldSamplesPerCycle, 0.0);
    for (std::size_t cycle = 0U; cycle < kHeldCapturedCycleCount; ++cycle) {
        const std::size_t offset = cycle * kHeldSamplesPerCycle;
        for (std::size_t phase = 0U; phase < kHeldSamplesPerCycle; ++phase) {
            raw_mean[phase] += static_cast<double>(normalized_source[offset + phase]);
        }
    }
    for (double &sample : raw_mean) {
        sample /= static_cast<double>(kHeldCapturedCycleCount);
    }

    const double target_boundary_delta =
        0.5 * ((raw_mean[1U] - raw_mean[0U]) +
               (raw_mean.back() - raw_mean[raw_mean.size() - 2U]));
    const double closure_correction =
        raw_mean.front() - raw_mean.back() - target_boundary_delta;
    HeldTextureDecomposition result;
    result.mean.resize(kHeldSamplesPerCycle);
    for (std::size_t phase = 0U; phase < kHeldSamplesPerCycle; ++phase) {
        result.mean[phase] = static_cast<float>(
            raw_mean[phase] + closure_correction * static_cast<double>(phase) /
                                  static_cast<double>(kHeldSamplesPerCycle - 1U));
    }

    constexpr std::uint32_t taper_frames = static_cast<std::uint32_t>(
        kHeldSamplesPerCycle * kHeldResidualTaperFractionPerEdge + 0.5);
    result.residuals.resize(normalized_source.size());
    std::vector<float> raw_residual(normalized_source.size());
    std::vector<float> reconstructed(normalized_source.size());
    double maximum_boundary_magnitude = 0.0;
    for (std::size_t cycle = 0U; cycle < kHeldCapturedCycleCount; ++cycle) {
        const std::size_t offset = cycle * kHeldSamplesPerCycle;
        for (std::size_t phase = 0U; phase < kHeldSamplesPerCycle; ++phase) {
            double taper = 1.0;
            if (phase < taper_frames) {
                taper = smoothstep(static_cast<double>(phase) /
                                   static_cast<double>(taper_frames - 1U));
            } else if (phase >= kHeldSamplesPerCycle - taper_frames) {
                taper =
                    smoothstep(static_cast<double>(kHeldSamplesPerCycle - 1U - phase) /
                               static_cast<double>(taper_frames - 1U));
            }
            const std::size_t index = offset + phase;
            raw_residual[index] =
                static_cast<float>(static_cast<double>(normalized_source[index]) -
                                   static_cast<double>(result.mean[phase]));
            result.residuals[index] =
                static_cast<float>(static_cast<double>(raw_residual[index]) * taper);
            reconstructed[index] =
                static_cast<float>(static_cast<double>(result.mean[phase]) +
                                   static_cast<double>(result.residuals[index]));
        }
        maximum_boundary_magnitude =
            std::max({maximum_boundary_magnitude,
                      std::abs(static_cast<double>(result.residuals[offset])),
                      std::abs(static_cast<double>(
                          result.residuals[offset + kHeldSamplesPerCycle - 1U]))});
    }

    const double source_rms = signal_rms(normalized_source);
    const double safe_source_rms = std::max(source_rms, 1e-30);
    const double seam = std::abs(static_cast<double>(result.mean.front()) -
                                 static_cast<double>(result.mean.back()));
    result.metrics = {
        source_rms,
        signal_rms(result.mean),
        signal_rms(raw_residual) / safe_source_rms,
        signal_rms(result.residuals) / safe_source_rms,
        error_rms(normalized_source, reconstructed) / safe_source_rms,
        seam,
        seam / std::max(derivative_rms(result.mean), 1e-30),
        std::abs(closure_correction),
        std::abs(closure_correction) / safe_source_rms,
        maximum_boundary_magnitude,
        taper_frames,
    };
    return result;
}

HeldResult<HeldTelemetrySummary>
summarize_held_interval_telemetry(const FiniteResponsiveCapture &capture,
                                  const HeldCaptureInterval &interval,
                                  const double target_rpm) {
    using enum contract::ContractIssueCode;
    if (!finite(target_rpm) || !(target_rpm > 0.0)) {
        return one_issue(invalid_value, "target_rpm",
                         "held telemetry target RPM must be finite and positive");
    }
    auto begin_result = frame_at_revolutions(capture, interval.start_revolutions);
    if (const auto *failure = std::get_if<contract::ValidationReport>(&begin_result)) {
        return *failure;
    }
    auto end_result = frame_at_revolutions(capture, interval.end_revolutions);
    if (const auto *failure = std::get_if<contract::ValidationReport>(&end_result)) {
        return *failure;
    }
    const double begin = std::get<double>(begin_result);
    const double end = std::get<double>(end_result);
    HeldTelemetrySummary result;
    double map_sum = 0.0;
    double rpm_error_square_sum = 0.0;
    for (const auto &block : capture.blocks) {
        const auto &endpoint = block.endpoint;
        if (static_cast<double>(endpoint.delivery_frame) < begin ||
            static_cast<double>(endpoint.delivery_frame) > end) {
            continue;
        }
        map_sum += endpoint.mean_intake_manifold_pressure_pa_abs;
        const double error = endpoint.engine_speed_rpm - target_rpm;
        rpm_error_square_sum += error * error;
        result.maximum_absolute_rpm_error =
            std::max(result.maximum_absolute_rpm_error, std::abs(error));
        ++result.telemetry_endpoint_count;
    }
    if (result.telemetry_endpoint_count == 0U) {
        return one_issue(missing_value, "capture.blocks",
                         "held capture interval contains no telemetry endpoints");
    }
    result.mean_manifold_pressure_pa_abs =
        map_sum / static_cast<double>(result.telemetry_endpoint_count);
    result.rpm_error_rms = std::sqrt(
        rpm_error_square_sum / static_cast<double>(result.telemetry_endpoint_count));
    return result;
}

contract::Sha256Digest
held_float32_payload_identity(const std::span<const float> samples) {
    std::vector<std::byte> bytes;
    bytes.reserve(samples.size() * sizeof(float));
    for (const float sample : samples) {
        const std::uint32_t value = std::bit_cast<std::uint32_t>(sample);
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }
    return contract::sha256(bytes);
}

HeldResult<HeldCookedCell> cook_held_state(const HeldStateScenarioSpec &spec,
                                           const FiniteResponsiveCapture &capture) {
    using enum contract::ContractIssueCode;
    auto report = validate_transform_capture(capture);
    require(report, capture.engine_id == spec.scenario.engine.value,
            inconsistent_semantics, "capture.engine_id",
            "held capture names a different engine than its state spec");
    require(report, capture.scenario_id == spec.scenario.id.value,
            inconsistent_semantics, "capture.scenario_id",
            "held capture names a different scenario than its state spec");
    require(report,
            capture.preparation_block_count == spec.preparation_block_count &&
                capture.total_block_count == spec.total_block_count,
            inconsistent_semantics, "capture.horizon",
            "held capture horizon differs from its state spec");
    if (!report.ok()) {
        return report;
    }

    auto interval_result = choose_held_capture_interval(capture);
    if (const auto *failure =
            std::get_if<contract::ValidationReport>(&interval_result)) {
        return *failure;
    }
    const auto interval = std::get<HeldCaptureInterval>(interval_result);
    auto telemetry_result =
        summarize_held_interval_telemetry(capture, interval, spec.rpm);
    if (const auto *failure =
            std::get_if<contract::ValidationReport>(&telemetry_result)) {
        return *failure;
    }
    const auto telemetry = std::get<HeldTelemetrySummary>(telemetry_result);

    HeldCookedCell result;
    result.id = spec.id;
    result.rpm = spec.rpm;
    result.lane_id = spec.lane.id;
    result.throttle_01 = spec.lane.throttle_01;
    result.coalesced_authored_lanes.push_back(spec.lane.id);
    result.coalesced_capture_throttles_01.push_back(spec.lane.throttle_01);
    result.routes.reserve(capture.buses.size());
    for (std::size_t bus_index = 0U; bus_index < capture.buses.size(); ++bus_index) {
        auto normalized_result =
            phase_normalize_held_capture(capture, bus_index, interval);
        if (const auto *failure =
                std::get_if<contract::ValidationReport>(&normalized_result)) {
            return *failure;
        }
        auto normalized = std::get<std::vector<float>>(std::move(normalized_result));
        auto decomposition_result = decompose_held_texture(normalized);
        if (const auto *failure =
                std::get_if<contract::ValidationReport>(&decomposition_result)) {
            return *failure;
        }
        HeldCookedRoute route;
        route.route_id = capture.buses[bus_index].descriptor.id;
        route.source_interval = interval;
        route.texture =
            std::get<HeldTextureDecomposition>(std::move(decomposition_result));
        route.telemetry = telemetry;
        route.mean_payload_sha256 = held_float32_payload_identity(route.texture.mean);
        route.residual_payload_sha256 =
            held_float32_payload_identity(route.texture.residuals);
        route.identity_sha256 = route_identity(spec, capture, route);
        result.routes.push_back(std::move(route));
    }
    result.identity_sha256 = cell_identity(spec, result);
    return result;
}

} // namespace crankwave::responsive
