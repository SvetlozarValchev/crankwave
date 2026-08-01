#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/render_manifest.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

inline constexpr std::string_view
    kGenericChenFlynnLowOrderModelPredictionApplicability =
        "generic-chen-flynn-low-order-model-prediction";

// Public evidence for one represented four-stroke boundary. Exact-sample boundaries
// use equal bracket indices and canonical +0 for the fraction. Interpolated
// boundaries retain adjacent post-step sample identities and the fraction actually
// used by cycle quadrature and the pressure observer.
struct OperatingPointBoundaryEvidence {
    std::uint64_t left_bracket_sample_index = 0;
    std::uint64_t right_bracket_sample_index = 0;
    double fraction_from_left_01 = 0.0;
    double scenario_time_s = 0.0;
    double theta_unwrapped_rad = 0.0;

    friend bool operator==(const OperatingPointBoundaryEvidence &,
                           const OperatingPointBoundaryEvidence &) = default;
};

struct CompletedCycleRangeEvidence {
    std::uint32_t completed_cycle_count = 0;
    std::uint64_t first_completed_cycle_ordinal = 0;
    std::uint64_t last_completed_cycle_ordinal = 0;
    OperatingPointBoundaryEvidence start_boundary;
    OperatingPointBoundaryEvidence end_boundary;

    friend bool operator==(const CompletedCycleRangeEvidence &,
                           const CompletedCycleRangeEvidence &) = default;
};

struct MeanBoundaryPressurePa {
    GasVolumeId gas_volume_id;
    double pressure_pa_abs = 0.0;

    friend bool operator==(const MeanBoundaryPressurePa &,
                           const MeanBoundaryPressurePa &) = default;
};

struct EndBoundaryPressurePa {
    GasVolumeId gas_volume_id;
    double pressure_pa_abs = 0.0;

    friend bool operator==(const EndBoundaryPressurePa &,
                           const EndBoundaryPressurePa &) = default;
};

// These are completed-cycle/block means. Aggregate loss is the signed
// running-direction contribution, so its value is negative at admitted positive
// held speed. The corresponding block aggregate_loss_work_j below is a positive
// consumed-work magnitude.
struct CycleMeanTorqueBreakdown {
    TorqueValueNm indicated_gas;
    TorqueValueNm aggregate_loss;
    TorqueValueNm starter;
    TorqueValueNm net_shaft;

    friend bool operator==(const CycleMeanTorqueBreakdown &,
                           const CycleMeanTorqueBreakdown &) = default;
};

// Source evidence retained for every complete cycle in the fixed trailing sample.
// The contract reduces work and each ascending end-boundary pressure lane in written
// order, starting from canonical +0, so no independently supplied sample total or
// mean can forge the result.
struct HeldSpeedCompletedCycleEvidence {
    std::uint64_t completed_cycle_ordinal = 0;
    double indicated_gas_work_j = 0.0;
    double aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    std::vector<EndBoundaryPressurePa> end_boundary_pressures;

    friend bool operator==(const HeldSpeedCompletedCycleEvidence &,
                           const HeldSpeedCompletedCycleEvidence &) = default;
};

struct HeldSpeedCycleBlockEvidence {
    CompletedCycleRangeEvidence cycles;
    std::vector<HeldSpeedCompletedCycleEvidence> completed_cycles;
    double indicated_gas_work_j = 0.0;
    double aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    CycleMeanTorqueBreakdown cycle_mean_torque;
    double net_bmep_pa = 0.0;
    double mean_power_w = 0.0;
    std::vector<MeanBoundaryPressurePa> mean_boundary_pressures;

    friend bool operator==(const HeldSpeedCycleBlockEvidence &,
                           const HeldSpeedCycleBlockEvidence &) = default;
};

struct HeldSpeedFixedHorizonSampleEvidence {
    MethodIdentity method;
    std::uint32_t trailing_complete_cycle_count = 0;
    double fixed_preparation_horizon_s = 0.0;
    HeldSpeedCycleBlockEvidence trailing_complete_cycles;
    std::uint64_t last_eligible_completed_cycle_ordinal_at_fixed_horizon = 0;
    OperatingPointBoundaryEvidence last_eligible_cycle_end_boundary_at_fixed_horizon;

    friend bool operator==(const HeldSpeedFixedHorizonSampleEvidence &,
                           const HeldSpeedFixedHorizonSampleEvidence &) = default;
};

struct HeldSpeedOperatingPointAmbientConditions {
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double relative_humidity_01 = 0.0;

    friend bool operator==(const HeldSpeedOperatingPointAmbientConditions &,
                           const HeldSpeedOperatingPointAmbientConditions &) = default;
};

struct HeldSpeedOperatingPointFuelConditions {
    std::string fuel_id;
    double lower_heating_value_j_per_kg = 0.0;
    double stoichiometric_air_fuel_mass_ratio = 0.0;

    friend bool operator==(const HeldSpeedOperatingPointFuelConditions &,
                           const HeldSpeedOperatingPointFuelConditions &) = default;
};

struct HeldSpeedOperatingPointThermalConditions {
    double gas_temperature_k = 0.0;
    double wall_temperature_k = 0.0;
    double coolant_temperature_k = 0.0;
    double oil_temperature_k = 0.0;

    friend bool operator==(const HeldSpeedOperatingPointThermalConditions &,
                           const HeldSpeedOperatingPointThermalConditions &) = default;
};

struct HeldSpeedOperatingPointCrankcaseConditions {
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;

    friend bool
    operator==(const HeldSpeedOperatingPointCrankcaseConditions &,
               const HeldSpeedOperatingPointCrankcaseConditions &) = default;
};

