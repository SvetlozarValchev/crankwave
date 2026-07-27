#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/render_manifest.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::contract {

enum class FailureKind : std::uint8_t {
    invalid_specification,
    unreachable_target,
    cancelled,
    event_schedule_violation,
    nonphysical_state,
    numerical_failure,
    incomplete_source_route,
    evidence_rights_failure,
    artifact_publication_failure,
    contract_violation,
};

struct FailureTolerance {
    std::string quantity_id;
    double attempted_value = 0.0;
    double tolerance = 0.0;

    friend bool operator==(const FailureTolerance &,
                           const FailureTolerance &) = default;
};

struct FailureContext {
    FailureKind kind = FailureKind::contract_violation;
    std::string detail_code;
    std::string model_id;
    std::string profile_id;
    std::uint64_t sample_index = 0;
    std::uint64_t step_end_index = 0;
    double scenario_time_s = 0.0;
    double theta_rad = 0.0;
    std::optional<EngineId> engine_id;
    std::optional<CylinderId> cylinder_id;
    std::optional<PortId> port_id;
    std::optional<GasVolumeId> gas_volume_id;
    std::optional<FlowEdgeId> flow_edge_id;
    std::optional<RouteId> route_id;
    std::string state_summary;
    std::string attempted_recovery;
    std::vector<FailureTolerance> tolerances;

    friend bool operator==(const FailureContext &, const FailureContext &) = default;
};

struct AssetPayloadIdentity {
    AudioAssetId id;
    std::uint64_t byte_count = 0;
    Sha256Digest payload_sha256;

    friend bool operator==(const AssetPayloadIdentity &,
                           const AssetPayloadIdentity &) = default;
};

struct RenderRequestRecord {
    ResolvedRenderInputs resolved_inputs;
    ProvenanceLedger provenance;
    SourceMatrixContract source_matrix;
    std::vector<AssetPayloadIdentity> asset_payloads;

    friend bool operator==(const RenderRequestRecord &,
                           const RenderRequestRecord &) = default;
};

enum class ActiveBoundKind : std::uint8_t {
    throttle_lower,
    throttle_upper,
};

struct ActiveReachabilityBound {
    ActiveBoundKind kind = ActiveBoundKind::throttle_lower;
    // Requested throttle boundary occupied by the nearest feasible point.
    double throttle_01 = 0.0;

    friend bool operator==(const ActiveReachabilityBound &,
                           const ActiveReachabilityBound &) = default;
};

struct ReachabilityCandidate {
    std::uint64_t stable_candidate_id = 0;
    double throttle_01 = 0.0;
    double actuator_torque_nm = 0.0;
    double achieved_net_bmep_pa = 0.0;
    bool settled = false;

    friend bool operator==(const ReachabilityCandidate &,
                           const ReachabilityCandidate &) = default;
};

struct ReachabilityEvidence {
    std::uint32_t evaluated_candidate_count = 0;
    std::uint32_t iteration_count = 0;
    // In a load-target result these are an exact copy of the requested search
    // interval, not an inferred interval around the retained probes.
    double requested_throttle_lower_bound_01 = 0.0;
    double requested_throttle_upper_bound_01 = 0.0;
    std::vector<ReachabilityCandidate> probes;

    friend bool operator==(const ReachabilityEvidence &,
                           const ReachabilityEvidence &) = default;
};

inline constexpr std::uint32_t kMaxRetainedReachabilityProbes = 256;

struct ReachedTarget {
    double target_net_bmep_pa = 0.0;
    double achieved_net_bmep_pa = 0.0;
    double signed_error_pa = 0.0;
    double tolerance_pa = 0.0;
    ReachabilityCandidate selected;
    ReachabilityEvidence search;

    friend bool operator==(const ReachedTarget &, const ReachedTarget &) = default;
};

struct UnreachableTarget {
    double target_net_bmep_pa = 0.0;
    double achieved_net_bmep_pa = 0.0;
    double signed_error_pa = 0.0;
    double tolerance_pa = 0.0;
    ReachabilityCandidate nearest_feasible;
    std::vector<ActiveReachabilityBound> active_bounds;
    ReachabilityEvidence search;
    FailureContext context;
    RenderRequestRecord request;

    friend bool operator==(const UnreachableTarget &,
                           const UnreachableTarget &) = default;
};

[[nodiscard]] bool is_better_nearest_candidate(const ReachabilityCandidate &candidate,
                                               const ReachabilityCandidate &incumbent,
                                               double target_net_bmep_pa) noexcept;

struct RenderSuccess {
    RenderManifest manifest;
    std::optional<ReachedTarget> reached_target;
};

struct RenderFailure {
    FailureContext context;
    RenderRequestRecord request;
    // Preflight and evidence failures retain the exact validation diagnostics that
    // caused the render to be rejected. Runtime failures normally leave this empty
    // and describe their state through FailureContext.
    ValidationReport validation;
};

using RenderResult = std::variant<RenderSuccess, UnreachableTarget, RenderFailure>;

[[nodiscard]] ValidationReport validate(const FailureContext &context);
[[nodiscard]] ValidationReport validate(const ReachedTarget &reached);
[[nodiscard]] ValidationReport validate(const UnreachableTarget &unreachable);
[[nodiscard]] ValidationReport validate(const RenderFailure &failure);
[[nodiscard]] ValidationReport validate(const RenderResult &result,
                                        const RenderScenario &requested_scenario,
                                        const ProvenanceLedger &provenance,
                                        const SourceMatrixContract &source_matrix);

} // namespace engine_sim_offline::contract
