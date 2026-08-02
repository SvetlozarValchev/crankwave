#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/one_level_master_rod_kinematics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::simulation;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 const char *message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message};
    }
}

constexpr double source_inch() {
    // Preserve pristine units::inch written order rather than spelling 0.0254.
    return (1.0 / 100.0) * 2.54;
}

constexpr double source_cubic_centimeter() {
    return (1.0 / 100.0) * (1.0 / 100.0) * (1.0 / 100.0);
}

struct RadialFiveFixture {
    OneLevelMasterRodDriver driver;
    std::array<OneLevelMasterRodCylinder, 5> cylinders;
};

RadialFiveFixture radial_five() {
    constexpr double inch = source_inch();
    constexpr double bore_m = 5.0 * inch;
    constexpr double area_m2 = kLegacyPi * bore_m * bore_m / 4.0;
    constexpr double deck_m = 15.75 * inch;
    constexpr double compression_m = 1.0 * inch;
    constexpr double chamber_m3 = 290.0 * source_cubic_centimeter();
    constexpr double degree = kLegacyPi / 180.0;

    RadialFiveFixture fixture{
        {
            2.75 * inch,
            0.0,
            0.0,
            12.0 * inch,
        },
        {},
    };
    fixture.cylinders[0] = {
        CylinderId{1}, 0.0, 12.0 * inch, area_m2, deck_m,
        compression_m, 0.0, chamber_m3,  0.0,     OneLevelMasterRodRootJournal{},
    };
    for (std::size_t index = 1; index < fixture.cylinders.size(); ++index) {
        const double phase = static_cast<double>(index) * 72.0 * degree;
        fixture.cylinders[index] = {
            CylinderId{static_cast<std::uint32_t>(index + 1U)},
            phase,
            9.1 * inch,
            area_m2,
            deck_m,
            compression_m,
            0.0,
            chamber_m3,
            0.0,
            OneLevelMasterRodSlavePin{2.9 * inch, phase},
        };
    }
    return fixture;
}

void test_pristine_radial_five_positions_and_volumes() {
    const auto fixture = radial_five();
    // These constants retain pristine's 3.14159265359 degree conversion and
    // cylinder-axis offset. They intentionally differ from an idealized
    // std::numbers::pi radial construction by roughly 1e-14 m.
    constexpr std::array positions_at_zero{
        0.2966884182100739,  0.23636052362641263, 0.24982221193915546,
        0.33193581168442715, 0.36922311895283944,
    };
    constexpr std::array volumes_at_zero{
        0.0012775929143807362, 0.0020418077989744514, 0.001871279345240786,
        0.0008310899670770286, 0.0003587460303692227,
    };
    constexpr std::array positions_at_quarter_turn{
        0.37465000000000004, 0.3166326919425342,  0.24461453110684137,
        0.24461453110685413, 0.31663269194256444,
    };
    constexpr std::array volumes_at_quarter_turn{
        0.00028999999999999973, 0.0010249450977455848, 0.0019372486159029462,
        0.0019372486159027846,  0.0010249450977452017,
    };
    constexpr double position_tolerance_m = 2.0e-15;
    constexpr double volume_tolerance_m3 = 2.0e-17;

    for (std::size_t index = 0; index < fixture.cylinders.size(); ++index) {
        const auto zero = evaluate_one_level_master_rod(
            fixture.driver, fixture.cylinders[index], 0.0, 200.0);
        const auto quarter = evaluate_one_level_master_rod(
            fixture.driver, fixture.cylinders[index], kLegacyPi / 2.0, 200.0);
        expect(zero.valid && quarter.valid,
               "pristine radial-five position oracle was rejected");
        expect_near(zero.piston_axis_position_m, positions_at_zero[index],
                    position_tolerance_m,
                    "radial-five zero-angle piston position changed");
        expect_near(zero.chamber_volume_m3, volumes_at_zero[index], volume_tolerance_m3,
                    "radial-five zero-angle chamber volume changed");
        expect_near(quarter.piston_axis_position_m, positions_at_quarter_turn[index],
                    position_tolerance_m,
                    "radial-five quarter-turn piston position changed");
        expect_near(quarter.chamber_volume_m3, volumes_at_quarter_turn[index],
                    volume_tolerance_m3,
                    "radial-five quarter-turn chamber volume changed");
    }
}

