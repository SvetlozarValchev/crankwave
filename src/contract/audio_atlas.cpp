#include "engine_sim_offline/contract/audio_atlas.hpp"

#include "validation_support.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

using detail::require;

[[nodiscard]] std::string indexed(std::string_view owner, std::size_t index) {
    return std::string{owner} + "[" + std::to_string(index) + "]";
}

[[nodiscard]] bool valid_relative_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find('\\') != std::string_view::npos ||
        path.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0U;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin
                                                 : end - begin);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return true;
}

[[nodiscard]] bool valid_frame_range(const AudioAtlasFrameRange &range) {
    return range.begin < range.end;
}

[[nodiscard]] bool strict_interior(const AudioAtlasFrameRange &inner,
                                   const AudioAtlasFrameRange &outer) {
    return outer.begin < inner.begin && inner.end < outer.end;
}

[[nodiscard]] bool valid_rpm_range(const AudioAtlasRpmRange &range) {
    return detail::finite_positive(range.minimum) &&
           detail::finite_positive(range.maximum) &&
           range.minimum < range.maximum;
}

[[nodiscard]] bool strict_interior(const AudioAtlasRpmRange &inner,
                                   const AudioAtlasRpmRange &outer) {
    return outer.minimum < inner.minimum && inner.maximum < outer.maximum;
}

[[nodiscard]] bool valid_fractional_frame(
    const AudioAtlasFractionalFrame &frame) {
    if (!detail::unit_interval(frame.fraction_from_left_01)) {
        return false;
    }
    if (frame.left_frame == frame.right_frame) {
        return frame.fraction_from_left_01 == 0.0;
    }
    return frame.left_frame != std::numeric_limits<std::uint64_t>::max() &&
           frame.right_frame == frame.left_frame + 1U &&
           frame.fraction_from_left_01 > 0.0 &&
           frame.fraction_from_left_01 < 1.0;
}

[[nodiscard]] long double
fractional_value(const AudioAtlasFractionalFrame &frame) {
    return static_cast<long double>(frame.left_frame) +
           static_cast<long double>(frame.fraction_from_left_01);
}

void validate_identity(ValidationReport &report,
                       const AudioAtlasContentIdentity &identity,
                       const std::string &path) {
    require(report, is_valid_semantic_id(identity.id),
            ContractIssueCode::invalid_value, path + ".id",
            "content identity requires a canonical semantic ID");
    require(report, !identity.sha256.is_zero(), ContractIssueCode::invalid_value,
            path + ".sha256", "content identity requires a nonzero digest");
}

void validate_handoff(ValidationReport &report,
                      const AudioAtlasMovingSegment &segment,
                      const std::string &path) {
    const auto &handoff = segment.handoff;
    require(report, handoff.transition_frames > 0U,
            ContractIssueCode::invalid_value, path + ".transition_frames",
            "handoff transition must contain at least one frame");
    require(report,
            segment.captured_frames.begin <= segment.usable_frames.begin &&
                segment.usable_frames.end <= segment.captured_frames.end &&
                segment.usable_frames.begin - segment.captured_frames.begin >=
                    handoff.transition_frames &&
                segment.captured_frames.end - segment.usable_frames.end >=
                    handoff.transition_frames,
            ContractIssueCode::inconsistent_shape, path + ".transition_frames",
            "captured guards must each contain the complete handoff transition");
    require(report, detail::finite_nonnegative(handoff.maximum_rpm_error),
            ContractIssueCode::invalid_value, path + ".maximum_rpm_error",
            "maximum RPM error must be finite and nonnegative");
    require(report,
            detail::finite_nonnegative(
                handoff.maximum_normalized_rpm_slope_error_per_second),
            ContractIssueCode::invalid_value,
            path + ".maximum_normalized_rpm_slope_error_per_second",
            "maximum normalized RPM-slope error must be finite and nonnegative");
    require(report, detail::finite_nonnegative(handoff.maximum_load_error) &&
                        handoff.maximum_load_error <= 2.0,
            ContractIssueCode::invalid_value, path + ".maximum_load_error",
            "maximum load error must lie in [0, 2]");
    require(report,
            detail::finite_nonnegative(
                handoff.maximum_crank_phase_error_revolutions) &&
                handoff.maximum_crank_phase_error_revolutions <= 0.5,
            ContractIssueCode::invalid_value,
            path + ".maximum_crank_phase_error_revolutions",
            "maximum crank-phase error must lie in [0, 0.5] revolutions");
}

