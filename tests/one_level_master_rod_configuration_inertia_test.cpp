#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/one_level_master_rod_configuration_inertia.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <variant>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::simulation;

void expect(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_near(const double actual, const double expected,
                 const double absolute_tolerance, const double relative_tolerance,
                 const char *message) {
    const double tolerance =
        absolute_tolerance + relative_tolerance * std::abs(expected);
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message};
    }
}

constexpr double source_inch() {
    return (1.0 / 100.0) * 2.54;
}

constexpr double source_cubic_centimeter() {
    return (1.0 / 100.0) * (1.0 / 100.0) * (1.0 / 100.0);
}

[[nodiscard]] OneLevelMasterRodMechanismCylinderPlan
planned_cylinder(const std::size_t index, const OneLevelMasterRodCylinder &geometry,
                 OneLevelMasterRodCylinderKinematicsPlan kinematics,
                 const double piston_mass_kg, const double rod_mass_kg,
                 const double rod_inertia_kg_m2,
                 const double rod_center_from_big_end_m) {
    constexpr double bore_m = 5.0 * source_inch();
    OneLevelMasterRodMechanismCylinderPlan planned;
    planned.crankshaft_id = CrankshaftId{7};
    planned.bank_id = BankId{static_cast<std::uint32_t>(index + 1U)};
    planned.chamber_volume_id = GasVolumeId{static_cast<std::uint32_t>(100U + index)};
    planned.exhaust_route_id = RouteId{static_cast<std::uint32_t>(200U + index)};
    planned.bore_m = bore_m;
    planned.piston_area_m2 = geometry.piston_area_m2;
    planned.fixed_geometry_volume_m3 =
        geometry.head_chamber_volume_m3 +
        geometry.piston_area_m2 *
            (geometry.deck_height_m - geometry.piston_wrist_pin_position_m -
             geometry.piston_compression_height_m);
    planned.ignition_wire_angle_rad = 0.1 * static_cast<double>(index);
    planned.piston_mass_kg = piston_mass_kg;
    planned.connecting_rod_mass_kg = rod_mass_kg;
    planned.connecting_rod_inertia_kg_m2 = rod_inertia_kg_m2;
    planned.connecting_rod_center_of_mass_from_big_end_m = rod_center_from_big_end_m;
    planned.kinematics = std::move(kinematics);
    return planned;
}

[[nodiscard]] OneLevelMasterRodMechanismKinematicsPlan radial_five_plan() {
    constexpr double inch = source_inch();
    constexpr double bore_m = 5.0 * inch;
    constexpr double piston_area_m2 = kLegacyPi * bore_m * bore_m / 4.0;
    constexpr double deck_height_m = 15.75 * inch;
    constexpr double compression_height_m = 1.0 * inch;
    constexpr double head_volume_m3 = 290.0 * source_cubic_centimeter();
    constexpr double degree = kLegacyPi / 180.0;
    constexpr double master_bank_angle_rad = 0.13;

    const OneLevelMasterRodDriver driver{
        2.75 * inch,
        0.27,
        master_bank_angle_rad,
        12.0 * inch,
    };
    const OneLevelMasterRodCylinder root_geometry{
        CylinderId{1}, master_bank_angle_rad,          12.0 * inch, piston_area_m2,
        deck_height_m, compression_height_m,           0.0,         head_volume_m3,
        0.0,           OneLevelMasterRodRootJournal{},
    };

    OneLevelMasterRodMechanismKinematicsPlan plan;
    plan.engine_id = EngineId{9};
    plan.engine_profile_id = "radial-five-inertia";
    plan.output_crankshaft_id = CrankshaftId{7};
    plan.crank_tdc_reference_rad = 0.37;
    plan.rigid_crank_group = {1U, 0.12, 2.5};
    plan.cylinders.push_back(planned_cylinder(
        0U, root_geometry, OneLevelMasterRodDirectRootPlan{driver, root_geometry}, 0.62,
        0.81, 0.0024, 0.37 * root_geometry.connecting_rod_length_m));

    for (std::size_t index = 1U; index < 5U; ++index) {
        const double phase_rad = static_cast<double>(index) * 72.0 * degree;
        const OneLevelMasterRodCylinder slave_geometry{
            CylinderId{static_cast<std::uint32_t>(index + 1U)},
            master_bank_angle_rad + phase_rad,
            9.1 * inch,
            piston_area_m2,
            deck_height_m,
            compression_height_m,
            0.0,
            head_volume_m3,
            0.0,
            OneLevelMasterRodSlavePin{2.9 * inch, phase_rad},
        };
        constexpr std::array center_fractions{0.5, 0.3, 0.7, 0.0};
        plan.cylinders.push_back(planned_cylinder(
            index, slave_geometry,
            OneLevelMasterRodSlaveAttachmentPlan{0U, slave_geometry},
            0.39 + 0.03 * static_cast<double>(index),
            0.52 + 0.04 * static_cast<double>(index),
            0.0009 + 0.0002 * static_cast<double>(index),
            center_fractions[index - 1U] * slave_geometry.connecting_rod_length_m));
    }
    return plan;
}

