#include "simulation/forward_vehicle_road_load.hpp"

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

[[nodiscard]] const detail::ForwardVehicleRoadLoadStep &
require_step(const detail::ForwardVehicleRoadLoadCalculation &calculation,
             std::string_view message) {
    const auto *step = std::get_if<detail::ForwardVehicleRoadLoadStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

void test_pristine_passive_road_load_equation() {
    const auto calculation = detail::advance_forward_vehicle_road_load({
        1395.0,
        0.29,
        2.05,
        165.0,
        0.0,
        0.0,
        30.0,
        0.1,
    });
    const auto &step = require_step(calculation, "BMW road load was rejected");
    expect_near(step.air_density_kg_m3, 1.184121069848, 1e-15,
                "pristine fixed air density changed");
    expect_near(step.aerodynamic_drag_force_n, 316.78198921108617, 1e-12,
                "aerodynamic drag equation changed");
    expect_near(step.passive_road_load_force_n, 481.78198921108617, 1e-12,
                "rolling and aerodynamic loads were not additive");
    expect_near(step.final_speed_m_s, 29.965463656687376, 1e-14,
                "passive road load advanced the vehicle incorrectly");
    expect(step.disposition == detail::ForwardVehicleRoadLoadDisposition::moving,
           "ordinary road load did not retain moving disposition");
}

void test_service_brake_is_explicit_and_normalized() {
    const auto calculation = detail::advance_forward_vehicle_road_load({
        1395.0,
        0.29,
        2.05,
        165.0,
        14000.0,
        0.5,
        30.0,
        0.1,
    });
    const auto &step = require_step(calculation, "service brake was rejected");
    expect_near(step.service_brake_force_n, 7000.0, 0.0,
                "normalized brake application produced wrong force");
    expect_near(step.requested_resisting_force_n, 7481.781989211086, 1e-12,
                "service brake did not add to passive road load");
    expect_near(step.final_speed_m_s, 29.463671541992035, 1e-14,
                "service brake advanced the vehicle incorrectly");

    const auto zero = detail::advance_forward_vehicle_road_load({
        1000.0,
        0.0,
        0.0,
        0.0,
        14000.0,
        0.0,
        10.0,
        0.01,
    });
    const auto &zero_step = require_step(zero, "zero brake was rejected");
    expect(zero_step.service_brake_force_n == 0.0 && zero_step.final_speed_m_s == 10.0,
           "zero brake application changed motion");
}

void test_resisting_load_cannot_reverse_through_rest() {
    const auto calculation = detail::advance_forward_vehicle_road_load({
        1000.0,
        0.0,
        0.0,
        200.0,
        1800.0,
        1.0,
        0.1,
        1.0,
    });
    const auto &step = require_step(calculation, "within-step stop was rejected");
    expect(step.disposition ==
                   detail::ForwardVehicleRoadLoadDisposition::stopped_within_step &&
               step.final_speed_m_s == 0.0 &&
               step.applied_resisting_impulse_n_s == 100.0 &&
               step.applied_average_resisting_force_n == 100.0,
           "resisting load drove through the forward stop boundary");

    const auto resting = detail::advance_forward_vehicle_road_load({
        1000.0,
        0.3,
        2.0,
        200.0,
        1800.0,
        1.0,
        0.0,
        1.0,
    });
    const auto &resting_step = require_step(resting, "resting vehicle was rejected");
    expect(resting_step.disposition ==
                   detail::ForwardVehicleRoadLoadDisposition::held_at_rest &&
               resting_step.final_speed_m_s == 0.0 &&
               resting_step.applied_resisting_impulse_n_s == 0.0,
           "road load created reverse motion from rest");
}

void test_invalid_inputs_are_typed() {
    detail::ForwardVehicleRoadLoadInput input{
        1000.0, 0.3, 2.0, 200.0, 1800.0, 0.5, 10.0, 0.01,
    };
    input.service_brake_application_01 = 1.01;
    auto calculation = detail::advance_forward_vehicle_road_load(input);
    auto *error = std::get_if<detail::ForwardVehicleRoadLoadInputError>(&calculation);
    expect(error != nullptr && error->issue ==
                                   detail::ForwardVehicleRoadLoadInputIssue::
                                       service_brake_application_out_of_range,
           "out-of-range brake application returned wrong issue");

    input.service_brake_application_01 = 0.0;
    input.initial_speed_m_s = -0.0;
    calculation = detail::advance_forward_vehicle_road_load(input);
    error = std::get_if<detail::ForwardVehicleRoadLoadInputError>(&calculation);
    expect(error != nullptr &&
               error->issue ==
                   detail::ForwardVehicleRoadLoadInputIssue::negative_initial_speed,
           "negative-zero speed was not rejected canonically");

    input.initial_speed_m_s = 10.0;
    input.drag_coefficient = std::numeric_limits<double>::quiet_NaN();
    calculation = detail::advance_forward_vehicle_road_load(input);
    error = std::get_if<detail::ForwardVehicleRoadLoadInputError>(&calculation);
    expect(error != nullptr &&
               error->issue ==
                   detail::ForwardVehicleRoadLoadInputIssue::nonfinite_drag_coefficient,
           "nonfinite drag coefficient returned wrong issue");
}

void run_tests() {
    test_pristine_passive_road_load_equation();
    test_service_brake_is_explicit_and_normalized();
    test_resisting_load_cannot_reverse_through_rest();
    test_invalid_inputs_are_typed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "forward vehicle road-load failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
