#include "simulation/inertial_crank_dynamics.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"

#include "engine_sim_offline/contract/common.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline::simulation;

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

[[nodiscard]] InertialCrankDynamicsConfiguration configuration(double inertia = 2.0) {
    return {
        inertia,
        {
            {10.0, 4.0},
            {20.0, 8.0},
        },
    };
}

[[nodiscard]] InertialCrankDynamics
compiled(InertialCrankDynamicsConfiguration value = configuration()) {
    auto result = compile_inertial_crank_dynamics(std::move(value));
    const auto *error = std::get_if<InertialCrankDynamicsError>(&result);
    expect(error == nullptr, "valid inertial-crank configuration was rejected");
    return std::get<InertialCrankDynamics>(std::move(result));
}

[[nodiscard]] const InertialCrankStepResult &
require_result(const InertialCrankStepCalculation &calculation,
               std::string_view message) {
    const auto *result = std::get_if<InertialCrankStepResult>(&calculation);
    expect(result != nullptr, message);
    return *result;
}

void expect_configuration_error(
    InertialCrankDynamicsConfiguration value,
    InertialCrankConfigurationIssue expected_issue,
    std::size_t expected_index = kNoInertialCrankBrakePoint) {
    const auto compilation = compile_inertial_crank_dynamics(std::move(value));
    const auto *error = std::get_if<InertialCrankDynamicsError>(&compilation);
    expect(error != nullptr &&
               error->code == InertialCrankDynamicsErrorCode::invalid_configuration &&
               error->configuration_issue == expected_issue &&
               error->brake_point_index == expected_index,
           "invalid inertial-crank configuration returned the wrong typed error");
}

[[nodiscard]] const InertialCrankDynamicsError &
require_step_error(const InertialCrankStepCalculation &calculation,
                   InertialCrankDynamicsErrorCode expected_code,
                   std::string_view message) {
    const auto *error = std::get_if<InertialCrankDynamicsError>(&calculation);
    expect(error != nullptr && error->code == expected_code, message);
    return *error;
}

void test_configuration_admission_and_owned_snapshot() {
    auto value = configuration();
    auto model = compiled(value);
    value.equivalent_inertia_kg_m2 = 9.0;
    value.passive_brake_curve.front().resisting_torque_nm = 99.0;
    expect(model.equivalent_inertia_kg_m2() == 2.0 &&
               model.passive_brake_curve().front().resisting_torque_nm == 4.0,
           "compiled inertial-crank configuration did not own its snapshot");

    value = configuration();
    value.equivalent_inertia_kg_m2 = std::numeric_limits<double>::quiet_NaN();
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::nonfinite_equivalent_inertia);
    value = configuration(0.0);
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::nonpositive_equivalent_inertia);
    value = configuration(-1.0);
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::nonpositive_equivalent_inertia);

    value = configuration();
    value.passive_brake_curve.clear();
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::insufficient_brake_curve_points);
    value = configuration();
    value.passive_brake_curve.resize(1U);
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::insufficient_brake_curve_points);

    value = configuration();
    value.passive_brake_curve[1].angular_speed_rad_s =
        std::numeric_limits<double>::infinity();
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::nonfinite_brake_speed, 1U);
    value = configuration();
    value.passive_brake_curve[0].angular_speed_rad_s = -1.0;
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::negative_brake_speed, 0U);
    value = configuration();
    value.passive_brake_curve[1].resisting_torque_nm =
        std::numeric_limits<double>::quiet_NaN();
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::nonfinite_brake_torque, 1U);
    value = configuration();
    value.passive_brake_curve[0].resisting_torque_nm = -1.0;
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::negative_brake_torque, 0U);
    value = configuration();
    value.passive_brake_curve[1].angular_speed_rad_s = 10.0;
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::unstable_brake_speed_order, 1U);
    value = configuration();
    value.passive_brake_curve[1].angular_speed_rad_s = 9.0;
    expect_configuration_error(
        value, InertialCrankConfigurationIssue::unstable_brake_speed_order, 1U);
}

