#include "crankwave/responsive/directional_cook.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

[[nodiscard]] std::string_view
direction_id(const DirectionalSweepDirection direction) noexcept {
    return direction == DirectionalSweepDirection::rising ? "rising" : "falling";
}

[[nodiscard]] std::string number_token(const double value) {
    char storage[64]{};
    const auto [end, error] = std::to_chars(storage, storage + sizeof(storage), value);
    return error == std::errc{} ? std::string{storage, end} : std::string{};
}

[[nodiscard]] contract::ValidationReport
validate_capture(const DirectionalSweepScenarioSpec &spec,
                 const FiniteResponsiveCapture &capture) {
    using enum contract::ContractIssueCode;
    contract::ValidationReport report;
    require(report, capture.engine_id == spec.scenario.engine.value,
            inconsistent_semantics, "capture.engine_id",
            "directional capture names a different engine than its scenario spec");
    require(report, capture.scenario_id == spec.scenario.id.value,
            inconsistent_semantics, "capture.scenario_id",
            "directional capture names a different scenario than its scenario spec");
    require(report,
            !spec.identity_sha256.is_zero() &&
                !spec.profile_selection_sha256.is_zero() &&
                !capture.engine_provenance.bundle.sha256.is_zero() &&
                !capture.scenario_provenance.bundle.sha256.is_zero(),
            invalid_value, "capture.provenance",
            "directional scenario and capture provenance identities must be nonzero");
    require(report, capture.motion_mode == EngineMotionMode::held_dyno,
            unsupported_value, "capture.motion_mode",
            "directional capture requires held-dyno execution");
    require(report,
            capture.physics_rate.numerator == kResponsivePhysicsRateHz &&
                capture.physics_rate.denominator == 1U &&
                capture.delivery_rate.numerator == 192'000U &&
                capture.delivery_rate.denominator == 1U,
            unsupported_value, "capture.rates",
            "directional capture requires exact 10 kHz physics and 192 kHz delivery");
    require(report,
            capture.physics_frames_per_block == 200U &&
                capture.delivery_frames_per_block == 3'840U,
            unsupported_value, "capture.block_shape",
            "directional capture requires exact 20 ms native blocks");
    require(report,
            capture.preparation_block_count == spec.preparation_block_count &&
                capture.total_block_count == spec.total_block_count &&
                capture.blocks.size() == capture.total_block_count,
            inconsistent_shape, "capture.blocks",
            "directional capture horizon differs from its scenario spec");
    require(report,
            capture.audible_first_delivery_frame ==
                    capture.preparation_block_count *
                        capture.delivery_frames_per_block &&
                capture.audible_delivery_frame_count ==
                    (capture.total_block_count - capture.preparation_block_count) *
                        capture.delivery_frames_per_block &&
                capture.total_delivery_frame_count ==
                    capture.total_block_count * capture.delivery_frames_per_block,
            inconsistent_semantics, "capture.audible_horizon",
            "directional capture audible horizon differs from its block partition");
    require(report,
            !capture.buses.empty() &&
                capture.buses.size() == capture.selected_bus_ids.size(),
            missing_value, "capture.buses",
            "directional capture requires selected source-route buses");
    std::map<std::string, bool, std::less<>> bus_ids;
    for (std::size_t index = 0U; index < capture.buses.size(); ++index) {
        const auto &bus = capture.buses[index];
        require(
            report,
            !capture.selected_bus_ids[index].empty() &&
                bus_ids.emplace(capture.selected_bus_ids[index], true).second &&
                bus.descriptor.id == capture.selected_bus_ids[index] &&
                bus.descriptor.channel_count == 1U &&
                bus.descriptor.sample_rate == capture.delivery_rate &&
                bus.audible_interleaved_samples.size() ==
                    capture.audible_delivery_frame_count &&
                std::ranges::all_of(
                    bus.audible_interleaved_samples,
                    [](const float sample) { return std::isfinite(sample); }),
            inconsistent_shape, "capture.buses[" + std::to_string(index) + "]",
            "directional capture bus must be one unique complete mono audible tape");
    }
    double prior_revolutions = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < capture.blocks.size(); ++index) {
        const auto &block = capture.blocks[index];
        const auto &endpoint = block.endpoint;
        require(report,
                block.block_ordinal == index &&
                    endpoint.delivery_frame ==
                        (index + 1U) * capture.delivery_frames_per_block &&
                    finite(endpoint.engine_speed_rpm) &&
                    endpoint.engine_speed_rpm > 0.0 &&
                    finite(endpoint.mean_intake_manifold_pressure_pa_abs) &&
                    endpoint.mean_intake_manifold_pressure_pa_abs > 0.0 &&
                    finite(endpoint.requested_throttle_01) &&
                    finite(endpoint.resolved_engine_throttle_01) &&
                    finite(endpoint.unwrapped_crank_revolutions) &&
                    endpoint.unwrapped_crank_revolutions > prior_revolutions,
                invalid_value, "capture.blocks[" + std::to_string(index) + "].endpoint",
                "directional capture endpoint must be finite, physical, and strictly "
                "ordered");
        prior_revolutions = endpoint.unwrapped_crank_revolutions;
    }
    return report;
}

