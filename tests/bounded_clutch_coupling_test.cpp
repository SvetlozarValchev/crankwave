#include "simulation/bounded_clutch_coupling.hpp"

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

namespace detail = crankwave::simulation::detail;

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

[[nodiscard]] detail::ForwardGearReduction
require_reduction(const detail::ForwardGearReductionCalculation &calculation,
                  std::string_view message) {
    const auto *reduction = std::get_if<detail::ForwardGearReduction>(&calculation);
    expect(reduction != nullptr, message);
    return *reduction;
}

[[nodiscard]] const detail::BoundedClutchCouplingStep &
require_step(const detail::BoundedClutchCouplingCalculation &calculation,
             std::string_view message) {
    const auto *step = std::get_if<detail::BoundedClutchCouplingStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

[[nodiscard]] detail::ForwardGearReduction simple_reduction() {
    return require_reduction(
        detail::calculate_forward_gear_reduction({1000.0, 2.0, 4.0, 0.4}),
        "simple forward gear was rejected");
}

void test_forward_gear_reduction_matches_pristine_mapping() {
    const auto reduction = require_reduction(
        detail::calculate_forward_gear_reduction({1395.0, 4.21, 2.93, 0.315}),
        "BMW first gear was rejected");
    expect_near(reduction.crank_speed_per_vehicle_speed_rad_per_m, 39.159682539682542,
                0.0, "gear and differential speed mapping changed");
    expect_near(reduction.crank_reflected_vehicle_inertia_kg_m2, 0.90969515736143713,
                0.0, "reflected vehicle inertia changed");
    expect(reduction.wheel_force_per_crank_torque_n_per_nm ==
               reduction.crank_speed_per_vehicle_speed_rad_per_m,
           "wheel-force and shaft-speed factors disagreed");
}

void test_available_clutch_torque_locks_both_inertias() {
    const auto calculation = detail::advance_bounded_clutch_coupling({
        0.5,
        100.0,
        2.0,
        simple_reduction(),
        3000.0,
        1.0,
        0.01,
    });
    const auto &step = require_step(calculation, "locking clutch was rejected");
    expect(step.disposition == detail::BoundedClutchCouplingDisposition::tracking,
           "available clutch torque did not track zero slip");
    expect_near(step.required_torque_on_engine_nm, -2500.0, 1e-12,
                "locking torque was incorrect");
    expect_near(step.applied_torque_on_engine_nm, -2500.0, 1e-12,
                "available locking torque was not applied");
    expect_near(step.applied_wheel_force_n, 50000.0, 1e-10,
                "equal-and-opposite wheel force was incorrect");
    expect_near(step.final_engine_speed_rad_s, 50.0, 1e-14,
                "engine missed locked shaft speed");
    expect_near(step.final_vehicle_speed_m_s, 2.5, 1e-14,
                "vehicle missed locked shaft speed");
    expect(step.final_slip_rad_s.has_value() && *step.final_slip_rad_s == 0.0,
           "tracking clutch retained slip");
}

void test_each_clutch_torque_direction_saturates() {
    const auto engine_driving = detail::advance_bounded_clutch_coupling({
        0.5,
        100.0,
        2.0,
        simple_reduction(),
        100.0,
        1.0,
        0.01,
    });
    const auto &driving =
        require_step(engine_driving, "engine-driving saturation was rejected");
    expect(driving.disposition == detail::BoundedClutchCouplingDisposition::
                                      engine_driving_torque_limited &&
               driving.applied_torque_on_engine_nm == -100.0,
           "engine-driving saturation returned wrong torque or disposition");
    expect_near(driving.final_engine_speed_rad_s, 98.0, 0.0,
                "engine-driving saturation advanced engine incorrectly");
    expect_near(driving.final_vehicle_speed_m_s, 2.02, 1e-15,
                "engine-driving saturation advanced vehicle incorrectly");

    const auto vehicle_backdrive = detail::advance_bounded_clutch_coupling({
        0.5,
        20.0,
        5.0,
        simple_reduction(),
        100.0,
        1.0,
        0.01,
    });
    const auto &backdrive =
        require_step(vehicle_backdrive, "vehicle backdrive was rejected");
    expect(backdrive.disposition == detail::BoundedClutchCouplingDisposition::
                                        vehicle_backdrive_torque_limited &&
               backdrive.applied_torque_on_engine_nm == 100.0,
           "vehicle-backdrive saturation returned wrong torque or disposition");
    expect_near(backdrive.final_engine_speed_rad_s, 22.0, 0.0,
                "vehicle backdrive advanced engine incorrectly");
    expect_near(backdrive.final_vehicle_speed_m_s, 4.98, 1e-15,
                "vehicle backdrive advanced vehicle incorrectly");
}

void test_neutral_and_disengaged_clutch_transmit_zero() {
    const auto neutral = detail::advance_bounded_clutch_coupling({
        0.5,
        100.0,
        5.0,
        std::nullopt,
        3000.0,
        1.0,
        0.01,
    });
    const auto &neutral_step = require_step(neutral, "neutral was rejected");
    expect(neutral_step.disposition ==
                   detail::BoundedClutchCouplingDisposition::neutral &&
               !neutral_step.predicted_slip_rad_s.has_value() &&
               !neutral_step.final_slip_rad_s.has_value() &&
               neutral_step.applied_torque_on_engine_nm == 0.0 &&
               neutral_step.final_engine_speed_rad_s == 100.0 &&
               neutral_step.final_vehicle_speed_m_s == 5.0,
           "neutral changed either independent inertia");

    const auto disengaged = detail::advance_bounded_clutch_coupling({
        0.5,
        100.0,
        2.0,
        simple_reduction(),
        3000.0,
        0.0,
        0.01,
    });
    const auto &disengaged_step =
        require_step(disengaged, "disengaged clutch was rejected");
    expect(disengaged_step.disposition ==
                   detail::BoundedClutchCouplingDisposition::disengaged &&
               disengaged_step.applied_torque_on_engine_nm == 0.0 &&
               disengaged_step.final_engine_speed_rad_s == 100.0 &&
               disengaged_step.final_vehicle_speed_m_s == 2.0,
           "disengaged clutch transmitted torque");
}

void test_linear_vehicle_state_survives_gear_selection() {
    const auto first = simple_reduction();
    const auto second = require_reduction(
        detail::calculate_forward_gear_reduction({1000.0, 1.0, 4.0, 0.4}),
        "second forward gear was rejected");
    const auto first_step = detail::advance_bounded_clutch_coupling(
        {0.5, 60.0, 3.0, first, 3000.0, 0.0, 0.01});
    const auto second_step = detail::advance_bounded_clutch_coupling(
        {0.5, 60.0, 3.0, second, 3000.0, 0.0, 0.01});
    expect(require_step(first_step, "first gear selection failed")
                       .final_vehicle_speed_m_s == 3.0 &&
               require_step(second_step, "second gear selection failed")
                       .final_vehicle_speed_m_s == 3.0,
           "selecting another forward ratio rewrote linear vehicle speed");
}

void test_invalid_inputs_are_typed() {
    auto reduction = detail::calculate_forward_gear_reduction({1000.0, -1.0, 4.0, 0.4});
    const auto *gear_error =
        std::get_if<detail::ForwardGearReductionInputError>(&reduction);
    expect(gear_error != nullptr &&
               gear_error->issue ==
                   detail::ForwardGearReductionInputIssue::nonpositive_gear_ratio,
           "negative forward ratio returned wrong issue");

    detail::BoundedClutchCouplingInput input{
        0.5, 100.0, 2.0, simple_reduction(), 3000.0, 1.1, 0.01,
    };
    auto calculation = detail::advance_bounded_clutch_coupling(input);
    auto *error = std::get_if<detail::BoundedClutchCouplingInputError>(&calculation);
    expect(
        error != nullptr &&
            error->issue ==
                detail::BoundedClutchCouplingInputIssue::clutch_engagement_out_of_range,
        "out-of-range clutch engagement returned wrong issue");

    input.clutch_engagement_01 = 1.0;
    input.predicted_engine_speed_rad_s = std::numeric_limits<double>::quiet_NaN();
    calculation = detail::advance_bounded_clutch_coupling(input);
    error = std::get_if<detail::BoundedClutchCouplingInputError>(&calculation);
    expect(error != nullptr && error->issue == detail::BoundedClutchCouplingInputIssue::
                                                   nonfinite_predicted_engine_speed,
           "nonfinite engine speed returned wrong issue");
}

void run_tests() {
    test_forward_gear_reduction_matches_pristine_mapping();
    test_available_clutch_torque_locks_both_inertias();
    test_each_clutch_torque_direction_saturates();
    test_neutral_and_disengaged_clutch_transmit_zero();
    test_linear_vehicle_state_survives_gear_selection();
    test_invalid_inputs_are_typed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "bounded clutch coupling failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
