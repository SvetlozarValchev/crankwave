#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include <cmath>

namespace crankwave::simulation::detail {
namespace {

[[nodiscard]] PositiveSpeedRigidCrankZohInputError
input_error(PositiveSpeedRigidCrankZohInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] PositiveSpeedConfigurationDependentCrankZohInputError
configuration_input_error(
    PositiveSpeedConfigurationDependentCrankZohInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] NonnegativeSpeedConfigurationDependentCrankZohInputError
nonnegative_configuration_input_error(
    NonnegativeSpeedConfigurationDependentCrankZohInputIssue issue) noexcept {
    return {issue};
}

[[nodiscard]] NonnegativeSpeedConfigurationDependentCrankZohInputIssue
nonnegative_configuration_input_issue(
    PositiveSpeedConfigurationDependentCrankZohInputIssue issue) noexcept {
    using NonnegativeIssue = NonnegativeSpeedConfigurationDependentCrankZohInputIssue;
    using PositiveIssue = PositiveSpeedConfigurationDependentCrankZohInputIssue;
    switch (issue) {
    case PositiveIssue::nonfinite_instantaneous_inertia:
        return NonnegativeIssue::nonfinite_instantaneous_inertia;
    case PositiveIssue::nonpositive_instantaneous_inertia:
        return NonnegativeIssue::nonpositive_instantaneous_inertia;
    case PositiveIssue::nonfinite_inertia_derivative:
        return NonnegativeIssue::nonfinite_inertia_derivative;
    case PositiveIssue::nonfinite_theta:
        return NonnegativeIssue::nonfinite_theta;
    case PositiveIssue::nonfinite_angular_speed:
        return NonnegativeIssue::nonfinite_angular_speed;
    case PositiveIssue::nonpositive_angular_speed:
        return NonnegativeIssue::negative_angular_speed;
    case PositiveIssue::nonfinite_upstream_engine_torque:
        return NonnegativeIssue::nonfinite_upstream_engine_torque;
    case PositiveIssue::nonfinite_resisting_torque:
        return NonnegativeIssue::nonfinite_resisting_torque;
    case PositiveIssue::negative_resisting_torque:
        return NonnegativeIssue::negative_resisting_torque;
    case PositiveIssue::nonfinite_duration:
        return NonnegativeIssue::nonfinite_duration;
    case PositiveIssue::nonpositive_duration:
        return NonnegativeIssue::nonpositive_duration;
    case PositiveIssue::nonfinite_derived_value:
        return NonnegativeIssue::nonfinite_derived_value;
    }
    return NonnegativeIssue::nonfinite_derived_value;
}

} // namespace

PositiveSpeedRigidCrankZohCalculation advance_positive_speed_rigid_crank_zoh(
    const PositiveSpeedRigidCrankZohInput &input) noexcept {
    if (!std::isfinite(input.equivalent_inertia_kg_m2)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_equivalent_inertia);
    }
    if (!(input.equivalent_inertia_kg_m2 > 0.0)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonpositive_equivalent_inertia);
    }
    if (!std::isfinite(input.initial_state.theta_rad)) {
        return input_error(PositiveSpeedRigidCrankZohInputIssue::nonfinite_theta);
    }
    if (!std::isfinite(input.initial_state.angular_speed_rad_s)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_angular_speed);
    }
    if (!(input.initial_state.angular_speed_rad_s > 0.0)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonpositive_angular_speed);
    }
    if (!std::isfinite(input.held_upstream_engine_torque_nm)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_upstream_engine_torque);
    }
    if (!std::isfinite(input.held_resisting_torque_nm)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_resisting_torque);
    }
    if (input.held_resisting_torque_nm < 0.0) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::negative_resisting_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(PositiveSpeedRigidCrankZohInputIssue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(PositiveSpeedRigidCrankZohInputIssue::nonpositive_duration);
    }

    const double omega0 = input.initial_state.angular_speed_rad_s;
    const double net_torque_nm =
        input.held_upstream_engine_torque_nm - input.held_resisting_torque_nm;
    const double alpha_rad_s2 = net_torque_nm / input.equivalent_inertia_kg_m2;
    const double duration_squared_s2 = input.duration_s * input.duration_s;
    const double omega1 = omega0 + alpha_rad_s2 * input.duration_s;
    const double angular_displacement_rad =
        omega0 * input.duration_s + 0.5 * alpha_rad_s2 * duration_squared_s2;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;

    if (!std::isfinite(net_torque_nm) || !std::isfinite(alpha_rad_s2) ||
        !std::isfinite(duration_squared_s2) || !std::isfinite(omega1) ||
        !std::isfinite(angular_displacement_rad) || !std::isfinite(theta1)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_derived_value);
    }
    if (!(omega1 > 0.0)) {
        const double stall_time_s = -omega0 / alpha_rad_s2;
        const double stall_theta_rad = input.initial_state.theta_rad +
                                       omega0 * stall_time_s +
                                       0.5 * alpha_rad_s2 * stall_time_s * stall_time_s;
        if (!std::isfinite(stall_time_s) || !(stall_time_s > 0.0) ||
            stall_time_s > input.duration_s || !std::isfinite(stall_theta_rad)) {
            return input_error(
                PositiveSpeedRigidCrankZohInputIssue::nonfinite_derived_value);
        }
        return PositiveSpeedRigidCrankZohStall{
            input, net_torque_nm, alpha_rad_s2, omega1, stall_time_s, stall_theta_rad,
        };
    }

    const double kinetic_energy_change_j =
        0.5 * input.equivalent_inertia_kg_m2 * ((omega1 * omega1) - (omega0 * omega0));
    const double net_torque_work_j = net_torque_nm * angular_displacement_rad;
    const double energy_residual_j = kinetic_energy_change_j - net_torque_work_j;
    const double upstream_engine_torque_work_j =
        input.held_upstream_engine_torque_nm * angular_displacement_rad;
    const double resisting_torque_work_j =
        input.held_resisting_torque_nm * angular_displacement_rad;
    if (!std::isfinite(kinetic_energy_change_j) || !std::isfinite(net_torque_work_j) ||
        !std::isfinite(energy_residual_j) ||
        !std::isfinite(upstream_engine_torque_work_j) ||
        !std::isfinite(resisting_torque_work_j)) {
        return input_error(
            PositiveSpeedRigidCrankZohInputIssue::nonfinite_derived_value);
    }

    return PositiveSpeedRigidCrankZohStep{
        input,
        {theta1, omega1},
        net_torque_nm,
        alpha_rad_s2,
        angular_displacement_rad,
        upstream_engine_torque_work_j,
        resisting_torque_work_j,
        net_torque_work_j,
        kinetic_energy_change_j,
        energy_residual_j,
    };
}