[[nodiscard]] contract::Sha256Digest
cell_identity(const DirectionalSweepScenarioSpec &spec,
              const FiniteResponsiveCapture &capture, const std::string_view bus_id,
              const DirectionalCookedCell &cell) {
    detail::CanonicalIdentityBytes identity{kDirectionalCellIdentityMethodId};
    identity.digest(spec.identity_sha256);
    identity.digest(capture.engine_provenance.bundle.sha256);
    identity.digest(capture.scenario_provenance.bundle.sha256);
    identity.string(bus_id);
    identity.string(cell.id);
    identity.string(direction_id(cell.direction));
    identity.string(cell.lane_id);
    identity.f64(cell.capture_throttle_01);
    identity.f64(cell.rpm);
    identity.f64(cell.crossing_revolutions);
    identity.f64(cell.source_cycle_begin_revolutions);
    identity.f64(cell.source_cycle_end_revolutions);
    identity.digest(cell.source_payload_sha256);
    identity.digest(cell.seam_closed_payload_sha256);
    identity.f64(cell.telemetry.mean_manifold_pressure_pa_abs);
    identity.f64(cell.telemetry.mean_engine_speed_rpm);
    identity.f64(cell.telemetry.mean_requested_throttle_01);
    identity.f64(cell.telemetry.mean_resolved_engine_throttle_01);
    identity.u64(cell.telemetry.endpoint_count);
    identity.u64(cell.telemetry.state_masks.size());
    for (const std::uint32_t mask : cell.telemetry.state_masks) {
        identity.u32(mask);
    }
    identity.f64(cell.closure.signal_rms);
    identity.f64(cell.closure.source_adjacent_derivative_rms);
    identity.f64(cell.closure.seam_absolute_delta);
    identity.f64(cell.closure.seam_over_source_adjacent_derivative_rms);
    identity.f64(
        cell.closure.maximum_adjacent_delta_over_source_adjacent_derivative_rms);
    identity.f64(cell.closure.correction_rms_over_source_rms);
    identity.string(kDirectionalCaptureMethodId);
    identity.string(kDirectionalSeamAlgorithmId);
    return identity.finish();
}

[[nodiscard]] contract::Sha256Digest
route_capture_identity(const DirectionalCookedRouteCapture &route) {
    detail::CanonicalIdentityBytes identity{kDirectionalCaptureIdentityMethodId};
    identity.string("route");
    identity.string(route.bus_id);
    identity.u64(route.cells.size());
    for (const auto &cell : route.cells) {
        identity.string(cell.id);
        identity.digest(cell.identity_sha256);
    }
    return identity.finish();
}

[[nodiscard]] contract::Sha256Digest
capture_identity(const DirectionalCookedCapture &capture) {
    detail::CanonicalIdentityBytes identity{kDirectionalCaptureIdentityMethodId};
    identity.string("capture");
    identity.string(capture.id);
    identity.string(capture.engine_id);
    identity.string(direction_id(capture.direction));
    identity.string(capture.lane.id);
    identity.f64(capture.lane.throttle_01);
    identity.digest(capture.profile_selection_sha256);
    identity.digest(capture.engine_provenance_sha256);
    identity.digest(capture.scenario_provenance_sha256);
    identity.digest(capture.scenario_spec_sha256);
    identity.u64(capture.routes.size());
    for (const auto &route : capture.routes) {
        identity.string(route.bus_id);
        identity.digest(route.identity_sha256);
    }
    return identity.finish();
}

