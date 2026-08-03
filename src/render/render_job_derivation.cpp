#include "render/render_job_derivation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::render_detail {
namespace {

constexpr std::size_t kMaximumPortableRelativePathBytes = 240;
constexpr std::size_t kArtifactsPerRoute = 3;
constexpr std::size_t kMasterArtifactCount = 2;

[[nodiscard]] std::optional<std::size_t>
presentation_artifact_count(std::size_t route_count) noexcept {
    if (route_count > (std::numeric_limits<std::size_t>::max() - kMasterArtifactCount) /
                          kArtifactsPerRoute) {
        return std::nullopt;
    }
    return route_count * kArtifactsPerRoute + kMasterArtifactCount;
}

[[nodiscard]] RenderJobDerivationError error(RenderJobDerivationErrorCode code,
                                             std::string path, std::string message) {
    return {code, std::move(path), std::move(message)};
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.resize(digest.bytes.size() * 2);
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] bool valid_metadata_field(std::string_view value) noexcept {
    return !value.empty() &&
           value.size() <= artifacts::kMaximumAuditionMetadataFieldBytes &&
           value.find('\0') == std::string_view::npos;
}

[[nodiscard]] const contract::ArtifactRequirement *
find_artifact(const contract::OutputContract &output, std::string_view role) {
    const auto found = std::ranges::find(output.required_artifacts, role,
                                         &contract::ArtifactRequirement::role);
    return found == output.required_artifacts.end() ? nullptr : &*found;
}

using PendingArtifactResult = std::variant<PendingArtifact, RenderJobDerivationError>;

[[nodiscard]] PendingArtifactResult
project_artifact(const contract::OutputContract &output, std::string_view role,
                 std::string path) {
    const auto *required = find_artifact(output, role);
    if (required == nullptr || required->kind != contract::ArtifactKind::audio ||
        !required->audio.has_value()) {
        return error(RenderJobDerivationErrorCode::artifact_projection_failed,
                     std::move(path),
                     "the presentation renderer requires one owned audio artifact");
    }
    auto relative_path = derive_audio_artifact_path(role);
    if (auto *path_error = std::get_if<RenderJobDerivationError>(&relative_path)) {
        path_error->path = std::move(path);
        return std::move(*path_error);
    }
    return PendingArtifact{
        required->role,
        required->kind,
        std::get<std::string>(std::move(relative_path)),
        required->audio,
        required->diagnostic,
    };
}

[[nodiscard]] bool is_raw_bus(contract::OutputBusKind kind) noexcept {
    return kind == contract::OutputBusKind::master_engine_raw;
}

[[nodiscard]] bool is_audition_bus(contract::OutputBusKind kind) noexcept {
    return kind == contract::OutputBusKind::master_engine_audition;
}

} // namespace

AudioArtifactPathResult derive_audio_artifact_path(std::string_view role) {
    if (!contract::is_valid_semantic_id(role) ||
        role.find('%') != std::string_view::npos) {
        return error(RenderJobDerivationErrorCode::invalid_artifact_role,
                     "artifact.role",
                     "audio artifact path projection requires a canonical role "
                     "without percent bytes");
    }

    std::string result{"audio/"};
    result.reserve(result.size() + role.size() + 4);
    for (const char byte : role) {
        if (byte == '/') {
            result += "%2f";
        } else {
            result.push_back(byte);
        }
    }
    result += ".wav";
    if (result.size() > kMaximumPortableRelativePathBytes) {
        return error(RenderJobDerivationErrorCode::artifact_path_too_long,
                     "artifact.relative_path",
                     "derived audio artifact path exceeds the portable publication "
                     "bound");
    }
    return result;
}

