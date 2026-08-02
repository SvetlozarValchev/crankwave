#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/torque.hpp"
#include "simulation/chen_flynn_cycle_mean_loss.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"
#include "simulation/operating_cycle_accountant.hpp"

#include <variant>

namespace engine_sim_offline::simulation {

// Dynamic warm preparation holds this speed only until a complete four-stroke
// cycle has been observed. Released operation derives its cycle-mean speed from
// the represented cycle duration instead of retaining this initial value.
struct OneLevelMasterRodDynamicAccountingInputs {
    ChenFlynnCycleMeanLossPlan coefficients;
    contract::TorqueTermMask aggregate_loss_terms = 0;
    contract::TorqueTermMask starter_terms = 0;
    double initial_engine_speed_rpm = 0.0;

    friend bool operator==(const OneLevelMasterRodDynamicAccountingInputs &,
                           const OneLevelMasterRodDynamicAccountingInputs &) = default;
};

using OneLevelMasterRodCycleAccountingPlanCompileResult =
    std::variant<OperatingCycleAccountingPlan, contract::ValidationReport>;

// Joins the immutable articulated mechanism geometry to the capture plan's
// canonical cylinder/chamber order. This constructs accounting evidence only; it
// does not admit a runtime, widen a mechanism gate, or compile the accountant.
[[nodiscard]] OneLevelMasterRodCycleAccountingPlanCompileResult
compile_one_level_master_rod_dynamic_cycle_accounting_plan(
    const OneLevelMasterRodMechanismKinematicsPlan &mechanism_plan,
    const LowOrderCapturePlan &capture_plan,
    const OneLevelMasterRodDynamicAccountingInputs &inputs);

} // namespace engine_sim_offline::simulation
