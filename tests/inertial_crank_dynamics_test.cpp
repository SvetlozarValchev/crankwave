#include "simulation/inertial_crank_dynamics.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"
#include "simulation/positive_speed_rigid_crank_zoh.hpp"

#include "crankwave/contract/common.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace crankwave::simulation;
namespace crank_detail = crankwave::simulation::detail;

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

[[nodiscard]] const crank_detail::PositiveSpeedRigidCrankZohStep &
require_primitive_step(
    const crank_detail::PositiveSpeedRigidCrankZohCalculation &calculation,
    std::string_view message) {
    const auto *step =
        std::get_if<crank_detail::PositiveSpeedRigidCrankZohStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

[[nodiscard]] const crank_detail::NonnegativeSpeedConfigurationDependentCrankZohStep &
require_nonnegative_configuration_step(
    const crank_detail::NonnegativeSpeedConfigurationDependentCrankZohCalculation
        &calculation,
    std::string_view message) {
    const auto *step =
        std::get_if<crank_detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
            &calculation);
    expect(step != nullptr, message);
    return *step;
}

void expect_canonical_positive_zero(double value, std::string_view message) {
    expect(value == 0.0 && !std::signbit(value), message);
}

void expect_nonnegative_configuration_error(
    const crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInput &input,
    crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInputIssue
        expected_issue,
    std::string_view message) {
    const auto calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    const auto *error = std::get_if<
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInputError>(
        &calculation);
    expect(error != nullptr && error->issue == expected_issue, message);
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
    const auto &brake = piecewise_linear_positive_speed_passive_brake_method_identity();

    expect(crank.id == kRigidCrankZohWorkEnergyMethodId && crank.version == 1U &&
               crank.configuration_sha256 ==
                   crankwave::contract::sha256(
                       std::as_bytes(std::span<const char>{crank_descriptor.data(),
                                                           crank_descriptor.size()})),
           "crank-dynamics identity is not bound to its canonical descriptor");
    expect(brake.id == kPiecewiseLinearPositiveSpeedPassiveBrakeMethodId &&
               brake.version == 1U &&
               brake.configuration_sha256 ==
                   crankwave::contract::sha256(
                       std::as_bytes(std::span<const char>{brake_descriptor.data(),
                                                           brake_descriptor.size()})),
           "passive-brake identity is not bound to its canonical descriptor");
}

void test_shared_positive_speed_primitive_evidence() {
    const crank_detail::PositiveSpeedRigidCrankZohInput input{
        2.0, {1.0, 15.0}, 12.0, 6.0, 0.5,
    };
    const auto calculation =
        crank_detail::advance_positive_speed_rigid_crank_zoh(input);
    const auto &step = require_primitive_step(
        calculation, "valid shared rigid-crank step was rejected");

    expect(step.input == input &&
               step.final_state ==
                   crank_detail::PositiveSpeedRigidCrankState{8.875, 16.5},
           "shared rigid-crank primitive did not retain its exact input/final state");
    expect_near(step.held_net_torque_nm, 6.0, 0.0,
                "shared primitive net torque changed");
    expect_near(step.angular_acceleration_rad_s2, 3.0, 0.0,
                "shared primitive angular acceleration changed");
    expect_near(step.angular_displacement_rad, 7.875, 0.0,
                "shared primitive angular displacement changed");
    expect_near(step.upstream_engine_torque_work_j, 94.5, 0.0,
                "shared primitive upstream-engine work changed");
    expect_near(step.resisting_torque_work_j, 47.25, 0.0,
                "shared primitive resisting work changed");
    expect_near(step.held_net_torque_work_j, 47.25, 0.0,
                "shared primitive net work changed");
    expect_near(step.kinetic_energy_change_j, 47.25, 0.0,
                "shared primitive kinetic-energy change changed");
    expect_near(step.energy_residual_j, 0.0, 1e-14,
                "shared primitive energy residual exceeded binary64 roundoff");
}

