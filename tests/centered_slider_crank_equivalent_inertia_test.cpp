#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/positive_speed_rigid_crank_zoh.hpp"

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

using namespace engine_sim_offline;

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

[[nodiscard]] contract::LegacyCylinderAssembly bmw_cylinder(double phase_rad) {
    contract::LegacyCylinderAssembly cylinder;
    cylinder.parameters.connecting_rod_length_m.value = 0.135;
    cylinder.parameters.piston_mass_kg.value = 0.280;
    cylinder.parameters.connecting_rod_mass_kg.value = 0.300;
    cylinder.parameters.connecting_rod_inertia_kg_m2.value = 0.0015884918028487504;
    cylinder.kinematics = contract::LegacyDirectJournalKinematics{
        {{0.084}, {}},
        {{0.042}, {}},
        {{phase_rad}, {}},
    };
    return cylinder;
}

[[nodiscard]] contract::LegacyMechanismProfile bmw_mechanism() {
    contract::LegacyMechanismProfile mechanism;
    mechanism.crank.authored_crank_inertia_kg_m2.value = 0.206881602991773;
    mechanism.cylinders = {
        bmw_cylinder(0.0), bmw_cylinder(2.0), bmw_cylinder(4.0),
        bmw_cylinder(4.0), bmw_cylinder(2.0), bmw_cylinder(0.0),
    };
    return mechanism;
}

[[nodiscard]] simulation::CenteredSliderCrankConfigurationInertiaCylinderPlan
bmw_configuration_cylinder(double geometric_tdc_rad) {
    return {
        geometric_tdc_rad, 0.042, 0.135, 0.280, 0.300, 0.0015884918028487504,
    };
}

[[nodiscard]] simulation::CenteredSliderCrankConfigurationInertiaPlan
bmw_configuration_plan(double attached_inertia_kg_m2) {
    constexpr double legacy_pi = 3.14159265359;
    constexpr double degree = legacy_pi / 180.0;
    return {
        0.206881602991773,
        attached_inertia_kg_m2,
        {
            bmw_configuration_cylinder(30.0 * degree),
            bmw_configuration_cylinder(150.0 * degree),
            bmw_configuration_cylinder(270.0 * degree),
            bmw_configuration_cylinder(270.0 * degree),
            bmw_configuration_cylinder(150.0 * degree),
            bmw_configuration_cylinder(30.0 * degree),
        },
    };
}

[[nodiscard]] const simulation::CenteredSliderCrankCycleMeanInertia &require_result(
    const simulation::CenteredSliderCrankCycleMeanInertiaCalculation &calculation) {
    const auto *result =
        std::get_if<simulation::CenteredSliderCrankCycleMeanInertia>(&calculation);
    expect(result != nullptr, "valid slider-crank mechanism was rejected");
    return *result;
}

[[nodiscard]] const simulation::CenteredSliderCrankConfigurationInertia &
require_configuration(
    const simulation::CenteredSliderCrankConfigurationInertiaCalculation &calculation) {
    const auto *result =
        std::get_if<simulation::CenteredSliderCrankConfigurationInertia>(&calculation);
    expect(result != nullptr,
           "valid configuration-dependent slider-crank sample was rejected");
    return *result;
}

void test_method_identity_is_bound_to_the_fixed_quadrature() {
    const auto descriptor =
        simulation::centered_slider_crank_cycle_mean_inertia_method_descriptor();
    const auto &identity =
        simulation::centered_slider_crank_cycle_mean_inertia_method_identity();
    expect(identity.id ==
                   simulation::kCenteredSliderCrankCycleMeanEquivalentInertiaMethodId &&
               identity.version ==
                   simulation::
                       kCenteredSliderCrankCycleMeanEquivalentInertiaMethodVersion &&
               !identity.configuration_sha256.is_zero() &&
               descriptor.find("quadrature=4096-point-uniform-midpoint") !=
                   std::string_view::npos,
           "cycle-mean inertia method identity lost its fixed quadrature");
}

