#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::contract {

struct BuildIdentity {
    std::string git_commit_id;
    Sha256Digest source_closure_sha256;
    std::string compiler_id;
    std::string compiler_version;
    std::string target_triple;
    std::string standard_library_id;
    std::string standard_library_identity;
    std::string math_library_id;
    std::string math_library_identity;
    std::string compiler_runtime_id;
    std::string compiler_runtime_identity;

    friend bool operator==(const BuildIdentity &, const BuildIdentity &) = default;
};

struct FloatingPointIdentity {
    std::string format;
    std::string rounding;
    bool fma_contraction = false;
    bool flush_to_zero = false;
    bool denormals_are_zero = false;

    friend bool operator==(const FloatingPointIdentity &,
                           const FloatingPointIdentity &) = default;
};

struct DeterminismEnvelope {
    BuildIdentity build;
    std::string numeric_policy_id;
    std::string instruction_set_profile;
    FloatingPointIdentity floating_point;
    std::uint32_t deterministic_worker_count = 0;
    std::string deterministic_reduction_topology;

    friend bool operator==(const DeterminismEnvelope &,
                           const DeterminismEnvelope &) = default;
};

enum class RandomComponentKind : std::uint8_t {
    unspecified,
    combustion,
    presentation_jitter,
    presentation_air_noise,
    starter,
};

struct ComponentSeed {
    RandomComponentKind kind = RandomComponentKind::unspecified;
    std::optional<CylinderId> cylinder_id;
    std::optional<RouteId> route_id;
    std::uint64_t initial_state = 0;
    std::uint64_t stream = 0;

    friend bool operator==(const ComponentSeed &, const ComponentSeed &) = default;
};

struct RandomPlan {
    MethodIdentity generator;
    std::uint64_t public_seed = 0;
    MethodIdentity derivation;
    std::vector<ComponentSeed> component_seeds;

    friend bool operator==(const RandomPlan &, const RandomPlan &) = default;
};

struct OutputContract {
    std::string source_matrix_id;
    Sha256Digest source_matrix_sha256;
    DistributionIntent distribution = DistributionIntent::unspecified;
    std::vector<SourceRouteRequirement> required_source_routes;
    std::vector<OutputBusRequirement> required_output_buses;
    std::vector<ArtifactRequirement> required_artifacts;
    std::vector<DeclaredOmission> declared_omissions;

    friend bool operator==(const OutputContract &, const OutputContract &) = default;
};

struct ArtifactRecord {
    std::string role;
    ArtifactKind kind = ArtifactKind::unspecified;
    std::string relative_path;
    std::optional<AudioContract> audio;
    std::uint64_t byte_count = 0;
    Sha256Digest payload_sha256;
    bool diagnostic = false;

    friend bool operator==(const ArtifactRecord &, const ArtifactRecord &) = default;
};

struct RouteRecord {
    RouteId route_id;
    std::string semantic_id;
    SourceRouteKind kind = SourceRouteKind::unspecified;
    RouteDisposition disposition = RouteDisposition::rendered;
    std::string disposition_reason;
    std::vector<std::string> artifact_roles;

    friend bool operator==(const RouteRecord &, const RouteRecord &) = default;
};

struct OutputBusRecord {
    std::string semantic_id;
    OutputBusKind kind = OutputBusKind::unspecified;
    std::vector<std::string> artifact_roles;

    friend bool operator==(const OutputBusRecord &, const OutputBusRecord &) = default;
};

struct ResolvedRenderInputs {
    EngineSpec engine;
    PresentationCalibration presentation;
    RenderScenario scenario;

    friend bool operator==(const ResolvedRenderInputs &,
                           const ResolvedRenderInputs &) = default;
};

struct SimulationManifestInputs {
    ResolvedRenderInputs resolved;

    friend bool operator==(const SimulationManifestInputs &,
                           const SimulationManifestInputs &) = default;
};

struct RenderManifestContent {
    std::uint32_t schema_version = 0;
    SimulationManifestInputs inputs;
    ProvenanceBundleRef provenance;
    DeterminismEnvelope determinism;
    RenderRates rates;
    RandomPlan randomness;
    OutputContract output_contract;
    std::vector<RouteRecord> routes;
    std::vector<OutputBusRecord> output_buses;
    std::vector<ArtifactRecord> artifacts;

    friend bool operator==(const RenderManifestContent &,
                           const RenderManifestContent &) = default;
};

struct ExecutionFacts {
    std::string run_id;
    std::string started_utc;
    std::chrono::nanoseconds wall_elapsed{};
    std::string host_os;
    std::string cpu_model;
    std::uint32_t logical_cpu_count = 0;
    std::uint32_t observed_process_threads = 0;
    std::uint32_t concurrent_render_jobs = 0;
    std::optional<std::uint64_t> peak_resident_bytes;

    friend bool operator==(const ExecutionFacts &, const ExecutionFacts &) = default;
};

struct RenderManifest {
    RenderManifestContent content;
    std::optional<ExecutionFacts> execution;
};

[[nodiscard]] OutputContract
resolve_output_contract(const SourceMatrixContract &source_matrix);

// Validates the complete resolved request before a renderer or sink is admitted.
// This includes cross-record route ownership and delivery media shape that cannot be
// established by validating each input independently.
[[nodiscard]] ValidationReport validate_render_admission(
    const EngineSpec &engine, const PresentationCalibration &presentation,
    const RenderScenario &scenario, const ProvenanceLedger &provenance,
    const SourceMatrixContract &source_matrix);

[[nodiscard]] bool same_content_identity(const RenderManifest &lhs,
                                         const RenderManifest &rhs);

[[nodiscard]] ValidationReport validate(const RenderManifestContent &content,
                                        const ProvenanceLedger &provenance,
                                        const SourceMatrixContract &source_matrix);
[[nodiscard]] ValidationReport validate(const ExecutionFacts &execution);
[[nodiscard]] ValidationReport validate(const RenderManifest &manifest,
                                        const ProvenanceLedger &provenance,
                                        const SourceMatrixContract &source_matrix);

} // namespace engine_sim_offline::contract