void validate_timeline(ValidationReport &report,
                       const AudioAtlasMovingSegment &segment,
                       const std::string &path) {
    const auto &knots = segment.timeline.knots;
    require(report, knots.size() >= 2U, ContractIssueCode::missing_value, path,
            "moving segment requires at least two state-timeline knots");
    if (knots.empty()) {
        return;
    }
    require(report, knots.front().frame == segment.captured_frames.begin,
            ContractIssueCode::inconsistent_shape, path + "[0].frame",
            "timeline must begin at the captured-frame boundary");
    require(report, knots.back().frame == segment.captured_frames.end,
            ContractIssueCode::inconsistent_shape,
            path + "[" + std::to_string(knots.size() - 1U) + "].frame",
            "timeline end sentinel must equal the captured-frame end");

    for (std::size_t index = 0U; index < knots.size(); ++index) {
        const auto &knot = knots[index];
        const auto knot_path = indexed(path, index);
        if (index != 0U) {
            require(report, knots[index - 1U].frame < knot.frame,
                    ContractIssueCode::inconsistent_shape, knot_path + ".frame",
                    "timeline knot frames must be strictly increasing");
            require(report,
                    knots[index - 1U].unwrapped_crank_revolutions <
                        knot.unwrapped_crank_revolutions,
                    ContractIssueCode::inconsistent_semantics,
                    knot_path + ".unwrapped_crank_revolutions",
                    "moving-segment crank position must advance monotonically");
        }
        require(report, detail::finite_positive(knot.rpm),
                ContractIssueCode::invalid_value, knot_path + ".rpm",
                "timeline RPM must be finite and positive");
        require(report, detail::finite(knot.rpm_slope_rpm_per_second),
                ContractIssueCode::invalid_value,
                knot_path + ".rpm_slope_rpm_per_second",
                "timeline RPM slope must be finite");
        require(report, detail::unit_interval(knot.requested_throttle_01),
                ContractIssueCode::invalid_value,
                knot_path + ".requested_throttle_01",
                "requested throttle must lie in [0, 1]");
        require(report,
                detail::finite(knot.signed_load_coordinate) &&
                    knot.signed_load_coordinate >= -1.0 &&
                    knot.signed_load_coordinate <= 1.0,
                ContractIssueCode::invalid_value,
                knot_path + ".signed_load_coordinate",
                "signed load coordinate must lie in [-1, 1]");
        require(report, detail::finite_positive(knot.manifold_pressure_pa_abs),
                ContractIssueCode::invalid_value,
                knot_path + ".manifold_pressure_pa_abs",
                "absolute manifold pressure must be finite and positive");
        require(report, detail::finite(knot.unwrapped_crank_revolutions),
                ContractIssueCode::invalid_value,
                knot_path + ".unwrapped_crank_revolutions",
                "unwrapped crank position must be finite");
        require(report, (knot.state_mask & ~kAudioAtlasKnownStateMask) == 0U,
                ContractIssueCode::unsupported_value, knot_path + ".state_mask",
                "timeline state mask contains unknown bits");
        require(report,
                (knot.transition_mask & ~kAudioAtlasKnownStateMask) == 0U,
                ContractIssueCode::unsupported_value,
                knot_path + ".transition_mask",
                "timeline transition mask contains unknown bits");
    }
}

