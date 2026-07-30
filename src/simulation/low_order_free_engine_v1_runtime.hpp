#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "simulation/fixed_horizon_cycle_sampling.hpp"
#include "simulation/kinematic_scenario_schedule.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/operating_cycle_accountant.hpp"
#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct LowOrderFreeEngineV1StepView {
    std::reference_wrapper<const LegacyMechanismStep> mechanics;
    std::reference_wrapper<const LegacyLowOrderGasStep> gas;
    contract::TorqueTelemetry capture_torque;
};

using LowOrderFreeEngineV1AdvanceResult =
    std::variant<LowOrderFreeEngineV1StepView, LowOrderEngineCoreV1Completed,
                 contract::FailureContext>;

// Owns warm held preparation and the subsequent unconstrained positive-speed crank
// motion around one shared low-order core. The core retains all gas, flame,
// randomness, pressure-history, and event state across the release boundary.
class LowOrderFreeEngineV1Runtime final {
  public:
    LowOrderFreeEngineV1Runtime(const LowOrderFreeEngineV1Runtime &) = delete;
    LowOrderFreeEngineV1Runtime &
    operator=(const LowOrderFreeEngineV1Runtime &) = delete;
    LowOrderFreeEngineV1Runtime(LowOrderFreeEngineV1Runtime &&) noexcept = default;
    LowOrderFreeEngineV1Runtime &
    operator=(LowOrderFreeEngineV1Runtime &&) noexcept = default;

    [[nodiscard]] LowOrderFreeEngineV1AdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core);
    [[nodiscard]] LowOrderFreeEngineV1AdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core, const LiveControlOverrides &overrides);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool held_preparation_active() const noexcept;
    [[nodiscard]] std::uint64_t accepted_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t release_frame_index() const noexcept;

  private:
    LowOrderFreeEngineV1Runtime(
        ScenarioControlCursor control_cursor, OperatingCycleAccountant accountant,
        FixedHorizonCycleSampler sampler,
        std::vector<std::size_t> physical_gas_step_indices,
        std::vector<OperatingGasVolumePressureSample> pressure_samples,
        contract::RationalRateHz rate, std::uint64_t expected_sample_count,
        std::uint64_t release_frame_index, double initial_engine_speed_rpm,
        double initial_theta_rad, double equivalent_inertia_kg_m2, std::string model_id,
        std::string profile_id, std::string scenario_id, contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary, const LegacyMechanismStep *mechanics = nullptr,
          std::optional<contract::GasVolumeId> gas_volume_id = std::nullopt) const;
    [[nodiscard]] LowOrderFreeEngineV1AdvanceResult
    fail(contract::FailureContext failure);
    [[nodiscard]] std::optional<contract::FailureContext>
    update_accounting(const LegacyMechanismStep &mechanics,
                      const LegacyLowOrderGasStep &gas);
    [[nodiscard]] std::optional<contract::FailureContext>
    observe_preparation_cycle(const OperatingCycleBoundaryCrossing &crossing,
                              const LegacyMechanismStep &mechanics);
    [[nodiscard]] std::optional<contract::FailureContext>
    finalize_preparation(const LegacyMechanismStep &mechanics);

    ScenarioControlCursor control_cursor_;
    OperatingCycleAccountant accountant_;
    FixedHorizonCycleSampler sampler_;
    std::vector<std::size_t> physical_gas_step_indices_;
    std::vector<OperatingGasVolumePressureSample> pressure_samples_;
    contract::RationalRateHz rate_;
    std::uint64_t expected_sample_count_ = 0;
    std::uint64_t release_frame_index_ = 0;
    std::uint64_t accepted_sample_count_ = 0;
    double step_s_ = 0.0;
    double initial_engine_speed_rpm_ = 0.0;
    double equivalent_inertia_kg_m2_ = 0.0;
    detail::PositiveSpeedRigidCrankState crank_state_;
    std::optional<double> previous_indicated_gas_torque_nm_;
    std::optional<double> applied_lagged_loss_torque_nm_;
    std::optional<OperatingCompletedCycle> latest_completed_cycle_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    bool preparation_finalized_ = false;
    bool terminal_completed_ = false;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderFreeEngineV1Runtime, contract::ValidationReport>
    compile_low_order_free_engine_v1_runtime(const contract::EngineSpec &,
                                             const contract::RenderScenario &,
                                             const LowOrderCapturePlan &,
                                             const contract::Sha256Digest &);
};

using LowOrderFreeEngineV1CompileResult =
    std::variant<LowOrderFreeEngineV1Runtime, contract::ValidationReport>;

[[nodiscard]] LowOrderFreeEngineV1CompileResult
compile_low_order_free_engine_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256);

} // namespace engine_sim_offline::simulation
