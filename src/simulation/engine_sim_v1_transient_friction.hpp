#pragma once

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation {

struct EngineSimV1PositiveSpeedCrankFrictionPlan {
    double running_friction_torque_magnitude_nm = 0.0;

    friend bool operator==(const EngineSimV1PositiveSpeedCrankFrictionPlan &,
                           const EngineSimV1PositiveSpeedCrankFrictionPlan &) = default;
};

struct EngineSimV1PositiveSpeedCrankFriction {
    // Signed crank torque for the admitted positive-running direction.
    double torque_nm = 0.0;

    friend bool operator==(const EngineSimV1PositiveSpeedCrankFriction &,
                           const EngineSimV1PositiveSpeedCrankFriction &) = default;
};

enum class EngineSimV1CrankFrictionIssue : std::uint8_t {
    nonfinite_running_friction_torque,
    negative_running_friction_torque,
};

struct EngineSimV1CrankFrictionError {
    EngineSimV1CrankFrictionIssue issue =
        EngineSimV1CrankFrictionIssue::nonfinite_running_friction_torque;

    friend bool operator==(const EngineSimV1CrankFrictionError &,
                           const EngineSimV1CrankFrictionError &) = default;
};

using EngineSimV1CrankFrictionCalculation =
    std::variant<EngineSimV1PositiveSpeedCrankFriction, EngineSimV1CrankFrictionError>;

// Pristine engine-sim represents crank friction as a zero-speed rotation constraint
// with symmetric torque limits. At every positive speed admitted by the current
// FreeEngine runtime the constraint saturates in the running-opposite direction.
[[nodiscard]] EngineSimV1CrankFrictionCalculation
calculate_engine_sim_v1_positive_speed_crank_friction(
    const EngineSimV1PositiveSpeedCrankFrictionPlan &plan) noexcept;

} // namespace engine_sim_offline::simulation
