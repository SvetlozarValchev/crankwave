#include "simulation/centered_slider_crank_equivalent_inertia.hpp"

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
    cylinder.parameters.crank_radius_m.value = 0.042;
    cylinder.parameters.connecting_rod_length_m.value = 0.135;
    cylinder.parameters.piston_mass_kg.value = 0.280;
    cylinder.parameters.connecting_rod_mass_kg.value = 0.300;
    cylinder.parameters.connecting_rod_inertia_kg_m2.value = 0.0015884918028487504;
    cylinder.parameters.journal_angle_rad.value = phase_rad;
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

[[nodiscard]] const simulation::CenteredSliderCrankCycleMeanInertia &require_result(
    const simulation::CenteredSliderCrankCycleMeanInertiaCalculation &calculation) {
    const auto *result =
        std::get_if<simulation::CenteredSliderCrankCycleMeanInertia>(&calculation);
    expect(result != nullptr, "valid slider-crank mechanism was rejected");
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
        mechanism.cylinders[index].parameters.journal_angle_rad.value =
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

void run_tests() {
    test_method_identity_is_bound_to_the_fixed_quadrature();
    test_crank_only_mechanism_preserves_authored_inertia();
    test_bmw_m52_cycle_mean_components_and_phase_invariance();
    test_invalid_inputs_return_typed_cylinder_evidence();
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