PositiveSpeedConfigurationDependentCrankZohCalculation
advance_positive_speed_configuration_dependent_crank_zoh(
    const PositiveSpeedConfigurationDependentCrankZohInput &input) noexcept {
    using Issue = PositiveSpeedConfigurationDependentCrankZohInputIssue;
    if (!std::isfinite(input.instantaneous_inertia_kg_m2)) {
        return configuration_input_error(Issue::nonfinite_instantaneous_inertia);
    }
    if (!(input.instantaneous_inertia_kg_m2 > 0.0)) {
        return configuration_input_error(Issue::nonpositive_instantaneous_inertia);
    }
    if (!std::isfinite(input.inertia_derivative_kg_m2_per_rad)) {
        return configuration_input_error(Issue::nonfinite_inertia_derivative);
    }
    if (!std::isfinite(input.initial_state.theta_rad)) {
        return configuration_input_error(Issue::nonfinite_theta);
    }
    if (!std::isfinite(input.initial_state.angular_speed_rad_s)) {
        return configuration_input_error(Issue::nonfinite_angular_speed);
    }
    if (!(input.initial_state.angular_speed_rad_s > 0.0)) {
        return configuration_input_error(Issue::nonpositive_angular_speed);
    }
    if (!std::isfinite(input.held_upstream_engine_torque_nm)) {
        return configuration_input_error(Issue::nonfinite_upstream_engine_torque);
    }
    if (!std::isfinite(input.held_resisting_torque_nm)) {
        return configuration_input_error(Issue::nonfinite_resisting_torque);
    }
    if (input.held_resisting_torque_nm < 0.0) {
        return configuration_input_error(Issue::negative_resisting_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return configuration_input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return configuration_input_error(Issue::nonpositive_duration);
    }

    const double omega0 = input.initial_state.angular_speed_rad_s;
    const double omega_squared = omega0 * omega0;
    const double held_applied_net_torque_nm =
        input.held_upstream_engine_torque_nm - input.held_resisting_torque_nm;
    const double velocity_inertia_torque_nm =
        0.5 * input.inertia_derivative_kg_m2_per_rad * omega_squared;
    const double effective_accelerating_torque_nm =
        held_applied_net_torque_nm - velocity_inertia_torque_nm;
    const double alpha_rad_s2 =
        effective_accelerating_torque_nm / input.instantaneous_inertia_kg_m2;
    const double omega1 = omega0 + alpha_rad_s2 * input.duration_s;
    const double angular_displacement_rad = omega1 * input.duration_s;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;

    if (!std::isfinite(omega_squared) || !std::isfinite(held_applied_net_torque_nm) ||
        !std::isfinite(velocity_inertia_torque_nm) ||
        !std::isfinite(effective_accelerating_torque_nm) ||
        !std::isfinite(alpha_rad_s2) || !std::isfinite(omega1) ||
        !std::isfinite(angular_displacement_rad) || !std::isfinite(theta1)) {
        return configuration_input_error(Issue::nonfinite_derived_value);
    }
    if (!(omega1 > 0.0)) {
        const double stall_time_s = -omega0 / alpha_rad_s2;
        const double stall_theta_rad = input.initial_state.theta_rad +
                                       omega0 * stall_time_s +
                                       0.5 * alpha_rad_s2 * stall_time_s * stall_time_s;
        if (!std::isfinite(stall_time_s) || !(stall_time_s > 0.0) ||
            stall_time_s > input.duration_s || !std::isfinite(stall_theta_rad)) {
            return configuration_input_error(Issue::nonfinite_derived_value);
        }
        return PositiveSpeedConfigurationDependentCrankZohStall{
            input,
            held_applied_net_torque_nm,
            velocity_inertia_torque_nm,
            effective_accelerating_torque_nm,
            alpha_rad_s2,
            omega1,
            stall_time_s,
            stall_theta_rad,
        };
    }

    return PositiveSpeedConfigurationDependentCrankZohStep{
        input,
        {theta1, omega1},
        held_applied_net_torque_nm,
        velocity_inertia_torque_nm,
        effective_accelerating_torque_nm,
        alpha_rad_s2,
        angular_displacement_rad,
    };
}

NonnegativeSpeedConfigurationDependentCrankZohCalculation
advance_nonnegative_speed_configuration_dependent_crank_zoh(
    const NonnegativeSpeedConfigurationDependentCrankZohInput &input) noexcept {
    using Disposition = NonnegativeSpeedConfigurationDependentCrankZohDisposition;
    using Issue = NonnegativeSpeedConfigurationDependentCrankZohInputIssue;

    const auto positive_calculation =
        advance_positive_speed_configuration_dependent_crank_zoh(input);
    if (const auto *step = std::get_if<PositiveSpeedConfigurationDependentCrankZohStep>(
            &positive_calculation)) {
        return NonnegativeSpeedConfigurationDependentCrankZohStep{
            step->input,
            step->final_state,
            Disposition::advanced,
            step->held_applied_net_torque_nm,
            step->velocity_inertia_torque_nm,
            step->effective_accelerating_torque_nm,
            step->angular_acceleration_rad_s2,
            step->angular_displacement_rad,
            step->final_state.angular_speed_rad_s,
            0.0,
            0.0,
        };
    }
    if (const auto *stall =
            std::get_if<PositiveSpeedConfigurationDependentCrankZohStall>(
                &positive_calculation)) {
        const double angular_displacement_rad =
            stall->input.initial_state.angular_speed_rad_s * stall->stall_time_s +
            0.5 * stall->angular_acceleration_rad_s2 * stall->stall_time_s *
                stall->stall_time_s;
        if (!std::isfinite(angular_displacement_rad)) {
            return nonnegative_configuration_input_error(
                Issue::nonfinite_derived_value);
        }
        return NonnegativeSpeedConfigurationDependentCrankZohStep{
            stall->input,
            {stall->stall_theta_rad, 0.0},
            Disposition::stopped,
            stall->held_applied_net_torque_nm,
            stall->velocity_inertia_torque_nm,
            stall->effective_accelerating_torque_nm,
            stall->angular_acceleration_rad_s2,
            angular_displacement_rad,
            stall->predicted_final_angular_speed_rad_s,
            stall->stall_time_s,
            stall->stall_theta_rad,
        };
    }

    const auto &positive_error =
        std::get<PositiveSpeedConfigurationDependentCrankZohInputError>(
            positive_calculation);
    using PositiveIssue = PositiveSpeedConfigurationDependentCrankZohInputIssue;
    if (positive_error.issue != PositiveIssue::nonpositive_angular_speed) {
        return nonnegative_configuration_input_error(
            nonnegative_configuration_input_issue(positive_error.issue));
    }

    const double omega0 = input.initial_state.angular_speed_rad_s;
    if (omega0 == 0.0 && std::signbit(omega0)) {
        return nonnegative_configuration_input_error(
            Issue::noncanonical_angular_speed_zero);
    }
    if (omega0 < 0.0) {
        return nonnegative_configuration_input_error(Issue::negative_angular_speed);
    }

    if (!std::isfinite(input.held_upstream_engine_torque_nm)) {
        return nonnegative_configuration_input_error(
            Issue::nonfinite_upstream_engine_torque);
    }
    if (!std::isfinite(input.held_resisting_torque_nm)) {
        return nonnegative_configuration_input_error(Issue::nonfinite_resisting_torque);
    }
    if (input.held_resisting_torque_nm < 0.0) {
        return nonnegative_configuration_input_error(Issue::negative_resisting_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return nonnegative_configuration_input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return nonnegative_configuration_input_error(Issue::nonpositive_duration);
    }

    const double omega_squared = omega0 * omega0;
    const double held_applied_net_torque_nm =
        input.held_upstream_engine_torque_nm - input.held_resisting_torque_nm;
    const double velocity_inertia_torque_nm =
        0.5 * input.inertia_derivative_kg_m2_per_rad * omega_squared;
    const double effective_accelerating_torque_nm =
        held_applied_net_torque_nm - velocity_inertia_torque_nm;
    const double unconstrained_alpha_rad_s2 =
        effective_accelerating_torque_nm / input.instantaneous_inertia_kg_m2;
    const double unconstrained_omega1 =
        omega0 + unconstrained_alpha_rad_s2 * input.duration_s;
    if (!std::isfinite(omega_squared) || !std::isfinite(held_applied_net_torque_nm) ||
        !std::isfinite(velocity_inertia_torque_nm) ||
        !std::isfinite(effective_accelerating_torque_nm) ||
        !std::isfinite(unconstrained_alpha_rad_s2) ||
        !std::isfinite(unconstrained_omega1)) {
        return nonnegative_configuration_input_error(Issue::nonfinite_derived_value);
    }

    if (!(effective_accelerating_torque_nm > 0.0)) {
        return NonnegativeSpeedConfigurationDependentCrankZohStep{
            input,
            {input.initial_state.theta_rad, 0.0},
            Disposition::held_at_rest,
            held_applied_net_torque_nm,
            velocity_inertia_torque_nm,
            effective_accelerating_torque_nm,
            0.0,
            0.0,
            unconstrained_omega1,
            0.0,
            0.0,
        };
    }

    const double angular_displacement_rad = unconstrained_omega1 * input.duration_s;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;
    if (!std::isfinite(angular_displacement_rad) || !std::isfinite(theta1)) {
        return nonnegative_configuration_input_error(Issue::nonfinite_derived_value);
    }
    return NonnegativeSpeedConfigurationDependentCrankZohStep{
        input,
        {theta1, unconstrained_omega1},
        Disposition::advanced,
        held_applied_net_torque_nm,
        velocity_inertia_torque_nm,
        effective_accelerating_torque_nm,
        unconstrained_alpha_rad_s2,
        angular_displacement_rad,
        unconstrained_omega1,
        0.0,
        0.0,
    };
}

} // namespace crankwave::simulation::detail