void test_shared_positive_speed_primitive_rejections_and_stall() {
    crank_detail::PositiveSpeedRigidCrankZohInput input{
        1.0, {0.0, 11.0}, -12.0, 0.0, 1.0,
    };
    auto calculation = crank_detail::advance_positive_speed_rigid_crank_zoh(input);
    const auto *stall =
        std::get_if<crank_detail::PositiveSpeedRigidCrankZohStall>(&calculation);
    expect(stall != nullptr && stall->predicted_final_angular_speed_rad_s == -1.0 &&
               stall->held_net_torque_nm == -12.0 &&
               stall->angular_acceleration_rad_s2 == -12.0,
           "shared primitive did not return typed within-step stall evidence");
    expect_near(stall->stall_time_s, 11.0 / 12.0, 0.0,
                "shared primitive stall time changed");
    expect_near(stall->stall_theta_rad, 121.0 / 24.0, 1e-15,
                "shared primitive stall angle changed");

    input.initial_state.angular_speed_rad_s = 0.0;
    calculation = crank_detail::advance_positive_speed_rigid_crank_zoh(input);
    auto *error =
        std::get_if<crank_detail::PositiveSpeedRigidCrankZohInputError>(&calculation);
    expect(error != nullptr && error->issue ==
                                   crank_detail::PositiveSpeedRigidCrankZohInputIssue::
                                       nonpositive_angular_speed,
           "shared primitive admitted zero initial speed");

    input.initial_state.angular_speed_rad_s = -1.0;
    calculation = crank_detail::advance_positive_speed_rigid_crank_zoh(input);
    error =
        std::get_if<crank_detail::PositiveSpeedRigidCrankZohInputError>(&calculation);
    expect(error != nullptr && error->issue ==
                                   crank_detail::PositiveSpeedRigidCrankZohInputIssue::
                                       nonpositive_angular_speed,
           "shared primitive admitted reverse initial speed");

    input.initial_state.angular_speed_rad_s = 11.0;
    input.held_resisting_torque_nm = -1.0;
    calculation = crank_detail::advance_positive_speed_rigid_crank_zoh(input);
    error =
        std::get_if<crank_detail::PositiveSpeedRigidCrankZohInputError>(&calculation);
    expect(error != nullptr && error->issue ==
                                   crank_detail::PositiveSpeedRigidCrankZohInputIssue::
                                       negative_resisting_torque,
           "shared primitive admitted a signed negative resisting magnitude");
}

void test_configuration_dependent_crank_primitive_applies_velocity_inertia() {
    const crank_detail::PositiveSpeedConfigurationDependentCrankZohInput input{
        3.0, 0.2, {1.0, 10.0}, 20.0, 4.0, 0.5,
    };
    const auto calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    const auto *step =
        std::get_if<crank_detail::PositiveSpeedConfigurationDependentCrankZohStep>(
            &calculation);
    expect(step != nullptr && step->input == input,
           "valid configuration-dependent crank step was rejected");
    expect_near(step->held_applied_net_torque_nm, 16.0, 0.0,
                "configuration-dependent applied torque changed");
    expect_near(step->velocity_inertia_torque_nm, 10.0, 0.0,
                "configuration-dependent velocity inertia torque changed");
    expect_near(step->effective_accelerating_torque_nm, 6.0, 0.0,
                "configuration-dependent effective torque changed");
    expect_near(step->angular_acceleration_rad_s2, 2.0, 0.0,
                "configuration-dependent angular acceleration changed");
    expect_near(step->final_state.angular_speed_rad_s, 11.0, 0.0,
                "configuration-dependent omega update changed");
    expect_near(step->angular_displacement_rad, 5.5, 0.0,
                "configuration-dependent semi-implicit displacement changed");
    expect_near(step->final_state.theta_rad, 6.5, 0.0,
                "configuration-dependent semi-implicit theta update changed");
}

