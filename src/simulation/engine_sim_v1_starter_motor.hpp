#pragma once

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation {

// The pristine starter is a one-way target-speed constraint. This primitive
// expresses its isolated-crank equivalent in the runtime's positive-forward
// convention.
struct EngineSimV1StarterMotorInput {
    bool enabled = false;
    double target_angular_speed_rad_s = 0.0;
    double unconstrained_predicted_angular_speed_rad_s = 0.0;
    double maximum_torque_nm = 0.0;
    double equivalent_inertia_kg_m2 = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const EngineSimV1StarterMotorInput &,
                           const EngineSimV1StarterMotorInput &) = default;
};

struct EngineSimV1StarterMotorTorque {
    EngineSimV1StarterMotorInput input;
    double required_isolated_crank_torque_nm = 0.0;
    double applied_crank_torque_nm = 0.0;

    friend bool operator==(const EngineSimV1StarterMotorTorque &,
                           const EngineSimV1StarterMotorTorque &) = default;
};

enum class EngineSimV1StarterMotorInputIssue : std::uint8_t {
    nonfinite_target_angular_speed,
    negative_target_angular_speed,
    noncanonical_target_angular_speed_zero,
    nonfinite_unconstrained_predicted_angular_speed,
    nonfinite_maximum_torque,
    negative_maximum_torque,
    noncanonical_maximum_torque_zero,
    nonfinite_equivalent_inertia,
    nonpositive_equivalent_inertia,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_torque,
};

struct EngineSimV1StarterMotorInputError {
    EngineSimV1StarterMotorInputIssue issue =
        EngineSimV1StarterMotorInputIssue::nonfinite_derived_torque;

    friend bool operator==(const EngineSimV1StarterMotorInputError &,
                           const EngineSimV1StarterMotorInputError &) = default;
};

using EngineSimV1StarterMotorCalculation =
    std::variant<EngineSimV1StarterMotorTorque, EngineSimV1StarterMotorInputError>;

// The preconstraint speed is signed: external forces may predict reverse motion even
// though the committed crank state is constrained to nonnegative rotation. When
// enabled below target speed, returns
//
//   clamp(I * (target_omega - unconstrained_predicted_omega) / dt,
//         0,
//         maximum_torque)
//
// in that written binary64 order. Disabled, at-target, and overrunning states
// return canonical positive zero. Engagement and release remain caller-owned.
[[nodiscard]] EngineSimV1StarterMotorCalculation
calculate_engine_sim_v1_starter_motor_torque(
    const EngineSimV1StarterMotorInput &input) noexcept;

} // namespace engine_sim_offline::simulation