[[nodiscard]] OneLevelMasterRodMechanismKinematicsPlan pristine_radial_five_plan() {
    auto plan = radial_five_plan();
    constexpr double degree = kLegacyPi / 180.0;
    plan.engine_profile_id = "pristine-radial-five-inertia-oracle";
    plan.crank_tdc_reference_rad = 67.5 * degree;
    plan.rigid_crank_group.authored_crank_inertia_kg_m2 = 0.8581916343875;
    auto &root =
        std::get<OneLevelMasterRodDirectRootPlan>(plan.cylinders.front().kinematics);
    root.driver.crank_journal_global_phase_rad = 0.0;
    root.driver.master_bank_angle_rad = 0.0;
    root.cylinder.bank_angle_rad = 0.0;

    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        auto &planned = plan.cylinders[index];
        auto *geometry = std::visit(
            [](auto &kinematics) { return &kinematics.cylinder; }, planned.kinematics);
        geometry->bank_angle_rad = static_cast<double>(index) * 72.0 * degree;
        planned.piston_mass_kg = 0.2;
        planned.connecting_rod_mass_kg = 0.1;
        planned.connecting_rod_inertia_kg_m2 = 0.0015884918028487504;
        planned.connecting_rod_center_of_mass_from_big_end_m =
            0.5 * geometry->connecting_rod_length_m;
    }
    return plan;
}

[[nodiscard]] OneLevelMasterRodArticulatedState
require_state(const OneLevelMasterRodArticulatedStateCalculation &calculation) {
    const auto *state = std::get_if<OneLevelMasterRodArticulatedState>(&calculation);
    expect(state != nullptr, "valid articulated state was rejected");
    return *state;
}

[[nodiscard]] OneLevelMasterRodConfigurationInertia
require_inertia(const OneLevelMasterRodConfigurationInertiaCalculation &calculation) {
    const auto *inertia =
        std::get_if<OneLevelMasterRodConfigurationInertia>(&calculation);
    expect(inertia != nullptr, "valid master-rod configuration inertia was rejected");
    return *inertia;
}

void expect_error(
    const OneLevelMasterRodConfigurationInertiaCalculation &calculation,
    const OneLevelMasterRodConfigurationInertiaIssue expected_issue,
    const std::size_t expected_index = kNoOneLevelMasterRodInertiaCylinder) {
    const auto *actual =
        std::get_if<OneLevelMasterRodConfigurationInertiaError>(&calculation);
    if (actual == nullptr || actual->issue != expected_issue ||
        actual->cylinder_index != expected_index) {
        std::cerr << "expected issue=" << static_cast<int>(expected_issue)
                  << " index=" << expected_index;
        if (actual != nullptr) {
            std::cerr << " actual issue=" << static_cast<int>(actual->issue)
                      << " index=" << actual->cylinder_index;
        }
        std::cerr << '\n';
    }
    expect(actual != nullptr && actual->issue == expected_issue &&
               actual->cylinder_index == expected_index,
           "configuration inertia returned the wrong fail-closed error");
}