void test_crank_only_mechanism_preserves_authored_inertia() {
    contract::LegacyMechanismProfile mechanism;
    mechanism.crank.authored_crank_inertia_kg_m2.value = 0.2;
    const auto result = require_result(
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(mechanism));
    expect(result.authored_crank_inertia_kg_m2 == 0.2 &&
               result.piston_translation_inertia_kg_m2 == 0.0 &&
               result.connecting_rod_translation_inertia_kg_m2 == 0.0 &&
               result.connecting_rod_rotation_inertia_kg_m2 == 0.0 &&
               result.engine_equivalent_inertia_kg_m2 == 0.2,
           "crank-only mechanism did not preserve its authored inertia");
}

void test_bmw_m52_cycle_mean_components_and_phase_invariance() {
    auto mechanism = bmw_mechanism();
    const auto result = require_result(
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(mechanism));
    expect_near(result.piston_translation_inertia_kg_m2, 0.0015194627749090956, 2.0e-15,
                "BMW piston cycle-mean inertia changed");
    expect_near(result.connecting_rod_translation_inertia_kg_m2, 0.0019945989575649754,
                2.0e-15, "BMW rod translation cycle-mean inertia changed");
    expect_near(result.connecting_rod_rotation_inertia_kg_m2, 0.0004729872942733615,
                2.0e-15, "BMW rod rotation cycle-mean inertia changed");
    expect_near(result.engine_equivalent_inertia_kg_m2, 0.2108686520185204, 3.0e-15,
                "BMW engine equivalent inertia changed");

    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        std::get<contract::LegacyDirectJournalKinematics>(
            mechanism.cylinders[index].kinematics)
            .journal_angle_rad.value =
                0.123456789 * static_cast<double>(index + 1U);
    }
    const auto shifted = require_result(
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(mechanism));
    expect(shifted == result, "full-cycle mean inertia changed with journal phase");
}

void test_invalid_inputs_return_typed_cylinder_evidence() {
    auto invalid_crank = bmw_mechanism();
    invalid_crank.crank.authored_crank_inertia_kg_m2.value =
        std::numeric_limits<double>::quiet_NaN();
    const auto crank_calculation =
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(invalid_crank);
    const auto *crank_error =
        std::get_if<simulation::CenteredSliderCrankCycleMeanInertiaError>(
            &crank_calculation);
    expect(crank_error != nullptr &&
               crank_error->issue ==
                   simulation::CenteredSliderCrankCycleMeanInertiaIssue::
                       nonfinite_authored_crank_inertia &&
               crank_error->cylinder_index ==
                   simulation::kNoCenteredSliderCrankInertiaCylinder,
           "invalid crank inertia returned the wrong typed evidence");

    auto invalid_rod = bmw_mechanism();
    invalid_rod.cylinders[3].parameters.connecting_rod_length_m.value = 0.042;
    const auto rod_calculation =
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(invalid_rod);
    const auto *rod_error =
        std::get_if<simulation::CenteredSliderCrankCycleMeanInertiaError>(
            &rod_calculation);
    expect(rod_error != nullptr &&
               rod_error->issue ==
                   simulation::CenteredSliderCrankCycleMeanInertiaIssue::
                       connecting_rod_not_longer_than_crank_radius &&
               rod_error->cylinder_index == 3U,
           "invalid rod geometry returned the wrong cylinder evidence");
}

void test_configuration_inertia_preserves_fixed_crank_and_attached_load() {
    const simulation::CenteredSliderCrankConfigurationInertiaPlan plan{
        0.2,
        0.05,
        {},
    };
    const auto sample = require_configuration(
        simulation::evaluate_centered_slider_crank_configuration_inertia(plan, 123.0));
    expect(sample.authored_crank_inertia_kg_m2 == 0.2 &&
               sample.attached_inertia_kg_m2 == 0.05 &&
               sample.piston_translation_inertia_kg_m2 == 0.0 &&
               sample.connecting_rod_translation_inertia_kg_m2 == 0.0 &&
               sample.connecting_rod_rotation_inertia_kg_m2 == 0.0 &&
               sample.total_inertia_kg_m2 == 0.25 &&
               sample.total_derivative_kg_m2_per_rad == 0.0,
           "fixed crank and attached inertia changed with configuration");
}

