#include "simulation/crankwave_transient_friction.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace crankwave::simulation;

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

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    expect(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

CrankwavePistonWallCylinderPlan m52_cylinder_plan() {
    return {
        0x1.6b2f7682c25fcp-8,  0.042,    0.135, 0.0675, 0.280, 0.300,
        0.0015884918028487504, 101325.0,
    };
}

void test_positive_speed_crank_friction_matches_pristine_constraint_limit() {
    constexpr double kM52TenPoundFeetNm =
        10.0 * (4.44822 * ((1.0 / 100.0) * 2.54 * 12.0));
    const auto calculation =
        calculate_crankwave_positive_speed_crank_friction({kM52TenPoundFeetNm});
    const auto *result =
        std::get_if<CrankwavePositiveSpeedCrankFriction>(&calculation);
    expect(result != nullptr && std::bit_cast<std::uint64_t>(result->torque_nm) ==
                                    std::bit_cast<std::uint64_t>(-kM52TenPoundFeetNm),
           "positive-running crank friction did not saturate at the pristine "
           "opposing torque limit");

    const auto zero = calculate_crankwave_positive_speed_crank_friction({0.0});
    const auto *zero_result = std::get_if<CrankwavePositiveSpeedCrankFriction>(&zero);
    expect(zero_result != nullptr && zero_result->torque_nm == 0.0,
           "zero authored crank friction was rejected or changed");
}

void test_invalid_authored_crank_friction_is_typed() {
    const auto nonfinite = calculate_crankwave_positive_speed_crank_friction(
        {std::numeric_limits<double>::infinity()});
    const auto *nonfinite_error =
        std::get_if<CrankwaveCrankFrictionError>(&nonfinite);
    expect(nonfinite_error != nullptr &&
               nonfinite_error->issue ==
                   CrankwaveCrankFrictionIssue::nonfinite_running_friction_torque,
           "nonfinite authored crank friction returned the wrong error");

    const auto negative = calculate_crankwave_positive_speed_crank_friction({-1.0});
    const auto *negative_error = std::get_if<CrankwaveCrankFrictionError>(&negative);
    expect(negative_error != nullptr &&
               negative_error->issue ==
                   CrankwaveCrankFrictionIssue::negative_running_friction_torque,
           "negative authored crank friction returned the wrong error");
}

void test_piston_friction_matches_pristine_written_order() {
    const CrankwavePistonWallStepInput input{
        m52_cylinder_plan(), 1.1, 314.1592653589793, 450000.0, 800.0,
    };
    const auto calculation = stage_crankwave_piston_wall_friction(input);
    const auto *stage = std::get_if<CrankwavePistonWallFrictionStage>(&calculation);
    expect(stage != nullptr, "valid pristine piston friction input was rejected");
    expect(stage->input == input,
           "piston friction stage did not retain its exact causal input");
    expect_bits(stage->slider_axis_derivative_m_per_rad, -0x1.5fab721bfcff8p-5,
                "slider-axis derivative changed written-order geometry");
    expect_bits(stage->signed_slider_axis_velocity_m_s, -0x1.af904c373750bp+3,
                "signed piston velocity changed written-order geometry");
    expect_bits(stage->friction_force_magnitude_n, 0x1.3dba2fa282927p+8,
                "piston friction force changed pristine constants or operation "
                "order");
    expect_bits(stage->signed_slider_axis_friction_force_n, 0x1.3dba2fa282927p+8,
                "piston friction force did not oppose signed slider velocity");
    expect_bits(stage->generalized_friction_torque_nm, -0x1.b477104d486dcp+3,
                "piston friction generalized torque changed written order");
}

void test_piston_friction_zero_speed_and_positive_running_sign() {
    const auto zero_calculation = stage_crankwave_piston_wall_friction({
        m52_cylinder_plan(),
        0.0,
        300.0,
        101325.0,
        1200.0,
    });
    const auto *zero =
        std::get_if<CrankwavePistonWallFrictionStage>(&zero_calculation);
    expect(zero != nullptr && zero->signed_slider_axis_velocity_m_s == 0.0 &&
               zero->friction_force_magnitude_n == 0.0 &&
               zero->signed_slider_axis_friction_force_n == 0.0 &&
               zero->generalized_friction_torque_nm == 0.0,
           "pristine low-speed attenuation did not reach zero at dead center");

    const auto attenuated_calculation = stage_crankwave_piston_wall_friction({
        m52_cylinder_plan(),
        0.01,
        1.0,
        101325.0,
        800.0,
    });
    const auto *attenuated =
        std::get_if<CrankwavePistonWallFrictionStage>(&attenuated_calculation);
    expect(attenuated != nullptr,
           "valid sub-millimeter-per-second friction stage was rejected");
    expect_bits(attenuated->signed_slider_axis_velocity_m_s, -0x1.20b3347f799f1p-11,
                "low-speed piston velocity changed written-order geometry");
    expect_bits(attenuated->friction_force_magnitude_n, 0x1.79fd55e180953p+0,
                "pristine sub-limit friction attenuation changed");
    expect_bits(attenuated->generalized_friction_torque_nm, -0x1.aa459a446b07cp-11,
                "attenuated piston friction torque changed");

    for (const double phase : {0.2, 1.1, 2.7, 3.7, 5.8}) {
        const auto calculation = stage_crankwave_piston_wall_friction({
            m52_cylinder_plan(),
            phase,
            300.0,
            101325.0,
            1200.0,
        });
        const auto *stage =
            std::get_if<CrankwavePistonWallFrictionStage>(&calculation);
        expect(stage != nullptr && stage->generalized_friction_torque_nm < 0.0,
               "positive-running piston friction did not oppose crank motion");
    }
}

void test_ideal_wall_reaction_matches_static_slider_crank_balance() {
    constexpr double kPhase = 1.1;
    constexpr double kChamberPressurePa = 450000.0;
    auto plan = m52_cylinder_plan();
    const auto stage_calculation = stage_crankwave_piston_wall_friction({
        plan,
        kPhase,
        0.0,
        kChamberPressurePa,
        0.0,
    });
    const auto *stage =
        std::get_if<CrankwavePistonWallFrictionStage>(&stage_calculation);
    expect(stage != nullptr, "static piston friction stage was rejected");

    const auto reaction_calculation =
        calculate_crankwave_next_piston_wall_reaction(*stage, 0.0);
    const auto *reaction =
        std::get_if<CrankwavePistonWallReaction>(&reaction_calculation);
    expect(reaction != nullptr, "static ideal wall reaction was rejected");

    const double sine = std::sin(kPhase);
    const double root =
        std::sqrt(plan.connecting_rod_length_m * plan.connecting_rod_length_m -
                  plan.crank_radius_m * plan.crank_radius_m * sine * sine);
    const double axial_pressure_force =
        plan.piston_area_m2 * (kChamberPressurePa - plan.crankcase_pressure_pa_abs);
    const double expected_wall_magnitude =
        axial_pressure_force * plan.crank_radius_m * sine / root;
    expect_near(reaction->signed_wall_on_piston_force_n, expected_wall_magnitude,
                1.0e-12, "static wall force did not match slider-crank force balance");
    expect_near(reaction->wall_reaction_magnitude_n, expected_wall_magnitude, 1.0e-12,
                "static wall magnitude did not match slider-crank force balance");
}

void test_nonmidpoint_rod_center_matches_dead_center_inverse_dynamics() {
    auto plan = m52_cylinder_plan();
    plan.connecting_rod_center_of_mass_from_crank_pin_m =
        0.4 * plan.connecting_rod_length_m;
    const auto stage_calculation = stage_crankwave_piston_wall_friction({
        plan,
        0.0,
        300.0,
        plan.crankcase_pressure_pa_abs,
        0.0,
    });
    const auto *stage =
        std::get_if<CrankwavePistonWallFrictionStage>(&stage_calculation);
    expect(stage != nullptr,
           "nonmidpoint dead-center piston friction stage was rejected");

    constexpr double angular_acceleration_rad_s2 = -120.0;
    const auto reaction_calculation = calculate_crankwave_next_piston_wall_reaction(
        *stage, angular_acceleration_rad_s2);
    const auto *reaction =
        std::get_if<CrankwavePistonWallReaction>(&reaction_calculation);
    expect(reaction != nullptr, "nonmidpoint dead-center wall reaction was rejected");

    const double center_fraction = plan.connecting_rod_center_of_mass_from_crank_pin_m /
                                   plan.connecting_rod_length_m;
    const double expected_signed_wall_n =
        angular_acceleration_rad_s2 * plan.crank_radius_m *
        (-plan.connecting_rod_inertia_kg_m2 /
             (plan.connecting_rod_length_m * plan.connecting_rod_length_m) +
         plan.connecting_rod_mass_kg * center_fraction * (1.0 - center_fraction));
    expect_near(reaction->signed_wall_on_piston_force_n, expected_signed_wall_n,
                1.0e-12,
                "nonmidpoint wall reaction missed the dead-center Newton-Euler "
                "closed form");
    expect_near(reaction->wall_reaction_magnitude_n, std::abs(expected_signed_wall_n),
                1.0e-12,
                "nonmidpoint wall magnitude disagrees with its signed reaction");
}

void test_dynamic_wall_reaction_is_retained_for_only_the_next_stage() {
    const CrankwavePistonWallStepInput input{
        m52_cylinder_plan(), 1.1, 314.1592653589793, 450000.0, 800.0,
    };
    const auto stage_calculation = stage_crankwave_piston_wall_friction(input);
    const auto *stage =
        std::get_if<CrankwavePistonWallFrictionStage>(&stage_calculation);
    expect(stage != nullptr, "dynamic piston friction stage was rejected");
    const auto reaction_calculation =
        calculate_crankwave_next_piston_wall_reaction(*stage, -120.0);
    const auto *reaction =
        std::get_if<CrankwavePistonWallReaction>(&reaction_calculation);
    expect(reaction != nullptr, "dynamic ideal wall reaction was rejected");
    expect_bits(reaction->signed_wall_on_piston_force_n, 0x1.780dceb4e247dp+8,
                "dynamic signed wall reaction changed inverse-dynamics order");
    expect_bits(reaction->wall_reaction_magnitude_n, 0x1.780dceb4e247dp+8,
                "dynamic wall magnitude changed inverse-dynamics order");

    // Calculating W_n cannot mutate the already-staged F_n. Only an explicitly
    // constructed next boundary may consume W_n.
    expect_bits(stage->friction_force_magnitude_n, 0x1.3dba2fa282927p+8,
                "next wall calculation fed back into current-step friction");
    auto next_input = input;
    next_input.retained_previous_wall_reaction_magnitude_n =
        reaction->wall_reaction_magnitude_n;
    const auto next_calculation = stage_crankwave_piston_wall_friction(next_input);
    const auto *next =
        std::get_if<CrankwavePistonWallFrictionStage>(&next_calculation);
    expect(next != nullptr &&
               std::bit_cast<std::uint64_t>(next->friction_force_magnitude_n) !=
                   std::bit_cast<std::uint64_t>(stage->friction_force_magnitude_n),
           "retained wall reaction did not affect the explicitly following stage");
}

void test_invalid_piston_wall_inputs_are_typed() {
    auto invalid_geometry =
        CrankwavePistonWallStepInput{m52_cylinder_plan(), 1.0, 100.0, 101325.0, 0.0};
    invalid_geometry.plan.connecting_rod_length_m =
        invalid_geometry.plan.crank_radius_m;
    const auto geometry_calculation =
        stage_crankwave_piston_wall_friction(invalid_geometry);
    const auto *geometry_error =
        std::get_if<CrankwavePistonWallError>(&geometry_calculation);
    expect(geometry_error != nullptr &&
               geometry_error->issue ==
                   CrankwavePistonWallIssue::invalid_slider_crank_geometry,
           "invalid piston geometry returned the wrong typed error");

    auto negative_wall =
        CrankwavePistonWallStepInput{m52_cylinder_plan(), 1.0, 100.0, 101325.0, -1.0};
    const auto wall_calculation =
        stage_crankwave_piston_wall_friction(negative_wall);
    const auto *wall_error = std::get_if<CrankwavePistonWallError>(&wall_calculation);
    expect(wall_error != nullptr &&
               wall_error->issue ==
                   CrankwavePistonWallIssue::negative_retained_wall_reaction,
           "negative retained wall reaction returned the wrong typed error");

    const auto valid_calculation = stage_crankwave_piston_wall_friction(
        {m52_cylinder_plan(), 1.0, 100.0, 101325.0, 0.0});
    const auto *valid =
        std::get_if<CrankwavePistonWallFrictionStage>(&valid_calculation);
    expect(valid != nullptr, "valid stage for acceleration error test was rejected");
    const auto acceleration_calculation =
        calculate_crankwave_next_piston_wall_reaction(
            *valid, std::numeric_limits<double>::infinity());
    const auto *acceleration_error =
        std::get_if<CrankwavePistonWallError>(&acceleration_calculation);
    expect(acceleration_error != nullptr &&
               acceleration_error->issue ==
                   CrankwavePistonWallIssue::nonfinite_angular_acceleration,
           "nonfinite angular acceleration returned the wrong typed error");
}

} // namespace

int main() {
    try {
        test_positive_speed_crank_friction_matches_pristine_constraint_limit();
        test_invalid_authored_crank_friction_is_typed();
        test_piston_friction_matches_pristine_written_order();
        test_piston_friction_zero_speed_and_positive_running_sign();
        test_ideal_wall_reaction_matches_static_slider_crank_balance();
        test_nonmidpoint_rod_center_matches_dead_center_inverse_dynamics();
        test_dynamic_wall_reaction_is_retained_for_only_the_next_stage();
        test_invalid_piston_wall_inputs_are_typed();
    } catch (const std::exception &error) {
        std::cerr << "Crankwave transient friction failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
