#pragma once

#include <cstdint>
#include <variant>

namespace crankwave::simulation {

// The pristine starter is a one-way target-speed constraint. This primitive
// expresses its isolated-crank equivalent in the runtime's positive-forward
// convention.
struct CrankwaveStarterMotorInput {
    bool enabled = false;
    double target_angular_speed_rad_s = 0.0;
    double unconstrained_predicted_angular_speed_rad_s = 0.0;
    double maximum_torque_nm = 0.0;
    double equivalent_inertia_kg_m2 = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const CrankwaveStarterMotorInput &,
                           const CrankwaveStarterMotorInput &) = default;
};

struct CrankwaveStarterMotorTorque {
    CrankwaveStarterMotorInput input;
    double required_isolated_crank_torque_nm = 0.0;
    double applied_crank_torque_nm = 0.0;

    friend bool operator==(const CrankwaveStarterMotorTorque &,
                           const CrankwaveStarterMotorTorque &) = default;
};

enum class CrankwaveStarterMotorInputIssue : std::uint8_t {
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

struct CrankwaveStarterMotorInputError {
    CrankwaveStarterMotorInputIssue issue =
        CrankwaveStarterMotorInputIssue::nonfinite_derived_torque;

    friend bool operator==(const CrankwaveStarterMotorInputError &,
                           const CrankwaveStarterMotorInputError &) = default;
};

using CrankwaveStarterMotorCalculation =
    std::variant<CrankwaveStarterMotorTorque, CrankwaveStarterMotorInputError>;

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
[[nodiscard]] CrankwaveStarterMotorCalculation
calculate_crankwave_starter_motor_torque(
    const CrankwaveStarterMotorInput &input) noexcept;

} // namespace crankwave::simulation
