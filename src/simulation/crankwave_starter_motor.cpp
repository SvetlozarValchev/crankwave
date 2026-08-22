#include "simulation/crankwave_starter_motor.hpp"

#include <cmath>

namespace crankwave::simulation {
namespace {

[[nodiscard]] CrankwaveStarterMotorInputError
input_error(CrankwaveStarterMotorInputIssue issue) noexcept {
    return {issue};
}

} // namespace

CrankwaveStarterMotorCalculation calculate_crankwave_starter_motor_torque(
    const CrankwaveStarterMotorInput &input) noexcept {
    using Issue = CrankwaveStarterMotorInputIssue;

    if (!std::isfinite(input.target_angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_target_angular_speed);
    }
    if (input.target_angular_speed_rad_s < 0.0) {
        return input_error(Issue::negative_target_angular_speed);
    }
    if (input.target_angular_speed_rad_s == 0.0 &&
        std::signbit(input.target_angular_speed_rad_s)) {
        return input_error(Issue::noncanonical_target_angular_speed_zero);
    }
    if (!std::isfinite(input.unconstrained_predicted_angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_unconstrained_predicted_angular_speed);
    }
    if (!std::isfinite(input.maximum_torque_nm)) {
        return input_error(Issue::nonfinite_maximum_torque);
    }
    if (input.maximum_torque_nm < 0.0) {
        return input_error(Issue::negative_maximum_torque);
    }
    if (input.maximum_torque_nm == 0.0 && std::signbit(input.maximum_torque_nm)) {
        return input_error(Issue::noncanonical_maximum_torque_zero);
    }
    if (!std::isfinite(input.equivalent_inertia_kg_m2)) {
        return input_error(Issue::nonfinite_equivalent_inertia);
    }
    if (!(input.equivalent_inertia_kg_m2 > 0.0)) {
        return input_error(Issue::nonpositive_equivalent_inertia);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(Issue::nonpositive_duration);
    }

    if (!input.enabled || input.unconstrained_predicted_angular_speed_rad_s >=
                              input.target_angular_speed_rad_s) {
        return CrankwaveStarterMotorTorque{input, 0.0, 0.0};
    }

    const double speed_error_rad_s = input.target_angular_speed_rad_s -
                                     input.unconstrained_predicted_angular_speed_rad_s;
    const double required_torque_nm =
        input.equivalent_inertia_kg_m2 * speed_error_rad_s / input.duration_s;
    if (!std::isfinite(required_torque_nm)) {
        return input_error(Issue::nonfinite_derived_torque);
    }

    const double applied_torque_nm = required_torque_nm < input.maximum_torque_nm
                                         ? required_torque_nm
                                         : input.maximum_torque_nm;
    return CrankwaveStarterMotorTorque{
        input,
        required_torque_nm,
        applied_torque_nm,
    };
}

} // namespace crankwave::simulation