void test_theta_contract_and_shared_articulated_state() {
    const auto plan = radial_five_plan();
    for (const double theta_rad : {-0.41, 0.73, 5.27}) {
        const auto state_calculation =
            evaluate_one_level_master_rod_articulated_state(plan, theta_rad);
        const auto &state = require_state(state_calculation);
        expect(state.cylinders.size() == plan.cylinders.size(),
               "articulated state lost a planned cylinder");

        // The public contract is positive dynamic-crank theta, while pristine
        // body psi decreases. Check that conversion directly against the existing
        // one-level piston evaluator at several angles and for every slave.
        const double body_angle_psi_rad = plan.crank_tdc_reference_rad - theta_rad;
        for (std::size_t index = 0; index < state.cylinders.size(); ++index) {
            const auto legacy = evaluate_one_level_master_rod_plan(
                plan, index, body_angle_psi_rad, 173.0);
            expect(legacy.valid, "theta-contract comparison geometry was rejected");
            const auto &geometry =
                std::visit([](const auto &kinematics) { return &kinematics.cylinder; },
                           plan.cylinders[index].kinematics);
            const double axis_angle_rad = geometry->bank_angle_rad + kLegacyPi / 2.0;
            const double axis_x = std::cos(axis_angle_rad);
            const double axis_y = std::sin(axis_angle_rad);
            const auto &wrist = state.cylinders[index].wrist_pin;
            const double position_m = wrist.x_m * axis_x + wrist.y_m * axis_y;
            const double position_first =
                wrist.dx_dtheta_m_per_rad * axis_x + wrist.dy_dtheta_m_per_rad * axis_y;
            expect_near(position_m, legacy.piston_axis_position_m, 3.0e-15, 2.0e-14,
                        "shared wrist position disagrees with pristine geometry");
            expect_near(position_first, legacy.piston_axis_derivative_m_per_rad,
                        3.0e-15, 2.0e-14,
                        "theta derivative changed sign relative to mechanics");

            const auto &body = state.cylinders[index];
            const double rod_x = body.wrist_pin.x_m - body.big_end.x_m;
            const double rod_y = body.wrist_pin.y_m - body.big_end.y_m;
            const double rod_dx =
                body.wrist_pin.dx_dtheta_m_per_rad - body.big_end.dx_dtheta_m_per_rad;
            const double rod_dy =
                body.wrist_pin.dy_dtheta_m_per_rad - body.big_end.dy_dtheta_m_per_rad;
            const double rod_d2x = body.wrist_pin.d2x_dtheta2_m_per_rad2 -
                                   body.big_end.d2x_dtheta2_m_per_rad2;
            const double rod_d2y = body.wrist_pin.d2y_dtheta2_m_per_rad2 -
                                   body.big_end.d2y_dtheta2_m_per_rad2;
            expect_near(
                rod_x * rod_x + rod_y * rod_y,
                geometry->connecting_rod_length_m * geometry->connecting_rod_length_m,
                2.0e-16, 2.0e-14, "articulated rod endpoints changed authored length");
            expect_near(rod_x * rod_dx + rod_y * rod_dy, 0.0, 2.0e-15, 0.0,
                        "first rod-length constraint derivative changed");
            expect_near(
                rod_dx * rod_dx + rod_dy * rod_dy + rod_x * rod_d2x + rod_y * rod_d2y,
                0.0, 3.0e-14, 0.0, "second rod-length constraint derivative changed");

            const double fraction =
                plan.cylinders[index].connecting_rod_center_of_mass_from_big_end_m /
                geometry->connecting_rod_length_m;
            expect_near(
                body.rod_center_of_mass.x_m,
                body.big_end.x_m + fraction * (body.wrist_pin.x_m - body.big_end.x_m),
                3.0e-16, 2.0e-14, "rod COM is not on the authored B-to-W fraction");
            expect_near(body.rod_center_of_mass.dx_dtheta_m_per_rad,
                        body.big_end.dx_dtheta_m_per_rad +
                            fraction * (body.wrist_pin.dx_dtheta_m_per_rad -
                                        body.big_end.dx_dtheta_m_per_rad),
                        3.0e-16, 2.0e-14,
                        "rod COM first derivative lost the authored fraction");
        }
    }
}

