#pragma once

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation::detail {

struct PositiveSpeedRigidCrankState {
    double theta_rad = 0.0;
    double angular_speed_rad_s = 0.0;

    friend bool operator==(const PositiveSpeedRigidCrankState &,
                           const PositiveSpeedRigidCrankState &) = default;
};

// Both torques are zero-order held for duration_s. The resisting torque is an
// explicit nonnegative magnitude acting opposite positive crank rotation.
struct PositiveSpeedRigidCrankZohInput {
    double equivalent_inertia_kg_m2 = 0.0;
    PositiveSpeedRigidCrankState initial_state;
    double held_upstream_engine_torque_nm = 0.0;
    double held_resisting_torque_nm = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const PositiveSpeedRigidCrankZohInput &,
                           const PositiveSpeedRigidCrankZohInput &) = default;
};

struct PositiveSpeedRigidCrankZohStep {
    PositiveSpeedRigidCrankZohInput input;
    PositiveSpeedRigidCrankState final_state;
    double held_net_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double angular_displacement_rad = 0.0;
    double upstream_engine_torque_work_j = 0.0;
    // Positive absorbed-work magnitude for positive crank displacement.
    double resisting_torque_work_j = 0.0;
    double held_net_torque_work_j = 0.0;
    double kinetic_energy_change_j = 0.0;
    // kinetic_energy_change_j - held_net_torque_work_j.
    double energy_residual_j = 0.0;

    friend bool operator==(const PositiveSpeedRigidCrankZohStep &,
                           const PositiveSpeedRigidCrankZohStep &) = default;
};

enum class PositiveSpeedRigidCrankZohInputIssue : std::uint8_t {
    nonfinite_equivalent_inertia,
    nonpositive_equivalent_inertia,
    nonfinite_theta,
    nonfinite_angular_speed,
    nonpositive_angular_speed,
    nonfinite_upstream_engine_torque,
    nonfinite_resisting_torque,
    negative_resisting_torque,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct PositiveSpeedRigidCrankZohInputError {
    PositiveSpeedRigidCrankZohInputIssue issue =
        PositiveSpeedRigidCrankZohInputIssue::nonfinite_derived_value;

    friend bool operator==(const PositiveSpeedRigidCrankZohInputError &,
                           const PositiveSpeedRigidCrankZohInputError &) = default;
};

// A step that reaches zero or reverse speed is not published as a completed
// positive-speed state. This result identifies the exact constant-acceleration
// stopping point inside the rejected step.
struct PositiveSpeedRigidCrankZohStall {
    PositiveSpeedRigidCrankZohInput input;
    double held_net_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double predicted_final_angular_speed_rad_s = 0.0;
    double stall_time_s = 0.0;
    double stall_theta_rad = 0.0;

    friend bool operator==(const PositiveSpeedRigidCrankZohStall &,
                           const PositiveSpeedRigidCrankZohStall &) = default;
};

using PositiveSpeedRigidCrankZohCalculation =
    std::variant<PositiveSpeedRigidCrankZohStep, PositiveSpeedRigidCrankZohInputError,
                 PositiveSpeedRigidCrankZohStall>;

[[nodiscard]] PositiveSpeedRigidCrankZohCalculation
advance_positive_speed_rigid_crank_zoh(
    const PositiveSpeedRigidCrankZohInput &input) noexcept;

struct PositiveSpeedConfigurationDependentCrankZohInput {
    double instantaneous_inertia_kg_m2 = 0.0;
    double inertia_derivative_kg_m2_per_rad = 0.0;
    PositiveSpeedRigidCrankState initial_state;
    double held_upstream_engine_torque_nm = 0.0;
    double held_resisting_torque_nm = 0.0;
    double duration_s = 0.0;

    friend bool
    operator==(const PositiveSpeedConfigurationDependentCrankZohInput &,
               const PositiveSpeedConfigurationDependentCrankZohInput &) = default;
};

struct PositiveSpeedConfigurationDependentCrankZohStep {
    PositiveSpeedConfigurationDependentCrankZohInput input;
    PositiveSpeedRigidCrankState final_state;
    double held_applied_net_torque_nm = 0.0;
    double velocity_inertia_torque_nm = 0.0;
    double effective_accelerating_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double angular_displacement_rad = 0.0;