void test_dual_derivative_matches_position_and_volume_finite_difference() {
    const auto fixture = radial_five();
    constexpr double body_angle = 0.731;
    constexpr double angular_speed = 173.0;
    constexpr double h = 1.0e-6;

    for (const auto &cylinder : fixture.cylinders) {
        const auto center = evaluate_one_level_master_rod(fixture.driver, cylinder,
                                                          body_angle, angular_speed);
        // The primitive differentiates with respect to increasing cycle angle,
        // while body psi moves in the negative direction.
        const auto before = evaluate_one_level_master_rod(
            fixture.driver, cylinder, body_angle + h, angular_speed);
        const auto after = evaluate_one_level_master_rod(fixture.driver, cylinder,
                                                         body_angle - h, angular_speed);
        expect(center.valid && before.valid && after.valid,
               "radial derivative finite-difference sample was rejected");
        const double position_derivative =
            (after.piston_axis_position_m - before.piston_axis_position_m) / (2.0 * h);
        const double volume_derivative =
            (after.chamber_volume_m3 - before.chamber_volume_m3) / (2.0 * h);
        expect_near(center.piston_axis_derivative_m_per_rad, position_derivative,
                    5.0e-11, "radial analytic piston derivative changed sign or value");
        expect_near(center.dvolume_dtheta_m3_per_rad, volume_derivative, 1.0e-12,
                    "radial analytic chamber derivative disagrees with geometry");
        expect_near(center.piston_speed_abs_m_s,
                    std::abs(center.piston_axis_derivative_m_per_rad * angular_speed),
                    1.0e-13, "radial absolute piston speed changed");
    }
}

void test_wrist_pin_position_offsets_slave_volume_without_changing_motion() {
    const auto fixture = radial_five();
    const auto &centered_cylinder = fixture.cylinders[2];
    auto offset_cylinder = centered_cylinder;
    constexpr double wrist_pin_offset_m = 0.001;
    offset_cylinder.piston_wrist_pin_position_m = wrist_pin_offset_m;

    constexpr double body_angle_rad = 0.731;
    constexpr double angular_speed_rad_s = 173.0;
    const auto centered = evaluate_one_level_master_rod(
        fixture.driver, centered_cylinder, body_angle_rad, angular_speed_rad_s);
    const auto offset = evaluate_one_level_master_rod(
        fixture.driver, offset_cylinder, body_angle_rad, angular_speed_rad_s);
    expect(centered.valid && offset.valid,
           "wrist-pin slave-volume comparison was rejected");
    expect(offset.piston_axis_position_m == centered.piston_axis_position_m &&
               offset.piston_axis_derivative_m_per_rad ==
                   centered.piston_axis_derivative_m_per_rad &&
               offset.dvolume_dtheta_m3_per_rad == centered.dvolume_dtheta_m3_per_rad &&
               offset.piston_speed_abs_m_s == centered.piston_speed_abs_m_s,
           "wrist-pin position changed one-level slave kinematics");
    const double expected_volume_delta_m3 =
        centered_cylinder.piston_area_m2 * wrist_pin_offset_m;
    expect_near(offset.chamber_volume_m3,
                centered.chamber_volume_m3 - expected_volume_delta_m3, 1.0e-18,
                "wrist-pin position did not apply a constant slave volume offset");

    const auto centered_certificate =
        certify_one_level_master_rod_full_cycle(fixture.driver, centered_cylinder);
    const auto offset_certificate =
        certify_one_level_master_rod_full_cycle(fixture.driver, offset_cylinder);
    expect(centered_certificate.admitted() && offset_certificate.admitted(),
           "wrist-pin slave fixture lost full-cycle admission");
    expect_near(offset_certificate.minimum_chamber_volume_m3,
                centered_certificate.minimum_chamber_volume_m3 -
                    expected_volume_delta_m3,
                1.0e-18,
                "wrist-pin slave certificate did not retain the constant volume "
                "offset");
}