void test_inertia_is_the_exact_shared_state_energy_reduction() {
    const auto plan = radial_five_plan();
    constexpr double attached_inertia_kg_m2 = 0.045;
    constexpr double theta_rad = 0.731;
    constexpr double omega_rad_s = 137.0;
    const auto state_calculation =
        evaluate_one_level_master_rod_articulated_state(plan, theta_rad);
    const auto inertia_calculation =
        evaluate_one_level_master_rod_configuration_inertia(
            plan, attached_inertia_kg_m2, theta_rad);
    const auto &state = require_state(state_calculation);
    const auto &inertia = require_inertia(inertia_calculation);

    double piston_m = 0.0;
    double rod_translation_m = 0.0;
    double rod_rotation_m = 0.0;
    double piston_prime = 0.0;
    double rod_translation_prime = 0.0;
    double rod_rotation_prime = 0.0;
    for (std::size_t index = 0; index < state.cylinders.size(); ++index) {
        const auto &planned = plan.cylinders[index];
        const auto &body = state.cylinders[index];
        const auto &wrist = body.wrist_pin;
        const auto &center = body.rod_center_of_mass;
        piston_m += planned.piston_mass_kg *
                    (wrist.dx_dtheta_m_per_rad * wrist.dx_dtheta_m_per_rad +
                     wrist.dy_dtheta_m_per_rad * wrist.dy_dtheta_m_per_rad);
        rod_translation_m += planned.connecting_rod_mass_kg *
                             (center.dx_dtheta_m_per_rad * center.dx_dtheta_m_per_rad +
                              center.dy_dtheta_m_per_rad * center.dy_dtheta_m_per_rad);
        rod_rotation_m += planned.connecting_rod_inertia_kg_m2 *
                          body.rod_angle_first_derivative_rad_per_rad *
                          body.rod_angle_first_derivative_rad_per_rad;
        piston_prime += 2.0 * planned.piston_mass_kg *
                        (wrist.dx_dtheta_m_per_rad * wrist.d2x_dtheta2_m_per_rad2 +
                         wrist.dy_dtheta_m_per_rad * wrist.d2y_dtheta2_m_per_rad2);
        rod_translation_prime +=
            2.0 * planned.connecting_rod_mass_kg *
            (center.dx_dtheta_m_per_rad * center.d2x_dtheta2_m_per_rad2 +
             center.dy_dtheta_m_per_rad * center.d2y_dtheta2_m_per_rad2);
        rod_rotation_prime += 2.0 * planned.connecting_rod_inertia_kg_m2 *
                              body.rod_angle_first_derivative_rad_per_rad *
                              body.rod_angle_second_derivative_rad_per_rad2;
    }
    expect(inertia.authored_crank_inertia_kg_m2 ==
                   plan.rigid_crank_group.authored_crank_inertia_kg_m2 &&
               inertia.attached_inertia_kg_m2 == attached_inertia_kg_m2,
           "constant inertias were not included exactly once");
    expect_near(inertia.piston_translation_inertia_kg_m2, piston_m, 1.0e-18, 1.0e-15,
                "piston kinetic energy was not preserved");
    expect_near(inertia.connecting_rod_translation_inertia_kg_m2, rod_translation_m,
                1.0e-18, 1.0e-15,
                "root/slave rod translation energy was not preserved");
    expect_near(inertia.connecting_rod_rotation_inertia_kg_m2, rod_rotation_m, 1.0e-18,
                1.0e-15, "root/slave rod rotation energy was not preserved");
    expect_near(inertia.piston_translation_derivative_kg_m2_per_rad, piston_prime,
                1.0e-18, 1.0e-15,
                "piston inertia derivative did not reduce shared state");
    expect_near(inertia.connecting_rod_translation_derivative_kg_m2_per_rad,
                rod_translation_prime, 1.0e-18, 1.0e-15,
                "rod translation derivative did not reduce shared state");
    expect_near(inertia.connecting_rod_rotation_derivative_kg_m2_per_rad,
                rod_rotation_prime, 1.0e-18, 1.0e-15,
                "rod rotation derivative did not reduce shared state");

    const double component_total = plan.rigid_crank_group.authored_crank_inertia_kg_m2 +
                                   attached_inertia_kg_m2 + piston_m +
                                   rod_translation_m + rod_rotation_m;
    expect_near(inertia.total_inertia_kg_m2, component_total, 1.0e-18, 1.0e-15,
                "whole-mechanism kinetic energy double-counted a body");
    const double direct_energy_j = 0.5 * component_total * omega_rad_s * omega_rad_s;
    expect_near(0.5 * inertia.total_inertia_kg_m2 * omega_rad_s * omega_rad_s,
                direct_energy_j, 2.0e-13, 1.0e-15,
                "configuration inertia did not preserve total kinetic energy");
}

