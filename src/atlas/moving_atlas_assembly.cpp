#include "engine_sim_offline/atlas_assembly.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline {
namespace {

using Direction = authoring::AtlasBakeMovingDirection;

struct MonotoneRun {
    std::size_t first_knot = 0U;
    std::size_t last_knot = 0U;
};

struct TraversalCrop {
    std::uint64_t captured_begin = 0U;
    std::uint64_t captured_end = 0U;
    std::uint64_t usable_begin = 0U;
    std::uint64_t usable_end = 0U;
};

[[nodiscard]] AudioAtlasAssemblyResult
fail(const AudioAtlasAssemblyErrorCode code, std::string detail_code,
     std::string message, contract::ValidationReport report = {}) noexcept {
    return AudioAtlasAssemblyError{code, std::move(detail_code), std::move(message),
                                   std::move(report)};
}

[[nodiscard]] bool
valid_identity(const contract::AudioAtlasContentIdentity &identity) noexcept {
    return contract::is_valid_semantic_id(identity.id) && !identity.sha256.is_zero();
}

[[nodiscard]] bool valid_bundle(const contract::ProvenanceBundleRef &bundle) noexcept {
    return contract::is_valid_semantic_id(bundle.id) && !bundle.sha256.is_zero();
}

[[nodiscard]] bool
valid_fractional_position(const contract::AudioAtlasFractionalFrame &position,
                          const std::uint64_t frame_count) noexcept {
    if (!std::isfinite(position.fraction_from_left_01) ||
        position.fraction_from_left_01 < 0.0 || position.fraction_from_left_01 >= 1.0) {
        return false;
    }
    if (position.left_frame == position.right_frame) {
        return position.fraction_from_left_01 == 0.0 &&
               position.left_frame <= frame_count;
    }
    return position.left_frame != std::numeric_limits<std::uint64_t>::max() &&
           position.right_frame == position.left_frame + 1U &&
           position.right_frame <= frame_count && position.fraction_from_left_01 > 0.0;
}

[[nodiscard]] bool valid_capture_shape(const AtlasMovingLaneCapture &capture) noexcept {
    if (!contract::is_valid_semantic_id(capture.engine_id) ||
        !contract::is_valid_semantic_id(capture.scenario_id) ||
        capture.sample_rate.numerator == 0U || capture.sample_rate.denominator == 0U ||
        capture.frame_count == 0U ||
        capture.frame_count > std::numeric_limits<std::size_t>::max() ||
        capture.timeline.knots.size() < 2U ||
        capture.timeline.knots.front().frame != 0U ||
        capture.timeline.knots.back().frame != capture.frame_count) {
        return false;
    }

    for (std::size_t index = 0U; index < capture.timeline.knots.size(); ++index) {
        const auto &knot = capture.timeline.knots[index];
        if ((index != 0U && capture.timeline.knots[index - 1U].frame >= knot.frame) ||
            !std::isfinite(knot.rpm) || !(knot.rpm > 0.0) ||
            !std::isfinite(knot.rpm_slope_rpm_per_second) ||
            !std::isfinite(knot.requested_throttle_01) ||
            knot.requested_throttle_01 < 0.0 || knot.requested_throttle_01 > 1.0 ||
            !std::isfinite(knot.signed_load_coordinate) ||
            knot.signed_load_coordinate < -1.0 || knot.signed_load_coordinate > 1.0 ||
            !std::isfinite(knot.manifold_pressure_pa_abs) ||
            !(knot.manifold_pressure_pa_abs > 0.0) ||
            !std::isfinite(knot.unwrapped_crank_revolutions) ||
            (index != 0U &&
             capture.timeline.knots[index - 1U].unwrapped_crank_revolutions >=
                 knot.unwrapped_crank_revolutions) ||
            (knot.state_mask & ~contract::kAudioAtlasKnownStateMask) != 0U ||
            knot.transition_mask != 0U) {
            return false;
        }
    }

    std::optional<std::uint64_t> previous_ordinal;
    long double previous_position = -1.0L;
    for (const auto &boundary : capture.crank_boundaries) {
        if (!valid_fractional_position(boundary.position, capture.frame_count)) {
            return false;
        }
        const auto position =
            static_cast<long double>(boundary.position.left_frame) +
            static_cast<long double>(boundary.position.fraction_from_left_01);
        if ((previous_ordinal.has_value() &&
             (*previous_ordinal == std::numeric_limits<std::uint64_t>::max() ||
              boundary.completed_cycle_ordinal != *previous_ordinal + 1U)) ||
            position <= previous_position) {
            return false;
        }
        previous_ordinal = boundary.completed_cycle_ordinal;
        previous_position = position;
    }
    return true;
}

[[nodiscard]] bool knot_matches_direction(
    const contract::AudioAtlasTimelineKnot &knot,
    const Direction direction) noexcept {
    return direction == Direction::rising
               ? knot.rpm_slope_rpm_per_second > 0.0
               : knot.rpm_slope_rpm_per_second < 0.0;
}

[[nodiscard]] bool run_covers(const contract::AudioAtlasStateTimeline &timeline,
                              const MonotoneRun run,
                              const authoring::AtlasBakeRpmRange &range,
                              const Direction direction) noexcept {
    const auto first = timeline.knots[run.first_knot].rpm;
    const auto last = timeline.knots[run.last_knot].rpm;
    return direction == Direction::rising
               ? first <= range.minimum && last >= range.maximum && last > first
               : first >= range.maximum && last <= range.minimum && last < first;
}

[[nodiscard]] std::vector<MonotoneRun>
covering_runs(const contract::AudioAtlasStateTimeline &timeline,
              const authoring::AtlasBakeRpmRange &range, const Direction direction) {
    std::vector<MonotoneRun> result;
    std::optional<std::size_t> run_start;
    for (std::size_t index = 0U; index < timeline.knots.size(); ++index) {
        if (knot_matches_direction(timeline.knots[index], direction)) {
            if (!run_start.has_value()) {
                run_start = index;
            }
            continue;
        }
        if (run_start.has_value() && index - *run_start >= 2U) {
            const MonotoneRun run{*run_start, index - 1U};
            if (run_covers(timeline, run, range, direction)) {
                result.push_back(run);
            }
        }
        run_start.reset();
    }
    if (run_start.has_value() && timeline.knots.size() - *run_start >= 2U) {
        const MonotoneRun run{*run_start, timeline.knots.size() - 1U};
        if (run_covers(timeline, run, range, direction)) {
            result.push_back(run);
        }
    }
    return result;
}

[[nodiscard]] std::optional<long double>
interpolate_crossing(const contract::AudioAtlasStateTimeline &timeline,
                     const MonotoneRun run, const double target_rpm,
                     const Direction direction,
                     const bool departing_boundary) noexcept {
    for (std::size_t right = run.first_knot + 1U; right <= run.last_knot; ++right) {
        const auto &left_knot = timeline.knots[right - 1U];
        const auto &right_knot = timeline.knots[right];
        const auto left = left_knot.rpm;
        const auto next = right_knot.rpm;
        bool brackets = false;
        if (direction == Direction::rising) {
            brackets = departing_boundary ? left <= target_rpm && target_rpm < next
                                          : left < target_rpm && target_rpm <= next;
        } else {
            brackets = departing_boundary ? left >= target_rpm && target_rpm > next
                                          : left > target_rpm && target_rpm >= next;
        }
        if (!brackets) {
            continue;
        }
        if (left == target_rpm) {
            return static_cast<long double>(left_knot.frame);
        }
        if (next == target_rpm) {
            return static_cast<long double>(right_knot.frame);
        }
        const long double fraction =
            (static_cast<long double>(target_rpm) - static_cast<long double>(left)) /
            (static_cast<long double>(next) - static_cast<long double>(left));
        if (!(fraction > 0.0L && fraction < 1.0L)) {
            return std::nullopt;
        }
        return static_cast<long double>(left_knot.frame) +
               fraction * static_cast<long double>(right_knot.frame - left_knot.frame);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t>
first_frame_at_or_after(const long double position) noexcept {
    if (!std::isfinite(position) || position < 0.0L ||
        position > static_cast<long double>(contract::kMaximumResolvedFrameIndex)) {
        return std::nullopt;
    }
    const auto rounded = std::ceil(position);
    if (rounded < 0.0L ||
        rounded > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(rounded);
}

[[nodiscard]] std::optional<TraversalCrop>
derive_crop(const contract::AudioAtlasStateTimeline &timeline, const MonotoneRun run,
            const authoring::AtlasBakeMovingSegment &segment) noexcept {
    const bool rising = segment.direction == Direction::rising;
    const auto captured_start_rpm =
        rising ? segment.captured_rpm.minimum : segment.captured_rpm.maximum;
    const auto captured_end_rpm =
        rising ? segment.captured_rpm.maximum : segment.captured_rpm.minimum;
    const auto usable_start_rpm =
        rising ? segment.usable_rpm.minimum : segment.usable_rpm.maximum;
    const auto usable_end_rpm =
        rising ? segment.usable_rpm.maximum : segment.usable_rpm.minimum;

    const auto captured_start = interpolate_crossing(timeline, run, captured_start_rpm,
                                                     segment.direction, true);
    const auto captured_end =
        interpolate_crossing(timeline, run, captured_end_rpm, segment.direction, false);
    const auto usable_start =
        interpolate_crossing(timeline, run, usable_start_rpm, segment.direction, true);
    const auto usable_end =
        interpolate_crossing(timeline, run, usable_end_rpm, segment.direction, false);
    if (!captured_start.has_value() || !captured_end.has_value() ||
        !usable_start.has_value() || !usable_end.has_value()) {
        return std::nullopt;
    }
    const auto captured_begin_frame = first_frame_at_or_after(*captured_start);
    const auto captured_end_frame = first_frame_at_or_after(*captured_end);
    const auto usable_begin_frame = first_frame_at_or_after(*usable_start);
    const auto usable_end_frame = first_frame_at_or_after(*usable_end);
    if (!captured_begin_frame.has_value() || !captured_end_frame.has_value() ||
        !usable_begin_frame.has_value() || !usable_end_frame.has_value() ||
        !(*captured_begin_frame < *usable_begin_frame &&
          *usable_begin_frame < *usable_end_frame &&
          *usable_end_frame < *captured_end_frame)) {
        return std::nullopt;
    }
    return TraversalCrop{*captured_begin_frame, *captured_end_frame,
                         *usable_begin_frame, *usable_end_frame};
}

[[nodiscard]] std::optional<contract::AudioAtlasTimelineKnot>
interpolate_knot(const contract::AudioAtlasStateTimeline &timeline,
                 const std::uint64_t frame) noexcept {
    const auto found = std::ranges::lower_bound(
        timeline.knots, frame, {}, &contract::AudioAtlasTimelineKnot::frame);
    if (found != timeline.knots.end() && found->frame == frame) {
        return *found;
    }
    if (found == timeline.knots.begin() || found == timeline.knots.end()) {
        return std::nullopt;
    }
    const auto &right = *found;
    const auto &left = *(found - 1);
    if (left.state_mask != right.state_mask || left.transition_mask != 0U ||
        right.transition_mask != 0U || !(left.frame < frame && frame < right.frame)) {
        return std::nullopt;
    }
    const auto fraction = static_cast<double>(frame - left.frame) /
                          static_cast<double>(right.frame - left.frame);
    const auto lerp = [fraction](const double first, const double second) {
        return first + (second - first) * fraction;
    };
    return contract::AudioAtlasTimelineKnot{
        frame,
        lerp(left.rpm, right.rpm),
        lerp(left.rpm_slope_rpm_per_second, right.rpm_slope_rpm_per_second),
        lerp(left.requested_throttle_01, right.requested_throttle_01),
        lerp(left.signed_load_coordinate, right.signed_load_coordinate),
        lerp(left.manifold_pressure_pa_abs, right.manifold_pressure_pa_abs),
        lerp(left.unwrapped_crank_revolutions, right.unwrapped_crank_revolutions),
        left.state_mask,
        0U,
    };
}

[[nodiscard]] std::optional<contract::AudioAtlasStateTimeline>
crop_timeline(const contract::AudioAtlasStateTimeline &source,
              const TraversalCrop crop) {
    const auto start = interpolate_knot(source, crop.captured_begin);
    const auto end = interpolate_knot(source, crop.captured_end);
    if (!start.has_value() || !end.has_value()) {
        return std::nullopt;
    }
    contract::AudioAtlasStateTimeline result;
    result.knots.reserve(source.knots.size());
    result.knots.push_back(*start);
    for (const auto &knot : source.knots) {
        if (knot.frame > crop.captured_begin && knot.frame < crop.captured_end) {
            result.knots.push_back(knot);
        }
    }
    result.knots.push_back(*end);
    for (auto &knot : result.knots) {
        knot.frame -= crop.captured_begin;
    }
    return result;
}

[[nodiscard]] bool
timeline_matches_segment(const contract::AudioAtlasStateTimeline &timeline,
                         const authoring::AtlasBakeMovingSegment &segment) noexcept {
    if (timeline.knots.size() < 2U) {
        return false;
    }
    for (std::size_t index = 0U; index < timeline.knots.size(); ++index) {
        const auto &knot = timeline.knots[index];
        const auto normalized_slope = knot.rpm_slope_rpm_per_second / knot.rpm;
        if (knot.state_mask != segment.state_mask || knot.transition_mask != 0U ||
            knot.signed_load_coordinate != segment.load_coordinate ||
            !std::isfinite(normalized_slope) ||
            normalized_slope < segment.normalized_rpm_slope.minimum_per_second ||
            normalized_slope > segment.normalized_rpm_slope.maximum_per_second ||
            (segment.direction == Direction::rising && !(normalized_slope > 0.0)) ||
            (segment.direction == Direction::falling && !(normalized_slope < 0.0))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<contract::AudioAtlasCrankBoundary>
crop_boundaries(const std::span<const contract::AudioAtlasCrankBoundary> source,
                const TraversalCrop crop) {
    std::vector<contract::AudioAtlasCrankBoundary> result;
    for (const auto &boundary : source) {
        const auto &position = boundary.position;
        const auto numeric = static_cast<long double>(position.left_frame) +
                             position.fraction_from_left_01;
        if (numeric < static_cast<long double>(crop.captured_begin) ||
            numeric > static_cast<long double>(crop.captured_end) ||
            position.left_frame < crop.captured_begin ||
            position.right_frame > crop.captured_end) {
            continue;
        }
        auto rebased = boundary;
        rebased.position.left_frame -= crop.captured_begin;
        rebased.position.right_frame -= crop.captured_begin;
        result.push_back(rebased);
    }
    return result;
}

[[nodiscard]] std::optional<std::vector<std::byte>>
encode_f32le_crop(const std::span<const float> samples, const TraversalCrop crop) {
    if (crop.captured_end > samples.size() ||
        crop.captured_begin >= crop.captured_end) {
        return std::nullopt;
    }
    const auto frame_count = crop.captured_end - crop.captured_begin;
    if (frame_count > std::numeric_limits<std::size_t>::max() / 4U) {
        return std::nullopt;
    }
    std::vector<std::byte> result(static_cast<std::size_t>(frame_count) * 4U);
    std::size_t write = 0U;
    for (std::uint64_t frame = crop.captured_begin; frame < crop.captured_end;
         ++frame) {
        const auto sample = samples[static_cast<std::size_t>(frame)];
        if (!std::isfinite(sample)) {
            return std::nullopt;
        }
        const auto bits = std::bit_cast<std::uint32_t>(sample);
        result[write++] = static_cast<std::byte>(bits & 0xffU);
        result[write++] = static_cast<std::byte>((bits >> 8U) & 0xffU);
        result[write++] = static_cast<std::byte>((bits >> 16U) & 0xffU);
        result[write++] = static_cast<std::byte>((bits >> 24U) & 0xffU);
    }
    return result;
}

[[nodiscard]] std::string artifact_id(const std::string_view segment_id,
                                      const std::size_t bus_index) {
    return std::string{segment_id} + ".bus-" + std::to_string(bus_index);
}

[[nodiscard]] std::string artifact_path(const std::string_view segment_id,
                                        const std::size_t bus_index) {
    return "audio/" + std::string{segment_id} + "/bus-" + std::to_string(bus_index) +
           ".f32le";
}

} // namespace

AudioAtlasAssemblyResult assemble_moving_audio_atlas(
    const CompiledAtlasBake &bake,
    const std::span<const AtlasMovingLaneAssemblyInputView> moving_inputs,
    const AudioAtlasAssemblyProvenance &provenance) noexcept {
    try {
        const auto segments = bake.moving_segments();
        const auto buses = bake.audio_buses();
        const auto sources = bake.scenario_sources();
        const auto engine = bake.engine();
        const auto rate = bake.audio_sample_rate();
        if (segments.empty() || buses.empty() ||
            moving_inputs.size() != segments.size() || rate.denominator != 1U ||
            rate.numerator_hz == 0U ||
            rate.numerator_hz > std::numeric_limits<std::uint32_t>::max()) {
            return fail(AudioAtlasAssemblyErrorCode::invalid_request,
                        "audio-atlas-assembly-request-invalid",
                        "assembly inputs do not match the compiled moving atlas");
        }
        if (!valid_identity(provenance.engine) ||
            !valid_identity(provenance.bake_document) ||
            !valid_identity(provenance.renderer_build) ||
            !valid_bundle(provenance.source_inputs) ||
            provenance.engine.id != engine.id() ||
            provenance.bake_document.id != bake.id()) {
            return fail(
                AudioAtlasAssemblyErrorCode::identity_mismatch,
                "audio-atlas-provenance-invalid",
                "explicit atlas provenance is invalid or names another compilation");
        }

        AssembledAudioAtlas assembled;
        auto &manifest = assembled.manifest;
        manifest.id = std::string{bake.id()};
        manifest.engine = std::string{engine.id()};
        manifest.public_seed = bake.public_seed();
        manifest.audio.sample_rate_hz = static_cast<std::uint32_t>(rate.numerator_hz);
        manifest.audio.encoding = contract::AudioAtlasSampleEncoding::float32le;
        manifest.audio.channel_layout = contract::AudioAtlasChannelLayout::mono;
        manifest.audio.buses.reserve(buses.size());
        for (const auto &bus : buses) {
            manifest.audio.buses.push_back({bus.id});
        }
        const auto &domain = bake.domain();
        manifest.domain = {domain.minimum_rpm, domain.maximum_rpm,
                           domain.minimum_load_coordinate,
                           domain.maximum_load_coordinate};
        manifest.provenance = {provenance.engine, provenance.bake_document,
                               provenance.renderer_build, provenance.source_inputs};
        manifest.moving_segments.reserve(segments.size());
        manifest.artifacts.reserve(segments.size() * buses.size());
        assembled.payloads.reserve(segments.size() * buses.size());

        for (std::size_t segment_index = 0U; segment_index < segments.size();
             ++segment_index) {
            const auto &planned = segments[segment_index];
            const auto &authored = planned.capture;
            const auto &input = moving_inputs[segment_index];
            if (input.capture == nullptr ||
                input.moving_segment_id != authored.id.value ||
                planned.scenario_source_index >= sources.size()) {
                return fail(AudioAtlasAssemblyErrorCode::invalid_request,
                            "audio-atlas-moving-input-order-invalid",
                            "moving capture inputs must match exact authored order and "
                            "identity");
            }
            const auto &capture = *input.capture;
            const auto &source = sources[planned.scenario_source_index];
            if (!valid_identity(input.source_scenario) ||
                !valid_identity(input.capture_configuration) ||
                input.source_scenario.id != planned.scenario.id() ||
                capture.engine_id != engine.id() ||
                capture.scenario_id != planned.scenario.id() ||
                source.scenario.id() != planned.scenario.id()) {
                return fail(AudioAtlasAssemblyErrorCode::identity_mismatch,
                            "audio-atlas-moving-source-identity-mismatch",
                            "capture or source identity names another engine scenario");
            }
            if (!valid_capture_shape(capture) ||
                capture.sample_rate.numerator != rate.numerator_hz ||
                capture.sample_rate.denominator != rate.denominator ||
                capture.buses.size() != buses.size()) {
                return fail(AudioAtlasAssemblyErrorCode::capture_mismatch,
                            "audio-atlas-moving-capture-shape-invalid",
                            "moving capture clock, timeline, boundaries, or bus shape "
                            "is invalid");
            }
            for (std::size_t bus_index = 0U; bus_index < buses.size(); ++bus_index) {
                if (capture.buses[bus_index].id != buses[bus_index].session_bus_id ||
                    capture.buses[bus_index].samples.size() != capture.frame_count) {
                    return fail(AudioAtlasAssemblyErrorCode::capture_mismatch,
                                "audio-atlas-moving-bus-mismatch",
                                "captured buses must match the compiled bus order, "
                                "identity, and horizon");
                }
            }

            const auto runs = covering_runs(capture.timeline, authored.captured_rpm,
                                            authored.direction);
            if (runs.size() != 1U) {
                return fail(AudioAtlasAssemblyErrorCode::traversal_not_unique,
                            "audio-atlas-moving-traversal-not-unique",
                            "capture must contain exactly one monotone traversal of "
                            "the authored RPM range");
            }
            const auto crop = derive_crop(capture.timeline, runs.front(), authored);
            if (!crop.has_value() || crop->captured_end > capture.frame_count) {
                return fail(AudioAtlasAssemblyErrorCode::timeline_invalid,
                            "audio-atlas-moving-crossing-invalid",
                            "authored captured and usable RPM crossings could not be "
                            "resolved exactly");
            }
            const auto frame_count = crop->captured_end - crop->captured_begin;
            const auto usable_begin = crop->usable_begin - crop->captured_begin;
            const auto usable_end = crop->usable_end - crop->captured_begin;
            if (usable_begin < authored.handoff.transition_frames ||
                frame_count - usable_end < authored.handoff.transition_frames) {
                return fail(
                    AudioAtlasAssemblyErrorCode::timeline_invalid,
                    "audio-atlas-moving-handoff-guard-too-short",
                    "captured traversal does not retain both authored handoff guards");
            }
            auto timeline = crop_timeline(capture.timeline, *crop);
            if (!timeline.has_value()) {
                return fail(AudioAtlasAssemblyErrorCode::timeline_invalid,
                            "audio-atlas-moving-timeline-crop-failed",
                            "captured timeline endpoints could not be interpolated and "
                            "rebased");
            }
            if (!timeline_matches_segment(*timeline, authored)) {
                return fail(AudioAtlasAssemblyErrorCode::slope_outside_envelope,
                            "audio-atlas-moving-state-envelope-mismatch",
                            "measured state, load, direction, or normalized macro "
                            "slope lies outside the authored envelope");
            }
            auto boundaries = crop_boundaries(capture.crank_boundaries, *crop);
            if (boundaries.size() < 2U) {
                return fail(AudioAtlasAssemblyErrorCode::timeline_invalid,
                            "audio-atlas-moving-cycle-evidence-insufficient",
                            "cropped traversal must retain at least two exact "
                            "crank-cycle boundaries");
            }

            contract::AudioAtlasMovingSegment segment;
            segment.id = authored.id.value;
            segment.direction = authored.direction == Direction::rising
                                    ? contract::AudioAtlasMovingDirection::rising
                                    : contract::AudioAtlasMovingDirection::falling;
            segment.load_coordinate = authored.load_coordinate;
            segment.state_mask = authored.state_mask;
            segment.normalized_rpm_slope = {
                authored.normalized_rpm_slope.minimum_per_second,
                authored.normalized_rpm_slope.maximum_per_second};
            segment.captured_frames = {0U, frame_count};
            segment.usable_frames = {usable_begin, usable_end};
            segment.captured_rpm = {authored.captured_rpm.minimum,
                                    authored.captured_rpm.maximum};
            segment.usable_rpm = {authored.usable_rpm.minimum,
                                  authored.usable_rpm.maximum};
            segment.source_scenario = input.source_scenario;
            segment.capture_configuration = input.capture_configuration;
            segment.timeline = std::move(*timeline);
            segment.crank_boundaries = std::move(boundaries);
            segment.handoff = {
                authored.handoff.transition_frames, authored.handoff.maximum_rpm_error,
                authored.handoff.maximum_normalized_rpm_slope_error_per_second,
                authored.handoff.maximum_load_error,
                authored.handoff.maximum_crank_phase_error_revolutions};
            segment.artifacts.reserve(buses.size());

            for (std::size_t bus_index = 0U; bus_index < buses.size(); ++bus_index) {
                auto payload =
                    encode_f32le_crop(capture.buses[bus_index].samples, *crop);
                if (!payload.has_value()) {
                    return fail(
                        AudioAtlasAssemblyErrorCode::payload_invalid,
                        "audio-atlas-moving-pcm-crop-invalid",
                        "captured PCM is non-finite or cannot be cropped as Float32LE");
                }
                const auto id = artifact_id(segment.id, bus_index);
                const auto path = artifact_path(segment.id, bus_index);
                const auto digest = contract::sha256(*payload);
                manifest.artifacts.push_back(
                    {id, path, frame_count, static_cast<std::uint64_t>(payload->size()),
                     digest});
                segment.artifacts.push_back({buses[bus_index].id, id});
                assembled.payloads.push_back({id, std::move(*payload)});
            }
            manifest.moving_segments.push_back(std::move(segment));
        }

        auto report = contract::validate(manifest);
        if (!report.ok()) {
            return fail(AudioAtlasAssemblyErrorCode::contract_rejected,
                        "audio-atlas-assembled-manifest-rejected",
                        "assembled moving atlas failed its sole current contract",
                        std::move(report));
        }
        return assembled;
    } catch (const std::bad_alloc &) {
        return fail(AudioAtlasAssemblyErrorCode::resource_limit,
                    "audio-atlas-assembly-allocation-failed",
                    "allocation failed while assembling the moving atlas");
    } catch (...) {
        return fail(AudioAtlasAssemblyErrorCode::internal_failure,
                    "audio-atlas-assembly-internal-failure",
                    "unexpected failure while assembling the moving atlas");
    }
}

} // namespace engine_sim_offline
