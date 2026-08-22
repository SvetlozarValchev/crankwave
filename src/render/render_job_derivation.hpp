#pragma once

#include "artifacts/audition_wav_encoder.hpp"
#include "crankwave/contract/result.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "render/native_presentation_publisher.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace crankwave::render_detail {

enum class RenderJobDerivationErrorCode : std::uint8_t {
    invalid_artifact_role,
    artifact_path_too_long,
    route_projection_failed,
    output_bus_projection_failed,
    artifact_projection_failed,
    audition_metadata_invalid,
};

struct RenderJobDerivationError {
    RenderJobDerivationErrorCode code =
        RenderJobDerivationErrorCode::artifact_projection_failed;
    std::string path;
    std::string message;

    friend bool operator==(const RenderJobDerivationError &,
                           const RenderJobDerivationError &) = default;
};

using AudioArtifactPathResult = std::variant<std::string, RenderJobDerivationError>;
using AuditionMetadataResult =
    std::variant<artifacts::AuditionWaveMetadata, RenderJobDerivationError>;

struct RenderJobProjection {
    contract::OutputContract output_contract;
    PendingArtifact telemetry_artifact;
    std::vector<NativePresentationRouteArtifacts> route_artifacts;
    PendingArtifact raw_master_artifact;
    PendingArtifact audition_master_artifact;
    artifacts::AuditionWaveMetadata audition_metadata;
    std::vector<contract::RouteRecord> routes;
    std::vector<contract::OutputBusRecord> output_buses;
};

using RenderJobProjectionResult =
    std::variant<RenderJobProjection, RenderJobDerivationError>;

[[nodiscard]] AudioArtifactPathResult derive_audio_artifact_path(std::string_view role);

[[nodiscard]] AuditionMetadataResult
derive_audition_metadata(const contract::ResolvedRenderInputs &inputs,
                         const contract::SourceMatrixContract &source_matrix);

[[nodiscard]] RenderJobProjectionResult derive_render_job_projection(
    const contract::RenderRequestRecord &request,
    const presentation::AdmittedPresentationCalibration &calibration);

} // namespace crankwave::render_detail