struct HeldSpeedOperatingPointAccessoryConditions {
    std::string configuration_id;
    Sha256Digest content_sha256;

    friend bool
    operator==(const HeldSpeedOperatingPointAccessoryConditions &,
               const HeldSpeedOperatingPointAccessoryConditions &) = default;
};

// Compact, human-facing conditions accompany the exact canonical request digest.
// Resolution ledgers and full operating journals remain owned by the request; they
// are not copied into every result.
struct HeldSpeedOperatingPointConditions {
    std::string engine_profile_id;
    double engine_speed_rpm = 0.0;
    double initial_theta_unwrapped_rad = 0.0;
    double cycle_reference_theta_rad = 0.0;
    double throttle_01 = 0.0;
    RationalRateHz physics_rate_hz;
    HeldSpeedOperatingPointAmbientConditions ambient;
    HeldSpeedOperatingPointFuelConditions fuel;
    HeldSpeedOperatingPointThermalConditions initial_thermal_state;
    HeldSpeedOperatingPointCrankcaseConditions crankcase;
    double total_displacement_m3 = 0.0;
    HeldSpeedOperatingPointAccessoryConditions accessory_configuration;
    bool starter_mechanically_disengaged = false;
    TorqueTermMask starter_included_terms = 0;

    friend bool operator==(const HeldSpeedOperatingPointConditions &,
                           const HeldSpeedOperatingPointConditions &) = default;
};

struct HeldSpeedOperatingPointResult {
    Sha256Digest simulation_request_identity_v4_sha256;
    HeldSpeedOperatingPointConditions conditions;
    std::string applicability_label;
    HeldSpeedFixedHorizonSampleEvidence sampling;

    // The one fixed trailing sample is the reportable operating point. Returning it
    // by reference avoids a second mutable copy that can drift from the evidence.
    [[nodiscard]] const HeldSpeedCycleBlockEvidence &reported_block() const noexcept {
        return sampling.trailing_complete_cycles;
    }

    friend bool operator==(const HeldSpeedOperatingPointResult &,
                           const HeldSpeedOperatingPointResult &) = default;
};

// Work is accumulated only over the released inertial interval
// [release_frame_index, end_frame_index]. Passive-brake work is a positive
// absorbed-work magnitude. The signed residual is evaluated as
//
//   (net_shaft_work_j - passive_brake_absorbed_work_j)
//       - kinetic_energy_change_j.
//
// Retaining all three terms makes the residual independently recomputable instead
// of accepting an unauditable scalar quality claim.
struct InertialDynoEnergyBalanceEvidence {
    double net_shaft_work_j = 0.0;
    double passive_brake_absorbed_work_j = 0.0;
    double kinetic_energy_change_j = 0.0;
    double residual_j = 0.0;

    friend bool operator==(const InertialDynoEnergyBalanceEvidence &,
                           const InertialDynoEnergyBalanceEvidence &) = default;
};

// A successful inertial result always represents completion of the requested fixed
// render horizon. first_target_reached_frame_index is evidence about the pull, not an
// early-terminal instruction: audio continues through end_frame_index whether or not
// the target was reached.
struct InertialDynoResult {
    Sha256Digest simulation_request_identity_v4_sha256;
    // Start is physics frame zero; release is the exact preparation-horizon frame.
    // Extrema cover every represented physics-frame state from start through end.
    double start_engine_speed_rpm = 0.0;
    double target_engine_speed_rpm = 0.0;
    double release_engine_speed_rpm = 0.0;
    double end_engine_speed_rpm = 0.0;
    double minimum_engine_speed_rpm = 0.0;
    double maximum_engine_speed_rpm = 0.0;
    // All frame identities are on the request's physics-rate grid. A target is
    // first reached by the earliest represented post-step state at or above it.
    std::uint64_t release_frame_index = 0;
    std::uint64_t end_frame_index = 0;
    std::optional<std::uint64_t> first_target_reached_frame_index;
    double equivalent_inertia_kg_m2 = 0.0;
    std::string brake_curve_resolution_id;
    MethodIdentity brake_torque_method;
    MethodIdentity crank_dynamics_method;
    InertialDynoEnergyBalanceEvidence energy_balance;

    friend bool operator==(const InertialDynoResult &,
                           const InertialDynoResult &) = default;
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
    std::optional<HeldSpeedOperatingPointResult> held_speed_operating_point;
    std::optional<InertialDynoResult> inertial_dyno;
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
[[nodiscard]] ValidationReport
validate(const HeldSpeedOperatingPointResult &operating_point);
[[nodiscard]] ValidationReport
validate(const HeldSpeedOperatingPointResult &operating_point,
         const RenderScenario &requested_scenario, const EngineSpec &engine,
         const Sha256Digest &expected_simulation_request_identity_v4_sha256);
[[nodiscard]] ValidationReport validate(const InertialDynoResult &inertial_dyno);
[[nodiscard]] ValidationReport
validate(const InertialDynoResult &inertial_dyno,
         const RenderScenario &requested_scenario,
         const Sha256Digest &expected_simulation_request_identity_v4_sha256);
[[nodiscard]] ValidationReport validate(const UnreachableTarget &unreachable);
[[nodiscard]] ValidationReport validate(const RenderFailure &failure);
[[nodiscard]] ValidationReport
validate(const RenderResult &result, const RenderScenario &requested_scenario,
         const Sha256Digest &expected_simulation_request_identity_v4_sha256,
         const ProvenanceLedger &provenance, const SourceMatrixContract &source_matrix);

} // namespace engine_sim_offline::contract