AuditionMetadataResult
derive_audition_metadata(const contract::ResolvedRenderInputs &inputs,
                         const contract::SourceMatrixContract &source_matrix) {
    const auto &engine = inputs.engine;
    const auto &presentation = inputs.presentation;
    const auto &scenario = inputs.scenario;
    const auto &method = presentation.methods.audition_mix.value;

    artifacts::AuditionWaveMetadata metadata;
    metadata.comment = "engine=" + engine.engine_id.value +
                       ";profile=" + engine.profile_id.value +
                       ";scenario=" + scenario.scenario_id +
                       ";presentation=" + presentation.calibration_id +
                       ";source_matrix=" + source_matrix.id;
    metadata.title =
        "engine=" + engine.engine_id.value + ";scenario=" + scenario.scenario_id;
    metadata.software =
        "engine-sim-offline;method=" + method.id +
        ";version=" + std::to_string(method.version) +
        ";configuration_sha256=" + digest_hex(method.configuration_sha256);

    if (!valid_metadata_field(metadata.comment) ||
        !valid_metadata_field(metadata.title) ||
        !valid_metadata_field(metadata.software)) {
        return error(RenderJobDerivationErrorCode::audition_metadata_invalid,
                     "presentation.audition.metadata",
                     "derived audition INFO fields must be nonempty, NUL-free, and "
                     "at most 4096 bytes");
    }
    return metadata;
}