void test_analytic_derivative_matches_tight_central_difference() {
    const auto plan = radial_five_plan();
    constexpr double attached_inertia_kg_m2 = 0.045;
    constexpr double step_rad = 1.0e-5;
    for (const double theta_rad : {-1.17, 0.13, 2.71, 8.43}) {
        const auto &center =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad));
        const auto &before =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad - step_rad));
        const auto &after =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad + step_rad));
        const double central_difference =
            (after.total_inertia_kg_m2 - before.total_inertia_kg_m2) / (2.0 * step_rad);
        if (std::abs(center.total_derivative_kg_m2_per_rad - central_difference) >
            2.0e-10 + 2.0e-8 * std::abs(central_difference)) {
            std::cerr << "theta=" << theta_rad
                      << " analytic=" << center.total_derivative_kg_m2_per_rad
                      << " finite-difference=" << central_difference << '\n';
        }
        expect_near(center.total_derivative_kg_m2_per_rad, central_difference, 2.0e-10,
                    2.0e-8, "analytic M-prime disagrees with tight central difference");
    }
}

void test_positive_finite_and_two_pi_periodic() {
    const auto plan = radial_five_plan();
    constexpr double attached_inertia_kg_m2 = 0.045;
    constexpr double two_pi = 2.0 * kLegacyPi;
    for (std::size_t sample = 0; sample < 257U; ++sample) {
        const double theta_rad = -two_pi + two_pi * static_cast<double>(sample) / 256.0;
        const auto &inertia =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad));
        expect(std::isfinite(inertia.total_inertia_kg_m2) &&
                   std::isfinite(inertia.total_derivative_kg_m2_per_rad) &&
                   inertia.total_inertia_kg_m2 > 0.0 &&
                   inertia.piston_translation_inertia_kg_m2 >= 0.0 &&
                   inertia.connecting_rod_translation_inertia_kg_m2 >= 0.0 &&
                   inertia.connecting_rod_rotation_inertia_kg_m2 >= 0.0,
               "full-cycle inertia lost positivity or finiteness");
    }

    for (const double theta_rad : {-0.91, 0.0, 1.37, 5.4}) {
        const auto &first =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad));
        const auto &periodic =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad + two_pi));
        expect_near(periodic.total_inertia_kg_m2, first.total_inertia_kg_m2, 2.0e-15,
                    3.0e-14, "M(theta) is not two-pi periodic");
        expect_near(periodic.total_derivative_kg_m2_per_rad,
                    first.total_derivative_kg_m2_per_rad, 2.0e-14, 3.0e-13,
                    "M-prime(theta) is not two-pi periodic");
    }
}

