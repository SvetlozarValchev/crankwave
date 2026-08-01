#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "simulation/inertial_crank_dynamics.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"
#include "simulation/operating_cycle_accountant.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct LowOrderInertialDynoV1StepView {
    std::reference_wrapper<const LegacyMechanismStep> mechanics;
    std::reference_wrapper<const LegacyLowOrderGasStep> gas;
    contract::TorqueTelemetry capture_torque;
};

using LowOrderInertialDynoV1AdvanceResult =
    std::variant<LowOrderInertialDynoV1StepView, LowOrderEngineCoreV1Completed,
                 contract::FailureContext>;

// Owns the M4 motion policy around one shared low-order core. Preparation remains
// kinematically held only until its fixed sampling horizon. Every later motion
// sample comes from the declared rigid-crank inertial equation and the prior
// committed gas/loss state; there is no prescribed RPM lane.
class LowOrderInertialDynoV1Runtime final {
  public:
    LowOrderInertialDynoV1Runtime(const LowOrderInertialDynoV1Runtime &) = delete;
    LowOrderInertialDynoV1Runtime &
    operator=(const LowOrderInertialDynoV1Runtime &) = delete;
    LowOrderInertialDynoV1Runtime(LowOrderInertialDynoV1Runtime &&) noexcept = default;
    LowOrderInertialDynoV1Runtime &
    operator=(LowOrderInertialDynoV1Runtime &&) noexcept = default;

    [[nodiscard]] LowOrderInertialDynoV1AdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core);
    [[nodiscard]] LowOrderInertialDynoV1AdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core,
            const LiveControlOverrides &overrides);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool held_preparation_active() const noexcept;
    [[nodiscard]] std::uint64_t accepted_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t release_frame_index() const noexcept;
    [[nodiscard]] const std::optional<contract::InertialDynoResult> &
    inertial_dyno_result() const noexcept;

  private:
    LowOrderInertialDynoV1Runtime(
        LowOrderOperatingPointV1Runtime preparation,
        OperatingCycleAccountant accountant, InertialCrankDynamics dynamics,
        std::vector<std::size_t> physical_gas_step_indices,
        std::vector<OperatingGasVolumePressureSample> pressure_samples,
        contract::RationalRateHz rate, std::uint64_t expected_sample_count,
        std::uint64_t release_frame_index, double initial_engine_speed_rpm,
        double initial_theta_rad, double target_engine_speed_rpm,
        contract::Sha256Digest simulation_request_identity_v5_sha256,
        std::string brake_curve_resolution_id,
        contract::MethodIdentity brake_torque_method,
        contract::MethodIdentity crank_dynamics_method, std::string model_id,
        std::string profile_id, std::string scenario_id, contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary, const LegacyMechanismStep *mechanics = nullptr,
          std::optional<contract::GasVolumeId> gas_volume_id = std::nullopt) const;
    [[nodiscard]] LowOrderInertialDynoV1AdvanceResult
    fail(contract::FailureContext failure);
    [[nodiscard]] std::optional<contract::FailureContext>
    update_accounting(const LegacyMechanismStep &mechanics,
                      const LegacyLowOrderGasStep &gas);
    [[nodiscard]] std::optional<contract::FailureContext>
    finalize_result(const LegacyMechanismStep &mechanics);

    LowOrderOperatingPointV1Runtime preparation_;
    OperatingCycleAccountant accountant_;
    InertialCrankDynamics dynamics_;
    std::vector<std::size_t> physical_gas_step_indices_;
    std::vector<OperatingGasVolumePressureSample> pressure_samples_;
    contract::RationalRateHz rate_;
    std::uint64_t expected_sample_count_ = 0;
    std::uint64_t release_frame_index_ = 0;
    std::uint64_t accepted_sample_count_ = 0;
    double step_s_ = 0.0;
    double initial_engine_speed_rpm_ = 0.0;
    double target_engine_speed_rpm_ = 0.0;
    InertialCrankState crank_state_;
    double minimum_engine_speed_rpm_ = 0.0;
    double maximum_engine_speed_rpm_ = 0.0;
    std::optional<std::uint64_t> first_target_reached_frame_index_;
    std::optional<double> previous_indicated_gas_torque_nm_;
    std::optional<double> applied_lagged_loss_torque_nm_;
    std::optional<OperatingCompletedCycle> latest_completed_cycle_;
    double released_net_shaft_work_j_ = 0.0;
    double released_passive_brake_work_j_ = 0.0;
    double release_angular_speed_rad_s_ = 0.0;
    contract::Sha256Digest simulation_request_identity_v5_sha256_;
    std::string brake_curve_resolution_id_;
    contract::MethodIdentity brake_torque_method_;
    contract::MethodIdentity crank_dynamics_method_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    std::optional<contract::InertialDynoResult> inertial_dyno_result_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderInertialDynoV1Runtime, contract::ValidationReport>
    compile_low_order_inertial_dyno_v1_runtime(const contract::EngineSpec &,
                                               const contract::RenderScenario &,
                                               const LowOrderCapturePlan &,
                                               const contract::Sha256Digest &);
};

using LowOrderInertialDynoV1CompileResult =
    std::variant<LowOrderInertialDynoV1Runtime, contract::ValidationReport>;

[[nodiscard]] LowOrderInertialDynoV1CompileResult
compile_low_order_inertial_dyno_v1_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v5_sha256);

} // namespace engine_sim_offline::simulation