RenderJobProjectionResult derive_render_job_projection(
    const contract::RenderRequestRecord &request,
    const presentation::AdmittedPresentationCalibration &calibration) {
    RenderJobProjection projection;
    projection.output_contract =
        contract::resolve_output_contract(request.source_matrix);
    const auto artifact_count = presentation_artifact_count(calibration.route_count());

    if (request.source_matrix.required_source_routes.size() !=
            calibration.route_count() ||
        !artifact_count.has_value() ||
        projection.output_contract.required_artifacts.size() != *artifact_count) {
        return error(RenderJobDerivationErrorCode::route_projection_failed,
                     "source_matrix",
                     "the admitted presentation job requires three artifacts per "
                     "published gas-source route and two master artifacts");
    }

    projection.routes.reserve(calibration.route_count());
    projection.route_artifacts.resize(calibration.route_count());
    std::unordered_set<std::string> projected_roles;
    for (std::size_t route_index = 0; route_index < calibration.route_count();
         ++route_index) {
        const auto route_id = calibration.routes()[route_index].route_id();
        const auto engine_route = std::ranges::find(
            request.resolved_inputs.engine.routes, route_id, &contract::RouteSpec::id);
        if (engine_route == request.resolved_inputs.engine.routes.end()) {
            return error(RenderJobDerivationErrorCode::route_projection_failed,
                         "engine.routes",
                         "admitted presentation route is absent from the owned "
                         "engine request");
        }
        const auto required =
            std::ranges::find(request.source_matrix.required_source_routes,
                              engine_route->semantic_id.value,
                              &contract::SourceRouteRequirement::semantic_id);
        const bool gas_route =
            required != request.source_matrix.required_source_routes.end() &&
            (required->kind == contract::SourceRouteKind::exhaust_outlet ||
             required->kind == contract::SourceRouteKind::intake_inlet);
        const bool expected_disposition =
            required != request.source_matrix.required_source_routes.end() &&
            ((required->kind == contract::SourceRouteKind::exhaust_outlet &&
              required->disposition == contract::RouteDisposition::rendered) ||
             (required->kind == contract::SourceRouteKind::intake_inlet &&
              required->disposition == contract::RouteDisposition::declared_silent));
        if (!gas_route || !expected_disposition ||
            required->artifact_roles.size() != 3) {
            return error(RenderJobDerivationErrorCode::route_projection_failed,
                         "source_matrix.required_source_routes",
                         "each admitted gas-source route requires positional "
                         "dry/configured-transfer/selected artifact roles");
        }

        auto &route_artifacts = projection.route_artifacts[route_index];
        std::array<PendingArtifact *, 3> destinations{
            &route_artifacts.dry,
            &route_artifacts.configured_transfer,
            &route_artifacts.selected,
        };
        for (std::size_t artifact_index = 0; artifact_index < destinations.size();
             ++artifact_index) {
            const auto &role = required->artifact_roles[artifact_index];
            if (!projected_roles.insert(role).second) {
                return error(RenderJobDerivationErrorCode::artifact_projection_failed,
                             "source_matrix.required_source_routes.artifact_roles",
                             "one audio artifact role was projected more than once");
            }
            auto pending =
                project_artifact(projection.output_contract, role,
                                 "source_matrix.required_source_routes.artifact_roles");
            if (auto *projection_error =
                    std::get_if<RenderJobDerivationError>(&pending)) {
                return std::move(*projection_error);
            }
            *destinations[artifact_index] =
                std::get<PendingArtifact>(std::move(pending));
        }

        projection.routes.push_back({
            route_id,
            required->semantic_id,
            required->kind,
            required->disposition,
            required->disposition_reason,
            required->artifact_roles,
        });
    }

    const contract::OutputBusRequirement *raw_bus = nullptr;
    const contract::OutputBusRequirement *audition_bus = nullptr;
    projection.output_buses.reserve(request.source_matrix.required_output_buses.size());
    for (const auto &bus : request.source_matrix.required_output_buses) {
        projection.output_buses.push_back(
            {bus.semantic_id, bus.kind, bus.artifact_roles});
        if (is_raw_bus(bus.kind)) {
            if (raw_bus != nullptr) {
                return error(RenderJobDerivationErrorCode::output_bus_projection_failed,
                             "source_matrix.required_output_buses",
                             "presentation job has more than one raw master bus");
            }
            raw_bus = &bus;
        } else if (is_audition_bus(bus.kind)) {
            if (audition_bus != nullptr) {
                return error(RenderJobDerivationErrorCode::output_bus_projection_failed,
                             "source_matrix.required_output_buses",
                             "presentation job has more than one audition master bus");
            }
            audition_bus = &bus;
        }
    }
    if (projection.output_buses.size() != 2 || raw_bus == nullptr ||
        audition_bus == nullptr || raw_bus->artifact_roles.size() != 1 ||
        audition_bus->artifact_roles.size() != 1) {
        return error(RenderJobDerivationErrorCode::output_bus_projection_failed,
                     "source_matrix.required_output_buses",
                     "presentation job requires exactly one raw and one audition "
                     "master artifact");
    }

    auto raw =
        project_artifact(projection.output_contract, raw_bus->artifact_roles.front(),
                         "source_matrix.required_output_buses.raw");
    if (auto *projection_error = std::get_if<RenderJobDerivationError>(&raw)) {
        return std::move(*projection_error);
    }
    projection.raw_master_artifact = std::get<PendingArtifact>(std::move(raw));
    if (!projected_roles.insert(projection.raw_master_artifact.role).second) {
        return error(RenderJobDerivationErrorCode::artifact_projection_failed,
                     "source_matrix.required_output_buses.raw",
                     "raw master artifact role aliases a route artifact");
    }

    auto audition = project_artifact(projection.output_contract,
                                     audition_bus->artifact_roles.front(),
                                     "source_matrix.required_output_buses.audition");
    if (auto *projection_error = std::get_if<RenderJobDerivationError>(&audition)) {
        return std::move(*projection_error);
    }
    projection.audition_master_artifact =
        std::get<PendingArtifact>(std::move(audition));
    if (!projected_roles.insert(projection.audition_master_artifact.role).second ||
        projected_roles.size() != *artifact_count) {
        return error(RenderJobDerivationErrorCode::artifact_projection_failed,
                     "source_matrix.required_artifacts",
                     "projected presentation artifact roles are not the exact "
                     "required set");
    }

    auto metadata =
        derive_audition_metadata(request.resolved_inputs, request.source_matrix);
    if (auto *metadata_error = std::get_if<RenderJobDerivationError>(&metadata)) {
        return std::move(*metadata_error);
    }
    projection.audition_metadata =
        std::get<artifacts::AuditionWaveMetadata>(std::move(metadata));
    return projection;
}

} // namespace engine_sim_offline::render_detail