void test_radial_five_full_cycle_certificate_margins() {
    const OneLevelMasterRodFullCycleCheck unavailable;
    expect(unavailable.reason == OneLevelMasterRodFullCycleReason::invalid_geometry &&
               std::isnan(unavailable.forward_reach_margin_m) &&
               std::isnan(unavailable.minimum_chamber_volume_m3),
           "default full-cycle check exposed unavailable certificate margins");

    const auto fixture = radial_five();

    const auto root =
        certify_one_level_master_rod_full_cycle(fixture.driver, fixture.cylinders[0]);
    expect(root.reason == OneLevelMasterRodFullCycleReason::admitted,
           "canonical radial-five root was not certified");
    expect(root.admitted(), "admitted root check did not report admission");
    expect_near(root.forward_reach_margin_m, 0.23495, 1.0e-15,
                "canonical radial-five root reach margin changed");
    expect_near(root.minimum_chamber_volume_m3, 0.00029, 1.0e-17,
                "canonical radial-five root minimum volume changed");

    for (std::size_t index = 1; index < fixture.cylinders.size(); ++index) {
        const auto slave = certify_one_level_master_rod_full_cycle(
            fixture.driver, fixture.cylinders[index]);
        expect(slave.reason == OneLevelMasterRodFullCycleReason::admitted,
               "canonical radial-five slave was not certified");
        expect(slave.admitted(), "admitted slave check did not report admission");
        expect_near(slave.forward_reach_margin_m, 0.08763, 1.0e-15,
                    "canonical radial-five slave reach margin changed");
        expect_near(slave.minimum_chamber_volume_m3, 0.00029, 1.0e-17,
                    "canonical radial-five slave volume bound changed");
    }
}

