#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include <cmath>

namespace engine_sim_offline::simulation::detail {
namespace {

[[nodiscard]] PositiveSpeedRigidCrankZohInputError
input_error(PositiveSpeedRigidCrankZohInputIssue issue) noexcept {
    return {issue};
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

} // namespace engine_sim_offline::simulation::detail
