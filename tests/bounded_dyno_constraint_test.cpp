#include "simulation/bounded_dyno_constraint.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

namespace detail = engine_sim_offline::simulation::detail;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] const detail::BoundedDynoConstraintStep &
require_step(const detail::BoundedDynoConstraintCalculation &calculation,
             std::string_view message) {
    const auto *step = std::get_if<detail::BoundedDynoConstraintStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

void test_constraint_tracks_target_with_absorbing_or_driving_torque() {
    detail::BoundedDynoConstraintInput input{
        2.0, 0.0, {0.5, 100.0}, 200.0, 100.0, 300.0, 100.0, 0.01,
    };
    auto calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &absorbing = require_step(calculation, "absorbing hold was rejected");
    expect(absorbing.disposition == detail::BoundedDynoConstraintDisposition::tracking,
           "available absorbing torque did not track");
    expect_near(absorbing.required_actuator_torque_nm, -200.0, 0.0,
                "absorbing hold required wrong actuator torque");
    expect_near(absorbing.applied_actuator_torque_nm, -200.0, 0.0,
                "absorbing hold applied wrong actuator torque");
    expect_near(absorbing.final_state.angular_speed_rad_s, 100.0, 0.0,
                "absorbing hold missed its target");

    input.held_upstream_engine_torque_nm = -50.0;
    calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &driving = require_step(calculation, "driving hold was rejected");
    expect_near(driving.applied_actuator_torque_nm, 50.0, 0.0,
                "driving hold applied wrong actuator torque");
    expect_near(driving.final_state.angular_speed_rad_s, 100.0, 0.0,
                "driving hold missed its target");
}

void test_constraint_exposes_each_torque_limit() {
    detail::BoundedDynoConstraintInput input{
        2.0, 0.0, {0.0, 100.0}, 500.0, 100.0, 200.0, 100.0, 0.01,
    };
    auto calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &absorbing = require_step(calculation, "absorbing limit was rejected");
    expect(absorbing.disposition ==
               detail::BoundedDynoConstraintDisposition::absorbing_torque_limited,
           "absorbing saturation returned wrong disposition");
    expect_near(absorbing.applied_actuator_torque_nm, -200.0, 0.0,
                "absorbing saturation exceeded its limit");
    expect_near(absorbing.final_state.angular_speed_rad_s, 101.5, 1e-14,
                "absorbing saturation hid achieved speed");

    input.held_upstream_engine_torque_nm = 0.0;
    input.target_angular_speed_rad_s = 102.0;
    calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &driving = require_step(calculation, "driving limit was rejected");
    expect(driving.disposition ==
               detail::BoundedDynoConstraintDisposition::driving_torque_limited,
           "driving saturation returned wrong disposition");
    expect_near(driving.applied_actuator_torque_nm, 100.0, 0.0,
                "driving saturation exceeded its limit");
    expect_near(driving.final_state.angular_speed_rad_s, 100.5, 1e-14,
                "driving saturation hid achieved speed");
}

void test_constraint_compensates_configuration_inertia_term() {
    const auto calculation = detail::advance_bounded_dyno_constraint({
        2.0,
        0.02,
        {0.0, 100.0},
        50.0,
        100.0,
        100.0,
        100.0,
        0.01,
    });
    const auto &step = require_step(calculation, "variable-inertia hold was rejected");
    expect_near(step.velocity_inertia_torque_nm, 100.0, 0.0,
                "velocity-inertia torque was not evaluated");
    expect_near(step.applied_actuator_torque_nm, 50.0, 0.0,
                "constraint did not compensate velocity inertia");
    expect_near(step.final_state.angular_speed_rad_s, 100.0, 0.0,
                "variable-inertia hold missed its target");
}

void test_invalid_and_unpreventable_reverse_are_typed() {
    detail::BoundedDynoConstraintInput input{
        1.0, 0.0, {0.0, 10.0}, -20.0, 10.0, 0.0, 0.0, 1.0,
    };
    const auto stalled = detail::advance_bounded_dyno_constraint(input);
    const auto *stall = std::get_if<detail::BoundedDynoConstraintStall>(&stalled);
    expect(stall != nullptr && stall->predicted_final_angular_speed_rad_s == -10.0 &&
               stall->stall_time_s == 0.5,
           "unpreventable reverse did not return exact stall evidence");

    input.maximum_absorbing_torque_nm = -1.0;
    const auto invalid = detail::advance_bounded_dyno_constraint(input);
    const auto *error = std::get_if<detail::BoundedDynoConstraintInputError>(&invalid);
    expect(error != nullptr && error->issue == detail::BoundedDynoConstraintInputIssue::
                                                   negative_maximum_absorbing_torque,
           "negative absorbing limit returned wrong input issue");
}

void run_tests() {
    test_constraint_tracks_target_with_absorbing_or_driving_torque();
    test_constraint_exposes_each_torque_limit();
    test_constraint_compensates_configuration_inertia_term();
    test_invalid_and_unpreventable_reverse_are_typed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "bounded dyno constraint failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