void test_full_cycle_geometry_uses_exact_root_and_resolved_slave_extrema() {
    const auto fixture = radial_five();

    const auto root_calculation = calculate_one_level_master_rod_full_cycle_geometry(
        fixture.driver, fixture.cylinders[0]);
    const auto *root =
        std::get_if<OneLevelMasterRodFullCycleGeometry>(&root_calculation);
    expect(root != nullptr, "canonical radial root extrema were not resolved");
    const double expected_minimum_position_m =
        fixture.driver.master_connecting_rod_length_m - fixture.driver.crank_radius_m;
    const double expected_maximum_position_m =
        fixture.driver.master_connecting_rod_length_m + fixture.driver.crank_radius_m;
    const double expected_swept_stroke_m =
        expected_maximum_position_m - expected_minimum_position_m;
    const auto &root_cylinder = fixture.cylinders[0];
    const double expected_maximum_sweep_volume_m3 =
        root_cylinder.piston_area_m2 *
        (root_cylinder.deck_height_m - expected_minimum_position_m -
         root_cylinder.piston_wrist_pin_position_m -
         root_cylinder.piston_compression_height_m);
    const double expected_maximum_chamber_volume_m3 =
        expected_maximum_sweep_volume_m3 + root_cylinder.head_chamber_volume_m3 -
        root_cylinder.piston_displacement_term_m3;
    const double expected_minimum_sweep_volume_m3 =
        root_cylinder.piston_area_m2 *
        (root_cylinder.deck_height_m - expected_maximum_position_m -
         root_cylinder.piston_wrist_pin_position_m -
         root_cylinder.piston_compression_height_m);
    const double expected_minimum_chamber_volume_m3 =
        expected_minimum_sweep_volume_m3 + root_cylinder.head_chamber_volume_m3 -
        root_cylinder.piston_displacement_term_m3;
    expect(root != nullptr && root->stationary_point_count == 2U &&
               root->minimum_piston_axis_position_m == expected_minimum_position_m &&
               root->maximum_piston_axis_position_m == expected_maximum_position_m &&
               root->swept_stroke_m == expected_swept_stroke_m &&
               root->minimum_chamber_volume_m3 == expected_minimum_chamber_volume_m3 &&
               root->maximum_chamber_volume_m3 == expected_maximum_chamber_volume_m3 &&
               root->swept_displacement_m3 ==
                   root_cylinder.piston_area_m2 * expected_swept_stroke_m &&
               root->piston_axis_path_length_m_per_crank_revolution ==
                   expected_swept_stroke_m + expected_swept_stroke_m,
           "radial root did not retain exact analytic dead-center arithmetic");

    const auto slave_calculation = calculate_one_level_master_rod_full_cycle_geometry(
        fixture.driver, fixture.cylinders[1]);
    const auto repeated_slave_calculation =
        calculate_one_level_master_rod_full_cycle_geometry(fixture.driver,
                                                           fixture.cylinders[1]);
    const auto *slave =
        std::get_if<OneLevelMasterRodFullCycleGeometry>(&slave_calculation);
    const auto *repeated_slave =
        std::get_if<OneLevelMasterRodFullCycleGeometry>(&repeated_slave_calculation);
    expect(slave != nullptr && repeated_slave != nullptr && *slave == *repeated_slave,
           "canonical radial slave extrema were unavailable or nondeterministic");
    const double nominal_twice_throw_stroke_m =
        fixture.driver.crank_radius_m + fixture.driver.crank_radius_m;
    expect(slave != nullptr && slave->stationary_point_count == 2U &&
               slave->swept_stroke_m > nominal_twice_throw_stroke_m &&
               slave->swept_displacement_m3 ==
                   fixture.cylinders[1].piston_area_m2 * slave->swept_stroke_m &&
               slave->piston_axis_path_length_m_per_crank_revolution ==
                   slave->swept_stroke_m + slave->swept_stroke_m &&
               slave->minimum_chamber_volume_m3 > 0.0 &&
               slave->maximum_chamber_volume_m3 > slave->minimum_chamber_volume_m3,
           "radial slave retained nominal stroke or inconsistent derived geometry");
    expect_near(slave->swept_stroke_m / source_inch(), 5.506507223, 1.0e-8,
                "canonical radial slave swept stroke changed");
    constexpr std::array expected_swept_strokes_m{
        0.13970000000000005, 0.13986528346522351, 0.14021918405069109,
        0.14021918405069142, 0.13986528346522331,
    };
    constexpr std::array expected_swept_displacements_m3{
        0.0017696758707481282, 0.0017717696299481252, 0.0017762527317859607,
        0.0017762527317859648, 0.0017717696299481228,
    };
    std::array<OneLevelMasterRodFullCycleGeometry, 5> geometries{};
    for (std::size_t index = 0; index < fixture.cylinders.size(); ++index) {
        const auto calculation = calculate_one_level_master_rod_full_cycle_geometry(
            fixture.driver, fixture.cylinders[index]);
        const auto *geometry =
            std::get_if<OneLevelMasterRodFullCycleGeometry>(&calculation);
        expect(geometry != nullptr,
               "canonical radial-five cylinder geometry was not resolved");
        geometries[index] = *geometry;
        expect_near(geometry->swept_stroke_m, expected_swept_strokes_m[index], 5.0e-16,
                    "canonical radial-five per-cylinder swept stroke changed");
        expect_near(geometry->swept_displacement_m3,
                    expected_swept_displacements_m3[index], 8.0e-18,
                    "canonical radial-five per-cylinder displacement changed");
    }
    expect(std::abs(geometries[1].swept_stroke_m - geometries[4].swept_stroke_m) <=
                   3.0e-16 &&
               std::abs(geometries[2].swept_stroke_m - geometries[3].swept_stroke_m) <=
                   4.0e-16 &&
               std::abs(geometries[1].swept_displacement_m3 -
                        geometries[4].swept_displacement_m3) <= 3.0e-18 &&
               std::abs(geometries[2].swept_displacement_m3 -
                        geometries[3].swept_displacement_m3) <= 5.0e-18,
           "canonical radial-five reflected slave pairs lost binary64 symmetry");
    expect(geometries[2].swept_stroke_m - geometries[1].swept_stroke_m > 3.0e-4 &&
               geometries[1].swept_stroke_m - geometries[0].swept_stroke_m > 1.0e-4,
           "canonical radial-five distinct slave/root stroke families collapsed");

    constexpr std::size_t dense_sample_count = 32768U;
    const double two_pi = 2.0 * std::acos(-1.0);
    double sampled_minimum_position_m = std::numeric_limits<double>::infinity();
    double sampled_maximum_position_m = -std::numeric_limits<double>::infinity();
    double sampled_minimum_volume_m3 = std::numeric_limits<double>::infinity();
    double sampled_maximum_volume_m3 = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < dense_sample_count; ++index) {
        const double angle_rad = (static_cast<double>(index) + 0.5) * two_pi /
                                 static_cast<double>(dense_sample_count);
        const auto sample = evaluate_one_level_master_rod(
            fixture.driver, fixture.cylinders[1], angle_rad, 0.0);
        expect(sample.valid, "dense radial slave extrema oracle was rejected");
        sampled_minimum_position_m =
            std::min(sampled_minimum_position_m, sample.piston_axis_position_m);
        sampled_maximum_position_m =
            std::max(sampled_maximum_position_m, sample.piston_axis_position_m);
        sampled_minimum_volume_m3 =
            std::min(sampled_minimum_volume_m3, sample.chamber_volume_m3);
        sampled_maximum_volume_m3 =
            std::max(sampled_maximum_volume_m3, sample.chamber_volume_m3);
    }
    expect(
        sampled_minimum_position_m >= slave->minimum_piston_axis_position_m - 1.0e-14 &&
            sampled_maximum_position_m <=
                slave->maximum_piston_axis_position_m + 1.0e-14 &&
            sampled_minimum_volume_m3 >= slave->minimum_chamber_volume_m3 - 1.0e-16 &&
            sampled_maximum_volume_m3 <= slave->maximum_chamber_volume_m3 + 1.0e-16,
        "resolved radial slave extrema did not contain a denser independent sweep");
}