void test_configuration_dependent_crank_primitive_rejects_invalid_inertia() {
    crank_detail::PositiveSpeedConfigurationDependentCrankZohInput input{
        3.0, 0.2, {1.0, 10.0}, 20.0, 4.0, 0.5,
    };
    input.inertia_derivative_kg_m2_per_rad = std::numeric_limits<double>::infinity();
    auto calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    const auto *error = std::get_if<
        crank_detail::PositiveSpeedConfigurationDependentCrankZohInputError>(
        &calculation);
    expect(error != nullptr &&
               error->issue ==
                   crank_detail::PositiveSpeedConfigurationDependentCrankZohInputIssue::
                       nonfinite_inertia_derivative,
           "nonfinite configuration-inertia derivative returned the wrong error");

    input.inertia_derivative_kg_m2_per_rad = 0.2;
    input.instantaneous_inertia_kg_m2 = 0.0;
    calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    error = std::get_if<
        crank_detail::PositiveSpeedConfigurationDependentCrankZohInputError>(
        &calculation);
    expect(error != nullptr &&
               error->issue ==
                   crank_detail::PositiveSpeedConfigurationDependentCrankZohInputIssue::
                       nonpositive_instantaneous_inertia,
           "nonpositive configuration inertia returned the wrong error");
}

void test_nonnegative_configuration_crank_preserves_positive_advance() {
    const crank_detail::PositiveSpeedConfigurationDependentCrankZohInput input{
        3.0, 0.2, {1.0, 10.0}, 20.0, 4.0, 0.5,
    };
    const auto positive_calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    const auto *positive_step =
        std::get_if<crank_detail::PositiveSpeedConfigurationDependentCrankZohStep>(
            &positive_calculation);
    expect(positive_step != nullptr,
           "positive-speed reference step for nonnegative parity was rejected");

    const auto nonnegative_calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    const auto &step = require_nonnegative_configuration_step(
        nonnegative_calculation,
        "nonnegative primitive rejected a positive-to-positive step");
    using Disposition =
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohDisposition;
    expect(step.disposition == Disposition::advanced &&
               step.input == positive_step->input &&
               step.final_state == positive_step->final_state &&
               step.held_applied_net_torque_nm ==
                   positive_step->held_applied_net_torque_nm &&
               step.velocity_inertia_torque_nm ==
                   positive_step->velocity_inertia_torque_nm &&
               step.effective_accelerating_torque_nm ==
                   positive_step->effective_accelerating_torque_nm &&
               step.angular_acceleration_rad_s2 ==
                   positive_step->angular_acceleration_rad_s2 &&
               step.angular_displacement_rad ==
                   positive_step->angular_displacement_rad &&
               step.unconstrained_predicted_final_angular_speed_rad_s ==
                   positive_step->final_state.angular_speed_rad_s,
           "nonnegative positive-speed path changed existing step output");
    expect_canonical_positive_zero(
        step.stall_time_s,
        "advanced nonnegative step did not clear stall-time evidence");
    expect_canonical_positive_zero(
        step.stall_theta_rad,
        "advanced nonnegative step did not clear stall-angle evidence");
}

void test_nonnegative_configuration_crank_commits_exact_stop() {
    crank_detail::PositiveSpeedConfigurationDependentCrankZohInput input{
        1.0, 0.0, {0.0, 11.0}, -12.0, 0.0, 1.0,
    };
    auto positive_calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    auto *positive_stall =
        std::get_if<crank_detail::PositiveSpeedConfigurationDependentCrankZohStall>(
            &positive_calculation);
    expect(positive_stall != nullptr,
           "positive-speed reference crossing did not return a stall");

    auto nonnegative_calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    auto *step =
        std::get_if<crank_detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
            &nonnegative_calculation);
    using Disposition =
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohDisposition;
    expect(step != nullptr && step->disposition == Disposition::stopped &&
               step->input == input &&
               step->held_applied_net_torque_nm ==
                   positive_stall->held_applied_net_torque_nm &&
               step->velocity_inertia_torque_nm ==
                   positive_stall->velocity_inertia_torque_nm &&
               step->effective_accelerating_torque_nm ==
                   positive_stall->effective_accelerating_torque_nm &&
               step->angular_acceleration_rad_s2 ==
                   positive_stall->angular_acceleration_rad_s2 &&
               step->unconstrained_predicted_final_angular_speed_rad_s ==
                   positive_stall->predicted_final_angular_speed_rad_s &&
               step->stall_time_s == positive_stall->stall_time_s &&
               step->stall_theta_rad == positive_stall->stall_theta_rad &&
               step->final_state.theta_rad == positive_stall->stall_theta_rad,
           "nonnegative crossing did not commit the existing exact stall evidence");
    expect_canonical_positive_zero(
        step->final_state.angular_speed_rad_s,
        "nonnegative crossing published noncanonical stopped speed");
    expect_near(step->angular_displacement_rad, 121.0 / 24.0, 1e-15,
                "nonnegative crossing committed the wrong stopped displacement");

    input.held_upstream_engine_torque_nm = -11.0;
    positive_calculation =
        crank_detail::advance_positive_speed_configuration_dependent_crank_zoh(input);
    positive_stall =
        std::get_if<crank_detail::PositiveSpeedConfigurationDependentCrankZohStall>(
            &positive_calculation);
    expect(positive_stall != nullptr &&
               positive_stall->predicted_final_angular_speed_rad_s == 0.0,
           "positive-speed exact-end stop did not return a stall");
    nonnegative_calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    step =
        std::get_if<crank_detail::NonnegativeSpeedConfigurationDependentCrankZohStep>(
            &nonnegative_calculation);
    expect(step != nullptr && step->disposition == Disposition::stopped &&
               step->stall_time_s == positive_stall->stall_time_s &&
               step->stall_theta_rad == positive_stall->stall_theta_rad &&
               step->final_state.theta_rad == positive_stall->stall_theta_rad,
           "exact-end zero crossing did not commit the existing stall point");
    expect_canonical_positive_zero(
        step->final_state.angular_speed_rad_s,
        "exact-end zero crossing did not commit canonical stopped speed");
}

