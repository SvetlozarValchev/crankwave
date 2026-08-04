#include "engine_sim_offline/contract/audio_package.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

[[nodiscard]] std::string indexed(std::string_view path, std::size_t index) {
    return std::string{path} + "[" + std::to_string(index) + "]";
}

[[nodiscard]] bool canonical_finite(double value) noexcept {
    return std::isfinite(value) && (value != 0.0 || !std::signbit(value));
}

[[nodiscard]] bool runtime_safe_integer(std::uint64_t value) noexcept {
    return value <= kMaximumResolvedFrameIndex;
}

void validate_identity(ValidationReport &report,
                       const AudioPackageContentIdentity &identity,
                       const std::string &path) {
    detail::require(report, is_valid_semantic_id(identity.id),
                    ContractIssueCode::invalid_value, path + ".id",
                    "content identity ID must be a canonical semantic ID");
    detail::require(report, !identity.sha256.is_zero(),
                    ContractIssueCode::invalid_value, path + ".sha256",
                    "content identity digest must be nonzero");
}

[[nodiscard]] bool valid_relative_artifact_path(std::string_view path) noexcept {
    if (path.empty() || path.front() == '/' || path.back() == '/') {
        return false;
    }
    for (const unsigned char byte : path) {
        const bool admitted = (byte >= 'a' && byte <= 'z') ||
                              (byte >= 'A' && byte <= 'Z') ||
                              (byte >= '0' && byte <= '9') || byte == '.' ||
                              byte == '_' || byte == '-' || byte == '/';
        if (!admitted) {
            return false;
        }
    }

    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return true;
}

[[nodiscard]] std::string portable_path_key(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    for (const char character : path) {
        result.push_back(character >= 'A' && character <= 'Z'
                             ? static_cast<char>(character - 'A' + 'a')
                             : character);
    }
    return result;
}

[[nodiscard]] bool valid_boundary(const AudioPackageSourceBoundary &boundary) {
    if (!runtime_safe_integer(boundary.left_frame) ||
        !runtime_safe_integer(boundary.right_frame) ||
        !canonical_finite(boundary.fraction_from_left_01)) {
        return false;
    }
    if (boundary.left_frame == boundary.right_frame) {
        return boundary.fraction_from_left_01 == 0.0;
    }
    return boundary.left_frame < kMaximumResolvedFrameIndex &&
           boundary.right_frame == boundary.left_frame + 1U &&
           boundary.fraction_from_left_01 > 0.0 && boundary.fraction_from_left_01 < 1.0;
}

// Every admitted boundary has the normalized coordinate left_frame + fraction.
[[nodiscard]] bool boundary_less(const AudioPackageSourceBoundary &lhs,
                                 const AudioPackageSourceBoundary &rhs) noexcept {
    return lhs.left_frame < rhs.left_frame ||
           (lhs.left_frame == rhs.left_frame &&
            lhs.fraction_from_left_01 < rhs.fraction_from_left_01);
}

[[nodiscard]] bool boundary_less_equal(const AudioPackageSourceBoundary &lhs,
                                       const AudioPackageSourceBoundary &rhs) noexcept {
    return !boundary_less(rhs, lhs);
}

void validate_boundary(ValidationReport &report,
                       const AudioPackageSourceBoundary &boundary,
                       std::uint64_t source_frame_count,
                       std::uint32_t edge_guard_frames, const std::string &path) {
    detail::require(
        report, valid_boundary(boundary), ContractIssueCode::invalid_value, path,
        "boundary must be an exact source frame or a strict fraction between "
        "adjacent source frames");
    detail::require(report, boundary.right_frame < source_frame_count,
                    ContractIssueCode::inconsistent_shape, path + ".right_frame",
                    "boundary bracket must lie inside every lane artifact");
    detail::require(report, boundary.left_frame >= edge_guard_frames,
                    ContractIssueCode::inconsistent_shape, path + ".left_frame",
                    "boundary must retain the declared left-edge source context");
    const auto available_right_context =
        boundary.right_frame < source_frame_count
            ? source_frame_count - boundary.right_frame - 1U
            : 0U;
    detail::require(report, available_right_context >= edge_guard_frames,
                    ContractIssueCode::inconsistent_shape, path + ".right_frame",
                    "boundary must retain the declared right-edge source context");
}

