#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "simulation/adjacent_cycle_block_convergence.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/operating_cycle_accountant.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct LowOrderOperatingPointV1Step {
    contract::TorqueTelemetry capture_torque;

    friend bool operator==(const LowOrderOperatingPointV1Step &,
                           const LowOrderOperatingPointV1Step &) = default;
};

using LowOrderOperatingPointV1AdvanceResult =
    std::variant<LowOrderOperatingPointV1Step, contract::FailureContext>;

// M4 policy layered directly on one transactional low-order core. It observes the
// exact mechanics+gas step, owns complete-cycle accounting and fixed-cutoff
// convergence, and supplies only truthful per-frame torque availability to capture.
// It does not own block transport, excitation, presentation, or publication.
class LowOrderOperatingPointV1Runtime final {
  public:
    LowOrderOperatingPointV1Runtime(const LowOrderOperatingPointV1Runtime &) = delete;
    LowOrderOperatingPointV1Runtime &
    operator=(const LowOrderOperatingPointV1Runtime &) = delete;
    LowOrderOperatingPointV1Runtime(
        LowOrderOperatingPointV1Runtime &&) noexcept = default;
    LowOrderOperatingPointV1Runtime &
    operator=(LowOrderOperatingPointV1Runtime &&) noexcept = default;

    [[nodiscard]] LowOrderOperatingPointV1AdvanceResult
    advance(const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] std::uint64_t accepted_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t fixed_cutoff_frame_count() const noexcept;
    [[nodiscard]] const std::optional<contract::HeldSpeedOperatingPointResult> &
    operating_point_result() const noexcept;

  private:
    struct ExpectedCylinderTransaction {
        contract::CylinderId cylinder_id;
        contract::RouteId exhaust_route_id;
        contract::PortId intake_port_id;
        contract::PortId exhaust_port_id;
        contract::GasVolumeId intake_runner_volume_id;
        contract::GasVolumeId chamber_volume_id;
        contract::GasVolumeId exhaust_primary_volume_id;
    };

    struct ExpectedRouteTransaction {
        contract::RouteId route_id;
        contract::GasVolumeId collector_volume_id;
        contract::FlowEdgeId collector_outlet_edge_id;
    };

    struct TransactionShape {
        std::vector<ExpectedCylinderTransaction> cylinders;
        std::vector<contract::GasVolumeIdentity> gas_volumes;
        std::vector<contract::FlowEdgeIdentity> flow_edges;
        std::vector<ExpectedRouteTransaction> exhaust_routes;
    };

    LowOrderOperatingPointV1Runtime(
        OperatingCycleAccountant accountant,
        AdjacentCycleBlockConvergenceObserver convergence,
        std::vector<std::size_t> physical_gas_step_indices,
        std::vector<OperatingGasVolumePressureSample> pressure_samples,
        TransactionShape transaction_shape,
        std::uint64_t fixed_cutoff_frame_count,
        contract::Sha256Digest simulation_request_identity_v2_sha256,
        contract::HeldSpeedOperatingPointConditions conditions,
        std::string model_id, std::string profile_id, std::string scenario_id,
        contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary, const LegacyMechanismStep *mechanics = nullptr,
          std::optional<contract::GasVolumeId> gas_volume_id = std::nullopt) const;
    [[nodiscard]] LowOrderOperatingPointV1AdvanceResult
    fail(contract::FailureContext failure);
    [[nodiscard]] std::optional<contract::FailureContext>
    validate_transaction(const LegacyMechanismStep &mechanics,
                         const LegacyLowOrderGasStep &gas) const;
    [[nodiscard]] std::optional<contract::FailureContext>
    observe_completed_cycle(const OperatingCycleBoundaryCrossing &crossing,
                            const LegacyMechanismStep &mechanics);
    [[nodiscard]] std::optional<contract::FailureContext>
    finalize_at_cutoff(const LegacyMechanismStep &mechanics);

    OperatingCycleAccountant accountant_;
    AdjacentCycleBlockConvergenceObserver convergence_;
    std::vector<std::size_t> physical_gas_step_indices_;
    std::vector<OperatingGasVolumePressureSample> pressure_samples_;
    TransactionShape transaction_shape_;
    std::uint64_t fixed_cutoff_frame_count_ = 0;
    std::uint64_t accepted_sample_count_ = 0;
    contract::Sha256Digest simulation_request_identity_v2_sha256_;
    contract::HeldSpeedOperatingPointConditions conditions_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    std::optional<contract::HeldSpeedOperatingPointResult> operating_point_result_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderOperatingPointV1Runtime,
                        contract::ValidationReport>
    compile_low_order_operating_point_v1_runtime(
        const contract::EngineSpec &, const contract::RenderScenario &,
        const LowOrderCapturePlan &, const contract::Sha256Digest &);
};

using LowOrderOperatingPointV1CompileResult =
    std::variant<LowOrderOperatingPointV1Runtime, contract::ValidationReport>;

// The caller supplies the already compiled shared capture topology and the canonical
// simulation-request-v2 digest retained by the opaque render job. No request encoder
// or presentation dependency enters the simulation layer.
[[nodiscard]] LowOrderOperatingPointV1CompileResult
compile_low_order_operating_point_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v2_sha256);

} // namespace engine_sim_offline::simulation