void test_full_cycle_geometry_fails_closed_outside_two_turning_point_subset() {
    const OneLevelMasterRodDriver driver{
        1.0,
        0.0,
        0.0,
        1.1646695401144738,
    };
    const OneLevelMasterRodCylinder four_turning_point_slave{
        CylinderId{1},
        1.7293803753630606,
        13.550777245464793,
        0.01,
        20.0,
        0.1,
        0.0,
        0.1,
        0.0,
        OneLevelMasterRodSlavePin{2.8906756825405315, -0.5627994782380452},
    };
    expect(certify_one_level_master_rod_full_cycle(driver, four_turning_point_slave)
               .admitted(),
           "four-turning-point rejection fixture lost its geometry certificate");
    const auto four_root_calculation =
        calculate_one_level_master_rod_full_cycle_geometry(driver,
                                                           four_turning_point_slave);
    const auto *four_root_issue =
        std::get_if<OneLevelMasterRodFullCycleGeometryIssue>(&four_root_calculation);
    expect(four_root_issue != nullptr && *four_root_issue ==
                                             OneLevelMasterRodFullCycleGeometryIssue::
                                                 stationary_point_isolation_ambiguous,
           "slave with more than two simple stationary points was flattened to one "
           "stroke");

    auto uncertified = radial_five();
    uncertified.driver.master_connecting_rod_length_m =
        uncertified.driver.crank_radius_m / 2.0;
    const auto uncertified_calculation =
        calculate_one_level_master_rod_full_cycle_geometry(uncertified.driver,
                                                           uncertified.cylinders[1]);
    const auto *uncertified_issue =
        std::get_if<OneLevelMasterRodFullCycleGeometryIssue>(&uncertified_calculation);
    expect(uncertified_issue != nullptr &&
               *uncertified_issue ==
                   OneLevelMasterRodFullCycleGeometryIssue::full_cycle_not_certified,
           "uncertified radial geometry reached stationary-point isolation");
}