void test_method_identities_bind_canonical_descriptors() {
    const auto crank_descriptor = rigid_crank_zoh_work_energy_method_descriptor();
    const auto brake_descriptor =
        piecewise_linear_positive_speed_passive_brake_method_descriptor();
    const auto &crank = rigid_crank_zoh_work_energy_method_identity();
    const auto &brake =
        piecewise_linear_positive_speed_passive_brake_method_identity();

    expect(crank.id == kRigidCrankZohWorkEnergyMethodId && crank.version == 1U &&
               crank.configuration_sha256 ==
                   engine_sim_offline::contract::sha256(std::as_bytes(
                       std::span<const char>{crank_descriptor.data(),
                                             crank_descriptor.size()})),
           "crank-dynamics identity is not bound to its canonical descriptor");
    expect(brake.id == kPiecewiseLinearPositiveSpeedPassiveBrakeMethodId &&
               brake.version == 1U &&
               brake.configuration_sha256 ==
                   engine_sim_offline::contract::sha256(std::as_bytes(
                       std::span<const char>{brake_descriptor.data(),
                                             brake_descriptor.size()})),
           "passive-brake identity is not bound to its canonical descriptor");
}

void test_piecewise_linear_brake_and_energy_consistent_update() {
    const auto model = compiled();
    const auto calculation = model.advance({{1.0, 15.0}, 12.0, 0.5});
    const auto &result =
        require_result(calculation, "valid inertial crank step was rejected");

    expect(result.initial_state == InertialCrankState{1.0, 15.0},
           "step result did not expose the admitted initial state");
    expect_near(result.applied_brake_torque_nm, 6.0, 0.0,
                "piecewise-linear passive brake interpolation changed");
    expect_near(result.held_total_crank_torque_nm, 12.0, 0.0,
                "held total crank torque was not exposed");
    expect_near(result.held_net_torque_nm, 6.0, 0.0,
                "running-direction brake sign changed");
    expect_near(result.angular_acceleration_rad_s2, 3.0, 0.0,
                "declared-inertia angular acceleration changed");
    expect_near(result.final_state.angular_speed_rad_s, 16.5, 0.0,
                "constant-acceleration omega update changed");
    expect_near(result.angular_displacement_rad, 7.875, 0.0,
                "constant-acceleration angular displacement changed");
    expect_near(result.final_state.theta_rad, 8.875, 0.0,
                "constant-acceleration theta update changed");
    expect_near(result.kinetic_energy_change_j, 47.25, 0.0,
                "rotational kinetic-energy delta changed");
    expect_near(result.held_net_torque_work_j, 47.25, 0.0,
                "held net-torque work changed");
    expect_near(result.energy_residual_j, 0.0, 1e-14,
                "energy-consistency residual exceeded binary64 roundoff");
}

