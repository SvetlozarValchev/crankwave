#include "engine_sim_offline/atlas_capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline {
namespace {

[[nodiscard]] AtlasMovingLaneCaptureResult
fail(AtlasMovingLaneCaptureErrorCode code, std::string detail_code,
     std::string message,
     std::optional<EngineSessionError> session_error = std::nullopt) {
    return AtlasMovingLaneCaptureError{code, std::move(detail_code),
                                       std::move(message),
                                       std::move(session_error)};
}

[[nodiscard]] bool multiply_fits(std::uint64_t left, std::uint64_t right,
                                 std::uint64_t &result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool same_descriptor(const EngineAudioBusDescriptor &left,
                                   const EngineAudioBusDescriptor &right) noexcept {
    return left.id == right.id && left.kind == right.kind &&
           left.source_route_kind == right.source_route_kind &&
           left.route_id == right.route_id &&
           left.signal_disposition == right.signal_disposition &&
           left.channel_count == right.channel_count &&
           left.sample_rate == right.sample_rate;
}

[[nodiscard]] EngineCycleStateFlagMask
state_mask(const contract::EngineCaptureSample &sample) noexcept {
    EngineCycleStateFlagMask result = 0U;
    const auto set = [&result](bool enabled, EngineCycleStateFlag flag) {
        if (enabled) {
            result |= engine_cycle_state_flag_mask(flag);
        }
    };
    set(sample.ignition_enabled, EngineCycleStateFlag::ignition_enabled);
    set(sample.fuel_enabled, EngineCycleStateFlag::fuel_enabled);
    set(sample.starter_enabled, EngineCycleStateFlag::starter_enabled);
    set(sample.dyno_enabled, EngineCycleStateFlag::dyno_enabled);
    set(sample.limiter_enabled, EngineCycleStateFlag::limiter_enabled);
    set(sample.limiter_cut_active, EngineCycleStateFlag::limiter_cut_active);
    return result;
}

[[nodiscard]] bool valid_telemetry(const EngineTelemetryFrame &frame) noexcept {
    const auto &sample = frame.engine;
    return std::isfinite(sample.engine_speed_rpm) && sample.engine_speed_rpm > 0.0 &&
           std::isfinite(sample.angular_acceleration_rad_s2) &&
           std::isfinite(sample.requested_throttle_01) &&
           sample.requested_throttle_01 >= 0.0 &&
           sample.requested_throttle_01 <= 1.0 &&
           std::isfinite(frame.mean_intake_manifold_pressure_pa_abs) &&
           frame.mean_intake_manifold_pressure_pa_abs > 0.0 &&
           std::isfinite(sample.theta_rad);
}

[[nodiscard]] contract::AudioAtlasTimelineKnot
make_knot(std::uint64_t frame, const EngineTelemetryFrame &telemetry,
          double signed_load_coordinate,
          EngineCycleStateFlagMask transition_mask) noexcept {
    const auto &sample = telemetry.engine;
    constexpr double kRadiansPerSecondToRpm =
        60.0 / (2.0 * std::numbers::pi_v<double>);
    return {
        frame,
        sample.engine_speed_rpm,
        sample.angular_acceleration_rad_s2 * kRadiansPerSecondToRpm,
        sample.requested_throttle_01,
        signed_load_coordinate,
        telemetry.mean_intake_manifold_pressure_pa_abs,
        sample.theta_rad / (2.0 * std::numbers::pi_v<double>),
        state_mask(sample),
        transition_mask,
    };
}

[[nodiscard]] std::optional<contract::AudioAtlasFractionalFrame>
rebase_boundary(double source_frame, std::uint64_t audible_first,
                std::uint64_t audible_frame_count) noexcept {
    if (!std::isfinite(source_frame) ||
        source_frame < static_cast<double>(audible_first)) {
        return std::nullopt;
    }
    const auto local = source_frame - static_cast<double>(audible_first);
    if (local < 0.0 || local > static_cast<double>(audible_frame_count) ||
        local > static_cast<double>(contract::kMaximumResolvedFrameIndex)) {
        return std::nullopt;
    }
    const auto left = static_cast<std::uint64_t>(std::floor(local));
    const auto fraction = local - static_cast<double>(left);
    if (fraction == 0.0) {
        return contract::AudioAtlasFractionalFrame{left, left, 0.0};
    }
    if (!(fraction > 0.0 && fraction < 1.0) ||
        left == std::numeric_limits<std::uint64_t>::max() ||
        left + 1U > audible_frame_count) {
        return std::nullopt;
    }
    return contract::AudioAtlasFractionalFrame{left, left + 1U, fraction};
}

[[nodiscard]] bool same_position(const contract::AudioAtlasFractionalFrame &left,
                                 const contract::AudioAtlasFractionalFrame &right) {
    return left == right;
}

[[nodiscard]] bool append_boundary(
    AtlasMovingLaneCapture &capture, const EngineCycleBoundaryEvidence &boundary,
    std::uint64_t ordinal, std::uint64_t audible_first,
    std::uint64_t audible_frame_count) {
    const auto position = rebase_boundary(boundary.delivery_frame, audible_first,
                                          audible_frame_count);
    if (!position.has_value()) {
        return true;
    }
    if (!capture.crank_boundaries.empty() &&
        same_position(capture.crank_boundaries.back().position, *position)) {
        return capture.crank_boundaries.back().completed_cycle_ordinal == ordinal;
    }
    if (!capture.crank_boundaries.empty()) {
        const auto &previous = capture.crank_boundaries.back();
        const long double previous_value =
            static_cast<long double>(previous.position.left_frame) +
            previous.position.fraction_from_left_01;
        const long double value = static_cast<long double>(position->left_frame) +
                                  position->fraction_from_left_01;
        if (previous.completed_cycle_ordinal ==
                std::numeric_limits<std::uint64_t>::max() ||
            ordinal != previous.completed_cycle_ordinal + 1U ||
            value <= previous_value) {
            return false;
        }
    }
    capture.crank_boundaries.push_back({ordinal, *position});
    return true;
}

[[nodiscard]] AtlasMovingLaneBusCapture
make_bus(const EngineAudioBusDescriptor &descriptor, std::size_t frame_count) {
    AtlasMovingLaneBusCapture result;
    result.id = descriptor.id;
    result.kind = descriptor.kind;
    result.source_route_kind = descriptor.source_route_kind;
    result.route_id = descriptor.route_id;
    result.signal_disposition = descriptor.signal_disposition;
    result.samples.reserve(frame_count);
    return result;
}

} // namespace

AtlasMovingLaneCaptureResult capture_atlas_moving_lane(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> selected_bus_ids,
    const double signed_load_coordinate) {
    if (selected_bus_ids.empty() || !std::isfinite(signed_load_coordinate) ||
        signed_load_coordinate < -1.0 || signed_load_coordinate > 1.0) {
        return fail(AtlasMovingLaneCaptureErrorCode::invalid_request,
                    "atlas-moving-invalid-request",
                    "capture requires buses and a finite signed load in [-1, 1]");
    }
    for (std::size_t index = 0U; index < selected_bus_ids.size(); ++index) {
        if (selected_bus_ids[index].empty() ||
            std::ranges::find(selected_bus_ids.begin(),
                              selected_bus_ids.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              selected_bus_ids[index]) !=
                selected_bus_ids.begin() + static_cast<std::ptrdiff_t>(index)) {
            return fail(AtlasMovingLaneCaptureErrorCode::invalid_request,
                        "atlas-moving-invalid-bus-selection",
                        "selected audio bus IDs must be nonempty and unique");
        }
    }

    auto created = create_engine_session(
        scenario, EngineSessionExecutionKind::finite_scenario);
    if (auto *error = std::get_if<EngineSessionError>(&created)) {
        return fail(AtlasMovingLaneCaptureErrorCode::session_failed,
                    "atlas-moving-session-create-failed",
                    "finite source session creation failed", std::move(*error));
    }
    auto session = std::get<EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    std::uint64_t expected_twenty_ms_numerator = 0U;
    if (descriptor.execution_kind != EngineSessionExecutionKind::finite_scenario ||
        descriptor.preparation_block_count == 0U ||
        descriptor.total_block_count <= descriptor.preparation_block_count ||
        descriptor.physics_frames_per_block == 0U ||
        descriptor.delivery_frames_per_block == 0U ||
        descriptor.delivery_rate.numerator == 0U ||
        descriptor.delivery_rate.denominator == 0U ||
        descriptor.audio_buses.empty() ||
        !multiply_fits(descriptor.delivery_frames_per_block, 50U,
                       expected_twenty_ms_numerator) ||
        !multiply_fits(expected_twenty_ms_numerator,
                       descriptor.delivery_rate.denominator,
                       expected_twenty_ms_numerator) ||
        expected_twenty_ms_numerator != descriptor.delivery_rate.numerator) {
        return fail(AtlasMovingLaneCaptureErrorCode::invalid_session,
                    "atlas-moving-invalid-finite-horizon",
                    "scenario must expose a finite audible horizon with exact 20 ms blocks");
    }

    std::uint64_t total_delivery_frames = 0U;
    std::uint64_t total_physics_frames = 0U;
    std::uint64_t audible_first = 0U;
    if (!multiply_fits(descriptor.total_block_count,
                       descriptor.physics_frames_per_block,
                       total_physics_frames) ||
        !multiply_fits(descriptor.total_block_count,
                       descriptor.delivery_frames_per_block,
                       total_delivery_frames) ||
        !multiply_fits(descriptor.preparation_block_count,
                       descriptor.delivery_frames_per_block, audible_first) ||
        audible_first >= total_delivery_frames) {
        return fail(AtlasMovingLaneCaptureErrorCode::invalid_session,
                    "atlas-moving-horizon-overflow",
                    "session delivery horizon is not representable");
    }
    const auto frame_count = total_delivery_frames - audible_first;
    if (frame_count > std::numeric_limits<std::size_t>::max()) {
        return fail(AtlasMovingLaneCaptureErrorCode::invalid_session,
                    "atlas-moving-tape-too-large",
                    "audible tape exceeds addressable memory");
    }

    std::vector<EngineAudioBusDescriptor> selected;
    selected.reserve(selected_bus_ids.size());
    for (const auto id : selected_bus_ids) {
        const auto count = std::ranges::count(
            descriptor.audio_buses, id, &EngineAudioBusDescriptor::id);
        if (count != 1) {
            return fail(AtlasMovingLaneCaptureErrorCode::invalid_request,
                        "atlas-moving-bus-not-unique",
                        "each selected bus must resolve exactly once");
        }
        const auto found = std::ranges::find(
            descriptor.audio_buses, id, &EngineAudioBusDescriptor::id);
        if (found->channel_count != 1U ||
            found->sample_rate != descriptor.delivery_rate) {
            return fail(AtlasMovingLaneCaptureErrorCode::invalid_request,
                        "atlas-moving-bus-format-invalid",
                        "selected buses must be mono at the delivery rate");
        }
        selected.push_back(*found);
    }

    AtlasMovingLaneCapture capture;
    capture.engine_id = descriptor.engine_id;
    capture.scenario_id = descriptor.scenario_id;
    capture.sample_rate = descriptor.delivery_rate;
    capture.audible_source_frame = audible_first;
    capture.frame_count = frame_count;
    capture.buses.reserve(selected.size());
    for (const auto &bus : selected) {
        capture.buses.push_back(
            make_bus(bus, static_cast<std::size_t>(frame_count)));
    }

    std::optional<EngineTelemetryFrame> audible_start_state;
    EngineCycleStateFlagMask previous_state_mask = 0U;
    std::uint64_t processed_blocks = 0U;
    while (true) {
        auto next = session.process_block();
        if (auto *error = std::get_if<EngineSessionError>(&next)) {
            return fail(AtlasMovingLaneCaptureErrorCode::session_failed,
                        "atlas-moving-session-process-failed",
                        "source session failed during capture", std::move(*error));
        }
        if (const auto *completed = std::get_if<EngineSessionCompleted>(&next)) {
            if (processed_blocks != descriptor.total_block_count ||
                completed->block_count != descriptor.total_block_count ||
                completed->physics_frame_count != total_physics_frames ||
                completed->delivery_frame_count != total_delivery_frames ||
                completed->live_controls_accepted ||
                capture.timeline.knots.size() !=
                    descriptor.total_block_count -
                            descriptor.preparation_block_count +
                        1U ||
                capture.timeline.knots.back().frame != frame_count) {
                return fail(AtlasMovingLaneCaptureErrorCode::invalid_session,
                            "atlas-moving-incomplete-session",
                            "session completion disagreed with the captured horizon");
            }
            for (const auto &bus : capture.buses) {
                if (bus.samples.size() != frame_count) {
                    return fail(AtlasMovingLaneCaptureErrorCode::invalid_payload,
                                "atlas-moving-incomplete-pcm",
                                "a selected bus did not fill its chronological tape");
                }
            }
            return capture;
        }

        if (processed_blocks >= descriptor.total_block_count) {
            return fail(AtlasMovingLaneCaptureErrorCode::invalid_block,
                        "atlas-moving-overlong-session",
                        "session exceeded its finite authored horizon");
        }
        const auto &block = std::get<EngineSessionBlockView>(next);
        const auto phase = processed_blocks < descriptor.preparation_block_count
                               ? EngineSessionBlockPhase::preparation
                               : EngineSessionBlockPhase::audible;
        const auto expected_first_delivery =
            processed_blocks * descriptor.delivery_frames_per_block;
        const auto expected_first_physics =
            processed_blocks * descriptor.physics_frames_per_block;
        if (block.block_ordinal() != processed_blocks || block.phase() != phase ||
            block.first_delivery_frame() != expected_first_delivery ||
            block.first_physics_frame() != expected_first_physics ||
            block.delivery_frame_count() != descriptor.delivery_frames_per_block ||
            block.physics_frame_count() != descriptor.physics_frames_per_block ||
            block.telemetry().size() != 1U ||
            (!block.telemetry().empty() &&
             block.telemetry().front().physics_step_end !=
                 expected_first_physics + block.physics_frame_count()) ||
            !valid_telemetry(block.telemetry().front())) {
            return fail(AtlasMovingLaneCaptureErrorCode::invalid_block,
                        "atlas-moving-block-invalid",
                        "session block clock, phase, or telemetry is invalid");
        }

        const auto &telemetry = block.telemetry().front();
        if (phase == EngineSessionBlockPhase::preparation) {
            audible_start_state = telemetry;
        } else {
            if (capture.timeline.knots.empty()) {
                if (!audible_start_state.has_value()) {
                    return fail(AtlasMovingLaneCaptureErrorCode::invalid_block,
                                "atlas-moving-start-state-missing",
                                "audible start has no exact preceding telemetry state");
                }
                previous_state_mask = state_mask(audible_start_state->engine);
                capture.timeline.knots.push_back(make_knot(
                    0U, *audible_start_state, signed_load_coordinate, 0U));
            }
            const auto local_endpoint =
                expected_first_delivery + block.delivery_frame_count() -
                audible_first;
            const auto current_state_mask = state_mask(telemetry.engine);
            capture.timeline.knots.push_back(make_knot(
                local_endpoint, telemetry, signed_load_coordinate,
                previous_state_mask ^ current_state_mask));
            previous_state_mask = current_state_mask;

            for (std::size_t index = 0U; index < selected.size(); ++index) {
                const auto &expected = selected[index];
                const auto count = std::ranges::count(
                    block.audio_buses(), expected.id,
                    [](const EngineAudioBusBlockView &bus) {
                        return bus.descriptor.id;
                    });
                if (count != 1) {
                    return fail(AtlasMovingLaneCaptureErrorCode::invalid_block,
                                "atlas-moving-block-bus-missing",
                                "selected bus was not published exactly once");
                }
                const auto found = std::ranges::find(
                    block.audio_buses(), expected.id,
                    [](const EngineAudioBusBlockView &bus) {
                        return bus.descriptor.id;
                    });
                if (!same_descriptor(found->descriptor, expected) ||
                    found->samples.size() != block.delivery_frame_count() ||
                    !std::ranges::all_of(found->samples, [](float sample) {
                        return std::isfinite(sample);
                    })) {
                    return fail(AtlasMovingLaneCaptureErrorCode::invalid_payload,
                                "atlas-moving-block-pcm-invalid",
                                "selected bus format or PCM payload changed");
                }
                capture.buses[index].samples.insert(
                    capture.buses[index].samples.end(), found->samples.begin(),
                    found->samples.end());
            }
        }

        for (const auto &cycle : block.cycle_evidence()) {
            if (cycle.completed_cycle_ordinal ==
                std::numeric_limits<std::uint64_t>::max()) {
                return fail(AtlasMovingLaneCaptureErrorCode::invalid_payload,
                            "atlas-moving-cycle-ordinal-overflow",
                            "exact cycle ordinal cannot be rebased");
            }
            if (!append_boundary(capture, cycle.start_boundary,
                                 cycle.completed_cycle_ordinal, audible_first,
                                 frame_count) ||
                !append_boundary(capture, cycle.end_boundary,
                                 cycle.completed_cycle_ordinal + 1U, audible_first,
                                 frame_count)) {
                return fail(AtlasMovingLaneCaptureErrorCode::invalid_payload,
                            "atlas-moving-cycle-stream-invalid",
                            "exact cycle boundaries are not chronological and contiguous");
            }
        }
        ++processed_blocks;
    }
}

} // namespace engine_sim_offline