[[nodiscard]] contract::ValidationReport
validate_model_input(const ResponsiveBakeProfile &profile,
                     const std::span<const DirectionalCookedCapture> captures) {
    using enum contract::ContractIssueCode;
    auto report = validate_responsive_bake_profile(profile);
    require(report, captures.size() == 2U * kResponsiveLoadLaneCount,
            inconsistent_shape, "captures",
            "directional model requires the complete two-direction three-lane capture "
            "lattice");
    if (captures.empty()) {
        return report;
    }
    const auto &first = captures.front();
    require(report, !first.engine_id.empty(), missing_value, "captures[0].engine_id",
            "directional captures must name an engine");
    require(report, !first.selected_bus_ids.empty(), missing_value,
            "captures[0].selected_bus_ids",
            "directional captures must select source-route buses");
    for (std::size_t index = 0U; index < captures.size(); ++index) {
        const auto &capture = captures[index];
        const std::size_t direction_index = index / kResponsiveLoadLaneCount;
        const std::size_t lane_index = index % kResponsiveLoadLaneCount;
        const auto expected_direction = direction_index == 0U
                                            ? DirectionalSweepDirection::rising
                                            : DirectionalSweepDirection::falling;
        require(report,
                capture.direction == expected_direction &&
                    capture.lane == profile.capture.load_lanes[lane_index] &&
                    capture.rpm_anchors == profile.rpm.anchors &&
                    capture.profile_selection_sha256 ==
                        profile.selection_identity_sha256,
                inconsistent_semantics, "captures[" + std::to_string(index) + "]",
                "directional captures are not in canonical direction/lane order");
        require(report,
                capture.engine_id == first.engine_id &&
                    capture.engine_provenance_sha256 ==
                        first.engine_provenance_sha256 &&
                    capture.selected_bus_ids == first.selected_bus_ids &&
                    capture.routes.size() == first.selected_bus_ids.size(),
                inconsistent_semantics, "captures[" + std::to_string(index) + "]",
                "directional captures disagree on engine provenance or bus order");
        for (std::size_t route_index = 0U; route_index < capture.routes.size();
             ++route_index) {
            const auto &route = capture.routes[route_index];
            require(report,
                    route.bus_id == capture.selected_bus_ids[route_index] &&
                        route.cells.size() == kResponsiveRpmAnchorCount,
                    inconsistent_shape,
                    "captures[" + std::to_string(index) + "].routes[" +
                        std::to_string(route_index) + "]",
                    "directional route capture has an incomplete RPM lattice");
            for (std::size_t rpm_index = 0U; rpm_index < route.cells.size();
                 ++rpm_index) {
                const auto &cell = route.cells[rpm_index];
                const bool finite_cell =
                    finite(cell.crossing_revolutions) &&
                    finite(cell.source_cycle_begin_revolutions) &&
                    finite(cell.source_cycle_end_revolutions) &&
                    cell.source_cycle_end_revolutions >
                        cell.source_cycle_begin_revolutions &&
                    finite(cell.telemetry.mean_manifold_pressure_pa_abs) &&
                    cell.telemetry.mean_manifold_pressure_pa_abs > 0.0 &&
                    finite(cell.telemetry.mean_engine_speed_rpm) &&
                    finite(cell.telemetry.mean_requested_throttle_01) &&
                    finite(cell.telemetry.mean_resolved_engine_throttle_01) &&
                    finite(cell.closure.signal_rms) &&
                    finite(cell.closure.source_adjacent_derivative_rms) &&
                    finite(cell.closure.seam_absolute_delta) &&
                    finite(cell.closure.seam_over_source_adjacent_derivative_rms) &&
                    finite(
                        cell.closure
                            .maximum_adjacent_delta_over_source_adjacent_derivative_rms) &&
                    finite(cell.closure.correction_rms_over_source_rms) &&
                    std::ranges::all_of(
                        cell.source,
                        [](const float value) { return std::isfinite(value); }) &&
                    std::ranges::all_of(
                        cell.seam_closed,
                        [](const float value) { return std::isfinite(value); }) &&
                    std::ranges::all_of(
                        cell.telemetry.state_masks,
                        [](const std::uint32_t mask) { return mask <= 0x1fU; });
                require(
                    report,
                    finite_cell && cell.rpm == profile.rpm.anchors[rpm_index] &&
                        cell.direction == capture.direction &&
                        cell.lane_id == capture.lane.id &&
                        cell.capture_throttle_01 == capture.lane.throttle_01 &&
                        cell.source.size() == kDirectionalSamplesPerCycle *
                                                  kDirectionalSourceCycleCount &&
                        cell.seam_closed.size() == cell.source.size() &&
                        directional_float32_payload_identity(cell.source) ==
                            cell.source_payload_sha256 &&
                        directional_float32_payload_identity(cell.seam_closed) ==
                            cell.seam_closed_payload_sha256,
                    inconsistent_semantics,
                    "captures[" + std::to_string(index) + "].routes[" +
                        std::to_string(route_index) + "].cells[" +
                        std::to_string(rpm_index) + "]",
                    "directional cell differs from its coordinate or payload identity");
            }
        }
    }
    return report;
}

