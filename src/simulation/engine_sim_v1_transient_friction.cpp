#include "simulation/engine_sim_v1_transient_friction.hpp"

#include <cmath>

namespace engine_sim_offline::simulation {

EngineSimV1CrankFrictionCalculation
calculate_engine_sim_v1_positive_speed_crank_friction(
    const EngineSimV1PositiveSpeedCrankFrictionPlan &plan) noexcept {
    if (!std::isfinite(plan.running_friction_torque_magnitude_nm)) {
        return EngineSimV1CrankFrictionError{
            EngineSimV1CrankFrictionIssue::nonfinite_running_friction_torque};
    }
    if (plan.running_friction_torque_magnitude_nm < 0.0) {
        return EngineSimV1CrankFrictionError{
            EngineSimV1CrankFrictionIssue::negative_running_friction_torque};
    }
    return EngineSimV1PositiveSpeedCrankFriction{
        -plan.running_friction_torque_magnitude_nm};
}

} // namespace engine_sim_offline::simulation
