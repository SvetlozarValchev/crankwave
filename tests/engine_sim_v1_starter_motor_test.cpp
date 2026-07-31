#include "simulation/engine_sim_v1_starter_motor.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_bits(double actual, double expected, std::string_view message) {
    expect(std::bit_cast<std::uint64_t>(actual) ==
               std::bit_cast<std::uint64_t>(expected),
           message);
}

const EngineSimV1StarterMotorTorque &
expect_torque(const EngineSimV1StarterMotorCalculation &calculation,
              std::string_view message) {
    const auto *torque = std::get_if<EngineSimV1StarterMotorTorque>(&calculation);
    expect(torque != nullptr, message);
    return *torque;
}

void expect_issue(EngineSimV1StarterMotorInput input,
                  EngineSimV1StarterMotorInputIssue expected,
                  std::string_view message) {
    const auto calculation = calculate_engine_sim_v1_starter_motor_torque(input);
    const auto *error = std::get_if<EngineSimV1StarterMotorInputError>(&calculation);
    expect(error != nullptr && error->issue == expected, message);
}

EngineSimV1StarterMotorInput valid_input() {
    return {
        true, 20.0, 5.0, 200.0, 0.4, 0.1,
    };
}

void test_unilateral_target_speed_torque() {
    const auto unsaturated =
        calculate_engine_sim_v1_starter_motor_torque(valid_input());
    const auto &unsaturated_torque =
        expect_torque(unsaturated, "valid unsaturated starter input was rejected");
    expect_bits(unsaturated_torque.required_isolated_crank_torque_nm, 60.0,
                "isolated target-speed torque changed written order");
    expect_bits(unsaturated_torque.applied_crank_torque_nm, 60.0,
                "unsaturated starter torque changed");

    auto saturated_input = valid_input();
    saturated_input.maximum_torque_nm = 40.0;
    const auto saturated =
        calculate_engine_sim_v1_starter_motor_torque(saturated_input);
    const auto &saturated_torque =
        expect_torque(saturated, "valid saturated starter input was rejected");
    expect_bits(saturated_torque.required_isolated_crank_torque_nm, 60.0,
                "saturated starter lost required-torque evidence");
    expect_bits(saturated_torque.applied_crank_torque_nm, 40.0,
                "starter torque did not saturate at its unilateral maximum");

    auto reverse_prediction_input = valid_input();
    reverse_prediction_input.unconstrained_predicted_angular_speed_rad_s = -5.0;
    const auto reverse_prediction =
        calculate_engine_sim_v1_starter_motor_torque(reverse_prediction_input);
    const auto &reverse_prediction_torque = expect_torque(
        reverse_prediction, "finite reverse preconstraint prediction was rejected");
    expect_bits(reverse_prediction_torque.required_isolated_crank_torque_nm, 100.0,
                "starter did not correct a reverse preconstraint prediction");
}

void test_disabled_and_overrunning_states_are_zero() {
    auto input = valid_input();
    input.enabled = false;
    const auto disabled_calculation =
        calculate_engine_sim_v1_starter_motor_torque(input);
    const auto &disabled = expect_torque(disabled_calculation,
                                         "valid disabled starter input was rejected");
    expect_bits(disabled.required_isolated_crank_torque_nm, 0.0,
                "disabled starter published required torque");
    expect_bits(disabled.applied_crank_torque_nm, 0.0,
                "disabled starter applied torque");

    input.enabled = true;
    input.unconstrained_predicted_angular_speed_rad_s =
        input.target_angular_speed_rad_s;
    const auto at_target_calculation =
        calculate_engine_sim_v1_starter_motor_torque(input);
    const auto &at_target = expect_torque(at_target_calculation,
                                          "valid at-target starter input was rejected");
    expect_bits(at_target.applied_crank_torque_nm, 0.0,
                "starter applied torque at target speed");

    input.unconstrained_predicted_angular_speed_rad_s =
        input.target_angular_speed_rad_s + 1.0;
    const auto overrunning_calculation =
        calculate_engine_sim_v1_starter_motor_torque(input);
    const auto &overrunning = expect_torque(
        overrunning_calculation, "valid overrunning starter input was rejected");
    expect_bits(overrunning.applied_crank_torque_nm, 0.0,
                "starter braked an overrunning crank");
}

void test_invalid_inputs_are_typed() {
    const double infinity = std::numeric_limits<double>::infinity();

    auto input = valid_input();
    input.target_angular_speed_rad_s = infinity;
    expect_issue(input,
                 EngineSimV1StarterMotorInputIssue::nonfinite_target_angular_speed,
                 "nonfinite target speed returned the wrong issue");
    input = valid_input();
    input.target_angular_speed_rad_s = -0.0;
    expect_issue(
        input,
        EngineSimV1StarterMotorInputIssue::noncanonical_target_angular_speed_zero,
        "negative-zero target speed returned the wrong issue");
    input = valid_input();
    input.unconstrained_predicted_angular_speed_rad_s =
        std::numeric_limits<double>::quiet_NaN();
    expect_issue(input,
                 EngineSimV1StarterMotorInputIssue::
                     nonfinite_unconstrained_predicted_angular_speed,
                 "nonfinite predicted speed returned the wrong issue");
    input = valid_input();
    input.maximum_torque_nm = -0.0;
    expect_issue(input,
                 EngineSimV1StarterMotorInputIssue::noncanonical_maximum_torque_zero,
                 "negative-zero maximum torque returned the wrong issue");
    input = valid_input();
    input.equivalent_inertia_kg_m2 = 0.0;
    expect_issue(input,
                 EngineSimV1StarterMotorInputIssue::nonpositive_equivalent_inertia,
                 "zero inertia returned the wrong issue");
    input = valid_input();
    input.duration_s = infinity;
    expect_issue(input, EngineSimV1StarterMotorInputIssue::nonfinite_duration,
                 "nonfinite duration returned the wrong issue");
}

} // namespace

int main() {
    try {
        test_unilateral_target_speed_torque();
        test_disabled_and_overrunning_states_are_zero();
        test_invalid_inputs_are_typed();
    } catch (const std::exception &error) {
        std::cerr << "engine-sim v1 starter motor test failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