void validate_boundaries(ValidationReport &report,
                         const AudioAtlasMovingSegment &segment,
                         const std::string &path) {
    require(report, segment.crank_boundaries.size() >= 2U,
            ContractIssueCode::missing_value, path,
            "moving segment requires at least two crank-cycle boundaries");
    long double previous_position = -1.0L;
    for (std::size_t index = 0U; index < segment.crank_boundaries.size(); ++index) {
        const auto &boundary = segment.crank_boundaries[index];
        const auto boundary_path = indexed(path, index);
        require(report, valid_fractional_frame(boundary.position),
                ContractIssueCode::invalid_value, boundary_path + ".position",
                "crank boundary must name one exact or fractionally bracketed frame");
        require(report,
                boundary.position.left_frame >= segment.captured_frames.begin &&
                    boundary.position.right_frame <= segment.captured_frames.end,
                ContractIssueCode::inconsistent_shape,
                boundary_path + ".position",
                "crank boundary lies outside captured frames");
        const auto position = fractional_value(boundary.position);
        if (index != 0U) {
            require(report,
                    segment.crank_boundaries[index - 1U]
                                .completed_cycle_ordinal !=
                            std::numeric_limits<std::uint64_t>::max() &&
                        boundary.completed_cycle_ordinal ==
                            segment.crank_boundaries[index - 1U]
                                    .completed_cycle_ordinal +
                                1U,
                    ContractIssueCode::inconsistent_semantics,
                    boundary_path + ".completed_cycle_ordinal",
                    "crank-boundary ordinals must be contiguous");
            require(report, position > previous_position,
                    ContractIssueCode::inconsistent_shape,
                    boundary_path + ".position",
                    "crank-boundary positions must be strictly increasing");
        }
        previous_position = position;
    }
}

} // namespace

