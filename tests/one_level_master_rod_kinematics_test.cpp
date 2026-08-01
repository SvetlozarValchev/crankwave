#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/one_level_master_rod_kinematics.hpp"

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
        CylinderId{1}, 0.0,    12.0 * inch,
        area_m2,       deck_m, compression_m,
        chamber_m3,    0.0,    OneLevelMasterRodRootJournal{},
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

void test_full_cycle_certificate_rejects_later_unreachable_geometry() {
    const OneLevelMasterRodDriver driver{
        0.05,
        0.0,
        0.0,
        0.2,
    };
    const OneLevelMasterRodCylinder cylinder{
        CylinderId{1}, 0.0, 0.05,
        0.01,          1.0, 0.01,
        0.001,         0.0, OneLevelMasterRodSlavePin{0.08, kLegacyPi / 2.0},
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
        CylinderId{1},
        3.0 * kLegacyPi / 2.0,
        0.05,
        0.01,
        1.0,
        0.01,
        0.001,
        0.0,
        OneLevelMasterRodSlavePin{0.05, 0.0},
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
        CylinderId{1}, 0.0,  0.2,
        0.01,          0.22, 0.01,
        0.0003,        0.0,  OneLevelMasterRodRootJournal{},
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
        CylinderId{1}, 0.0, 0.1,
        0.01,          1.0, 0.01,
        0.001,         0.0, OneLevelMasterRodSlavePin{0.5, kLegacyPi},
    };

    expect(!evaluate_one_level_master_rod(driver, cylinder, kLegacyPi / 2.0, 1.0).valid,
           "negative slider-axis position did not fail closed");
}

} // namespace

int main() {
    try {
        test_pristine_radial_five_positions_and_volumes();
        test_dual_derivative_matches_position_and_volume_finite_difference();
        test_radial_five_full_cycle_certificate_margins();
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