void test_direct_root_only_reduces_to_centered_slider_evaluator() {
    auto plan = radial_five_plan();
    plan.cylinders.erase(plan.cylinders.begin() + 1, plan.cylinders.end());
    const auto &root =
        std::get<OneLevelMasterRodDirectRootPlan>(plan.cylinders.front().kinematics);
    constexpr double attached_inertia_kg_m2 = 0.045;
    const double geometric_tdc_rad =
        plan.crank_tdc_reference_rad + root.driver.crank_journal_global_phase_rad -
        (root.driver.master_bank_angle_rad + kLegacyPi / 2.0);
    CenteredSliderCrankConfigurationInertiaPlan centered;
    centered.authored_crank_inertia_kg_m2 =
        plan.rigid_crank_group.authored_crank_inertia_kg_m2;
    centered.attached_inertia_kg_m2 = attached_inertia_kg_m2;
    centered.cylinders.push_back({
        geometric_tdc_rad,
        root.driver.crank_radius_m,
        root.driver.master_connecting_rod_length_m,
        plan.cylinders.front().connecting_rod_center_of_mass_from_big_end_m,
        plan.cylinders.front().piston_mass_kg,
        plan.cylinders.front().connecting_rod_mass_kg,
        plan.cylinders.front().connecting_rod_inertia_kg_m2,
    });

    for (const double theta_rad : {-0.4, 0.0, 0.73, 2.7, 7.1}) {
        const auto &radial =
            require_inertia(evaluate_one_level_master_rod_configuration_inertia(
                plan, attached_inertia_kg_m2, theta_rad));
        const auto centered_calculation =
            evaluate_centered_slider_crank_configuration_inertia(centered, theta_rad);
        const auto *direct =
            std::get_if<CenteredSliderCrankConfigurationInertia>(&centered_calculation);
        expect(direct != nullptr, "centered direct-root reduction was rejected");
        expect_near(radial.piston_translation_inertia_kg_m2,
                    direct->piston_translation_inertia_kg_m2, 3.0e-17, 8.0e-14,
                    "direct root piston inertia differs from centered evaluator");
        expect_near(radial.connecting_rod_translation_inertia_kg_m2,
                    direct->connecting_rod_translation_inertia_kg_m2, 3.0e-17, 8.0e-14,
                    "direct root rod translation differs from centered evaluator");
        expect_near(radial.connecting_rod_rotation_inertia_kg_m2,
                    direct->connecting_rod_rotation_inertia_kg_m2, 3.0e-17, 8.0e-14,
                    "direct root rod rotation differs from centered evaluator");
        expect_near(radial.total_inertia_kg_m2, direct->total_inertia_kg_m2, 3.0e-16,
                    8.0e-14,
                    "direct root total inertia differs from centered evaluator");
        expect_near(radial.total_derivative_kg_m2_per_rad,
                    direct->total_derivative_kg_m2_per_rad, 3.0e-15, 2.0e-12,
                    "direct root M-prime differs from centered evaluator");
    }
}

void test_pristine_radial_five_effective_inertia_oracle() {
    const auto plan = pristine_radial_five_plan();
    constexpr double degree = kLegacyPi / 180.0;
    // An independent pristine-geometry solver converged these values with nested
    // Richardson extrapolation. Its rows use decreasing body angle psi, so map
    // each state and flip M-prime into this evaluator's positive theta contract.
    // The tolerances cover the residual extrapolation error without admitting the
    // source constraint solver's timestep- and iteration-dependent drift.
    constexpr std::array body_angle_degrees{0.0, 72.0, 90.0, 180.0, 270.0};
    constexpr std::array expected_inertia_kg_m2{
        0.86261572442769807, 0.86229050368956373, 0.86228863986542725,
        0.86261572442769674, 0.86318882548678666,
    };
    constexpr std::array expected_dynamic_theta_derivative{
        4.33279256728536e-4, 2.12416447098462e-5, 0.0, -4.33279256700780e-4, 0.0,
    };
    for (std::size_t index = 0; index < body_angle_degrees.size(); ++index) {
        const double theta_rad =
            plan.crank_tdc_reference_rad - body_angle_degrees[index] * degree;
        const auto inertia = require_inertia(
            evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, theta_rad));
        expect_near(inertia.total_inertia_kg_m2, expected_inertia_kg_m2[index], 1.0e-11,
                    0.0, "source radial-five effective inertia differs from pristine");
        expect_near(inertia.total_derivative_kg_m2_per_rad,
                    expected_dynamic_theta_derivative[index], 1.0e-9, 0.0,
                    "source radial-five M-prime differs from pristine");
    }
}