ValidationReport validate(const AudioAtlasManifest &manifest) {
    ValidationReport report;
    require(report, manifest.schema == kAudioAtlasSchema,
            ContractIssueCode::unsupported_value, "schema",
            "audio atlas must use the sole current schema");
    require(report, is_valid_semantic_id(manifest.id),
            ContractIssueCode::invalid_value, "id",
            "atlas ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(manifest.engine),
            ContractIssueCode::invalid_value, "engine",
            "engine ID must be a canonical semantic ID");

    require(report, manifest.audio.sample_rate_hz > 0U,
            ContractIssueCode::invalid_value, "audio.sample_rate_hz",
            "atlas audio sample rate must be positive");
    require(report,
            manifest.audio.encoding == AudioAtlasSampleEncoding::float32le,
            ContractIssueCode::unsupported_value, "audio.encoding",
            "the first atlas slice accepts Float32 little-endian PCM only");
    require(report,
            manifest.audio.channel_layout == AudioAtlasChannelLayout::mono,
            ContractIssueCode::unsupported_value, "audio.channel_layout",
            "the first atlas slice accepts mono buses only");
    require(report, !manifest.audio.buses.empty(), ContractIssueCode::missing_value,
            "audio.buses", "atlas must declare at least one audio bus");
    std::unordered_set<std::string> bus_ids;
    for (std::size_t index = 0U; index < manifest.audio.buses.size(); ++index) {
        const auto path = indexed("audio.buses", index) + ".id";
        const auto &id = manifest.audio.buses[index].id;
        require(report, is_valid_semantic_id(id), ContractIssueCode::invalid_value,
                path, "audio bus ID must be canonical");
        if (!bus_ids.insert(id).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "audio bus IDs must be unique");
        }
    }

    require(report, detail::finite_positive(manifest.domain.minimum_rpm),
            ContractIssueCode::invalid_value, "domain.minimum_rpm",
            "minimum RPM must be finite and positive");
    require(report,
            detail::finite_positive(manifest.domain.maximum_rpm) &&
                manifest.domain.minimum_rpm < manifest.domain.maximum_rpm,
            ContractIssueCode::invalid_value, "domain.maximum_rpm",
            "maximum RPM must be finite and exceed minimum RPM");
    require(report,
            detail::finite(manifest.domain.minimum_load_coordinate) &&
                detail::finite(manifest.domain.maximum_load_coordinate) &&
                manifest.domain.minimum_load_coordinate >= -1.0 &&
                manifest.domain.maximum_load_coordinate <= 1.0 &&
                manifest.domain.minimum_load_coordinate <=
                    manifest.domain.maximum_load_coordinate,
            ContractIssueCode::invalid_value, "domain.load_coordinate",
            "load domain must be an ordered subset of [-1, 1]");

    require(report, manifest.stationary_tiles.empty(),
            ContractIssueCode::unsupported_value, "stationary_tiles",
            "stationary tiles are not admitted by the moving-only slice");
    require(report, manifest.transient_performances.empty(),
            ContractIssueCode::unsupported_value, "transient_performances",
            "transient performances are not admitted by the moving-only slice");
    require(report, manifest.lifecycle_performances.empty(),
            ContractIssueCode::unsupported_value, "lifecycle_performances",
            "lifecycle performances are not admitted by the moving-only slice");

    std::unordered_map<std::string, const AudioAtlasArtifact *> artifacts;
    std::unordered_map<std::string, std::size_t> artifact_use_count;
    for (std::size_t index = 0U; index < manifest.artifacts.size(); ++index) {
        const auto &artifact = manifest.artifacts[index];
        const auto path = indexed("artifacts", index);
        require(report, is_valid_semantic_id(artifact.id),
                ContractIssueCode::invalid_value, path + ".id",
                "artifact ID must be canonical");
        if (!artifacts.emplace(artifact.id, &artifact).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "artifact IDs must be unique");
        }
        require(report, valid_relative_path(artifact.relative_path),
                ContractIssueCode::invalid_value, path + ".relative_path",
                "artifact path must be a normalized safe relative path");
        require(report, artifact.frame_count > 0U,
                ContractIssueCode::invalid_value, path + ".frame_count",
                "audio artifact must contain at least one frame");
        const bool size_fits =
            artifact.frame_count <=
            std::numeric_limits<std::uint64_t>::max() / UINT64_C(4);
        require(report,
                size_fits && artifact.byte_count == artifact.frame_count * 4U,
                ContractIssueCode::inconsistent_shape, path + ".byte_count",
                "mono Float32 artifact byte count must equal frame count times four");
        require(report, !artifact.payload_sha256.is_zero(),
                ContractIssueCode::invalid_value, path + ".payload_sha256",
                "artifact payload digest must be nonzero");
    }
    require(report, !manifest.artifacts.empty(), ContractIssueCode::missing_value,
            "artifacts", "moving atlas must contain audio artifacts");
    require(report, !manifest.moving_segments.empty(),
            ContractIssueCode::missing_value, "moving_segments",
            "moving-only atlas must contain at least one moving segment");

    std::unordered_set<std::string> segment_ids;
    for (std::size_t index = 0U; index < manifest.moving_segments.size(); ++index) {
        const auto &segment = manifest.moving_segments[index];
        const auto path = indexed("moving_segments", index);
        require(report, is_valid_semantic_id(segment.id),
                ContractIssueCode::invalid_value, path + ".id",
                "moving-segment ID must be canonical");
        if (!segment_ids.insert(segment.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "moving-segment IDs must be unique");
        }
        const bool rising = segment.direction == AudioAtlasMovingDirection::rising;
        const bool falling = segment.direction == AudioAtlasMovingDirection::falling;
        require(report, rising || falling, ContractIssueCode::unsupported_value,
                path + ".direction", "moving direction is unsupported");
        require(report,
                detail::finite(segment.load_coordinate) &&
                    segment.load_coordinate >=
                        manifest.domain.minimum_load_coordinate &&
                    segment.load_coordinate <=
                        manifest.domain.maximum_load_coordinate,
                ContractIssueCode::invalid_value, path + ".load_coordinate",
                "moving-segment load lies outside the declared domain");
        require(report, (segment.state_mask & ~kAudioAtlasKnownStateMask) == 0U,
                ContractIssueCode::unsupported_value, path + ".state_mask",
                "moving-segment state mask contains unknown bits");
        require(report,
                detail::finite(
                    segment.normalized_rpm_slope.minimum_per_second) &&
                    detail::finite(
                        segment.normalized_rpm_slope.maximum_per_second) &&
                    segment.normalized_rpm_slope.minimum_per_second <=
                        segment.normalized_rpm_slope.maximum_per_second &&
                    (!rising ||
                     segment.normalized_rpm_slope.minimum_per_second > 0.0) &&
                    (!falling ||
                     segment.normalized_rpm_slope.maximum_per_second < 0.0),
                ContractIssueCode::invalid_value,
                path + ".normalized_rpm_slope",
                "normalized RPM-slope envelope must be finite, ordered, and match direction");
        require(report, valid_frame_range(segment.captured_frames),
                ContractIssueCode::invalid_value, path + ".captured_frames",
                "captured frame range must be nonempty");
        require(report,
                valid_frame_range(segment.usable_frames) &&
                    strict_interior(segment.usable_frames,
                                    segment.captured_frames),
                ContractIssueCode::inconsistent_shape, path + ".usable_frames",
                "usable frames must be a strict interior of captured frames");
        require(report, valid_rpm_range(segment.captured_rpm),
                ContractIssueCode::invalid_value, path + ".captured_rpm",
                "captured RPM range must be finite, positive, and ordered");
        require(report,
                valid_rpm_range(segment.usable_rpm) &&
                    strict_interior(segment.usable_rpm, segment.captured_rpm) &&
                    segment.usable_rpm.minimum >= manifest.domain.minimum_rpm &&
                    segment.usable_rpm.maximum <= manifest.domain.maximum_rpm,
                ContractIssueCode::inconsistent_shape, path + ".usable_rpm",
                "usable RPM must be interior to capture and inside the domain");
        validate_identity(report, segment.source_scenario,
                          path + ".source_scenario");
        validate_identity(report, segment.capture_configuration,
                          path + ".capture_configuration");

        std::unordered_set<std::string> segment_buses;
        for (std::size_t ref_index = 0U; ref_index < segment.artifacts.size();
             ++ref_index) {
            const auto &reference = segment.artifacts[ref_index];
            const auto ref_path = indexed(path + ".artifacts", ref_index);
            require(report, bus_ids.contains(reference.bus_id),
                    ContractIssueCode::dangling_reference,
                    ref_path + ".bus_id",
                    "segment artifact references an undeclared audio bus");
            if (!segment_buses.insert(reference.bus_id).second) {
                report.add(ContractIssueCode::duplicate_identity,
                           ref_path + ".bus_id",
                           "segment may bind each audio bus only once");
            }
            const auto found = artifacts.find(reference.artifact_id);
            require(report, found != artifacts.end(),
                    ContractIssueCode::dangling_reference,
                    ref_path + ".artifact_id",
                    "segment references an undeclared artifact");
            if (found != artifacts.end()) {
                ++artifact_use_count[reference.artifact_id];
                require(report,
                        segment.captured_frames.end <= found->second->frame_count,
                        ContractIssueCode::inconsistent_shape,
                        ref_path + ".artifact_id",
                        "captured frames exceed the referenced artifact");
            }
        }
        require(report, segment_buses == bus_ids,
                ContractIssueCode::inconsistent_shape, path + ".artifacts",
                "moving segment must bind every declared bus exactly once");
        validate_timeline(report, segment, path + ".timeline.knots");
        validate_boundaries(report, segment, path + ".crank_boundaries");
        validate_handoff(report, segment, path + ".handoff");
    }

    for (const auto &[id, artifact] : artifacts) {
        static_cast<void>(artifact);
        require(report, artifact_use_count[id] == 1U,
                ContractIssueCode::inconsistent_semantics, "artifacts",
                "each first-slice artifact must belong to exactly one segment bus");
    }

    validate_identity(report, manifest.provenance.engine, "provenance.engine");
    validate_identity(report, manifest.provenance.bake_document,
                      "provenance.bake_document");
    validate_identity(report, manifest.provenance.renderer_build,
                      "provenance.renderer_build");
    require(report, manifest.provenance.engine.id == manifest.engine,
            ContractIssueCode::inconsistent_semantics, "provenance.engine.id",
            "provenance engine identity must match the atlas engine");
    require(report, is_valid_semantic_id(manifest.provenance.source_inputs.id),
            ContractIssueCode::invalid_value, "provenance.source_inputs.id",
            "source-input bundle ID must be canonical");
    require(report, !manifest.provenance.source_inputs.sha256.is_zero(),
            ContractIssueCode::invalid_value, "provenance.source_inputs.sha256",
            "source-input bundle digest must be nonzero");
    return report;
}

} // namespace engine_sim_offline::contract