void test_nonnegative_configuration_crank_holds_canonical_rest() {
    using Disposition =
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohDisposition;
    crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInput input{
        2.0, 0.2, {4.0, 0.0}, 1.0, 2.0, 0.5,
    };
    auto calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    const auto &negative_torque = require_nonnegative_configuration_step(
        calculation, "canonical rest under negative effective torque was rejected");
    expect(negative_torque.disposition == Disposition::held_at_rest &&
               negative_torque.final_state.theta_rad == 4.0 &&
               negative_torque.held_applied_net_torque_nm == -1.0 &&
               negative_torque.velocity_inertia_torque_nm == 0.0 &&
               negative_torque.effective_accelerating_torque_nm == -1.0 &&
               negative_torque.unconstrained_predicted_final_angular_speed_rad_s ==
                   -0.25,
           "held-at-rest step lost its state or unconstrained torque evidence");
    expect_canonical_positive_zero(negative_torque.final_state.angular_speed_rad_s,
                                   "held-at-rest step published noncanonical speed");
    expect_canonical_positive_zero(
        negative_torque.angular_acceleration_rad_s2,
        "held-at-rest step published unconstrained negative acceleration");
    expect_canonical_positive_zero(negative_torque.angular_displacement_rad,
                                   "held-at-rest step published reverse displacement");

    input.held_upstream_engine_torque_nm = 2.0;
    calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    const auto &balanced = require_nonnegative_configuration_step(
        calculation, "canonical rest under zero effective torque was rejected");
    expect(balanced.disposition == Disposition::held_at_rest &&
               balanced.final_state.theta_rad == input.initial_state.theta_rad &&
               balanced.effective_accelerating_torque_nm == 0.0,
           "zero-torque rest did not remain held");
    expect_canonical_positive_zero(balanced.final_state.angular_speed_rad_s,
                                   "zero-torque rest published noncanonical speed");
    expect_canonical_positive_zero(
        balanced.angular_acceleration_rad_s2,
        "zero-torque rest published noncanonical acceleration");
    expect_canonical_positive_zero(
        balanced.angular_displacement_rad,
        "zero-torque rest published noncanonical displacement");
    expect_canonical_positive_zero(
        balanced.unconstrained_predicted_final_angular_speed_rad_s,
        "zero-torque rest published noncanonical unconstrained speed");
}