    friend bool
    operator==(const PositiveSpeedConfigurationDependentCrankZohStep &,
               const PositiveSpeedConfigurationDependentCrankZohStep &) = default;
};

enum class PositiveSpeedConfigurationDependentCrankZohInputIssue : std::uint8_t {
    nonfinite_instantaneous_inertia,
    nonpositive_instantaneous_inertia,
    nonfinite_inertia_derivative,
    nonfinite_theta,
    nonfinite_angular_speed,
    nonpositive_angular_speed,
    nonfinite_upstream_engine_torque,
    nonfinite_resisting_torque,
    negative_resisting_torque,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct PositiveSpeedConfigurationDependentCrankZohInputError {
    PositiveSpeedConfigurationDependentCrankZohInputIssue issue =
        PositiveSpeedConfigurationDependentCrankZohInputIssue::nonfinite_derived_value;

    friend bool
    operator==(const PositiveSpeedConfigurationDependentCrankZohInputError &,
               const PositiveSpeedConfigurationDependentCrankZohInputError &) = default;
};

struct PositiveSpeedConfigurationDependentCrankZohStall {
    PositiveSpeedConfigurationDependentCrankZohInput input;
    double held_applied_net_torque_nm = 0.0;
    double velocity_inertia_torque_nm = 0.0;
    double effective_accelerating_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double predicted_final_angular_speed_rad_s = 0.0;
    double stall_time_s = 0.0;
    double stall_theta_rad = 0.0;

    friend bool
    operator==(const PositiveSpeedConfigurationDependentCrankZohStall &,
               const PositiveSpeedConfigurationDependentCrankZohStall &) = default;
};

using PositiveSpeedConfigurationDependentCrankZohCalculation =
    std::variant<PositiveSpeedConfigurationDependentCrankZohStep,
                 PositiveSpeedConfigurationDependentCrankZohInputError,
                 PositiveSpeedConfigurationDependentCrankZohStall>;

// Advances a left-boundary reduced rigid mechanism under:
//
//   Q = M(theta) * alpha + 0.5 * dM/dtheta * omega^2
//
// M and dM/dtheta must describe the same admitted mechanism and left boundary.
[[nodiscard]] PositiveSpeedConfigurationDependentCrankZohCalculation
advance_positive_speed_configuration_dependent_crank_zoh(
    const PositiveSpeedConfigurationDependentCrankZohInput &input) noexcept;

using NonnegativeSpeedConfigurationDependentCrankZohInput =
    PositiveSpeedConfigurationDependentCrankZohInput;

enum class NonnegativeSpeedConfigurationDependentCrankZohDisposition : std::uint8_t {
    advanced,
    stopped,
    held_at_rest,
};

// A committed no-reverse step. Positive-speed advances retain the exact field
// arithmetic of PositiveSpeedConfigurationDependentCrankZohStep. A stopped step
// commits the positive-speed primitive's exact stall point, while held_at_rest
// exposes the unconstrained torque prediction but commits canonical positive-zero
// acceleration, displacement, and speed.
struct NonnegativeSpeedConfigurationDependentCrankZohStep {
    NonnegativeSpeedConfigurationDependentCrankZohInput input;
    PositiveSpeedRigidCrankState final_state;
    NonnegativeSpeedConfigurationDependentCrankZohDisposition disposition =
        NonnegativeSpeedConfigurationDependentCrankZohDisposition::held_at_rest;
    double held_applied_net_torque_nm = 0.0;
    double velocity_inertia_torque_nm = 0.0;
    double effective_accelerating_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double angular_displacement_rad = 0.0;
    double unconstrained_predicted_final_angular_speed_rad_s = 0.0;
    double stall_time_s = 0.0;
    double stall_theta_rad = 0.0;

    friend bool
    operator==(const NonnegativeSpeedConfigurationDependentCrankZohStep &,
               const NonnegativeSpeedConfigurationDependentCrankZohStep &) = default;
};

enum class NonnegativeSpeedConfigurationDependentCrankZohInputIssue : std::uint8_t {
    nonfinite_instantaneous_inertia,
    nonpositive_instantaneous_inertia,
    nonfinite_inertia_derivative,
    nonfinite_theta,
    nonfinite_angular_speed,
    negative_angular_speed,
    noncanonical_angular_speed_zero,
    nonfinite_upstream_engine_torque,
    nonfinite_resisting_torque,
    negative_resisting_torque,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

struct NonnegativeSpeedConfigurationDependentCrankZohInputError {
    NonnegativeSpeedConfigurationDependentCrankZohInputIssue issue =
        NonnegativeSpeedConfigurationDependentCrankZohInputIssue::
            nonfinite_derived_value;

    friend bool operator==(
        const NonnegativeSpeedConfigurationDependentCrankZohInputError &,
        const NonnegativeSpeedConfigurationDependentCrankZohInputError &) = default;
};

using NonnegativeSpeedConfigurationDependentCrankZohCalculation =
    std::variant<NonnegativeSpeedConfigurationDependentCrankZohStep,
                 NonnegativeSpeedConfigurationDependentCrankZohInputError>;

// Advances the same left-boundary mechanism without admitting reverse motion.
// Canonical positive zero is an admissible initial speed; negative zero is not.
[[nodiscard]] NonnegativeSpeedConfigurationDependentCrankZohCalculation
advance_nonnegative_speed_configuration_dependent_crank_zoh(
    const NonnegativeSpeedConfigurationDependentCrankZohInput &input) noexcept;

} // namespace engine_sim_offline::simulation::detail