void test_full_cycle_certificate_rejects_later_unreachable_geometry() {
    const OneLevelMasterRodDriver driver{
        0.05,
        0.0,
        0.0,
        0.2,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1}, 0.0,
        0.05,          0.01,
        1.0,           0.01,
        0.0,           0.001,
        0.0,           OneLevelMasterRodSlavePin{0.08, kLegacyPi / 2.0},
    };

    const auto initially_valid =
        evaluate_one_level_master_rod(driver, cylinder, 0.0, 1.0);
    expect(initially_valid.valid,
           "reachability fixture must be valid at its initial point");
    expect_near(initially_valid.piston_axis_position_m, 0.02178476627210968, 2.0e-14,
                "reachability fixture initial position changed");
    expect(!evaluate_one_level_master_rod(driver, cylinder, kLegacyPi / 2.0, 1.0).valid,
           "reachability fixture must fail later in the cycle");

    const auto check = certify_one_level_master_rod_full_cycle(driver, cylinder);
    expect(check.reason == OneLevelMasterRodFullCycleReason::reachability_not_certified,
           "later-unreachable geometry was not rejected analytically");
    expect(!check.admitted(), "unreachable geometry reported admission");
    expect_near(check.forward_reach_margin_m, -0.08, 1.0e-15,
                "unreachable slave margin changed");
    expect(std::isnan(check.minimum_chamber_volume_m3),
           "unreachable geometry reported a volume certificate");
}

void test_full_cycle_certificate_rejects_later_backward_solution() {
    const OneLevelMasterRodDriver driver{
        0.05,
        0.0,
        0.0,
        0.2,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1}, 3.0 * kLegacyPi / 2.0,
        0.05,          0.01,
        1.0,           0.01,
        0.0,           0.001,
        0.0,           OneLevelMasterRodSlavePin{0.05, 0.0},
    };

    const auto initially_valid =
        evaluate_one_level_master_rod(driver, cylinder, 0.0, 1.0);
    expect(initially_valid.valid,
           "backward-solution fixture must be valid at its initial point");
    expect_near(initially_valid.piston_axis_position_m, 0.05, 1.0e-12,
                "backward-solution fixture initial position changed");
    expect(!evaluate_one_level_master_rod(driver, cylinder, kLegacyPi, 1.0).valid,
           "backward-solution fixture must fail later in the cycle");

    const auto check = certify_one_level_master_rod_full_cycle(driver, cylinder);
    expect(check.reason == OneLevelMasterRodFullCycleReason::reachability_not_certified,
           "later-backward geometry was not rejected analytically");
    expect_near(check.forward_reach_margin_m, -0.05, 1.0e-15,
                "backward-solution reach margin changed");
}

void test_full_cycle_certificate_rejects_later_nonpositive_volume() {
    const OneLevelMasterRodDriver driver{
        0.05,
        0.0,
        0.0,
        0.2,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1}, 0.0, 0.2,    0.01, 0.22,
        0.01,          0.0, 0.0003, 0.0,  OneLevelMasterRodRootJournal{},
    };

    const auto initially_valid =
        evaluate_one_level_master_rod(driver, cylinder, 0.0, 1.0);
    expect(initially_valid.valid, "volume fixture must be valid at its initial point");
    expect_near(initially_valid.piston_axis_position_m, std::sqrt(0.0375), 1.0e-12,
                "volume fixture initial position changed");
    expect(!evaluate_one_level_master_rod(driver, cylinder, kLegacyPi / 2.0, 1.0).valid,
           "volume fixture must fail later in the cycle");

    const auto check = certify_one_level_master_rod_full_cycle(driver, cylinder);
    expect(check.reason ==
               OneLevelMasterRodFullCycleReason::chamber_volume_not_certified,
           "later-nonpositive volume was not rejected analytically");
    expect_near(check.forward_reach_margin_m, 0.15, 1.0e-15,
                "volume fixture reach margin changed");
    expect_near(check.minimum_chamber_volume_m3, -0.0001, 1.0e-17,
                "volume fixture lower bound changed");
}