void test_nonnegative_configuration_crank_starts_semi_implicitly() {
    const crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInput input{
        2.0, 0.2, {4.0, 0.0}, 6.0, 2.0, 0.5,
    };
    const auto calculation =
        crank_detail::advance_nonnegative_speed_configuration_dependent_crank_zoh(
            input);
    const auto &step = require_nonnegative_configuration_step(
        calculation, "positive effective torque did not start canonical rest");
    using Disposition =
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohDisposition;
    expect(step.disposition == Disposition::advanced &&
               step.held_applied_net_torque_nm == 4.0 &&
               step.velocity_inertia_torque_nm == 0.0 &&
               step.effective_accelerating_torque_nm == 4.0 &&
               step.angular_acceleration_rad_s2 == 2.0 &&
               step.final_state.angular_speed_rad_s == 1.0 &&
               step.angular_displacement_rad == 0.5 &&
               step.final_state.theta_rad == 4.5 &&
               step.unconstrained_predicted_final_angular_speed_rad_s == 1.0,
           "rest start did not use the existing semi-implicit omega/theta update");
}

void test_nonnegative_configuration_crank_rejects_invalid_inputs() {
    using Issue =
        crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInputIssue;
    crank_detail::NonnegativeSpeedConfigurationDependentCrankZohInput input{
        2.0, 0.2, {4.0, 0.0}, 6.0, 2.0, 0.5,
    };

    input.initial_state.angular_speed_rad_s = -0.0;
    expect_nonnegative_configuration_error(
        input, Issue::noncanonical_angular_speed_zero,
        "nonnegative primitive admitted negative-zero speed");

    input.initial_state.angular_speed_rad_s = -1.0;
    expect_nonnegative_configuration_error(
        input, Issue::negative_angular_speed,
        "nonnegative primitive admitted reverse initial speed");

    input.initial_state.angular_speed_rad_s = std::numeric_limits<double>::quiet_NaN();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_angular_speed,
        "nonnegative primitive admitted nonfinite initial speed");

    input.initial_state.angular_speed_rad_s = 0.0;
    input.instantaneous_inertia_kg_m2 = std::numeric_limits<double>::infinity();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_instantaneous_inertia,
        "nonnegative primitive admitted nonfinite instantaneous inertia");

    input.instantaneous_inertia_kg_m2 = 2.0;
    input.inertia_derivative_kg_m2_per_rad = std::numeric_limits<double>::quiet_NaN();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_inertia_derivative,
        "nonnegative primitive admitted nonfinite inertia derivative");

    input.inertia_derivative_kg_m2_per_rad = 0.2;
    input.initial_state.theta_rad = std::numeric_limits<double>::infinity();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_theta,
        "nonnegative primitive admitted nonfinite theta");

    input.initial_state.theta_rad = 4.0;
    input.held_upstream_engine_torque_nm = std::numeric_limits<double>::quiet_NaN();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_upstream_engine_torque,
        "rest branch admitted nonfinite upstream torque");

    input.held_upstream_engine_torque_nm = 6.0;
    input.held_resisting_torque_nm = std::numeric_limits<double>::infinity();
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_resisting_torque,
        "rest branch admitted nonfinite resisting torque");

    input.held_resisting_torque_nm = 2.0;
    input.duration_s = std::numeric_limits<double>::infinity();
    expect_nonnegative_configuration_error(input, Issue::nonfinite_duration,
                                           "rest branch admitted nonfinite duration");

    input.duration_s = 0.5;
    input.instantaneous_inertia_kg_m2 = std::numeric_limits<double>::denorm_min();
    input.held_upstream_engine_torque_nm = std::numeric_limits<double>::max();
    input.held_resisting_torque_nm = 0.0;
    expect_nonnegative_configuration_error(
        input, Issue::nonfinite_derived_value,
        "rest branch admitted nonfinite derived acceleration");
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
    test_shared_positive_speed_primitive_evidence();
    test_shared_positive_speed_primitive_rejections_and_stall();
    test_configuration_dependent_crank_primitive_applies_velocity_inertia();
    test_configuration_dependent_crank_primitive_rejects_invalid_inertia();
    test_nonnegative_configuration_crank_preserves_positive_advance();
    test_nonnegative_configuration_crank_commits_exact_stop();
    test_nonnegative_configuration_crank_holds_canonical_rest();
    test_nonnegative_configuration_crank_starts_semi_implicitly();
    test_nonnegative_configuration_crank_rejects_invalid_inputs();
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
