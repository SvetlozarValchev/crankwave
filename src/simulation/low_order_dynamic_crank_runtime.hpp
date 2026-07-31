#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "simulation/bounded_dyno_constraint.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/coupled_free_vehicle_drivetrain.hpp"
#include "simulation/engine_sim_v1_transient_friction.hpp"
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

struct LowOrderDynamicCrankPistonWallCylinderPlan {
    contract::CylinderId cylinder_id;
    contract::GasVolumeId chamber_volume_id;
    std::size_t mechanism_cylinder_index = 0;
    std::size_t chamber_gas_step_index = 0;
    double geometric_tdc_rad = 0.0;
    double initial_chamber_pressure_pa_abs = 0.0;
    EngineSimV1PistonWallCylinderPlan friction;

    friend bool
    operator==(const LowOrderDynamicCrankPistonWallCylinderPlan &,
               const LowOrderDynamicCrankPistonWallCylinderPlan &) = default;
};

struct LowOrderDynamicCrankStepView {
    std::reference_wrapper<const LegacyMechanismStep> mechanics;
    std::reference_wrapper<const LegacyLowOrderGasStep> gas;
    contract::TorqueTelemetry capture_torque;
};

// Simulation-internal inspection for verification and tuning. This deliberately is
// not part of the authored contract, capture result, session API, C ABI, or WASM ABI.
struct FreeVehicleRuntimeStateView {
    double engine_speed_rpm = 0.0;
    double vehicle_speed_m_s = 0.0;
    double vehicle_distance_m = 0.0;
    std::optional<contract::GearId> selected_gear_id;
    double clutch_engagement_01 = 0.0;
    double service_brake_application_01 = 0.0;
    double last_clutch_impulse_on_engine_nm_s = 0.0;
    double last_road_load_impulse_n_s = 0.0;
    std::optional<double> last_clutch_slip_rad_s;

    friend bool operator==(const FreeVehicleRuntimeStateView &,
                           const FreeVehicleRuntimeStateView &) = default;
};

using LowOrderDynamicCrankAdvanceResult =
    std::variant<LowOrderDynamicCrankStepView, LowOrderEngineCoreV1Completed,
                 contract::FailureContext>;

struct HeldDynoMotionPlan {
    std::vector<double> target_engine_speed_rpm;
    double maximum_absorbing_torque_nm = 0.0;
    double maximum_driving_torque_nm = 0.0;

    friend bool operator==(const HeldDynoMotionPlan &,
                           const HeldDynoMotionPlan &) = default;
};

struct FreeVehicleGearMotionPlan {
    contract::GearId gear_id;
    detail::ForwardGearReduction reduction;

    friend bool operator==(const FreeVehicleGearMotionPlan &,
                           const FreeVehicleGearMotionPlan &) = default;
};

struct FreeVehicleGearSelectionBoundary {
    std::uint64_t step_index = 0;
    std::optional<std::size_t> gear_index;

    friend bool operator==(const FreeVehicleGearSelectionBoundary &,
                           const FreeVehicleGearSelectionBoundary &) = default;
};

struct FreeVehicleScalarControlBoundary {
    std::uint64_t step_index = 0;
    double value = 0.0;

    friend bool operator==(const FreeVehicleScalarControlBoundary &,
                           const FreeVehicleScalarControlBoundary &) = default;
};

// Compact finite-scenario drivetrain state. Engine/gas state remains owned by the
// shared dynamic-crank runtime; this plan owns only the independently translating
// vehicle and its right-continuous authored control lanes.
struct FreeVehicleMotionPlan {
    double vehicle_mass_kg = 0.0;
    double drag_coefficient = 0.0;
    double frontal_area_m2 = 0.0;
    double differential_ratio = 0.0;
    double tire_radius_m = 0.0;
    double rolling_resistance_force_n = 0.0;
    double maximum_service_brake_force_n = 0.0;
    double maximum_clutch_torque_nm = 0.0;
    std::vector<FreeVehicleGearMotionPlan> gears;
    std::vector<FreeVehicleGearSelectionBoundary> selected_gear;
    std::vector<FreeVehicleScalarControlBoundary> clutch_engagement_01;
    std::vector<FreeVehicleScalarControlBoundary> service_brake_application_01;
    std::size_t next_selected_gear_boundary = 1U;
    std::size_t next_clutch_boundary = 1U;
    std::size_t next_service_brake_boundary = 1U;
    std::optional<std::size_t> current_gear_index;
    double current_clutch_engagement_01 = 0.0;
    double current_service_brake_application_01 = 0.0;
    double vehicle_speed_m_s = 0.0;
    double vehicle_distance_m = 0.0;
    double last_clutch_impulse_on_engine_nm_s = 0.0;
    double last_road_load_impulse_n_s = 0.0;
    std::optional<double> last_clutch_slip_rad_s;

    friend bool operator==(const FreeVehicleMotionPlan &,
                           const FreeVehicleMotionPlan &) = default;
};

// Owns either warm held preparation or a zero-duration cold bootstrap followed by
// nonnegative crank motion around one shared low-order core. The core retains all
// gas, flame, randomness, pressure-history, and event state across the release
// boundary. Reverse rotation is not part of this runtime.
class LowOrderDynamicCrankRuntime final {
  public:
    LowOrderDynamicCrankRuntime(const LowOrderDynamicCrankRuntime &) = delete;
    LowOrderDynamicCrankRuntime &
    operator=(const LowOrderDynamicCrankRuntime &) = delete;
    LowOrderDynamicCrankRuntime(LowOrderDynamicCrankRuntime &&) noexcept = default;
    LowOrderDynamicCrankRuntime &
    operator=(LowOrderDynamicCrankRuntime &&) noexcept = default;