void test_full_cycle_certificate_rejects_binary64_ambiguous_reach() {
    const OneLevelMasterRodDriver driver{
        1.3108598873974466e-05,
        0.0,
        4.3476180674614939,
        1.3108598873974468e-05,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1},
        driver.master_bank_angle_rad,
        driver.master_connecting_rod_length_m,
        0.01,
        1.0,
        0.01,
        0.0,
        0.001,
        0.0,
        OneLevelMasterRodRootJournal{},
    };

    expect(!evaluate_one_level_master_rod(
                driver, cylinder, driver.master_bank_angle_rad, 1.0)
                .valid,
           "binary64 ambiguity fixture unexpectedly produced a valid point");
    const auto check = certify_one_level_master_rod_full_cycle(driver, cylinder);
    expect(check.reason == OneLevelMasterRodFullCycleReason::reachability_not_certified,
           "ULP-scale linkage clearance was admitted as a stable full-cycle proof");
    expect(check.forward_reach_margin_m > 0.0,
           "binary64 ambiguity fixture lost its positive ideal reach margin");
    expect(std::isnan(check.minimum_chamber_volume_m3),
           "ambiguous reach reported a volume certificate");
}

void test_invalid_and_ambiguous_geometry_fails_closed() {
    auto fixture = radial_five();
    fixture.cylinders[0].bank_angle_rad = 0.125;
    expect(
        !evaluate_one_level_master_rod(fixture.driver, fixture.cylinders[0], 0.0, 1.0)
             .valid,
        "root cylinder differing from its driver did not fail closed");
    const auto mismatched_root =
        certify_one_level_master_rod_full_cycle(fixture.driver, fixture.cylinders[0]);
    expect(mismatched_root.reason == OneLevelMasterRodFullCycleReason::invalid_geometry,
           "ambiguous root geometry did not receive the invalid reason");
    expect(std::isnan(mismatched_root.forward_reach_margin_m) &&
               std::isnan(mismatched_root.minimum_chamber_volume_m3),
           "invalid geometry reported certificate margins");

    fixture = radial_five();
    fixture.driver.master_connecting_rod_length_m = fixture.driver.crank_radius_m / 2.0;
    expect(
        !evaluate_one_level_master_rod(fixture.driver, fixture.cylinders[1], 0.0, 1.0)
             .valid,
        "impossible master geometry did not fail closed");

    fixture = radial_five();
    fixture.cylinders[1].journal = OneLevelMasterRodSlavePin{
        2.9 * source_inch(), std::numeric_limits<double>::infinity()};
    expect(
        !evaluate_one_level_master_rod(fixture.driver, fixture.cylinders[1], 0.0, 1.0)
             .valid,
        "nonfinite slave phase did not fail closed");
}

void test_slider_solution_behind_bank_origin_fails_closed() {
    const OneLevelMasterRodDriver driver{
        0.05,
        0.0,
        0.0,
        0.2,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1}, 0.0, 0.1,   0.01, 1.0,
        0.01,          0.0, 0.001, 0.0,  OneLevelMasterRodSlavePin{0.5, kLegacyPi},
    };

    expect(!evaluate_one_level_master_rod(driver, cylinder, kLegacyPi / 2.0, 1.0).valid,
           "negative slider-axis position did not fail closed");
}

} // namespace

int main() {
    try {
        test_pristine_radial_five_positions_and_volumes();
        test_dual_derivative_matches_position_and_volume_finite_difference();
        test_wrist_pin_position_offsets_slave_volume_without_changing_motion();
        test_radial_five_full_cycle_certificate_margins();
        test_full_cycle_geometry_uses_exact_root_and_resolved_slave_extrema();
        test_full_cycle_geometry_fails_closed_outside_two_turning_point_subset();
        test_full_cycle_certificate_rejects_later_unreachable_geometry();
        test_full_cycle_certificate_rejects_later_backward_solution();
        test_full_cycle_certificate_rejects_later_nonpositive_volume();
        test_full_cycle_certificate_rejects_binary64_ambiguous_reach();
        test_invalid_and_ambiguous_geometry_fails_closed();
        test_slider_solution_behind_bank_origin_fails_closed();
        std::cout << "one-level master-rod kinematics tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
