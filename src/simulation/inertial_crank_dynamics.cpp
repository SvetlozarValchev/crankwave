#include "simulation/inertial_crank_dynamics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine_sim_offline::simulation {
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

[[nodiscard]] InertialCrankDynamicsError stall_error(
    double predicted_angular_speed_rad_s, const InertialCrankStepInput &input,
    double angular_acceleration_rad_s2) noexcept {
    InertialCrankDynamicsError error;
    error.code = InertialCrankDynamicsErrorCode::stall;
    error.angular_speed_rad_s = predicted_angular_speed_rad_s;
    error.stall_time_s =
        -input.initial_state.angular_speed_rad_s / angular_acceleration_rad_s2;
    error.stall_theta_rad =
        input.initial_state.theta_rad +
        input.initial_state.angular_speed_rad_s * error.stall_time_s +
        0.5 * angular_acceleration_rad_s2 * error.stall_time_s *
            error.stall_time_s;
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
    const double net_torque_nm = input.held_total_crank_torque_nm - brake_torque_nm;
    const double alpha_rad_s2 = net_torque_nm / configuration_.equivalent_inertia_kg_m2;
    const double duration_squared_s2 = input.duration_s * input.duration_s;
    const double omega1 = omega0 + alpha_rad_s2 * input.duration_s;
    const double angular_displacement_rad =
        omega0 * input.duration_s + 0.5 * alpha_rad_s2 * duration_squared_s2;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;

    if (!std::isfinite(brake_torque_nm) || !std::isfinite(net_torque_nm) ||
        !std::isfinite(alpha_rad_s2) || !std::isfinite(duration_squared_s2) ||
        !std::isfinite(omega1) || !std::isfinite(angular_displacement_rad) ||
        !std::isfinite(theta1)) {
        return input_error(InertialCrankInputIssue::nonfinite_derived_value);
    }
    if (!(omega1 > 0.0)) {
        return stall_error(omega1, input, alpha_rad_s2);
    }
    if (omega1 < curve.front().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::final_speed_below_curve, omega1);
    }
    if (omega1 > curve.back().angular_speed_rad_s) {
        return domain_error(InertialCrankDomainIssue::final_speed_above_curve, omega1);
    }

    const double kinetic_energy_change_j = 0.5 *
                                           configuration_.equivalent_inertia_kg_m2 *
                                           ((omega1 * omega1) - (omega0 * omega0));
    const double net_torque_work_j = net_torque_nm * angular_displacement_rad;
    const double energy_residual_j = kinetic_energy_change_j - net_torque_work_j;
    if (!std::isfinite(kinetic_energy_change_j) || !std::isfinite(net_torque_work_j) ||
        !std::isfinite(energy_residual_j)) {
        return input_error(InertialCrankInputIssue::nonfinite_derived_value);
    }

    return InertialCrankStepResult{
        input.initial_state,
        {theta1, omega1},
        input.held_total_crank_torque_nm,
        brake_torque_nm,
        net_torque_nm,
        alpha_rad_s2,
        angular_displacement_rad,
        kinetic_energy_change_j,
        net_torque_work_j,
        energy_residual_j,
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

} // namespace engine_sim_offline::simulation