    [[nodiscard]] LowOrderDynamicCrankAdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core);
    [[nodiscard]] LowOrderDynamicCrankAdvanceResult
    advance(LowOrderEngineCoreV1Runtime &core, const LiveControlOverrides &overrides);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool held_preparation_active() const noexcept;
    [[nodiscard]] std::uint64_t accepted_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t release_frame_index() const noexcept;
    [[nodiscard]] std::optional<FreeVehicleRuntimeStateView>
    free_vehicle_state() const noexcept;

  private:
    LowOrderDynamicCrankRuntime(
        ScenarioControlCursor control_cursor,
        std::optional<OperatingCycleAccountant> accountant,
        std::optional<FixedHorizonCycleSampler> sampler,
        std::vector<std::size_t> physical_gas_step_indices,
        std::vector<OperatingGasVolumePressureSample> pressure_samples,
        CenteredSliderCrankConfigurationInertiaPlan configuration_inertia_plan,
        std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders,
        contract::RationalRateHz rate, LowOrderExecutionExtent execution_extent,
        std::uint64_t release_frame_index, double initial_engine_speed_rpm,
        double initial_theta_rad, bool cold_bootstrap,
        double applied_positive_speed_crank_friction_torque_nm,
        double starter_maximum_torque_nm, double starter_target_speed_rad_s,
        std::optional<HeldDynoMotionPlan> held_dyno_motion,
        std::optional<FreeVehicleMotionPlan> free_vehicle_motion, std::string model_id,
        std::string profile_id, std::string scenario_id, contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary, const LegacyMechanismStep *mechanics = nullptr,
          std::optional<contract::GasVolumeId> gas_volume_id = std::nullopt) const;
    [[nodiscard]] LowOrderDynamicCrankAdvanceResult
    fail(contract::FailureContext failure);
    [[nodiscard]] std::optional<contract::FailureContext>
    update_accounting(const LegacyMechanismStep &mechanics,
                      const LegacyLowOrderGasStep &gas);
    [[nodiscard]] std::optional<contract::FailureContext>
    observe_preparation_cycle(const OperatingCycleBoundaryCrossing &crossing,
                              const LegacyMechanismStep &mechanics);
    [[nodiscard]] std::optional<contract::FailureContext>
    finalize_preparation(const LegacyMechanismStep &mechanics);
    [[nodiscard]] std::optional<contract::FailureContext> stage_piston_wall_friction();
    [[nodiscard]] std::optional<contract::FailureContext>
    calculate_next_piston_wall_reactions(double angular_acceleration_rad_s2);
    [[nodiscard]] std::optional<contract::FailureContext>
    commit_next_piston_wall_boundary(const LegacyMechanismStep &mechanics,
                                     const LegacyLowOrderGasStep &gas);

    ScenarioControlCursor control_cursor_;
    std::optional<OperatingCycleAccountant> accountant_;
    std::optional<FixedHorizonCycleSampler> sampler_;
    std::vector<std::size_t> physical_gas_step_indices_;
    std::vector<OperatingGasVolumePressureSample> pressure_samples_;
    CenteredSliderCrankConfigurationInertiaPlan configuration_inertia_plan_;
    std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders_;
    std::vector<double> piston_wall_boundary_phase_rad_;
    std::vector<double> piston_wall_boundary_pressure_pa_abs_;
    std::vector<double> retained_piston_wall_reaction_magnitude_n_;
    std::vector<EngineSimV1PistonWallFrictionStage> piston_wall_stages_;
    std::vector<double> candidate_piston_wall_reaction_magnitude_n_;
    std::vector<double> next_piston_wall_boundary_phase_rad_;
    std::vector<double> next_piston_wall_boundary_pressure_pa_abs_;
    contract::RationalRateHz rate_;
    LowOrderExecutionExtent execution_extent_ =
        LowOrderExecutionExtent::finite_scenario(0U);
    std::uint64_t release_frame_index_ = 0;
    std::uint64_t accepted_sample_count_ = 0;
    double step_s_ = 0.0;
    double initial_engine_speed_rpm_ = 0.0;
    double applied_positive_speed_crank_friction_torque_nm_ = 0.0;
    double starter_maximum_torque_nm_ = 0.0;
    double starter_target_speed_rad_s_ = 0.0;
    std::optional<HeldDynoMotionPlan> held_dyno_motion_;
    std::optional<FreeVehicleMotionPlan> free_vehicle_motion_;
    double piston_wall_boundary_angular_speed_rad_s_ = 0.0;
    double applied_piston_wall_friction_torque_nm_ = 0.0;
    std::uint64_t piston_wall_boundary_index_ = 0;
    detail::PositiveSpeedRigidCrankState crank_state_;
    std::optional<double> previous_indicated_gas_torque_nm_;
    std::optional<OperatingCompletedCycle> latest_completed_cycle_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    bool preparation_finalized_ = false;
    bool terminal_completed_ = false;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderDynamicCrankRuntime, contract::ValidationReport>
    compile_low_order_dynamic_crank_runtime(const contract::EngineSpec &,
                                            const contract::RenderScenario &,
                                            const LowOrderCapturePlan &,
                                            const contract::Sha256Digest &,
                                            LowOrderExecutionExtent);
};

using LowOrderDynamicCrankCompileResult =
    std::variant<LowOrderDynamicCrankRuntime, contract::ValidationReport>;

[[nodiscard]] LowOrderDynamicCrankCompileResult compile_low_order_dynamic_crank_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const LowOrderCapturePlan &capture_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256,
    LowOrderExecutionExtent execution_extent);

} // namespace engine_sim_offline::simulation