void validate_unit_values(ValidationReport &report, const AudioPackageCycleUnit &unit,
                          std::uint64_t source_frame_count,
                          std::uint32_t edge_guard_frames, const std::string &path) {
    detail::require(report, runtime_safe_integer(unit.completed_cycle_ordinal),
                    ContractIssueCode::invalid_value, path + ".completed_cycle_ordinal",
                    "cycle ordinal must be exactly representable by the runtime");
    validate_boundary(report, unit.start, source_frame_count, edge_guard_frames,
                      path + ".start");
    validate_boundary(report, unit.end, source_frame_count, edge_guard_frames,
                      path + ".end");
    if (valid_boundary(unit.start) && valid_boundary(unit.end)) {
        detail::require(report, boundary_less(unit.start, unit.end),
                        ContractIssueCode::inconsistent_shape, path + ".end",
                        "cycle unit must have positive source duration");
    }

    detail::require(report,
                    canonical_finite(unit.canonical_rpm) && unit.canonical_rpm > 0.0,
                    ContractIssueCode::invalid_value, path + ".canonical_rpm",
                    "canonical RPM must be finite and positive");
    detail::require(report,
                    canonical_finite(unit.measured_rpm) && unit.measured_rpm > 0.0,
                    ContractIssueCode::invalid_value, path + ".measured_rpm",
                    "measured RPM must be finite and positive");
    detail::require(report,
                    canonical_finite(unit.average_signed_load) &&
                        unit.average_signed_load >= -1.0 &&
                        unit.average_signed_load <= 1.0,
                    ContractIssueCode::invalid_value, path + ".average_signed_load",
                    "average signed load must be finite and in [-1, 1]");
    detail::require(report, canonical_finite(unit.average_net_torque_nm),
                    ContractIssueCode::invalid_value, path + ".average_net_torque_nm",
                    "average net torque must be finite");
    detail::require(report,
                    canonical_finite(unit.average_requested_throttle_01) &&
                        unit.average_requested_throttle_01 >= 0.0 &&
                        unit.average_requested_throttle_01 <= 1.0,
                    ContractIssueCode::invalid_value,
                    path + ".average_requested_throttle_01",
                    "average requested throttle must be finite and in [0, 1]");
    detail::require(report,
                    canonical_finite(unit.average_resolved_throttle_01) &&
                        unit.average_resolved_throttle_01 >= 0.0 &&
                        unit.average_resolved_throttle_01 <= 1.0,
                    ContractIssueCode::invalid_value,
                    path + ".average_resolved_throttle_01",
                    "average resolved throttle must be finite and in [0, 1]");
    detail::require(report, unit.transition_mask == 0U,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".transition_mask",
                    "normal-running cycle units must have one stable state mask");
}

struct ArtifactRegistry {
    std::unordered_map<std::string, const AudioPackageArtifact *> by_id;
    std::unordered_set<std::string> referenced_ids;
};

[[nodiscard]] ArtifactRegistry
validate_artifacts(ValidationReport &report,
                   const std::vector<AudioPackageArtifact> &artifacts) {
    ArtifactRegistry registry;
    std::unordered_set<std::string> portable_paths;
    detail::require(report, !artifacts.empty(), ContractIssueCode::missing_value,
                    "artifacts", "package must own at least one audio artifact");

    for (std::size_t index = 0; index < artifacts.size(); ++index) {
        const auto &artifact = artifacts[index];
        const auto path = indexed("artifacts", index);
        detail::require(report, is_valid_semantic_id(artifact.id),
                        ContractIssueCode::invalid_value, path + ".id",
                        "artifact ID must be a canonical semantic ID");
        if (!registry.by_id.emplace(artifact.id, &artifact).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "artifact IDs must be unique");
        }
        if (index > 0) {
            detail::require(report, artifacts[index - 1].id < artifact.id,
                            ContractIssueCode::inconsistent_semantics, path + ".id",
                            "artifacts must be ordered by strictly increasing ID");
        }
        detail::require(report, valid_relative_artifact_path(artifact.relative_path),
                        ContractIssueCode::invalid_value, path + ".relative_path",
                        "artifact path must be conservative, relative, and portable");
        if (!portable_paths.insert(portable_path_key(artifact.relative_path)).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".relative_path",
                       "artifact paths must be unique under portable case folding");
        }
        detail::require(report,
                        artifact.frame_count > 0 &&
                            runtime_safe_integer(artifact.frame_count),
                        ContractIssueCode::invalid_value, path + ".frame_count",
                        "artifact frame count must be positive and runtime-safe");
        detail::require(report,
                        artifact.byte_count > 0 &&
                            runtime_safe_integer(artifact.byte_count),
                        ContractIssueCode::invalid_value, path + ".byte_count",
                        "artifact byte count must be positive and runtime-safe");
        detail::require(report, !artifact.payload_sha256.is_zero(),
                        ContractIssueCode::invalid_value, path + ".payload_sha256",
                        "artifact payload digest must be nonzero");
    }
    return registry;
}