void test_configuration_inertia_dead_center_matches_closed_form() {
    const simulation::CenteredSliderCrankConfigurationInertiaPlan plan{
        0.2,
        0.05,
        {bmw_configuration_cylinder(0.0)},
    };
    expect(plan.cylinders.size() == 1U &&
               plan.cylinders.front().geometric_tdc_rad == 0.0,
           "compiled cylinder phase did not preserve geometric TDC");

    const auto sample = require_configuration(
        simulation::evaluate_centered_slider_crank_configuration_inertia(plan, 0.0));
    const auto &cylinder = plan.cylinders.front();
    const double expected_rod_translation = cylinder.connecting_rod_mass_kg *
                                            cylinder.crank_radius_m *
                                            cylinder.crank_radius_m / 4.0;
    const double expected_rod_rotation =
        cylinder.connecting_rod_inertia_kg_m2 * cylinder.crank_radius_m *
        cylinder.crank_radius_m /
        (cylinder.connecting_rod_length_m * cylinder.connecting_rod_length_m);
    expect(sample.piston_translation_inertia_kg_m2 == 0.0,
           "dead-center piston translation inertia was nonzero");
    expect_near(sample.connecting_rod_translation_inertia_kg_m2,
                expected_rod_translation, 1.0e-18,
                "dead-center rod translation inertia changed");
    expect_near(sample.connecting_rod_rotation_inertia_kg_m2, expected_rod_rotation,
                1.0e-18, "dead-center rod rotation inertia changed");
    expect(sample.total_derivative_kg_m2_per_rad == 0.0,
           "dead-center inertia derivative was nonzero");
}

void test_analytic_configuration_derivative_matches_kinetic_energy_coefficient() {
    const auto engine_only_plan = bmw_configuration_plan(0.0);
    const auto source_audit_sample = require_configuration(
        simulation::evaluate_centered_slider_crank_configuration_inertia(
            engine_only_plan, 0.0));
    expect_near(source_audit_sample.total_inertia_kg_m2, 0.21086713805253998, 3.0e-15,
                "BMW configuration inertia changed at the source audit angle");
    expect_near(source_audit_sample.total_derivative_kg_m2_per_rad,
                -0.002205458110961244, 3.0e-15,
                "BMW configuration inertia derivative changed at the source "
                "audit angle");

    const auto plan = bmw_configuration_plan(0.05);
    constexpr double step_rad = 1.0e-6;
    for (const double crank_angle_rad : {0.73, 2.21, 4.87, 8.43}) {
        const auto center = require_configuration(
            simulation::evaluate_centered_slider_crank_configuration_inertia(
                plan, crank_angle_rad));
        const auto before = require_configuration(
            simulation::evaluate_centered_slider_crank_configuration_inertia(
                plan, crank_angle_rad - step_rad));
        const auto after = require_configuration(
            simulation::evaluate_centered_slider_crank_configuration_inertia(
                plan, crank_angle_rad + step_rad));
        const double finite_difference =
            (after.total_inertia_kg_m2 - before.total_inertia_kg_m2) / (2.0 * step_rad);
        expect_near(center.total_derivative_kg_m2_per_rad, finite_difference, 5.0e-11,
                    "analytic configuration inertia derivative changed");

        constexpr double angular_speed_rad_s = 321.0;
        const double zero_torque_acceleration =
            -0.5 * center.total_derivative_kg_m2_per_rad * angular_speed_rad_s *
            angular_speed_rad_s / center.total_inertia_kg_m2;
        const double reconstructed_generalized_torque =
            center.total_inertia_kg_m2 * zero_torque_acceleration +
            0.5 * center.total_derivative_kg_m2_per_rad * angular_speed_rad_s *
                angular_speed_rad_s;
        expect_near(reconstructed_generalized_torque, 0.0, 2.0e-13,
                    "configuration inertia did not reconstruct zero generalized "
                    "torque");
    }
}