void test_endpoint_admission_and_causal_zero_order_hold() {
    const auto model = compiled();
    const auto at_lower = model.advance({{0.0, 10.0}, 4.0, 0.1});
    const auto &lower = require_result(at_lower, "lower brake endpoint was rejected");
    expect_near(lower.applied_brake_torque_nm, 4.0, 0.0,
                "lower endpoint brake torque changed");
    expect_near(lower.final_state.angular_speed_rad_s, 10.0, 0.0,
                "balanced lower endpoint did not hold speed");

    const auto at_upper = model.advance({{0.0, 20.0}, 8.0, 0.1});
    const auto &upper = require_result(at_upper, "upper brake endpoint was rejected");
    expect_near(upper.applied_brake_torque_nm, 8.0, 0.0,
                "upper endpoint brake torque changed");
    expect_near(upper.final_state.angular_speed_rad_s, 20.0, 0.0,
                "balanced upper endpoint did not hold speed");

    auto causal_model = compiled({
        1.0,
        {
            {10.0, 0.0},
            {20.0, 10.0},
        },
    });
    const auto first_calculation = causal_model.advance({{0.0, 10.0}, 10.0, 0.1});
    const auto &first = require_result(
        first_calculation, "first causal zero-order-held step was rejected");
    expect_near(first.applied_brake_torque_nm, 0.0, 0.0,
                "brake torque was sampled from a future within-step speed");
    expect_near(first.angular_acceleration_rad_s2, 10.0, 0.0,
                "initial-boundary torque sample was not held for the full step");
    expect_near(first.final_state.angular_speed_rad_s, 11.0, 1e-15,
                "first causal zero-order-held omega update changed");

    const auto second_calculation =
        causal_model.advance({first.final_state, 10.0, 0.1});
    const auto &second = require_result(
        second_calculation, "second causal zero-order-held step was rejected");
    expect_near(second.applied_brake_torque_nm, 1.0, 2e-15,
                "next-step brake did not use the prior published speed");
    expect_near(second.angular_acceleration_rad_s2, 9.0, 2e-15,
                "next causal torque hold changed");
}

void test_invalid_step_inputs_are_typed() {
    const auto model = compiled();
    auto input = InertialCrankStepInput{{0.0, 15.0}, 6.0, 0.1};

    input.initial_state.theta_rad = std::numeric_limits<double>::quiet_NaN();
    auto calculation = model.advance(input);
    auto error =
        require_step_error(calculation, InertialCrankDynamicsErrorCode::invalid_input,
                           "nonfinite theta did not return invalid-input error");
    expect(error.input_issue == InertialCrankInputIssue::nonfinite_theta,
           "nonfinite theta returned the wrong input issue");

    input = {{0.0, std::numeric_limits<double>::infinity()}, 6.0, 0.1};
    calculation = model.advance(input);
    expect(require_step_error(calculation,
                              InertialCrankDynamicsErrorCode::invalid_input,
                              "nonfinite omega was admitted")
                   .input_issue == InertialCrankInputIssue::nonfinite_angular_speed,
           "nonfinite omega returned the wrong input issue");

    input = {{0.0, 0.0}, 6.0, 0.1};
    calculation = model.advance(input);
    expect(require_step_error(calculation,
                              InertialCrankDynamicsErrorCode::invalid_input,
                              "zero omega was admitted")
                   .input_issue == InertialCrankInputIssue::nonpositive_angular_speed,
           "zero omega returned the wrong input issue");

    input = {{0.0, 15.0}, std::numeric_limits<double>::quiet_NaN(), 0.1};
    calculation = model.advance(input);
    expect(
        require_step_error(calculation, InertialCrankDynamicsErrorCode::invalid_input,
                           "nonfinite torque was admitted")
                .input_issue == InertialCrankInputIssue::nonfinite_total_crank_torque,
        "nonfinite torque returned the wrong input issue");

    input = {{0.0, 15.0}, 6.0, std::numeric_limits<double>::infinity()};
    calculation = model.advance(input);
    expect(require_step_error(calculation,
                              InertialCrankDynamicsErrorCode::invalid_input,
                              "nonfinite duration was admitted")
                   .input_issue == InertialCrankInputIssue::nonfinite_duration,
           "nonfinite duration returned the wrong input issue");

    input = {{0.0, 15.0}, 6.0, 0.0};
    calculation = model.advance(input);
    expect(require_step_error(calculation,
                              InertialCrankDynamicsErrorCode::invalid_input,
                              "zero duration was admitted")
                   .input_issue == InertialCrankInputIssue::nonpositive_duration,
           "zero duration returned the wrong input issue");

    const auto tiny_inertia_model = compiled(configuration(1e-300));
    calculation = tiny_inertia_model.advance(
        {{0.0, 15.0}, std::numeric_limits<double>::max(), 0.1});
    expect(require_step_error(calculation,
                              InertialCrankDynamicsErrorCode::invalid_input,
                              "derived overflow was admitted")
                   .input_issue == InertialCrankInputIssue::nonfinite_derived_value,
           "derived overflow returned the wrong input issue");
}

