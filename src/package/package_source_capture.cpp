#include "package/package_source_capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

namespace engine_sim_offline::package_detail {
namespace {

[[nodiscard]] PackageSourceCaptureResult
fail(const PackageSourceCaptureErrorCode code, std::string detail_code,
     std::string message,
     std::optional<EngineSessionError> session_error = std::nullopt) {
    return PackageSourceCaptureError{
        code,
        std::move(detail_code),
        std::move(message),
        std::move(session_error),
    };
}

[[nodiscard]] bool multiply_fits(const std::uint64_t left,
                                 const std::uint64_t right,
                                 std::uint64_t &result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool fits_size(const std::uint64_t value) noexcept {
    return value <= std::numeric_limits<std::size_t>::max();
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

[[nodiscard]] bool valid_boundary(const EngineCycleBoundaryEvidence &boundary,
                                  const std::uint64_t total_physics_frames,
                                  const std::uint64_t total_delivery_frames) noexcept {
    return boundary.left_physics_frame <= boundary.right_physics_frame &&
           boundary.right_physics_frame < total_physics_frames &&
           boundary.right_physics_frame - boundary.left_physics_frame <= 1U &&
           std::isfinite(boundary.fraction_from_left_01) &&
           boundary.fraction_from_left_01 >= 0.0 &&
           boundary.fraction_from_left_01 <= 1.0 &&
           std::isfinite(boundary.theta_unwrapped_rad) &&
           std::isfinite(boundary.time_s) && boundary.time_s >= 0.0 &&
           std::isfinite(boundary.delivery_frame) &&
           boundary.delivery_frame >= 0.0 &&
           boundary.delivery_frame <= static_cast<double>(total_delivery_frames);
}

[[nodiscard]] bool valid_cycle(const EngineCompletedCycleEvidence &cycle,
                               const std::uint64_t total_physics_frames,
                               const std::uint64_t total_delivery_frames) noexcept {
    const auto &start = cycle.start_boundary;
    const auto &end = cycle.end_boundary;
    return valid_boundary(start, total_physics_frames, total_delivery_frames) &&
           valid_boundary(end, total_physics_frames, total_delivery_frames) &&
           start.cycle_ordinal != std::numeric_limits<std::int64_t>::max() &&
           end.cycle_ordinal == start.cycle_ordinal + 1 &&
           start.right_physics_frame <= end.right_physics_frame &&
           end.theta_unwrapped_rad > start.theta_unwrapped_rad &&
           end.time_s > start.time_s && end.delivery_frame > start.delivery_frame &&
           std::isfinite(cycle.duration_s) && cycle.duration_s > 0.0 &&
           cycle.duration_s == end.time_s - start.time_s &&
           std::isfinite(cycle.mean_engine_speed_rpm) &&
           cycle.mean_engine_speed_rpm > 0.0;
}

[[nodiscard]] PackageSourceLaneBusCapture
make_bus_capture(const EngineAudioBusDescriptor &descriptor,
                 const std::size_t frame_count) {
    PackageSourceLaneBusCapture capture;
    capture.id = descriptor.id;
    capture.kind = descriptor.kind;
    capture.source_route_kind = descriptor.source_route_kind;
    capture.route_id = descriptor.route_id;
    capture.signal_disposition = descriptor.signal_disposition;
    capture.channel_count = descriptor.channel_count;
    capture.sample_rate = descriptor.sample_rate;
    capture.samples.reserve(frame_count);
    return capture;
}

} // namespace

PackageSourceCaptureResult capture_package_source_lane(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> selected_bus_ids) {
    if (selected_bus_ids.empty()) {
        return fail(PackageSourceCaptureErrorCode::invalid_bus_selection,
                    "package-source-no-buses",
                    "a package source lane must select at least one audio bus");
    }
    for (std::size_t index = 0; index < selected_bus_ids.size(); ++index) {
        if (selected_bus_ids[index].empty()) {
            return fail(PackageSourceCaptureErrorCode::invalid_bus_selection,
                        "package-source-empty-bus-id",
                        "a selected package source bus identity is empty");
        }
        if (std::ranges::find(selected_bus_ids.begin(),
                              selected_bus_ids.begin() +
                                  static_cast<std::ptrdiff_t>(index),
                              selected_bus_ids[index]) !=
            selected_bus_ids.begin() + static_cast<std::ptrdiff_t>(index)) {
            return fail(PackageSourceCaptureErrorCode::invalid_bus_selection,
                        "package-source-duplicate-bus-id",
                        "selected package source bus identities must be unique");
        }
    }

    auto created = create_engine_session(
        scenario, EngineSessionExecutionKind::finite_scenario);
    if (auto *error = std::get_if<EngineSessionError>(&created)) {
        auto owning = std::move(*error);
        return fail(PackageSourceCaptureErrorCode::session_failed,
                    "package-source-session-create-failed",
                    "the finite package source session could not be created",
                    std::move(owning));
    }
    auto session = std::get<EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    if (descriptor.execution_kind != EngineSessionExecutionKind::finite_scenario ||
        descriptor.total_block_count <= descriptor.preparation_block_count ||
        descriptor.physics_frames_per_block == 0U ||
        descriptor.delivery_frames_per_block == 0U ||
        descriptor.delivery_rate.numerator == 0U ||
        descriptor.delivery_rate.denominator == 0U) {
        return fail(PackageSourceCaptureErrorCode::incomplete_session,
                    "package-source-invalid-finite-horizon",
                    "the package source scenario has no valid finite audible horizon");
    }

    std::uint64_t total_physics_frames = 0U;
    std::uint64_t total_delivery_frames = 0U;
    std::uint64_t audible_first_delivery_frame = 0U;
    if (!multiply_fits(descriptor.total_block_count,
                       descriptor.physics_frames_per_block,
                       total_physics_frames) ||
        !multiply_fits(descriptor.total_block_count,
                       descriptor.delivery_frames_per_block,
                       total_delivery_frames) ||
        !multiply_fits(descriptor.preparation_block_count,
                       descriptor.delivery_frames_per_block,
                       audible_first_delivery_frame)) {
        return fail(PackageSourceCaptureErrorCode::incomplete_session,
                    "package-source-horizon-overflow",
                    "the package source session horizon cannot be represented");
    }
    const auto audible_delivery_frame_count =
        total_delivery_frames - audible_first_delivery_frame;
    if (!fits_size(audible_delivery_frame_count)) {
        return fail(PackageSourceCaptureErrorCode::incomplete_session,
                    "package-source-audio-size-overflow",
                    "the package source audible tape exceeds addressable memory");
    }

    std::vector<EngineAudioBusDescriptor> selected_descriptors;
    selected_descriptors.reserve(selected_bus_ids.size());
    for (const auto selected_id : selected_bus_ids) {
        const auto count = std::ranges::count(
            descriptor.audio_buses, selected_id, &EngineAudioBusDescriptor::id);
        if (count != 1) {
            return fail(PackageSourceCaptureErrorCode::invalid_bus_selection,
                        count == 0 ? "package-source-bus-missing"
                                   : "package-source-bus-duplicated",
                        count == 0
                            ? "a selected package source bus is not published by the session"
                            : "a selected package source bus identity is ambiguous");
        }
        const auto found = std::ranges::find(
            descriptor.audio_buses, selected_id, &EngineAudioBusDescriptor::id);
        if (found->channel_count != 1U ||
            found->sample_rate != descriptor.delivery_rate) {
            return fail(PackageSourceCaptureErrorCode::invalid_bus_format,
                        "package-source-bus-format-invalid",
                        "selected package source buses must be mono at the session delivery rate");
        }
        selected_descriptors.push_back(*found);
    }

    PackageSourceLaneCapture capture;
    capture.engine_id = descriptor.engine_id;
    capture.scenario_id = descriptor.scenario_id;
    capture.sample_rate = descriptor.delivery_rate;
    capture.audible_first_delivery_frame = audible_first_delivery_frame;
    capture.audible_delivery_frame_count = audible_delivery_frame_count;
    capture.buses.reserve(selected_descriptors.size());
    for (const auto &selected : selected_descriptors) {
        capture.buses.push_back(make_bus_capture(
            selected, static_cast<std::size_t>(audible_delivery_frame_count)));
    }

    std::uint64_t processed_blocks = 0U;
    std::uint64_t expected_first_physics_frame = 0U;
    std::uint64_t expected_first_delivery_frame = 0U;
    std::optional<EngineCompletedCycleEvidence> preceding_cycle;

    while (true) {
        auto result = session.process_block();
        if (auto *error = std::get_if<EngineSessionError>(&result)) {
            auto owning = std::move(*error);
            return fail(PackageSourceCaptureErrorCode::session_failed,
                        "package-source-session-processing-failed",
                        "the finite package source session failed while capturing",
                        std::move(owning));
        }
        if (const auto *completed = std::get_if<EngineSessionCompleted>(&result)) {
            if (processed_blocks != descriptor.total_block_count ||
                completed->block_count != descriptor.total_block_count ||
                completed->physics_frame_count != total_physics_frames ||
                completed->delivery_frame_count != total_delivery_frames ||
                completed->live_controls_accepted) {
                return fail(PackageSourceCaptureErrorCode::incomplete_session,
                            "package-source-early-or-inconsistent-completion",
                            "the package source session did not complete its exact authored horizon");
            }
            for (const auto &bus : capture.buses) {
                if (bus.samples.size() != audible_delivery_frame_count) {
                    return fail(PackageSourceCaptureErrorCode::incomplete_session,
                                "package-source-incomplete-audio",
                                "a selected package source bus did not fill its audible tape");
                }
            }
            if (capture.usable_cycles.size() !=
                capture.usable_cycle_lane_boundaries.size()) {
                return fail(PackageSourceCaptureErrorCode::incomplete_session,
                            "package-source-cycle-coordinate-mismatch",
                            "the package source cycle evidence and lane coordinates diverged");
            }
            return capture;
        }

        if (processed_blocks >= descriptor.total_block_count) {
            return fail(PackageSourceCaptureErrorCode::incomplete_session,
                        "package-source-overlong-session",
                        "the package source session exceeded its authored finite horizon");
        }
        const auto &block = std::get<EngineSessionBlockView>(result);
        const auto expected_phase =
            processed_blocks < descriptor.preparation_block_count
                ? EngineSessionBlockPhase::preparation
                : EngineSessionBlockPhase::audible;
        if (block.block_ordinal() != processed_blocks ||
            block.phase() != expected_phase ||
            block.first_physics_frame() != expected_first_physics_frame ||
            block.first_delivery_frame() != expected_first_delivery_frame ||
            block.physics_frame_count() != descriptor.physics_frames_per_block ||
            block.delivery_frame_count() != descriptor.delivery_frames_per_block) {
            return fail(PackageSourceCaptureErrorCode::invalid_block_sequence,
                        "package-source-block-clock-discontinuity",
                        "package source blocks contain a gap, overlap, or phase discontinuity");
        }

        for (std::size_t selected_index = 0;
             selected_index < selected_descriptors.size(); ++selected_index) {
            const auto &expected = selected_descriptors[selected_index];
            const auto count = std::ranges::count(
                block.audio_buses(), expected.id,
                [](const EngineAudioBusBlockView &bus) { return bus.descriptor.id; });
            if (count != 1) {
                return fail(PackageSourceCaptureErrorCode::invalid_bus_selection,
                            count == 0 ? "package-source-block-bus-missing"
                                       : "package-source-block-bus-duplicated",
                            "a selected package source bus was not published exactly once in a session block");
            }
            const auto found = std::ranges::find(
                block.audio_buses(), expected.id,
                [](const EngineAudioBusBlockView &bus) { return bus.descriptor.id; });
            if (!same_descriptor(found->descriptor, expected) ||
                found->samples.size() != block.delivery_frame_count()) {
                return fail(PackageSourceCaptureErrorCode::invalid_bus_format,
                            "package-source-block-bus-format-changed",
                            "a selected package source bus changed format or frame extent during capture");
            }
            if (!std::ranges::all_of(found->samples,
                                     [](const float sample) {
                                         return std::isfinite(sample);
                                     })) {
                return fail(PackageSourceCaptureErrorCode::invalid_audio_payload,
                            "package-source-nonfinite-audio",
                            "a selected package source bus contains a nonfinite sample");
            }
            if (block.phase() == EngineSessionBlockPhase::audible) {
                auto &samples = capture.buses[selected_index].samples;
                samples.insert(samples.end(), found->samples.begin(),
                               found->samples.end());
            }
        }

        for (const auto &cycle : block.cycle_evidence()) {
            if (!valid_cycle(cycle, total_physics_frames,
                             total_delivery_frames) ||
                cycle.end_boundary.right_physics_frame <
                    block.first_physics_frame() ||
                cycle.end_boundary.right_physics_frame >=
                    block.first_physics_frame() + block.physics_frame_count()) {
                return fail(PackageSourceCaptureErrorCode::invalid_cycle_evidence,
                            "package-source-cycle-invalid",
                            "the package source session published invalid exact-cycle boundaries");
            }
            if (preceding_cycle.has_value() &&
                (preceding_cycle->completed_cycle_ordinal ==
                     std::numeric_limits<std::uint64_t>::max() ||
                 cycle.completed_cycle_ordinal !=
                     preceding_cycle->completed_cycle_ordinal + 1U ||
                 cycle.start_boundary != preceding_cycle->end_boundary)) {
                return fail(PackageSourceCaptureErrorCode::invalid_cycle_evidence,
                            "package-source-cycle-nonmonotonic",
                            "the package source exact-cycle stream contains a gap, overlap, or reversal");
            }
            preceding_cycle = cycle;

            const auto start = cycle.start_boundary.delivery_frame;
            const auto end = cycle.end_boundary.delivery_frame;
            const auto audible_end = static_cast<double>(total_delivery_frames);
            if (start < static_cast<double>(audible_first_delivery_frame) ||
                end > audible_end) {
                if (capture.rejected_outside_audible_cycle_count ==
                    std::numeric_limits<std::uint64_t>::max()) {
                    return fail(PackageSourceCaptureErrorCode::invalid_cycle_evidence,
                                "package-source-cycle-count-overflow",
                                "the rejected package source cycle count overflowed");
                }
                ++capture.rejected_outside_audible_cycle_count;
                continue;
            }
            capture.usable_cycles.push_back(cycle);
            capture.usable_cycle_lane_boundaries.push_back({
                start - static_cast<double>(audible_first_delivery_frame),
                end - static_cast<double>(audible_first_delivery_frame),
            });
        }

        ++processed_blocks;
        expected_first_physics_frame += descriptor.physics_frames_per_block;
        expected_first_delivery_frame += descriptor.delivery_frames_per_block;
    }
}

} // namespace engine_sim_offline::package_detail
