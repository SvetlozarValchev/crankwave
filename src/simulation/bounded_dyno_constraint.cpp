#include "simulation/bounded_dyno_constraint.hpp"

#include <algorithm>
#include <cmath>

namespace engine_sim_offline::simulation::detail {
namespace {

[[nodiscard]] BoundedDynoConstraintInputError
input_error(BoundedDynoConstraintInputIssue issue) noexcept {
    return {issue};
}

} // namespace

BoundedDynoConstraintCalculation
advance_bounded_dyno_constraint(const BoundedDynoConstraintInput &input) noexcept {
    using Issue = BoundedDynoConstraintInputIssue;
    if (!std::isfinite(input.instantaneous_inertia_kg_m2)) {
        return input_error(Issue::nonfinite_instantaneous_inertia);
    }
    if (!(input.instantaneous_inertia_kg_m2 > 0.0)) {
        return input_error(Issue::nonpositive_instantaneous_inertia);
    }
    if (!std::isfinite(input.inertia_derivative_kg_m2_per_rad)) {
        return input_error(Issue::nonfinite_inertia_derivative);
    }
    if (!std::isfinite(input.initial_state.theta_rad)) {
        return input_error(Issue::nonfinite_theta);
    }
    if (!std::isfinite(input.initial_state.angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_initial_angular_speed);
    }
    if (!(input.initial_state.angular_speed_rad_s > 0.0)) {
        return input_error(Issue::nonpositive_initial_angular_speed);
    }
    if (!std::isfinite(input.held_upstream_engine_torque_nm)) {
        return input_error(Issue::nonfinite_upstream_engine_torque);
    }
    if (!std::isfinite(input.target_angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_target_angular_speed);
    }
    if (input.target_angular_speed_rad_s < 0.0) {
        return input_error(Issue::negative_target_angular_speed);
    }
    if (!std::isfinite(input.maximum_absorbing_torque_nm)) {
        return input_error(Issue::nonfinite_maximum_absorbing_torque);
    }
    if (input.maximum_absorbing_torque_nm < 0.0) {
        return input_error(Issue::negative_maximum_absorbing_torque);
    }
    if (!std::isfinite(input.maximum_driving_torque_nm)) {
        return input_error(Issue::nonfinite_maximum_driving_torque);
    }
    if (input.maximum_driving_torque_nm < 0.0) {
        return input_error(Issue::negative_maximum_driving_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(Issue::nonpositive_duration);
    }

    const double omega0 = input.initial_state.angular_speed_rad_s;
    const double velocity_inertia_torque_nm =
        0.5 * input.inertia_derivative_kg_m2_per_rad * omega0 * omega0;
    const double required_acceleration_rad_s2 =
        (input.target_angular_speed_rad_s - omega0) / input.duration_s;
    const double required_actuator_torque_nm =
        input.instantaneous_inertia_kg_m2 * required_acceleration_rad_s2 +
        velocity_inertia_torque_nm - input.held_upstream_engine_torque_nm;
    const double applied_actuator_torque_nm =
        std::clamp(required_actuator_torque_nm, -input.maximum_absorbing_torque_nm,
                   input.maximum_driving_torque_nm);
    const double angular_acceleration_rad_s2 =
        (input.held_upstream_engine_torque_nm + applied_actuator_torque_nm -
         velocity_inertia_torque_nm) /
        input.instantaneous_inertia_kg_m2;
    const double omega1 = omega0 + angular_acceleration_rad_s2 * input.duration_s;
    const double angular_displacement_rad = omega1 * input.duration_s;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;

    if (!std::isfinite(velocity_inertia_torque_nm) ||
        !std::isfinite(required_acceleration_rad_s2) ||
        !std::isfinite(required_actuator_torque_nm) ||
        !std::isfinite(applied_actuator_torque_nm) ||
        !std::isfinite(angular_acceleration_rad_s2) || !std::isfinite(omega1) ||
        !std::isfinite(angular_displacement_rad) || !std::isfinite(theta1)) {
        return input_error(Issue::nonfinite_derived_value);
    }

    if (!(omega1 > 0.0)) {
        const double stall_time_s = -omega0 / angular_acceleration_rad_s2;
        const double stall_theta_rad =
            input.initial_state.theta_rad + omega0 * stall_time_s +
            0.5 * angular_acceleration_rad_s2 * stall_time_s * stall_time_s;
        if (!std::isfinite(stall_time_s) || !(stall_time_s > 0.0) ||
            stall_time_s > input.duration_s || !std::isfinite(stall_theta_rad)) {
            return input_error(Issue::nonfinite_derived_value);
        }
        return BoundedDynoConstraintStall{
            input,
            required_actuator_torque_nm,
            applied_actuator_torque_nm,
            velocity_inertia_torque_nm,
            angular_acceleration_rad_s2,
            omega1,
            stall_time_s,
            stall_theta_rad,
        };
    }

    BoundedDynoConstraintDisposition disposition =
        BoundedDynoConstraintDisposition::tracking;
    if (applied_actuator_torque_nm > required_actuator_torque_nm) {
        disposition = BoundedDynoConstraintDisposition::absorbing_torque_limited;
    } else if (applied_actuator_torque_nm < required_actuator_torque_nm) {
        disposition = BoundedDynoConstraintDisposition::driving_torque_limited;
    }

    return BoundedDynoConstraintStep{
        input,
        {theta1, omega1},
        disposition,
        required_actuator_torque_nm,
        applied_actuator_torque_nm,
        velocity_inertia_torque_nm,
        angular_acceleration_rad_s2,
        angular_displacement_rad,
    };
}

} // namespace engine_sim_offline::simulation::detail