void test_configuration_inertia_reconstructs_pristine_coast_tick() {
    const auto plan = bmw_configuration_plan(0.0);
    constexpr double kCleanThetaRad = 0x1.0bf8d8db05a4ep+3;
    constexpr double kAngularSpeedRadS = 0x1.4617360a911a4p+9;
    constexpr double kAppliedGeneralizedTorqueNm = -0x1.9f8ac5f6f033cp+5;
    constexpr double kPristineAccelerationRadS2 = 0x1.ee3b030f449e8p+10;

    const auto inertia = require_configuration(
        simulation::evaluate_centered_slider_crank_configuration_inertia(
            plan, kCleanThetaRad));
    expect_near(inertia.total_inertia_kg_m2, 0x1.afdf1a80212bap-3, 3.0e-15,
                "BMW inertia changed at pristine coast tick 75278");
    expect_near(inertia.total_derivative_kg_m2_per_rad, -0x1.21153cd61c5f7p-9, 3.0e-15,
                "BMW inertia derivative changed at pristine coast tick 75278");

    const auto motion =
        simulation::detail::advance_positive_speed_configuration_dependent_crank_zoh({
            inertia.total_inertia_kg_m2,
            inertia.total_derivative_kg_m2_per_rad,
            {kCleanThetaRad, kAngularSpeedRadS},
            kAppliedGeneralizedTorqueNm,
            0.0,
            1.0e-4,
        });
    const auto *step = std::get_if<
        simulation::detail::PositiveSpeedConfigurationDependentCrankZohStep>(&motion);
    expect(step != nullptr,
           "configuration-dependent crank rejected pristine coast tick 75278");
    expect_near(step->angular_acceleration_rad_s2, 0x1.ee7f229b59e62p+10, 2.0e-10,
                "clean reduced acceleration changed at pristine coast tick 75278");
    expect_near(step->angular_acceleration_rad_s2, kPristineAccelerationRadS2, 1.1,
                "clean reduced acceleration escaped pristine coast tick 75278");
}

void test_configuration_inertia_rejects_invalid_phase_inputs_with_typed_evidence() {
    auto plan = bmw_configuration_plan(0.0);
    plan.cylinders[4].geometric_tdc_rad = std::numeric_limits<double>::infinity();
    const auto invalid_plan_calculation =
        simulation::evaluate_centered_slider_crank_configuration_inertia(plan, 0.0);
    const auto *plan_error =
        std::get_if<simulation::CenteredSliderCrankConfigurationInertiaError>(
            &invalid_plan_calculation);
    expect(plan_error != nullptr &&
               plan_error->issue ==
                   simulation::CenteredSliderCrankConfigurationInertiaIssue::
                       nonfinite_plan_value &&
               plan_error->cylinder_index == 4U,
           "nonfinite geometric TDC returned the wrong typed evidence");

    plan.cylinders[4].geometric_tdc_rad = 0.0;
    const auto calculation =
        simulation::evaluate_centered_slider_crank_configuration_inertia(
            plan, std::numeric_limits<double>::quiet_NaN());
    const auto *evaluate_error =
        std::get_if<simulation::CenteredSliderCrankConfigurationInertiaError>(
            &calculation);
    expect(evaluate_error != nullptr &&
               evaluate_error->issue ==
                   simulation::CenteredSliderCrankConfigurationInertiaIssue::
                       nonfinite_crank_angle,
           "nonfinite crank angle returned the wrong typed evidence");
}

void run_tests() {
    test_method_identity_is_bound_to_the_fixed_quadrature();
    test_crank_only_mechanism_preserves_authored_inertia();
    test_bmw_m52_cycle_mean_components_and_phase_invariance();
    test_invalid_inputs_return_typed_cylinder_evidence();
    test_configuration_inertia_preserves_fixed_crank_and_attached_load();
    test_configuration_inertia_dead_center_matches_closed_form();
    test_analytic_configuration_derivative_matches_kinetic_energy_coefficient();
    test_configuration_inertia_reconstructs_pristine_coast_tick();
    test_configuration_inertia_rejects_invalid_phase_inputs_with_typed_evidence();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "centered slider-crank equivalent inertia failure: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