struct CombinedMetrics {
    double seam_over_derivative = 0.0;
    double correction_over_source = 0.0;
};

[[nodiscard]] CombinedMetrics
combined_closure_metrics(const std::span<const double> source,
                         const std::span<const double> candidate) {
    double source_square_sum = 0.0;
    double derivative_square_sum = 0.0;
    double correction_square_sum = 0.0;
    for (std::size_t index = 0U; index < source.size(); ++index) {
        source_square_sum += source[index] * source[index];
        const double correction = candidate[index] - source[index];
        correction_square_sum += correction * correction;
        if (index != 0U) {
            const double derivative = source[index] - source[index - 1U];
            derivative_square_sum += derivative * derivative;
        }
    }
    const double source_rms =
        std::sqrt(source_square_sum / static_cast<double>(source.size()));
    const double derivative_rms =
        std::sqrt(derivative_square_sum / static_cast<double>(source.size() - 1U));
    const double seam = std::abs(candidate.front() - candidate.back());
    return {
        seam / std::max(derivative_rms, 1e-30),
        std::sqrt(correction_square_sum / static_cast<double>(source.size())) /
            std::max(source_rms, 1e-30),
    };
}

[[nodiscard]] contract::Sha256Digest
route_direction_identity(const DirectionalRouteDirectionModel &route) {
    detail::CanonicalIdentityBytes identity{kDirectionalModelIdentityMethodId};
    identity.string("route-direction");
    identity.string(route.bus_id);
    identity.string(direction_id(route.direction));
    identity.u64(route.cells.size());
    for (const auto &cell : route.cells) {
        identity.string(cell.id);
        identity.digest(cell.identity_sha256);
    }
    identity.u64(route.coalesced_load_aliases.size());
    for (const auto &alias : route.coalesced_load_aliases) {
        identity.string(alias.bus_id);
        identity.string(direction_id(alias.direction));
        identity.f64(alias.rpm);
        identity.f64(alias.manifold_pressure_pa_abs);
        identity.string(alias.retained_lane_id);
        identity.string(alias.coalesced_lane_id);
        identity.digest(alias.source_payload_sha256);
        identity.digest(alias.seam_closed_payload_sha256);
    }
    identity.string(kDirectionalLoadCoalescingMethodId);
    return identity.finish();
}

[[nodiscard]] contract::Sha256Digest
model_identity(const ResponsiveBakeProfile &profile,
               const DirectionalCookedModel &model) {
    detail::CanonicalIdentityBytes identity{kDirectionalModelIdentityMethodId};
    identity.digest(profile.selection_identity_sha256);
    identity.string(model.engine_id);
    identity.f64(model.outer_minimum_rpm);
    identity.f64(model.outer_maximum_rpm);
    identity.u64(model.rpm_anchors.size());
    for (const double rpm : model.rpm_anchors) {
        identity.f64(rpm);
    }
    identity.u64(model.selected_bus_ids.size());
    for (const auto &bus : model.selected_bus_ids) {
        identity.string(bus);
    }
    identity.u64(model.route_directions.size());
    for (const auto &route : model.route_directions) {
        identity.string(route.bus_id);
        identity.string(direction_id(route.direction));
        identity.digest(route.identity_sha256);
    }
    identity.f64(
        model.combined_seam_gate.maximum_seam_over_source_adjacent_derivative_rms);
    identity.f64(model.combined_seam_gate.maximum_correction_rms_over_source_rms);
    identity.f64(model.combined_seam_gate
                     .observed_maximum_seam_over_source_adjacent_derivative_rms);
    identity.f64(
        model.combined_seam_gate.observed_maximum_correction_rms_over_source_rms);
    identity.boolean(model.combined_seam_gate.passed);
    identity.string(kDirectionalCaptureMethodId);
    identity.string(kDirectionalSeamAlgorithmId);
    return identity.finish();
}

} // namespace

