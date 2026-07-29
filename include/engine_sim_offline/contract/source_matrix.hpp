#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/engine.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::contract {

enum class ArtifactKind : std::uint8_t {
    unspecified,
    audio,
    telemetry,
    routing_report,
    validation_report,
};

struct AudioContract {
    RationalRateHz sample_rate;
    std::uint64_t frame_count = 0;
    std::string channel_layout_id;
    std::string sample_encoding_id;

    friend bool operator==(const AudioContract &, const AudioContract &) = default;
};

struct ArtifactRequirement {
    std::string role;
    ArtifactKind kind = ArtifactKind::unspecified;
    std::optional<AudioContract> audio;
    bool diagnostic = false;

    friend bool operator==(const ArtifactRequirement &,
                           const ArtifactRequirement &) = default;
};

enum class DistributionIntent : std::uint8_t {
    unspecified,
    local_evaluation,
    distributable,
};

enum class OutputBusKind : std::uint8_t {
    unspecified,
    master_engine_raw,
    master_engine_audition,
};

enum class RouteDisposition : std::uint8_t {
    unspecified,
    rendered,
    not_applicable,
};

enum class OmissionKind : std::uint8_t {
    unspecified,
    source_route,
    external_system,
    presentation_scene,
    scenario_behavior,
};

struct SourceRouteRequirement {
    std::string semantic_id;
    SourceRouteKind kind = SourceRouteKind::unspecified;
    RouteDisposition disposition = RouteDisposition::unspecified;
    std::string disposition_reason;
    std::vector<std::string> artifact_roles;

    friend bool operator==(const SourceRouteRequirement &,
                           const SourceRouteRequirement &) = default;
};

struct OutputBusRequirement {
    std::string semantic_id;
    OutputBusKind kind = OutputBusKind::unspecified;
    std::vector<std::string> artifact_roles;

    friend bool operator==(const OutputBusRequirement &,
                           const OutputBusRequirement &) = default;
};

struct DeclaredOmission {
    std::string semantic_id;
    OmissionKind kind = OmissionKind::unspecified;
    std::string rationale;

    friend bool operator==(const DeclaredOmission &,
                           const DeclaredOmission &) = default;
};

// A source matrix is an independently selected policy object. Renderers may resolve
// paths and payload hashes, but they cannot silently remove required sources or buses.
struct SourceMatrixContract {
    std::string id;
    Sha256Digest sha256;
    DistributionIntent distribution = DistributionIntent::unspecified;
    std::vector<SourceRouteRequirement> required_source_routes;
    std::vector<OutputBusRequirement> required_output_buses;
    std::vector<ArtifactRequirement> required_artifacts;
    std::vector<DeclaredOmission> declared_omissions;

    friend bool operator==(const SourceMatrixContract &,
                           const SourceMatrixContract &) = default;
};

[[nodiscard]] ValidationReport validate(const SourceMatrixContract &source_matrix);
[[nodiscard]] ValidationReport
validate_evidence_rights(const ProvenanceLedger &provenance,
                         DistributionIntent distribution);

// The immutable policy counterpart of
// docs/contracts/M5_BMW_EXHAUST_ACOUSTIC_SOURCE_MATRIX.md.
[[nodiscard]] const SourceMatrixContract &bmw_m52b28_exhaust_acoustic_source_matrix();

} // namespace engine_sim_offline::contract