void test_invalid_plan_and_state_fail_closed() {
    const auto valid = radial_five_plan();
    expect_error(evaluate_one_level_master_rod_configuration_inertia(
                     valid, 0.0, std::numeric_limits<double>::quiet_NaN()),
                 OneLevelMasterRodConfigurationInertiaIssue::nonfinite_crank_angle);
    expect_error(
        evaluate_one_level_master_rod_configuration_inertia(
            valid, std::numeric_limits<double>::infinity(), 0.0),
        OneLevelMasterRodConfigurationInertiaIssue::nonfinite_attached_inertia);
    expect_error(evaluate_one_level_master_rod_configuration_inertia(valid, -0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::negative_attached_inertia);

    auto plan = valid;
    plan.engine_id = {};
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_plan_identity);
    plan = valid;
    plan.rigid_crank_group.authored_crank_inertia_kg_m2 = 0.0;
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_rigid_crank_group);
    plan = valid;
    plan.cylinders.clear();
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::empty_cylinder_set);
    plan = valid;
    plan.cylinders[2].piston_mass_kg = -1.0;
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_mass_property, 2U);
    plan = valid;
    plan.cylinders[1].connecting_rod_mass_kg = 0.0;
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_mass_property, 1U);
    plan = valid;
    plan.cylinders[4].connecting_rod_inertia_kg_m2 = -0.0;
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_mass_property, 4U);
    plan = valid;
    plan.cylinders[3].connecting_rod_center_of_mass_from_big_end_m = 1.0;
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_center_of_mass,
                 3U);
    plan = valid;
    std::get<OneLevelMasterRodSlaveAttachmentPlan>(plan.cylinders[4].kinematics)
        .master_cylinder_index = plan.cylinders.size();
    expect_error(evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::invalid_slave_attachment,
                 4U);
    plan = valid;
    std::get<OneLevelMasterRodSlavePin>(
        std::get<OneLevelMasterRodSlaveAttachmentPlan>(plan.cylinders[1].kinematics)
            .cylinder.journal)
        .throw_radius_m = 1.0;
    expect_error(
        evaluate_one_level_master_rod_configuration_inertia(plan, 0.0, 0.0),
        OneLevelMasterRodConfigurationInertiaIssue::uncertified_full_cycle_geometry,
        1U);
    plan = valid;
    plan.crank_tdc_reference_rad = -std::numeric_limits<double>::max();
    expect_error(evaluate_one_level_master_rod_configuration_inertia(
                     plan, 0.0, std::numeric_limits<double>::max()),
                 OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
    plan = valid;
    plan.rigid_crank_group.authored_crank_inertia_kg_m2 =
        std::numeric_limits<double>::max();
    expect_error(evaluate_one_level_master_rod_configuration_inertia(
                     plan, std::numeric_limits<double>::max(), 0.0),
                 OneLevelMasterRodConfigurationInertiaIssue::nonfinite_derived_value);
}

} // namespace

int main() {
    try {
        test_theta_contract_and_shared_articulated_state();
        test_inertia_is_the_exact_shared_state_energy_reduction();
        test_analytic_derivative_matches_tight_central_difference();
        test_positive_finite_and_two_pi_periodic();
        test_direct_root_only_reduces_to_centered_slider_evaluator();
        test_pristine_radial_five_effective_inertia_oracle();
        test_invalid_plan_and_state_fail_closed();
    } catch (const std::exception &error) {
        std::cerr << "one-level master-rod configuration inertia failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