contract::Sha256Digest
directional_float32_payload_identity(const std::span<const float> samples) {
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

DirectionalCookResult<DirectionalCookedCapture>
cook_directional_capture(const DirectionalSweepScenarioSpec &spec,
                         const FiniteResponsiveCapture &capture) {
    auto report = validate_capture(spec, capture);
    if (!report.ok()) {
        return report;
    }
    std::vector<ResponsiveCaptureEndpoint> endpoints;
    endpoints.reserve(capture.blocks.size());
    for (const auto &block : capture.blocks) {
        endpoints.push_back(block.endpoint);
    }

    DirectionalCookedCapture result;
    result.id = spec.id;
    result.engine_id = capture.engine_id;
    result.direction = spec.direction;
    result.lane = spec.lane;
    result.rpm_anchors = spec.rpm_anchors;
    result.profile_selection_sha256 = spec.profile_selection_sha256;
    result.selected_bus_ids = capture.selected_bus_ids;
    result.engine_provenance_sha256 = capture.engine_provenance.bundle.sha256;
    result.scenario_provenance_sha256 = capture.scenario_provenance.bundle.sha256;
    result.scenario_spec_sha256 = spec.identity_sha256;
    result.routes.reserve(capture.buses.size());
    for (std::size_t bus_index = 0U; bus_index < capture.buses.size(); ++bus_index) {
        DirectionalCookedRouteCapture route;
        route.bus_id = capture.selected_bus_ids[bus_index];
        route.cells.reserve(spec.rpm_anchors.size());
        const DirectionalCaptureView view{
            capture.audible_first_delivery_frame,
            capture.buses[bus_index].audible_interleaved_samples,
            endpoints,
        };
        for (const double rpm : spec.rpm_anchors) {
            auto transform = transform_directional_capture(view, rpm, spec.direction);
            if (const auto *failure =
                    std::get_if<DirectionalTransformError>(&transform)) {
                return one_issue(contract::ContractIssueCode::inconsistent_semantics,
                                 "capture.buses[" + std::to_string(bus_index) + "]",
                                 failure->code + ": " + failure->message);
            }
            auto transformed =
                std::get<DirectionalTextureTransform>(std::move(transform));
            DirectionalCookedCell cell;
            cell.id = result.engine_id + "-" +
                      std::string{direction_id(spec.direction)} + "-" + spec.lane.id +
                      "-" + number_token(rpm) + "rpm-" + route.bus_id + "-seam-closure";
            cell.rpm = rpm;
            cell.direction = spec.direction;
            cell.lane_id = spec.lane.id;
            cell.capture_throttle_01 = spec.lane.throttle_01;
            cell.crossing_revolutions = transformed.crossing_revolutions;
            cell.source_cycle_begin_revolutions =
                transformed.source_cycle_begin_revolutions;
            cell.source_cycle_end_revolutions =
                transformed.source_cycle_end_revolutions;
            cell.source = std::move(transformed.source);
            cell.seam_closed = std::move(transformed.seam_closed);
            cell.telemetry = std::move(transformed.telemetry);
            cell.closure = transformed.closure;
            cell.source_payload_sha256 =
                directional_float32_payload_identity(cell.source);
            cell.seam_closed_payload_sha256 =
                directional_float32_payload_identity(cell.seam_closed);
            cell.identity_sha256 = cell_identity(spec, capture, route.bus_id, cell);
            route.cells.push_back(std::move(cell));
        }
        route.identity_sha256 = route_capture_identity(route);
        result.routes.push_back(std::move(route));
    }
    result.identity_sha256 = capture_identity(result);
    return result;
}

DirectionalCookResult<DirectionalCookedModel>
assemble_directional_model(const ResponsiveBakeProfile &profile,
                           std::vector<DirectionalCookedCapture> captures) {
    using enum contract::ContractIssueCode;
    auto report = validate_model_input(profile, captures);
    if (!report.ok()) {
        return report;
    }

    DirectionalCombinedSeamGate seam_gate;
    constexpr std::size_t sample_count =
        kDirectionalSamplesPerCycle * kDirectionalSourceCycleCount;
    for (std::size_t capture_index = 0U; capture_index < captures.size();
         ++capture_index) {
        for (std::size_t rpm_index = 0U; rpm_index < kResponsiveRpmAnchorCount;
             ++rpm_index) {
            std::vector<double> combined_source(sample_count, 0.0);
            std::vector<double> combined_closed(sample_count, 0.0);
            for (const auto &route : captures[capture_index].routes) {
                const auto &cell = route.cells[rpm_index];
                for (std::size_t sample = 0U; sample < sample_count; ++sample) {
                    combined_source[sample] += static_cast<double>(cell.source[sample]);
                    combined_closed[sample] +=
                        static_cast<double>(cell.seam_closed[sample]);
                }
            }
            const auto metrics =
                combined_closure_metrics(combined_source, combined_closed);
            seam_gate.observed_maximum_seam_over_source_adjacent_derivative_rms =
                std::max(
                    seam_gate.observed_maximum_seam_over_source_adjacent_derivative_rms,
                    metrics.seam_over_derivative);
            seam_gate.observed_maximum_correction_rms_over_source_rms =
                std::max(seam_gate.observed_maximum_correction_rms_over_source_rms,
                         metrics.correction_over_source);
        }
    }
    seam_gate.passed =
        seam_gate.observed_maximum_seam_over_source_adjacent_derivative_rms <=
            seam_gate.maximum_seam_over_source_adjacent_derivative_rms &&
        seam_gate.observed_maximum_correction_rms_over_source_rms <=
            seam_gate.maximum_correction_rms_over_source_rms;
    if (!seam_gate.passed) {
        return one_issue(invalid_value, "combined_seam_gate",
                         "combined-route directional seam closure exceeds the accepted "
                         "metric thresholds");
    }

    DirectionalCookedModel result;
    result.engine_id = captures.front().engine_id;
    result.outer_minimum_rpm = profile.rpm.outer_minimum_rpm;
    result.outer_maximum_rpm = profile.rpm.outer_maximum_rpm;
    result.rpm_anchors = profile.rpm.anchors;
    result.selected_bus_ids = captures.front().selected_bus_ids;
    result.combined_seam_gate = seam_gate;
    result.route_directions.reserve(result.selected_bus_ids.size() * 2U);
    constexpr std::array directions{
        DirectionalSweepDirection::rising,
        DirectionalSweepDirection::falling,
    };
    for (std::size_t route_index = 0U; route_index < result.selected_bus_ids.size();
         ++route_index) {
        for (std::size_t direction_index = 0U; direction_index < directions.size();
             ++direction_index) {
            DirectionalRouteDirectionModel route;
            route.bus_id = result.selected_bus_ids[route_index];
            route.direction = directions[direction_index];
            route.cells.reserve(kResponsiveLoadLaneCount * kResponsiveRpmAnchorCount);
            for (std::size_t lane_index = 0U; lane_index < kResponsiveLoadLaneCount;
                 ++lane_index) {
                auto &capture =
                    captures[direction_index * kResponsiveLoadLaneCount + lane_index];
                auto &cells = capture.routes[route_index].cells;
                for (auto &cell : cells) {
                    const auto duplicate = std::ranges::find_if(
                        route.cells, [&](const DirectionalCookedCell &candidate) {
                            return candidate.rpm == cell.rpm &&
                                   candidate.telemetry.mean_manifold_pressure_pa_abs ==
                                       cell.telemetry.mean_manifold_pressure_pa_abs;
                        });
                    if (duplicate == route.cells.end()) {
                        route.cells.push_back(std::move(cell));
                        continue;
                    }
                    if (duplicate->source_payload_sha256 !=
                            cell.source_payload_sha256 ||
                        duplicate->seam_closed_payload_sha256 !=
                            cell.seam_closed_payload_sha256 ||
                        duplicate->source_cycle_begin_revolutions !=
                            cell.source_cycle_begin_revolutions ||
                        duplicate->source_cycle_end_revolutions !=
                            cell.source_cycle_end_revolutions) {
                        return one_issue(inconsistent_semantics, "captures",
                                         "tied directional RPM/MAP coordinates contain "
                                         "different source or seam-closed material");
                    }
                    route.coalesced_load_aliases.push_back(
                        {route.bus_id, route.direction, cell.rpm,
                         cell.telemetry.mean_manifold_pressure_pa_abs,
                         duplicate->lane_id, cell.lane_id, cell.source_payload_sha256,
                         cell.seam_closed_payload_sha256});
                }
            }
            route.identity_sha256 = route_direction_identity(route);
            result.route_directions.push_back(std::move(route));
        }
    }
    result.identity_sha256 = model_identity(profile, result);
    return result;
}

} // namespace crankwave::responsive