[[nodiscard]] std::uint64_t
validate_lane_artifacts(ValidationReport &report,
                        const std::vector<AudioPackageLaneArtifactRef> &references,
                        const std::vector<AudioPackageBusDescriptor> &buses,
                        ArtifactRegistry &registry, const std::string &path) {
    detail::require(report, references.size() == buses.size(),
                    ContractIssueCode::inconsistent_shape, path,
                    "lane must reference exactly one artifact per package bus");

    std::uint64_t shared_frame_count = 0;
    for (std::size_t index = 0; index < references.size(); ++index) {
        const auto &reference = references[index];
        const auto item_path = indexed(path, index);
        if (index >= buses.size() || reference.bus_id != buses[index].id) {
            report.add(
                ContractIssueCode::inconsistent_semantics, item_path + ".bus_id",
                "lane artifact references must exactly follow package bus order");
        }
        const auto found = registry.by_id.find(reference.artifact_id);
        if (found == registry.by_id.end()) {
            report.add(ContractIssueCode::dangling_reference,
                       item_path + ".artifact_id",
                       "lane artifact reference is absent from the artifact registry");
            continue;
        }
        if (!registry.referenced_ids.insert(reference.artifact_id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       item_path + ".artifact_id",
                       "an audio artifact may belong to only one operating lane");
        }
        if (shared_frame_count == 0) {
            shared_frame_count = found->second->frame_count;
        } else {
            detail::require(
                report, found->second->frame_count == shared_frame_count,
                ContractIssueCode::inconsistent_shape, item_path + ".artifact_id",
                "all bus tapes in one lane must share an exact frame clock");
        }
    }
    return shared_frame_count;
}

void validate_units_in_source_order(ValidationReport &report,
                                    const std::vector<AudioPackageCycleUnit> &units,
                                    std::uint64_t source_frame_count,
                                    std::uint32_t edge_guard_frames,
                                    AudioPackageRunningDirection direction,
                                    const std::string &path) {
    detail::require(report, !units.empty(), ContractIssueCode::missing_value, path,
                    "operating lane must contain at least one complete cycle unit");
    std::unordered_set<std::uint64_t> ordinals;
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto &unit = units[index];
        const auto item_path = indexed(path, index);
        validate_unit_values(report, unit, source_frame_count, edge_guard_frames,
                             item_path);
        if (!ordinals.insert(unit.completed_cycle_ordinal).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       item_path + ".completed_cycle_ordinal",
                       "cycle ordinals must be unique within one operating lane");
        }
        if (index == 0 || !valid_boundary(unit.start) || !valid_boundary(unit.end)) {
            continue;
        }

        const auto &previous = units[index - 1];
        if (direction == AudioPackageRunningDirection::rising) {
            detail::require(
                report, previous.completed_cycle_ordinal < unit.completed_cycle_ordinal,
                ContractIssueCode::inconsistent_semantics,
                item_path + ".completed_cycle_ordinal",
                "rising rows must preserve increasing source cycle order");
            detail::require(report, boundary_less_equal(previous.end, unit.start),
                            ContractIssueCode::inconsistent_shape, item_path + ".start",
                            "rising rows must not reuse overlapping source frames");
        } else {
            detail::require(
                report, previous.completed_cycle_ordinal > unit.completed_cycle_ordinal,
                ContractIssueCode::inconsistent_semantics,
                item_path + ".completed_cycle_ordinal",
                "falling rows must preserve decreasing source cycle order");
            detail::require(report, boundary_less_equal(unit.end, previous.start),
                            ContractIssueCode::inconsistent_shape, item_path + ".end",
                            "falling rows must not reuse overlapping source frames");
        }
    }
}

} // namespace

