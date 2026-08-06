#include "engine_sim_offline/contract/audio_atlas.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

[[nodiscard]] bool is_power_of_two(const std::uint32_t value) noexcept {
    return value >= 2U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] bool checked_product(const std::uint64_t lhs,
                                   const std::uint64_t rhs,
                                   std::uint64_t &result) noexcept {
    if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
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

[[nodiscard]] bool state_is_supported(const AudioAtlasDomain &domain,
                                      const std::uint32_t state_mask) {
    return std::ranges::find(domain.supported_state_masks, state_mask) !=
           domain.supported_state_masks.end();
}

void validate_state(ValidationReport &report, const AudioAtlasDomain &domain,
                    const std::uint32_t state_mask, const std::string &path) {
    require(report, (state_mask & ~kAudioAtlasKnownStateMask) == 0U,
            ContractIssueCode::unsupported_value, path,
            "engine state mask contains unknown bits");
    require(report, state_is_supported(domain, state_mask),
            ContractIssueCode::unsupported_value, path,
            "engine state mask is outside the package's supported states");
}

using ArtifactMap =
    std::unordered_map<std::string, const AudioAtlasArtifact *>;
using ArtifactUseMap = std::unordered_map<std::string, std::size_t>;

[[nodiscard]] const AudioAtlasArtifact *
reference_artifact(ValidationReport &report, const ArtifactMap &artifacts,
                   ArtifactUseMap &uses, std::string_view artifact_id,
                   const std::string &path,
                   const AudioAtlasArtifactEncoding expected_encoding,
                   const std::optional<std::uint64_t> expected_elements =
                       std::nullopt) {
    const auto found = artifacts.find(std::string{artifact_id});
    require(report, found != artifacts.end(), ContractIssueCode::dangling_reference,
            path, "artifact reference does not name a declared artifact");
    if (found == artifacts.end()) {
        return nullptr;
    }
    ++uses[found->first];
    const auto *artifact = found->second;
    require(report, artifact->encoding == expected_encoding,
            ContractIssueCode::inconsistent_shape, path,
            "artifact encoding does not match its package role");
    if (expected_elements.has_value()) {
        require(report, artifact->element_count == *expected_elements,
                ContractIssueCode::inconsistent_shape, path,
                "artifact element count does not match its package role");
    }
    return artifact;
}

[[nodiscard]] std::unordered_set<std::string>
validated_id_set(ValidationReport &report, const std::vector<std::string> &ids,
                 const std::string &path, std::string_view noun) {
    std::unordered_set<std::string> result;
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        const auto item_path = indexed(path, index);
        require(report, is_valid_semantic_id(ids[index]),
                ContractIssueCode::invalid_value, item_path,
                std::string{noun} + " ID must be canonical");
        if (!result.insert(ids[index]).second) {
            report.add(ContractIssueCode::duplicate_identity, item_path,
                       std::string{noun} + " IDs must be unique");
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::size_t>
find_anchor(const std::vector<double> &anchors, const double value) {
    for (std::size_t index = 0U; index < anchors.size(); ++index) {
        if (detail::nearly_equal(anchors[index], value)) {
            return index;
        }
    }
    return std::nullopt;
}

void validate_transient_envelope(ValidationReport &report,
                                 const AudioAtlasTransientEnvelope &envelope,
                                 const std::string &path) {
    require(report, envelope.attack_frames > 0U,
            ContractIssueCode::invalid_value, path + ".attack_frames",
            "transient attack must contain at least one frame");
    require(report, envelope.release_frames > 0U,
            ContractIssueCode::invalid_value, path + ".release_frames",
            "transient release must contain at least one frame");
    require(report, detail::finite_positive(envelope.maximum_gain_linear),
            ContractIssueCode::invalid_value, path + ".maximum_gain_linear",
            "transient maximum gain must be finite and positive");
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
            "atlas delivery accepts Float32 little-endian PCM only");
    require(report, !manifest.audio.buses.empty(), ContractIssueCode::missing_value,
            "audio.buses", "atlas must declare at least one output bus");
    std::unordered_set<std::string> bus_ids;
    for (std::size_t index = 0U; index < manifest.audio.buses.size(); ++index) {
        const auto path = indexed("audio.buses", index) + ".id";
        const auto &id = manifest.audio.buses[index].id;
        require(report, is_valid_semantic_id(id), ContractIssueCode::invalid_value,
                path, "output-bus ID must be canonical");
        if (!bus_ids.insert(id).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "output-bus IDs must be unique");
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
            manifest.domain.load_coordinate ==
                AudioAtlasLoadCoordinate::
                    measured_intake_manifold_pressure_pa_abs,
            ContractIssueCode::unsupported_value, "domain.load_coordinate",
            "load must use measured absolute intake-manifold pressure");
    require(report,
            detail::finite_positive(manifest.domain.minimum_load_pa_abs),
            ContractIssueCode::invalid_value, "domain.minimum_load_pa_abs",
            "minimum absolute load pressure must be finite and positive");
    require(report,
            detail::finite_positive(manifest.domain.maximum_load_pa_abs) &&
                manifest.domain.minimum_load_pa_abs <
                    manifest.domain.maximum_load_pa_abs,
            ContractIssueCode::invalid_value, "domain.maximum_load_pa_abs",
            "maximum absolute load pressure must exceed the minimum");
    require(report,
            detail::finite_positive(manifest.domain.phase_cycle_revolutions),
            ContractIssueCode::invalid_value,
            "domain.phase_cycle_revolutions",
            "phase cycle length must be finite and positive");
    require(report, !manifest.domain.supported_state_masks.empty(),
            ContractIssueCode::missing_value,
            "domain.supported_state_masks",
            "domain must declare at least one supported engine state");
    std::unordered_set<std::uint32_t> supported_states;
    for (std::size_t index = 0U;
         index < manifest.domain.supported_state_masks.size(); ++index) {
        const auto state_mask = manifest.domain.supported_state_masks[index];
        const auto path = indexed("domain.supported_state_masks", index);
        require(report, (state_mask & ~kAudioAtlasKnownStateMask) == 0U,
                ContractIssueCode::unsupported_value, path,
                "supported state contains unknown bits");
        if (!supported_states.insert(state_mask).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "supported state masks must be unique");
        }
    }
    require(report,
            manifest.domain.out_of_domain_behavior ==
                AudioAtlasOutOfDomainBehavior::unavailable,
            ContractIssueCode::unsupported_value,
            "domain.out_of_domain_behavior",
            "out-of-domain package requests must fail as unavailable");

    ArtifactMap artifacts;
    ArtifactUseMap artifact_use_count;
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
        require(report,
                artifact.path.size() <= 4096U &&
                    valid_relative_path(artifact.path),
                ContractIssueCode::invalid_value, path + ".path",
                "artifact path must be a normalized safe relative path");
        require(report, artifact.element_count > 0U,
                ContractIssueCode::invalid_value, path + ".element_count",
                "artifact must contain at least one encoded element");
        std::uint64_t bytes_per_element = 0U;
        if (artifact.encoding == AudioAtlasArtifactEncoding::float32le) {
            bytes_per_element = 4U;
        } else if (artifact.encoding ==
                   AudioAtlasArtifactEncoding::complex_float64le) {
            bytes_per_element = 16U;
        } else {
            report.add(ContractIssueCode::unsupported_value, path + ".encoding",
                       "artifact encoding is unsupported");
        }
        std::uint64_t expected_bytes = 0U;
        const bool size_fits =
            checked_product(artifact.element_count, bytes_per_element,
                            expected_bytes);
        require(report,
                bytes_per_element != 0U && size_fits &&
                    artifact.byte_count == expected_bytes,
                ContractIssueCode::inconsistent_shape, path + ".byte_count",
                "artifact byte count does not match its encoding and element count");
        require(report, !artifact.sha256.is_zero(),
                ContractIssueCode::invalid_value, path + ".sha256",
                "artifact digest must be nonzero");
    }
    require(report, !manifest.artifacts.empty(), ContractIssueCode::missing_value,
            "artifacts", "audio atlas must contain payload artifacts");

    const auto &texture = manifest.phase_texture;
    require(report, is_power_of_two(texture.samples_per_cycle),
            ContractIssueCode::invalid_value,
            "phase_texture.samples_per_cycle",
            "phase texture samples_per_cycle must be a power of two for canonical phase alignment");
    require(report, texture.residual_cycle_count >= 2U,
            ContractIssueCode::invalid_value,
            "phase_texture.residual_cycle_count",
            "residual selector requires at least two cycles");
    require(report,
            texture.residual_taper.method ==
                AudioAtlasResidualTaperMethod::boundary_zero_smoothstep_v1,
            ContractIssueCode::unsupported_value,
            "phase_texture.residual_taper.method",
            "residual taper method is unsupported");
    require(report, texture.residual_taper.boundary_value == 0.0,
            ContractIssueCode::inconsistent_semantics,
            "phase_texture.residual_taper.boundary_value",
            "boundary-zero residual banks must declare an exact zero boundary");
    require(report,
            detail::finite_positive(texture.residual_taper.fraction_per_edge) &&
                texture.residual_taper.fraction_per_edge <= 0.5,
            ContractIssueCode::invalid_value,
            "phase_texture.residual_taper.fraction_per_edge",
            "residual taper fraction must lie in (0, 0.5]");
    require(report,
            texture.residual_taper.frames_per_edge > 0U &&
                static_cast<std::uint64_t>(
                    texture.residual_taper.frames_per_edge) *
                        2U <=
                    texture.samples_per_cycle,
            ContractIssueCode::invalid_value,
            "phase_texture.residual_taper.frames_per_edge",
            "residual taper edges must fit inside one phase cycle");
    require(report,
            detail::nearly_equal(
                texture.residual_taper.fraction_per_edge *
                    static_cast<double>(texture.samples_per_cycle),
                static_cast<double>(texture.residual_taper.frames_per_edge)),
            ContractIssueCode::inconsistent_semantics,
            "phase_texture.residual_taper",
            "residual taper frame and fractional metadata disagree");
    require(report,
            texture.selector.method ==
                AudioAtlasSelectorMethod::splitmix64_shuffled_bags_v1,
            ContractIssueCode::unsupported_value, "phase_texture.selector.method",
            "residual selector method is unsupported");
    require(report, texture.selector.no_adjacent_repeat,
            ContractIssueCode::inconsistent_semantics,
            "phase_texture.selector.no_adjacent_repeat",
            "residual selection must prohibit adjacent repeats");
    require(report,
            texture.selector.change_phase ==
                AudioAtlasSelectorChangePhase::phase_cycle_boundary,
            ContractIssueCode::unsupported_value,
            "phase_texture.selector.change_phase",
            "residual selection may change only at a phase-cycle boundary");
    require(report,
            texture.interpolation.mean.method ==
                AudioAtlasMeanInterpolationMethod::common_delay_phase_warp_v1,
            ContractIssueCode::unsupported_value,
            "phase_texture.interpolation.mean.method",
            "mean interpolation method is unsupported");
    require(report,
            texture.interpolation.mean.energy_target ==
                AudioAtlasMeanEnergyTarget::linear_anchor_rms,
            ContractIssueCode::unsupported_value,
            "phase_texture.interpolation.mean.energy_target",
            "mean energy target is unsupported");
    require(report,
            texture.interpolation.residual.cross_cell_correlation ==
                AudioAtlasResidualCorrelation::independent,
            ContractIssueCode::unsupported_value,
            "phase_texture.interpolation.residual.cross_cell_correlation",
            "residual cells must remain independently selected");
    require(report,
            texture.interpolation.residual.energy_target ==
                AudioAtlasResidualEnergyTarget::linear_anchor_power,
            ContractIssueCode::unsupported_value,
            "phase_texture.interpolation.residual.energy_target",
            "residual energy target is unsupported");
    require(report,
            texture.interpolation.residual.normalization ==
                AudioAtlasResidualNormalization::
                    sqrt_target_power_over_weighted_anchor_power,
            ContractIssueCode::unsupported_value,
            "phase_texture.interpolation.residual.normalization",
            "residual normalization is unsupported");

    require(report, texture.rpm_anchors.size() >= 2U,
            ContractIssueCode::missing_value, "phase_texture.rpm_anchors",
            "phase texture requires at least two RPM anchors");
    for (std::size_t index = 0U; index < texture.rpm_anchors.size(); ++index) {
        const auto path = indexed("phase_texture.rpm_anchors", index);
        require(report, detail::finite_positive(texture.rpm_anchors[index]),
                ContractIssueCode::invalid_value, path,
                "RPM anchor must be finite and positive");
        if (index != 0U) {
            require(report,
                    texture.rpm_anchors[index - 1U] <
                        texture.rpm_anchors[index],
                    ContractIssueCode::inconsistent_shape, path,
                    "RPM anchors must be strictly ascending");
        }
    }
    if (!texture.rpm_anchors.empty()) {
        require(report,
                detail::nearly_equal(texture.rpm_anchors.front(),
                                     manifest.domain.minimum_rpm) &&
                    detail::nearly_equal(texture.rpm_anchors.back(),
                                         manifest.domain.maximum_rpm),
                ContractIssueCode::inconsistent_shape,
                "phase_texture.rpm_anchors",
                "first and last RPM anchors must close the declared domain");
    }

    require(report, !texture.load_lanes.empty(), ContractIssueCode::missing_value,
            "phase_texture.load_lanes",
            "phase texture requires at least one authored load lane");
    std::unordered_map<std::string, std::size_t> lane_indices;
    for (std::size_t index = 0U; index < texture.load_lanes.size(); ++index) {
        const auto &lane = texture.load_lanes[index];
        const auto path = indexed("phase_texture.load_lanes", index);
        require(report, is_valid_semantic_id(lane.id),
                ContractIssueCode::invalid_value, path + ".id",
                "load-lane ID must be canonical");
        if (!lane_indices.emplace(lane.id, index).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "load-lane IDs must be unique");
        }
        require(report, detail::unit_interval(lane.requested_throttle_01),
                ContractIssueCode::invalid_value,
                path + ".requested_throttle_01",
                "load-lane throttle must lie in [0, 1]");
        validate_state(report, manifest.domain, lane.state_mask,
                       path + ".state_mask");
    }

    require(report, !texture.source_route_ids.empty(),
            ContractIssueCode::missing_value,
            "phase_texture.source_route_ids",
            "phase texture requires at least one dry source route");
    const auto source_route_ids =
        validated_id_set(report, texture.source_route_ids,
                         "phase_texture.source_route_ids", "source-route");

    std::uint64_t residual_elements = 0U;
    const bool residual_size_fits = checked_product(
        texture.samples_per_cycle, texture.residual_cycle_count,
        residual_elements);
    require(report, residual_size_fits, ContractIssueCode::inconsistent_shape,
            "phase_texture.residual_cycle_count",
            "residual-bank element count overflows uint64");

    std::uint64_t expected_cells = 0U;
    const bool cell_count_fits =
        checked_product(texture.rpm_anchors.size(), texture.load_lanes.size(),
                        expected_cells);
    require(report,
            cell_count_fits && texture.cells.size() == expected_cells,
            ContractIssueCode::inconsistent_shape, "phase_texture.cells",
            "phase cells must form the complete RPM-anchor by load-lane grid");
    std::unordered_set<std::string> phase_cell_ids;
    std::vector<const AudioAtlasPhaseCell *> grid;
    if (cell_count_fits &&
        expected_cells <= std::numeric_limits<std::size_t>::max()) {
        grid.resize(static_cast<std::size_t>(expected_cells), nullptr);
    }
    const AudioAtlasPhaseCell *reference_cell = nullptr;
    for (std::size_t index = 0U; index < texture.cells.size(); ++index) {
        const auto &cell = texture.cells[index];
        const auto path = indexed("phase_texture.cells", index);
        require(report, is_valid_semantic_id(cell.id),
                ContractIssueCode::invalid_value, path + ".id",
                "phase-cell ID must be canonical");
        if (!phase_cell_ids.insert(cell.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "phase-cell IDs must be unique");
        }
        if (cell.id == texture.reference_cell_id) {
            if (reference_cell != nullptr) {
                report.add(ContractIssueCode::duplicate_identity, path + ".id",
                           "phase reference cell ID must resolve once");
            }
            reference_cell = &cell;
        }
        const auto rpm_index = find_anchor(texture.rpm_anchors, cell.rpm);
        require(report, rpm_index.has_value(),
                ContractIssueCode::dangling_reference, path + ".rpm",
                "phase-cell RPM does not name an authored anchor");
        const auto lane_found = lane_indices.find(cell.load_lane_id);
        require(report, lane_found != lane_indices.end(),
                ContractIssueCode::dangling_reference,
                path + ".load_lane_id",
                "phase cell does not name an authored load lane");
        if (rpm_index.has_value() && lane_found != lane_indices.end() &&
            !grid.empty()) {
            const auto grid_index =
                *rpm_index * texture.load_lanes.size() + lane_found->second;
            if (grid[grid_index] != nullptr) {
                report.add(ContractIssueCode::duplicate_identity,
                           path + ".load_lane_id",
                           "phase grid contains the same RPM/lane cell twice");
            } else {
                grid[grid_index] = &cell;
            }
        }
        require(report,
                detail::finite_positive(cell.load_coordinate_pa_abs) &&
                    cell.load_coordinate_pa_abs >=
                        manifest.domain.minimum_load_pa_abs &&
                    cell.load_coordinate_pa_abs <=
                        manifest.domain.maximum_load_pa_abs,
                ContractIssueCode::invalid_value,
                path + ".load_coordinate_pa_abs",
                "phase-cell load lies outside the declared pressure domain");
        require(report, detail::unit_interval(cell.requested_throttle_01),
                ContractIssueCode::invalid_value,
                path + ".requested_throttle_01",
                "phase-cell throttle must lie in [0, 1]");
        validate_state(report, manifest.domain, cell.state_mask,
                       path + ".state_mask");
        if (lane_found != lane_indices.end()) {
            const auto &lane = texture.load_lanes[lane_found->second];
            require(report,
                    detail::nearly_equal(cell.requested_throttle_01,
                                         lane.requested_throttle_01) &&
                        cell.state_mask == lane.state_mask,
                    ContractIssueCode::inconsistent_semantics, path,
                    "phase cell throttle/state metadata disagrees with its load lane");
        }
        require(report, detail::finite(cell.shift_to_canonical_samples),
                ContractIssueCode::invalid_value,
                path + ".shift_to_canonical_samples",
                "phase-alignment shift must be finite");

        std::unordered_set<std::string> cell_routes;
        for (std::size_t route_index = 0U; route_index < cell.routes.size();
             ++route_index) {
            const auto &route = cell.routes[route_index];
            const auto route_path = indexed(path + ".routes", route_index);
            require(report, source_route_ids.contains(route.route_id),
                    ContractIssueCode::dangling_reference,
                    route_path + ".route_id",
                    "phase cell references an undeclared source route");
            if (!cell_routes.insert(route.route_id).second) {
                report.add(ContractIssueCode::duplicate_identity,
                           route_path + ".route_id",
                           "phase cell may bind each source route only once");
            }
            require(report, detail::finite_nonnegative(route.mean_rms),
                    ContractIssueCode::invalid_value,
                    route_path + ".mean_rms",
                    "phase-cell mean RMS must be finite and nonnegative");
            require(report, detail::finite_nonnegative(route.residual_power),
                    ContractIssueCode::invalid_value,
                    route_path + ".residual_power",
                    "phase-cell residual power must be finite and nonnegative");
            require(report,
                    route.mean_artifact_id != route.residual_artifact_id,
                    ContractIssueCode::inconsistent_semantics, route_path,
                    "mean and residual roles must use distinct artifacts");
            static_cast<void>(reference_artifact(
                report, artifacts, artifact_use_count, route.mean_artifact_id,
                route_path + ".mean_artifact_id",
                AudioAtlasArtifactEncoding::float32le,
                texture.samples_per_cycle));
            if (residual_size_fits) {
                static_cast<void>(reference_artifact(
                    report, artifacts, artifact_use_count,
                    route.residual_artifact_id,
                    route_path + ".residual_artifact_id",
                    AudioAtlasArtifactEncoding::float32le, residual_elements));
            }
        }
        require(report, cell_routes == source_route_ids,
                ContractIssueCode::inconsistent_shape, path + ".routes",
                "phase cell must bind every source route exactly once");
    }
    require(report, is_valid_semantic_id(texture.reference_cell_id),
            ContractIssueCode::invalid_value,
            "phase_texture.reference_cell_id",
            "phase reference cell ID must be canonical");
    require(report, reference_cell != nullptr,
            ContractIssueCode::dangling_reference,
            "phase_texture.reference_cell_id",
            "phase reference does not name a phase cell");
    if (reference_cell != nullptr) {
        require(report,
                reference_cell->shift_to_canonical_samples == 0.0,
                ContractIssueCode::inconsistent_semantics,
                "phase_texture.reference_cell_id",
                "canonical reference cell must declare an exact zero shift");
    }
    for (std::size_t rpm_index = 0U; rpm_index < texture.rpm_anchors.size();
         ++rpm_index) {
        double previous_load = -std::numeric_limits<double>::infinity();
        for (std::size_t lane_index = 0U;
             lane_index < texture.load_lanes.size(); ++lane_index) {
            const auto grid_index =
                rpm_index * texture.load_lanes.size() + lane_index;
            if (grid_index >= grid.size() || grid[grid_index] == nullptr) {
                continue;
            }
            require(report,
                    grid[grid_index]->load_coordinate_pa_abs > previous_load,
                    ContractIssueCode::inconsistent_shape,
                    "phase_texture.cells",
                    "load coordinates must rise in authored lane order at each RPM anchor");
            previous_load = grid[grid_index]->load_coordinate_pa_abs;
        }
    }

    const auto &policy = manifest.transient_policy;
    require(report,
            policy.detection_method ==
                AudioAtlasTransientDetectionMethod::causal_throttle_window_v1,
            ContractIssueCode::unsupported_value,
            "transient_policy.detection_method",
            "transient detector must use the admitted causal throttle window");
    require(report, policy.throttle_window_frames > 0U,
            ContractIssueCode::invalid_value,
            "transient_policy.throttle_window_frames",
            "transient detector window must contain at least one frame");
    require(report,
            policy.minimum_history_frames > 0U &&
                policy.minimum_history_frames <= policy.throttle_window_frames,
            ContractIssueCode::invalid_value,
            "transient_policy.minimum_history_frames",
            "minimum detector history must fit inside its causal window");
    require(report,
            detail::unit_interval(policy.throttle_delta_rearm_01) &&
                detail::unit_interval(policy.throttle_delta_onset_01) &&
                detail::unit_interval(policy.throttle_delta_full_01) &&
                policy.throttle_delta_rearm_01 <
                    policy.throttle_delta_onset_01 &&
                policy.throttle_delta_onset_01 < policy.throttle_delta_full_01,
            ContractIssueCode::invalid_value, "transient_policy.throttle_delta",
            "rearm, onset, and full throttle deltas must be strictly ascending in [0, 1]");
    require(report, policy.refractory_frames > 0U,
            ContractIssueCode::invalid_value,
            "transient_policy.refractory_frames",
            "transient refractory duration must be positive");
    require(report, policy.opposite_return_frames > 0U,
            ContractIssueCode::invalid_value,
            "transient_policy.opposite_return_frames",
            "opposite-direction return duration must be positive");
    validate_transient_envelope(report, policy.rising,
                                "transient_policy.rising");
    validate_transient_envelope(report, policy.falling,
                                "transient_policy.falling");

    require(report, manifest.transient_layers.size() == 2U,
            ContractIssueCode::inconsistent_shape, "transient_layers",
            "production atlas requires one rising and one falling transient layer");
    std::unordered_set<std::string> transient_layer_ids;
    bool saw_rising = false;
    bool saw_falling = false;
    for (std::size_t index = 0U; index < manifest.transient_layers.size();
         ++index) {
        const auto &layer = manifest.transient_layers[index];
        const auto path = indexed("transient_layers", index);
        require(report, is_valid_semantic_id(layer.id),
                ContractIssueCode::invalid_value, path + ".id",
                "transient-layer ID must be canonical");
        if (!transient_layer_ids.insert(layer.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "transient-layer IDs must be unique");
        }
        if (layer.direction == AudioAtlasTransientDirection::rising) {
            require(report, !saw_rising, ContractIssueCode::duplicate_identity,
                    path + ".direction",
                    "atlas may declare only one rising transient layer");
            saw_rising = true;
        } else if (layer.direction == AudioAtlasTransientDirection::falling) {
            require(report, !saw_falling, ContractIssueCode::duplicate_identity,
                    path + ".direction",
                    "atlas may declare only one falling transient layer");
            saw_falling = true;
        } else {
            report.add(ContractIssueCode::unsupported_value,
                       path + ".direction",
                       "transient direction is unsupported");
        }
        const auto layer_routes = validated_id_set(
            report, layer.source_route_ids, path + ".source_route_ids",
            "transient source-route");
        require(report, layer_routes == source_route_ids,
                ContractIssueCode::inconsistent_shape,
                path + ".source_route_ids",
                "transient layer must declare the phase texture's complete source-route set");
        require(report, !layer.cells.empty(), ContractIssueCode::missing_value,
                path + ".cells",
                "transient layer requires at least one operating cell");
        std::unordered_set<std::string> transient_cell_ids;
        for (std::size_t cell_index = 0U; cell_index < layer.cells.size();
             ++cell_index) {
            const auto &cell = layer.cells[cell_index];
            const auto cell_path = indexed(path + ".cells", cell_index);
            require(report, is_valid_semantic_id(cell.id),
                    ContractIssueCode::invalid_value, cell_path + ".id",
                    "transient-cell ID must be canonical");
            if (!transient_cell_ids.insert(cell.id).second) {
                report.add(ContractIssueCode::duplicate_identity,
                           cell_path + ".id",
                           "transient-cell IDs must be unique inside their layer");
            }
            require(report,
                    detail::finite_positive(cell.rpm) &&
                        cell.rpm >= manifest.domain.minimum_rpm &&
                        cell.rpm <= manifest.domain.maximum_rpm,
                    ContractIssueCode::invalid_value, cell_path + ".rpm",
                    "transient-cell RPM lies outside the declared domain");
            require(report,
                    detail::finite_positive(cell.load_coordinate_pa_abs) &&
                        cell.load_coordinate_pa_abs >=
                            manifest.domain.minimum_load_pa_abs &&
                        cell.load_coordinate_pa_abs <=
                            manifest.domain.maximum_load_pa_abs,
                    ContractIssueCode::invalid_value,
                    cell_path + ".load_coordinate_pa_abs",
                    "transient-cell load lies outside the declared pressure domain");
            require(report, detail::unit_interval(cell.requested_throttle_01),
                    ContractIssueCode::invalid_value,
                    cell_path + ".requested_throttle_01",
                    "transient-cell throttle must lie in [0, 1]");
            validate_state(report, manifest.domain, cell.state_mask,
                           cell_path + ".state_mask");
            require(report, cell.cycle_count > 0U,
                    ContractIssueCode::invalid_value,
                    cell_path + ".cycle_count",
                    "transient cell must contain at least one complete phase cycle");
            require(report,
                    cell.samples_per_cycle == texture.samples_per_cycle,
                    ContractIssueCode::inconsistent_shape,
                    cell_path + ".samples_per_cycle",
                    "transient and phase-texture cells must use one canonical phase grid");
            require(report,
                    detail::finite_nonnegative(cell.phase_origin_revolutions) &&
                        cell.phase_origin_revolutions <
                            manifest.domain.phase_cycle_revolutions,
                    ContractIssueCode::invalid_value,
                    cell_path + ".phase_origin_revolutions",
                    "transient phase origin must lie inside one phase cycle");
            require(report,
                    cell.cycle_count > 0U &&
                        cell.source_cycle_origin_ordinal_mod_cycle_count <
                            cell.cycle_count,
                    ContractIssueCode::invalid_value,
                    cell_path +
                        ".source_cycle_origin_ordinal_mod_cycle_count",
                    "transient source-cycle origin ordinal must be reduced modulo cycle_count");
            require(report,
                    cell.seam_closure_method ==
                        AudioAtlasTransientCell::SeamClosureMethod::
                            phase_aligned_boundary_smoothstep_v1,
                    ContractIssueCode::unsupported_value,
                    cell_path + ".seam_closure.method",
                    "transient seam-closure method is unsupported");
            require(report,
                    cell.seam_closure_frames_per_side > 0U &&
                        static_cast<std::uint64_t>(
                            cell.seam_closure_frames_per_side) *
                                2U <=
                            cell.samples_per_cycle,
                    ContractIssueCode::invalid_value,
                    cell_path + ".seam_closure.frames_per_side",
                    "transient seam-closure width must fit inside one phase cycle");
            std::uint64_t transient_elements = 0U;
            const bool transient_size_fits = checked_product(
                cell.cycle_count, cell.samples_per_cycle, transient_elements);
            require(report, transient_size_fits,
                    ContractIssueCode::inconsistent_shape,
                    cell_path + ".cycle_count",
                    "transient-cell element count overflows uint64");
            std::unordered_set<std::string> cell_routes;
            std::optional<std::uint64_t> cell_element_count;
            for (std::size_t route_index = 0U;
                 route_index < cell.routes.size(); ++route_index) {
                const auto &route = cell.routes[route_index];
                const auto route_path =
                    indexed(cell_path + ".routes", route_index);
                require(report, layer_routes.contains(route.route_id),
                        ContractIssueCode::dangling_reference,
                        route_path + ".route_id",
                        "transient cell references an undeclared source route");
                if (!cell_routes.insert(route.route_id).second) {
                    report.add(ContractIssueCode::duplicate_identity,
                               route_path + ".route_id",
                               "transient cell may bind each source route only once");
                }
                const auto *artifact = reference_artifact(
                    report, artifacts, artifact_use_count, route.artifact_id,
                    route_path + ".artifact_id",
                    AudioAtlasArtifactEncoding::float32le,
                    transient_size_fits
                        ? std::optional<std::uint64_t>{transient_elements}
                        : std::nullopt);
                if (artifact != nullptr) {
                    if (!cell_element_count.has_value()) {
                        cell_element_count = artifact->element_count;
                    } else {
                        require(report,
                                artifact->element_count == *cell_element_count,
                                ContractIssueCode::inconsistent_shape,
                                route_path + ".artifact_id",
                                "all source routes in a transient cell must have equal duration");
                    }
                }
            }
            require(report, cell_routes == layer_routes,
                    ContractIssueCode::inconsistent_shape,
                    cell_path + ".routes",
                    "transient cell must bind every layer source route exactly once");
        }
    }
    require(report, saw_rising && saw_falling,
            ContractIssueCode::inconsistent_shape, "transient_layers",
            "production atlas requires rising and falling transient layers");

    std::unordered_set<std::string> lifecycle_ids;
    std::unordered_set<std::uint8_t> lifecycle_events;
    for (std::size_t index = 0U;
         index < manifest.lifecycle_performances.size(); ++index) {
        const auto &performance = manifest.lifecycle_performances[index];
        const auto path = indexed("lifecycle_performances", index);
        require(report, is_valid_semantic_id(performance.id),
                ContractIssueCode::invalid_value, path + ".id",
                "lifecycle-performance ID must be canonical");
        if (!lifecycle_ids.insert(performance.id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".id",
                       "lifecycle-performance IDs must be unique");
        }
        const auto event_value = static_cast<std::uint8_t>(performance.event);
        const bool event_supported =
            performance.event == AudioAtlasLifecycleEvent::startup ||
            performance.event == AudioAtlasLifecycleEvent::shutdown ||
            performance.event == AudioAtlasLifecycleEvent::limiter;
        require(report, event_supported, ContractIssueCode::unsupported_value,
                path + ".event", "lifecycle event is unsupported");
        if (event_supported && !lifecycle_events.insert(event_value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".event",
                       "atlas may declare each lifecycle event only once");
        }
        validate_state(report, manifest.domain, performance.entry_state_mask,
                       path + ".entry_state_mask");
        validate_state(report, manifest.domain, performance.exit_state_mask,
                       path + ".exit_state_mask");
        std::unordered_set<std::string> performance_buses;
        std::optional<std::uint64_t> performance_elements;
        for (std::size_t ref_index = 0U;
             ref_index < performance.artifacts.size(); ++ref_index) {
            const auto &reference = performance.artifacts[ref_index];
            const auto ref_path = indexed(path + ".artifacts", ref_index);
            require(report, bus_ids.contains(reference.bus_id),
                    ContractIssueCode::dangling_reference,
                    ref_path + ".bus_id",
                    "lifecycle artifact references an undeclared output bus");
            if (!performance_buses.insert(reference.bus_id).second) {
                report.add(ContractIssueCode::duplicate_identity,
                           ref_path + ".bus_id",
                           "lifecycle performance may bind each output bus only once");
            }
            const auto *artifact = reference_artifact(
                report, artifacts, artifact_use_count, reference.artifact_id,
                ref_path + ".artifact_id",
                AudioAtlasArtifactEncoding::float32le);
            if (artifact != nullptr) {
                if (!performance_elements.has_value()) {
                    performance_elements = artifact->element_count;
                } else {
                    require(report,
                            artifact->element_count == *performance_elements,
                            ContractIssueCode::inconsistent_shape,
                            ref_path + ".artifact_id",
                            "lifecycle output-bus artifacts must have equal duration");
                }
            }
        }
        require(report, performance_buses == bus_ids,
                ContractIssueCode::inconsistent_shape, path + ".artifacts",
                "lifecycle performance must bind every output bus exactly once");
        require(report, performance.timeline.size() >= 2U,
                ContractIssueCode::missing_value, path + ".timeline",
                "lifecycle timeline requires a start and exclusive end sentinel");
        for (std::size_t knot_index = 0U;
             knot_index < performance.timeline.size(); ++knot_index) {
            const auto &knot = performance.timeline[knot_index];
            const auto knot_path = indexed(path + ".timeline", knot_index);
            if (knot_index == 0U) {
                require(report, knot.frame == 0U,
                        ContractIssueCode::inconsistent_shape,
                        knot_path + ".frame",
                        "lifecycle timeline must begin at frame zero");
                require(report,
                        knot.state_mask == performance.entry_state_mask,
                        ContractIssueCode::inconsistent_semantics,
                        knot_path + ".state_mask",
                        "first lifecycle state must match entry_state_mask");
            } else {
                require(report,
                        performance.timeline[knot_index - 1U].frame < knot.frame,
                        ContractIssueCode::inconsistent_shape,
                        knot_path + ".frame",
                        "lifecycle timeline frames must be strictly increasing");
            }
            require(report, detail::finite_nonnegative(knot.rpm),
                    ContractIssueCode::invalid_value, knot_path + ".rpm",
                    "lifecycle RPM must be finite and nonnegative");
            require(report,
                    detail::finite_positive(knot.load_coordinate_pa_abs),
                    ContractIssueCode::invalid_value,
                    knot_path + ".load_coordinate_pa_abs",
                    "lifecycle absolute manifold pressure must be finite and positive");
            require(report, detail::unit_interval(knot.requested_throttle_01),
                    ContractIssueCode::invalid_value,
                    knot_path + ".requested_throttle_01",
                    "lifecycle throttle must lie in [0, 1]");
            validate_state(report, manifest.domain, knot.state_mask,
                           knot_path + ".state_mask");
        }
        if (!performance.timeline.empty()) {
            const auto &last = performance.timeline.back();
            require(report, last.state_mask == performance.exit_state_mask,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".timeline",
                    "final lifecycle state must match exit_state_mask");
            if (performance_elements.has_value()) {
                require(report, last.frame == *performance_elements,
                        ContractIssueCode::inconsistent_shape,
                        path + ".timeline",
                        "lifecycle end sentinel must equal artifact element count");
            }
        }
    }

    validate_identity(report, manifest.presentation.method_identity,
                      "presentation.method_identity");
    validate_identity(report, manifest.presentation.build_identity,
                      "presentation.build_identity");
    require(report,
            manifest.presentation.build_identity ==
                manifest.provenance.renderer_build,
            ContractIssueCode::inconsistent_semantics,
            "presentation.build_identity",
            "presentation build must match renderer provenance exactly");
    require(report, manifest.presentation.batch_frames > 0U,
            ContractIssueCode::invalid_value, "presentation.batch_frames",
            "presentation batch must contain at least one frame");
    require(report,
            detail::finite_positive(
                manifest.presentation.captured_to_source_scale),
            ContractIssueCode::invalid_value,
            "presentation.captured_to_source_scale",
            "captured-to-source calibration scale must be finite and positive");
    require(report, !manifest.presentation.source_routes.empty(),
            ContractIssueCode::missing_value,
            "presentation.source_routes",
            "presentation must map every dry source route");
    std::unordered_set<std::string> presented_routes;
    std::unordered_set<std::string> covered_buses;
    for (std::size_t index = 0U;
         index < manifest.presentation.source_routes.size(); ++index) {
        const auto &route = manifest.presentation.source_routes[index];
        const auto path = indexed("presentation.source_routes", index);
        require(report, source_route_ids.contains(route.route_id),
                ContractIssueCode::dangling_reference, path + ".route_id",
                "presentation mapping references an undeclared source route");
        if (!presented_routes.insert(route.route_id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".route_id",
                       "presentation may map each source route only once");
        }
        require(report, bus_ids.contains(route.output_bus_id),
                ContractIssueCode::dangling_reference,
                path + ".output_bus_id",
                "presentation mapping references an undeclared output bus");
        covered_buses.insert(route.output_bus_id);
        require(report, detail::unit_interval(route.wet_mix_01),
                ContractIssueCode::invalid_value, path + ".wet_mix_01",
                "route wet mix must lie in [0, 1]");
        if (route.transfer.kind == AudioAtlasTransferKind::direct) {
            require(report,
                    route.transfer.fft_size == 0U &&
                        route.transfer.coefficient_count == 0U &&
                        route.transfer.spectrum_artifact_id.empty(),
                    ContractIssueCode::inconsistent_shape,
                    path + ".transfer",
                    "direct transfer must not carry convolution fields");
        } else if (route.transfer.kind ==
                   AudioAtlasTransferKind::fixed_spectrum_convolution) {
            require(report, is_power_of_two(route.transfer.fft_size),
                    ContractIssueCode::invalid_value,
                    path + ".transfer.fft_size",
                    "convolution FFT size must be a power of two");
            require(report,
                    route.transfer.coefficient_count > 0U &&
                        route.transfer.coefficient_count <=
                            route.transfer.fft_size,
                    ContractIssueCode::invalid_value,
                    path + ".transfer.coefficient_count",
                    "convolution coefficient count must fit inside the FFT");
            if (route.transfer.coefficient_count <= route.transfer.fft_size) {
                const auto maximum_batch =
                    route.transfer.fft_size -
                    route.transfer.coefficient_count + 1U;
                require(report,
                        manifest.presentation.batch_frames <= maximum_batch,
                        ContractIssueCode::inconsistent_shape,
                        "presentation.batch_frames",
                        "presentation batch exceeds the transfer's valid overlap-save output");
            }
            static_cast<void>(reference_artifact(
                report, artifacts, artifact_use_count,
                route.transfer.spectrum_artifact_id,
                path + ".transfer.spectrum_artifact_id",
                AudioAtlasArtifactEncoding::complex_float64le,
                route.transfer.fft_size));
        } else {
            report.add(ContractIssueCode::unsupported_value,
                       path + ".transfer.kind",
                       "presentation transfer kind is unsupported");
        }
    }
    require(report, presented_routes == source_route_ids,
            ContractIssueCode::inconsistent_shape,
            "presentation.source_routes",
            "presentation must map every source route exactly once");
    require(report, covered_buses == bus_ids,
            ContractIssueCode::inconsistent_shape,
            "presentation.source_routes",
            "presentation must feed every declared output bus");
    require(report,
            manifest.presentation.master.method ==
                AudioAtlasMasterMethod::canonical_adaptive_v1,
            ContractIssueCode::unsupported_value,
            "presentation.master.method",
            "master presentation method is unsupported");
    require(report,
            detail::finite_nonnegative(
                manifest.presentation.master.volume_linear),
            ContractIssueCode::invalid_value,
            "presentation.master.volume_linear",
            "master volume must be finite and nonnegative");

    for (const auto &[id, artifact] : artifacts) {
        static_cast<void>(artifact);
        require(report, artifact_use_count[id] > 0U,
                ContractIssueCode::inconsistent_semantics, "artifacts",
                "every package artifact must be referenced by the manifest");
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
            ContractIssueCode::invalid_value,
            "provenance.source_inputs.sha256",
            "source-input bundle digest must be nonzero");
    return report;
}

} // namespace engine_sim_offline::contract
