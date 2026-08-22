#include "simulation/inertial_crank_dynamics.hpp"

#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace crankwave::simulation {
namespace {

[[nodiscard]] InertialCrankDynamicsError configuration_error(
    InertialCrankConfigurationIssue issue,
    std::size_t brake_point_index = kNoInertialCrankBrakePoint) noexcept {
    InertialCrankDynamicsError error;
    error.code = InertialCrankDynamicsErrorCode::invalid_configuration;
    error.configuration_issue = issue;
    error.brake_point_index = brake_point_index;
    return error;
}

[[nodiscard]] InertialCrankDynamicsError
input_error(InertialCrankInputIssue issue) noexcept {
    InertialCrankDynamicsError error;
    error.code = InertialCrankDynamicsErrorCode::invalid_input;
    error.input_issue = issue;
    return error;
}

[[nodiscard]] InertialCrankDynamicsError
domain_error(InertialCrankDomainIssue issue, double angular_speed_rad_s) noexcept {
    InertialCrankDynamicsError error;
    error.code = InertialCrankDynamicsErrorCode::brake_curve_out_of_domain;
    error.domain_issue = issue;
    error.angular_speed_rad_s = angular_speed_rad_s;
    return error;
}

[[nodiscard]] InertialCrankDynamicsError
stall_error(const detail::PositiveSpeedRigidCrankZohStall &stall) noexcept {
    InertialCrankDynamicsError error;
    error.code = InertialCrankDynamicsErrorCode::stall;
    error.angular_speed_rad_s = stall.predicted_final_angular_speed_rad_s;
    error.stall_time_s = stall.stall_time_s;
    error.stall_theta_rad = stall.stall_theta_rad;
    return error;
}

[[nodiscard]] double
interpolate_brake_torque(const std::vector<InertialCrankBrakePoint> &curve,
                         double angular_speed_rad_s) noexcept {
    if (angular_speed_rad_s == curve.back().angular_speed_rad_s) {
        return curve.back().resisting_torque_nm;
    }

    const auto upper =
        std::upper_bound(curve.begin(), curve.end(), angular_speed_rad_s,
                         [](double speed, const InertialCrankBrakePoint &point) {
                             return speed < point.angular_speed_rad_s;
                         });
    const auto lower = upper - 1;
    const double fraction = (angular_speed_rad_s - lower->angular_speed_rad_s) /
                            (upper->angular_speed_rad_s - lower->angular_speed_rad_s);
    return lower->resisting_torque_nm +
           (upper->resisting_torque_nm - lower->resisting_torque_nm) * fraction;
}

} // namespace

InertialCrankDynamics::InertialCrankDynamics(
    InertialCrankDynamicsConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

double InertialCrankDynamics::equivalent_inertia_kg_m2() const noexcept {
    return configuration_.equivalent_inertia_kg_m2;
}

const std::vector<InertialCrankBrakePoint> &
InertialCrankDynamics::passive_brake_curve() const noexcept {
    return configuration_.passive_brake_curve;
}

InertialCrankStepCalculation
InertialCrankDynamics::advance(const InertialCrankStepInput &input) const noexcept {
    if (!std::isfinite(input.initial_state.theta_rad)) {
        return input_error(InertialCrankInputIssue::nonfinite_theta);
    }
    if (!std::isfinite(input.initial_state.angular_speed_rad_s)) {
        return input_error(InertialCrankInputIssue::nonfinite_angular_speed);
    }
    if (!(input.initial_state.angular_speed_rad_s > 0.0)) {
        return input_error(InertialCrankInputIssue::nonpositive_angular_speed);
    }
    if (!std::isfinite(input.held_total_crank_torque_nm)) {
        return input_error(InertialCrankInputIssue::nonfinite_total_crank_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(InertialCrankInputIssue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(InertialCrankInputIssue::nonpositive_duration);
    }

    const auto &curve = configuration_.passive_brake_curve;
    const double omega0 = input.initial_state.angular_speed_rad_s;
    if (omega0 < curve.front().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::initial_speed_below_curve,
                            omega0);
    }
    if (omega0 > curve.back().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::initial_speed_above_curve,
                            omega0);
    }

    const double brake_torque_nm = interpolate_brake_torque(curve, omega0);
    const auto step_calculation = detail::advance_positive_speed_rigid_crank_zoh({
        configuration_.equivalent_inertia_kg_m2,
        {input.initial_state.theta_rad, input.initial_state.angular_speed_rad_s},
        input.held_total_crank_torque_nm,
        brake_torque_nm,
        input.duration_s,
    });
    if (std::holds_alternative<detail::PositiveSpeedRigidCrankZohInputError>(
            step_calculation)) {
        return input_error(InertialCrankInputIssue::nonfinite_derived_value);
    }
    if (const auto *stall =
            std::get_if<detail::PositiveSpeedRigidCrankZohStall>(&step_calculation)) {
        return stall_error(*stall);
    }
    const auto &step =
        std::get<detail::PositiveSpeedRigidCrankZohStep>(step_calculation);
    const double omega1 = step.final_state.angular_speed_rad_s;
    if (omega1 < curve.front().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::final_speed_below_curve, omega1);
    }
    if (omega1 > curve.back().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::final_speed_above_curve, omega1);
    }

    return InertialCrankStepResult{
        input.initial_state,
        {step.final_state.theta_rad, step.final_state.angular_speed_rad_s},
        input.held_total_crank_torque_nm,
        brake_torque_nm,
        step.held_net_torque_nm,
        step.angular_acceleration_rad_s2,
        step.angular_displacement_rad,
        step.kinetic_energy_change_j,
        step.held_net_torque_work_j,
        step.energy_residual_j,
    };
}

InertialCrankDynamicsCompilation
compile_inertial_crank_dynamics(InertialCrankDynamicsConfiguration configuration) {
    if (!std::isfinite(configuration.equivalent_inertia_kg_m2)) {
        return configuration_error(
            InertialCrankConfigurationIssue::nonfinite_equivalent_inertia);
    }
    if (!(configuration.equivalent_inertia_kg_m2 > 0.0)) {
        return configuration_error(
            InertialCrankConfigurationIssue::nonpositive_equivalent_inertia);
    }
    if (configuration.passive_brake_curve.size() < 2U) {
        return configuration_error(
            InertialCrankConfigurationIssue::insufficient_brake_curve_points);
    }

    for (std::size_t index = 0; index < configuration.passive_brake_curve.size();
         ++index) {
        const auto &point = configuration.passive_brake_curve[index];
        if (!std::isfinite(point.angular_speed_rad_s)) {
            return configuration_error(
                InertialCrankConfigurationIssue::nonfinite_brake_speed, index);
        }
        if (point.angular_speed_rad_s < 0.0) {
            return configuration_error(
                InertialCrankConfigurationIssue::negative_brake_speed, index);
        }
        if (!std::isfinite(point.resisting_torque_nm)) {
            return configuration_error(
                InertialCrankConfigurationIssue::nonfinite_brake_torque, index);
        }
        if (point.resisting_torque_nm < 0.0) {
            return configuration_error(
                InertialCrankConfigurationIssue::negative_brake_torque, index);
        }
        if (index != 0U &&
            !(point.angular_speed_rad_s >
              configuration.passive_brake_curve[index - 1U].angular_speed_rad_s)) {
            return configuration_error(
                InertialCrankConfigurationIssue::unstable_brake_speed_order, index);
        }
    }

    return InertialCrankDynamics{std::move(configuration)};
}

} // namespace crankwave::simulation