void test_curve_domain_has_no_extrapolation() {
    const auto model = compiled({
        1.0,
        {
            {10.0, 0.0},
            {20.0, 0.0},
        },
    });

    auto calculation = model.advance({{0.0, 9.0}, 0.0, 0.1});
    auto error = require_step_error(
        calculation, InertialCrankDynamicsErrorCode::brake_curve_out_of_domain,
        "initial speed below curve was extrapolated");
    expect(error.domain_issue == InertialCrankDomainIssue::initial_speed_below_curve &&
               error.angular_speed_rad_s == 9.0,
           "initial speed below curve returned wrong domain evidence");

    calculation = model.advance({{0.0, 21.0}, 0.0, 0.1});
    error = require_step_error(
        calculation, InertialCrankDynamicsErrorCode::brake_curve_out_of_domain,
        "initial speed above curve was extrapolated");
    expect(error.domain_issue == InertialCrankDomainIssue::initial_speed_above_curve &&
               error.angular_speed_rad_s == 21.0,
           "initial speed above curve returned wrong domain evidence");

    calculation = model.advance({{0.0, 19.0}, 10.0, 0.2});
    error = require_step_error(
        calculation, InertialCrankDynamicsErrorCode::brake_curve_out_of_domain,
        "predicted final speed above curve was published");
    expect(error.domain_issue == InertialCrankDomainIssue::final_speed_above_curve &&
               error.angular_speed_rad_s == 21.0,
           "final speed above curve returned wrong domain evidence");

    calculation = model.advance({{0.0, 11.0}, -2.0, 1.0});
    error = require_step_error(
        calculation, InertialCrankDynamicsErrorCode::brake_curve_out_of_domain,
        "positive predicted final speed below curve was published");
    expect(error.domain_issue == InertialCrankDomainIssue::final_speed_below_curve &&
               error.angular_speed_rad_s == 9.0,
           "final speed below curve returned wrong domain evidence");
}

void test_stall_is_distinct_and_reverse_is_never_published() {
    const auto model = compiled({
        1.0,
        {
            {0.0, 0.0},
            {20.0, 0.0},
        },
    });

    auto calculation = model.advance({{0.0, 11.0}, -11.0, 1.0});
    auto error = require_step_error(calculation, InertialCrankDynamicsErrorCode::stall,
                                    "zero-speed stall was published");
    expect(error.angular_speed_rad_s == 0.0,
           "zero-speed stall did not expose predicted speed");
    expect_near(error.stall_time_s, 1.0, 0.0,
                "exact stop did not expose its within-step time");
    expect_near(error.stall_theta_rad, 5.5, 0.0,
                "exact stop did not expose its crank angle");

    calculation = model.advance({{0.0, 11.0}, -12.0, 1.0});
    error = require_step_error(calculation, InertialCrankDynamicsErrorCode::stall,
                               "reverse crank state was published");
    expect(error.angular_speed_rad_s == -1.0,
           "reverse rejection did not expose predicted speed");
    expect_near(error.stall_time_s, 11.0 / 12.0, 0.0,
                "within-step reverse did not resolve its earlier stop time");
    expect_near(error.stall_theta_rad, 121.0 / 24.0, 1e-15,
                "within-step reverse did not resolve its stop angle");
}

void run_tests() {
    test_configuration_admission_and_owned_snapshot();
    test_method_identities_bind_canonical_descriptors();
    test_piecewise_linear_brake_and_energy_consistent_update();
    test_endpoint_admission_and_causal_zero_order_hold();
    test_invalid_step_inputs_are_typed();
    test_curve_domain_has_no_extrapolation();
    test_stall_is_distinct_and_reverse_is_never_published();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "inertial crank dynamics failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
