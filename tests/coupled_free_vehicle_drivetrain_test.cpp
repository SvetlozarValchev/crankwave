#include "simulation/coupled_free_vehicle_drivetrain.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
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

[[nodiscard]] detail::ForwardGearReduction simple_reduction() {
    const auto calculation =
        detail::calculate_forward_gear_reduction({1000.0, 2.0, 4.0, 0.4});
    const auto *reduction = std::get_if<detail::ForwardGearReduction>(&calculation);
    expect(reduction != nullptr, "simple forward gear was rejected");
    return *reduction;
}

[[nodiscard]] const detail::CoupledFreeVehicleDrivetrainStep &
require_step(const detail::CoupledFreeVehicleDrivetrainCalculation &calculation,
             std::string_view message) {
    const auto *step =
        std::get_if<detail::CoupledFreeVehicleDrivetrainStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

void test_service_brake_holds_rest_against_clutch() {
    const auto calculation = detail::advance_coupled_free_vehicle_drivetrain({
        0.5,
        100.0,
        1000.0,
        0.0,
        simple_reduction(),
        100.0,
        1.0,
        0.0,
        0.0,
        0.0,
        5000.0,
        1.0,
        0.01,
    });
    const auto &step = require_step(calculation, "held launch was rejected");
    expect(step.projection_pass_count == 128U,
           "coupled solve did not execute the frozen pass count");
    expect(step.road_load_disposition ==
               detail::ForwardVehicleRoadLoadDisposition::held_at_rest,
           "service brake did not hold the initially resting vehicle");
    expect_near(step.applied_clutch_impulse_on_engine_nm_s, -1.0, 0.0,
                "clutch impulse missed its engine-driving bound");
    expect_near(step.applied_road_load_impulse_n_s, 20.0, 0.0,
                "brake did not balance the clutch wheel impulse");
    expect_near(step.final_engine_speed_rad_s, 98.0, 0.0,
                "held launch changed engine speed incorrectly");
    expect(step.final_vehicle_speed_m_s == 0.0,
           "clutch leaked motion through the held road constraint");
}

void test_locked_gear_shares_road_load_between_inertias() {
    const auto calculation = detail::advance_coupled_free_vehicle_drivetrain({
        0.5,
        100.0,
        1000.0,
        5.0,
        simple_reduction(),
        3000.0,
        1.0,
        0.0,
        0.0,
        100.0,
        0.0,
        0.0,
        0.1,
    });
    const auto &step = require_step(calculation, "locked road-load step was rejected");

    // The locked pair has 1000 + 0.5 * 20^2 = 1200 kg of effective linear
    // inertia. A 10 N.s road impulse therefore reduces vehicle speed by 1/120 m/s.
    const double expected_vehicle_speed_m_s = 5.0 - 10.0 / 1200.0;
    const double expected_engine_speed_rad_s = 20.0 * expected_vehicle_speed_m_s;
    expect_near(step.applied_road_load_impulse_n_s, 10.0, 0.0,
                "road-load impulse missed its authored capacity");
    expect_near(step.final_vehicle_speed_m_s, expected_vehicle_speed_m_s, 1e-15,
                "locked vehicle did not carry reflected engine inertia");
    expect_near(step.final_engine_speed_rad_s, expected_engine_speed_rad_s, 1e-13,
                "locked engine did not decelerate with the vehicle");
    expect(step.final_clutch_slip_rad_s.has_value(),
           "selected gear omitted final clutch slip");
    expect_near(*step.final_clutch_slip_rad_s, 0.0, 1e-13,
                "road load broke the locked gear-speed relationship");
}

void test_locked_clutch_and_brake_commit_exact_stall_boundary() {
    constexpr double kEngineInertiaKgM2 = 0.5;
    constexpr double kPredictedEngineSpeedRadS = 0.001;
    constexpr double kVehicleMassKg = 1000.0;
    constexpr double kPredictedVehicleSpeedMS = 0.0001;
    const auto reduction = simple_reduction();
    const auto calculation = detail::advance_coupled_free_vehicle_drivetrain({
        kEngineInertiaKgM2,
        kPredictedEngineSpeedRadS,
        kVehicleMassKg,
        kPredictedVehicleSpeedMS,
        reduction,
        100.0,
        1.0,
        0.0,
        0.0,
        0.0,
        5000.0,
        1.0,
        0.0001,
    });
    const auto &step =
        require_step(calculation, "locked-clutch near-rest stall was rejected");
    const double expected_clutch_impulse_nm_s =
        -kEngineInertiaKgM2 * kPredictedEngineSpeedRadS;
    const double expected_road_load_impulse_n_s =
        kVehicleMassKg * kPredictedVehicleSpeedMS -
        expected_clutch_impulse_nm_s *
            reduction.crank_speed_per_vehicle_speed_rad_per_m;

    expect(step.applied_clutch_impulse_on_engine_nm_s == expected_clutch_impulse_nm_s,
           "stall boundary changed the exact engine-stopping clutch impulse");
    expect(step.applied_road_load_impulse_n_s == expected_road_load_impulse_n_s,
           "stall boundary changed the exact vehicle-stopping road impulse");
    expect(step.final_engine_speed_rad_s == 0.0 &&
               !std::signbit(step.final_engine_speed_rad_s) &&
               step.final_vehicle_speed_m_s == 0.0 &&
               !std::signbit(step.final_vehicle_speed_m_s) &&
               step.final_clutch_slip_rad_s.has_value() &&
               *step.final_clutch_slip_rad_s == 0.0 &&
               !std::signbit(*step.final_clutch_slip_rad_s),
           "locked clutch and brake did not commit canonical positive-zero engine, "
           "vehicle, and slip state");
    expect(step.road_load_disposition ==
               detail::ForwardVehicleRoadLoadDisposition::stopped_within_step,
           "near-rest vehicle did not report a within-step stop");
}

void test_neutral_retains_isolated_road_load_behavior() {
    const auto calculation = detail::advance_coupled_free_vehicle_drivetrain({
        0.5,
        100.0,
        1000.0,
        5.0,
        std::nullopt,
        3000.0,
        1.0,
        0.0,
        0.0,
        100.0,
        0.0,
        0.0,
        0.1,
    });
    const auto &step = require_step(calculation, "neutral road-load step was rejected");
    expect(step.clutch_disposition ==
                   detail::BoundedClutchCouplingDisposition::neutral &&
               step.applied_clutch_impulse_on_engine_nm_s == 0.0 &&
               step.final_engine_speed_rad_s == 100.0,
           "neutral transmitted clutch impulse");
    expect_near(step.final_vehicle_speed_m_s, 4.99, 0.0,
                "neutral diverged from isolated forward road load");
}

void run_tests() {
    test_service_brake_holds_rest_against_clutch();
    test_locked_gear_shares_road_load_between_inertias();
    test_locked_clutch_and_brake_commit_exact_stall_boundary();
    test_neutral_retains_isolated_road_load_behavior();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "coupled free-vehicle drivetrain failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