ValidationReport validate(const AudioPackageManifest &manifest) {
    ValidationReport report;
    detail::require(report, manifest.schema == kAudioPackageSchema,
                    ContractIssueCode::unsupported_value, "schema",
                    "audio package schema must be engine-sim-offline/audio-package");

    detail::require(report, is_valid_semantic_id(manifest.identity.package_id),
                    ContractIssueCode::invalid_value, "identity.package_id",
                    "package ID must be a canonical semantic ID");
    validate_identity(report, manifest.identity.engine, "identity.engine");
    validate_identity(report, manifest.identity.bake_plan, "identity.bake_plan");
    validate_identity(report, manifest.provenance.renderer_build,
                      "provenance.renderer_build");
    detail::require(report, is_valid_semantic_id(manifest.provenance.source_inputs.id),
                    ContractIssueCode::invalid_value, "provenance.source_inputs.id",
                    "source provenance ID must be a canonical semantic ID");
    detail::require(report, !manifest.provenance.source_inputs.sha256.is_zero(),
                    ContractIssueCode::invalid_value, "provenance.source_inputs.sha256",
                    "source provenance digest must be nonzero");

    detail::append_prefixed(report, validate(manifest.audio.sample_rate),
                            "audio.sample_rate");
    detail::require(report, runtime_safe_integer(manifest.audio.sample_rate.numerator),
                    ContractIssueCode::invalid_value, "audio.sample_rate.numerator",
                    "sample-rate numerator must remain exact in the browser runtime");
    detail::require(report,
                    runtime_safe_integer(manifest.audio.sample_rate.denominator),
                    ContractIssueCode::invalid_value, "audio.sample_rate.denominator",
                    "sample-rate denominator must remain exact in the browser runtime");
    detail::require(report, manifest.audio.container == AudioPackageAudioContainer::wav,
                    ContractIssueCode::unsupported_value, "audio.container",
                    "the first package gate admits only WAVE payload containers");
    detail::require(report, manifest.audio.encoding == AudioSampleEncoding::float32le,
                    ContractIssueCode::unsupported_value, "audio.encoding",
                    "the first package gate admits only Float32 little-endian tapes");
    detail::require(report, manifest.audio.channel_layout == AudioChannelLayout::mono,
                    ContractIssueCode::unsupported_value, "audio.channel_layout",
                    "each package bus must currently be a mono tape");

    detail::require(report, !manifest.buses.empty(), ContractIssueCode::missing_value,
                    "buses", "package must declare at least one ordered audio bus");
    std::unordered_set<std::string> bus_ids;
    std::unordered_set<std::string> source_route_ids;
    for (std::size_t index = 0; index < manifest.buses.size(); ++index) {
        const auto &bus = manifest.buses[index];
        const auto path = indexed("buses", index);
        detail::require(report, is_valid_semantic_id(bus.id),
                        ContractIssueCode::invalid_value, path + ".id",
                        "bus ID must be a canonical semantic ID");
        if (!bus_ids.insert(bus.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "package bus IDs must be unique");
        }
        const bool master = bus.kind == AudioPackageBusKind::master_engine_audition;
        const bool route = bus.kind == AudioPackageBusKind::source_route;
        detail::require(report, master || route, ContractIssueCode::invalid_value,
                        path + ".kind", "bus kind is not part of the package contract");
        const bool monitor = bus.disposition == AudioPackageBusDisposition::monitor_mix;
        const bool positional =
            bus.disposition == AudioPackageBusDisposition::positional_emitter;
        detail::require(report, monitor || positional, ContractIssueCode::invalid_value,
                        path + ".disposition",
                        "bus disposition is not part of the package contract");
        detail::require(
            report,
            (master && monitor && !bus.source_route.has_value()) ||
                (route && positional && bus.source_route.has_value()),
            ContractIssueCode::inconsistent_semantics, path,
            "master buses are monitor mixes; route buses are positional emitters");
        if (bus.source_route.has_value()) {
            const auto &source_route = *bus.source_route;
            const bool route_kind_valid =
                source_route.kind == SourceRouteKind::exhaust_outlet ||
                source_route.kind == SourceRouteKind::intake_inlet ||
                source_route.kind == SourceRouteKind::mechanical_engine ||
                source_route.kind == SourceRouteKind::mechanical_starter;
            detail::require(report, route_kind_valid, ContractIssueCode::invalid_value,
                            path + ".source_route.kind",
                            "source-route kind is not part of the package contract");
            detail::require(report, is_valid_semantic_id(source_route.semantic_id),
                            ContractIssueCode::invalid_value,
                            path + ".source_route.semantic_id",
                            "source-route semantic ID must be canonical");
            if (!source_route_ids.insert(source_route.semantic_id).second) {
                report.add(ContractIssueCode::duplicate_identity,
                           path + ".source_route.semantic_id",
                           "source-route semantic IDs must be unique across buses");
            }
            detail::require(report,
                            is_valid_semantic_id(source_route.emitter_anchor_id),
                            ContractIssueCode::invalid_value,
                            path + ".source_route.emitter_anchor_id",
                            "source-route emitter anchor must be canonical");
        }
    }

    auto artifact_registry = validate_artifacts(report, manifest.artifacts);

    const auto &running = manifest.running;
    detail::require(report, running.cycle_revolutions == 2U,
                    ContractIssueCode::unsupported_value, "running.cycle_revolutions",
                    "four-stroke package units must span exactly two revolutions");
    detail::require(
        report,
        canonical_finite(running.cycle_signal_alignment_frames) &&
            running.cycle_signal_alignment_frames >= 0.0,
        ContractIssueCode::invalid_value,
        "running.cycle_signal_alignment_frames",
        "cycle signal alignment must be a finite nonnegative delivery-frame offset");
    const auto &grid = running.rpm_grid;
    detail::require(report,
                    canonical_finite(grid.minimum_rpm) && grid.minimum_rpm > 0.0,
                    ContractIssueCode::invalid_value, "running.rpm_grid.minimum_rpm",
                    "grid minimum RPM must be finite and positive");
    detail::require(report,
                    canonical_finite(grid.playback_minimum_rpm) &&
                        grid.playback_minimum_rpm > grid.minimum_rpm,
                    ContractIssueCode::invalid_value,
                    "running.rpm_grid.playback_minimum_rpm",
                    "playback minimum must be finite and above the padded minimum");
    detail::require(report,
                    canonical_finite(grid.playback_maximum_rpm) &&
                        grid.playback_maximum_rpm > grid.playback_minimum_rpm,
                    ContractIssueCode::invalid_value,
                    "running.rpm_grid.playback_maximum_rpm",
                    "playback maximum must be finite and above its minimum");
    detail::require(report,
                    canonical_finite(grid.maximum_rpm) &&
                        grid.maximum_rpm > grid.playback_maximum_rpm,
                    ContractIssueCode::invalid_value, "running.rpm_grid.maximum_rpm",
                    "grid maximum must be finite and above the playback maximum");
    detail::require(report,
                    canonical_finite(grid.spacing_rpm) && grid.spacing_rpm > 0.0,
                    ContractIssueCode::invalid_value, "running.rpm_grid.spacing_rpm",
                    "grid spacing must be finite and positive");
    detail::require(report, grid.padding_rows_per_side > 0U,
                    ContractIssueCode::invalid_value,
                    "running.rpm_grid.padding_rows_per_side",
                    "grid must retain at least one padding row on each side");
    detail::require(report, grid.neighbor_radius_rows > 0U,
                    ContractIssueCode::invalid_value,
                    "running.rpm_grid.neighbor_radius_rows",
                    "selector must declare a nonzero neighboring-row radius");
    detail::require(report, grid.padding_rows_per_side >= grid.neighbor_radius_rows,
                    ContractIssueCode::inconsistent_semantics,
                    "running.rpm_grid.padding_rows_per_side",
                    "selector padding must cover the full neighboring-row radius");
    detail::require(report, grid.edge_guard_frames > 0U,
                    ContractIssueCode::invalid_value,
                    "running.rpm_grid.edge_guard_frames",
                    "every retained unit must have source-frame edge context");
    const auto padding_rpm =
        grid.spacing_rpm * static_cast<double>(grid.padding_rows_per_side);
    detail::require(
        report,
        detail::nearly_equal(grid.minimum_rpm, grid.playback_minimum_rpm - padding_rpm),
        ContractIssueCode::inconsistent_semantics, "running.rpm_grid.minimum_rpm",
        "full grid minimum must include the declared lower padding rows");
    detail::require(
        report,
        detail::nearly_equal(grid.maximum_rpm, grid.playback_maximum_rpm + padding_rpm),
        ContractIssueCode::inconsistent_semantics, "running.rpm_grid.maximum_rpm",
        "full grid maximum must include the declared upper padding rows");
    detail::require(
        report,
        canonical_finite(grid.maximum_assignment_error_rpm) &&
            grid.maximum_assignment_error_rpm >= 0.0 &&
            grid.maximum_assignment_error_rpm <= grid.spacing_rpm * 0.5,
        ContractIssueCode::invalid_value,
        "running.rpm_grid.maximum_assignment_error_rpm",
        "assignment error must be finite and no greater than half one grid step");

    detail::require(
        report, running.planes.size() >= 3U, ContractIssueCode::inconsistent_shape,
        "running.planes",
        "normal running requires at least coast, part-load, and power planes");
    std::unordered_set<std::string> plane_ids;
    std::unordered_set<std::string> source_scenario_ids;
    std::size_t grid_unit_count = 0;
    for (std::size_t plane_index = 0; plane_index < running.planes.size();
         ++plane_index) {
        const auto &plane = running.planes[plane_index];
        const auto path = indexed("running.planes", plane_index);
        detail::require(report, is_valid_semantic_id(plane.id),
                        ContractIssueCode::invalid_value, path + ".id",
                        "running plane ID must be a canonical semantic ID");
        if (!plane_ids.insert(plane.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "running plane IDs must be unique");
        }
        detail::require(report,
                        canonical_finite(plane.load_coordinate) &&
                            plane.load_coordinate >= -1.0 &&
                            plane.load_coordinate <= 1.0,
                        ContractIssueCode::invalid_value, path + ".load_coordinate",
                        "plane load coordinate must be finite and in [-1, 1]");
        if (plane_index > 0) {
            detail::require(
                report,
                running.planes[plane_index - 1].load_coordinate < plane.load_coordinate,
                ContractIssueCode::inconsistent_semantics, path + ".load_coordinate",
                "running planes must be ordered by strictly increasing load");
        }
        if (plane_index == 0) {
            detail::require(report, plane.load_coordinate == -1.0,
                            ContractIssueCode::inconsistent_semantics,
                            path + ".load_coordinate",
                            "first running plane must own the -1 coast endpoint");
        }
        if (plane_index + 1 == running.planes.size()) {
            detail::require(report, plane.load_coordinate == 1.0,
                            ContractIssueCode::inconsistent_semantics,
                            path + ".load_coordinate",
                            "last running plane must own the +1 power endpoint");
        }
        const bool direction_valid =
            plane.direction == AudioPackageRunningDirection::rising ||
            plane.direction == AudioPackageRunningDirection::falling;
        detail::require(report, direction_valid, ContractIssueCode::invalid_value,
                        path + ".direction",
                        "running direction is not part of the package contract");
        validate_identity(report, plane.source_scenario, path + ".source_scenario");
        if (!source_scenario_ids.insert(plane.source_scenario.id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".source_scenario.id",
                       "each operating lane must own an independent source scenario");
        }
        const auto source_frame_count =
            validate_lane_artifacts(report, plane.artifacts, manifest.buses,
                                    artifact_registry, path + ".artifacts");
        validate_units_in_source_order(report, plane.units, source_frame_count,
                                       grid.edge_guard_frames, plane.direction,
                                       path + ".units");

        if (plane_index == 0) {
            grid_unit_count = plane.units.size();
            detail::require(report, grid_unit_count >= 2U,
                            ContractIssueCode::inconsistent_shape, path + ".units",
                            "running grid must contain at least two RPM rows");
        } else {
            detail::require(report, plane.units.size() == grid_unit_count,
                            ContractIssueCode::inconsistent_shape, path + ".units",
                            "all running planes must share one RPM grid shape");
        }
        for (std::size_t unit_index = 0; unit_index < plane.units.size();
             ++unit_index) {
            const auto &unit = plane.units[unit_index];
            const auto unit_path = indexed(path + ".units", unit_index);
            const auto expected_rpm =
                grid.minimum_rpm + grid.spacing_rpm * static_cast<double>(unit_index);
            detail::require(
                report, detail::nearly_equal(unit.canonical_rpm, expected_rpm),
                ContractIssueCode::inconsistent_semantics, unit_path + ".canonical_rpm",
                "canonical RPM does not occupy the declared uniform grid");
            detail::require(
                report,
                detail::nearly_equal(unit.average_signed_load,
                                     plane.load_coordinate),
                ContractIssueCode::inconsistent_semantics,
                unit_path + ".average_signed_load",
                "cycle load coordinate must equal its authored running plane");
            detail::require(report,
                            canonical_finite(unit.measured_rpm) &&
                                std::abs(unit.measured_rpm - unit.canonical_rpm) <=
                                    grid.maximum_assignment_error_rpm,
                            ContractIssueCode::inconsistent_semantics,
                            unit_path + ".measured_rpm",
                            "measured RPM exceeds the declared row assignment error");
            if (plane_index > 0 && unit_index < running.planes.front().units.size()) {
                detail::require(
                    report,
                    detail::nearly_equal(
                        unit.canonical_rpm,
                        running.planes.front().units[unit_index].canonical_rpm),
                    ContractIssueCode::inconsistent_shape, unit_path + ".canonical_rpm",
                    "all load planes must share identical canonical RPM rows");
                detail::require(
                    report,
                    running.planes[plane_index - 1U]
                            .units[unit_index]
                            .average_net_torque_nm < unit.average_net_torque_nm,
                    ContractIssueCode::inconsistent_semantics,
                    unit_path + ".average_net_torque_nm",
                    "adjacent load planes must have strictly increasing cycle-mean "
                    "net torque at every RPM row");
            }
        }
    }
    if (grid_unit_count > 0) {
        const auto represented_maximum =
            grid.minimum_rpm +
            grid.spacing_rpm * static_cast<double>(grid_unit_count - 1U);
        detail::require(
            report, detail::nearly_equal(represented_maximum, grid.maximum_rpm),
            ContractIssueCode::inconsistent_shape, "running.rpm_grid.maximum_rpm",
            "grid bounds, spacing, and row count must close exactly");
    }

    validate_identity(report, running.idle.source_scenario,
                      "running.idle.source_scenario");
    if (!source_scenario_ids.insert(running.idle.source_scenario.id).second) {
        report.add(ContractIssueCode::duplicate_identity,
                   "running.idle.source_scenario.id",
                   "idle must own an independent source scenario");
    }
    const auto idle_frame_count =
        validate_lane_artifacts(report, running.idle.artifacts, manifest.buses,
                                artifact_registry, "running.idle.artifacts");
    validate_units_in_source_order(
        report, running.idle.units, idle_frame_count, grid.edge_guard_frames,
        AudioPackageRunningDirection::rising, "running.idle.units");

    detail::require(report, manifest.events.empty(),
                    ContractIssueCode::unsupported_value, "events",
                    "lifecycle events are outside the first audio package gate");
    for (std::size_t index = 0; index < manifest.artifacts.size(); ++index) {
        const auto &artifact = manifest.artifacts[index];
        if (!artifact.id.empty() &&
            !artifact_registry.referenced_ids.contains(artifact.id)) {
            report.add(ContractIssueCode::dangling_reference,
                       indexed("artifacts", index) + ".id",
                       "package artifact is not owned by any operating lane");
        }
    }

    return report;
}

} // namespace engine_sim_offline::contract
