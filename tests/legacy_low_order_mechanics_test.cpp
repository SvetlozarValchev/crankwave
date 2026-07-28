#include "contract_test_support.hpp"
#include "simulation/legacy_ignition_schedule.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;
using namespace engine_sim_offline::simulation;

void expect_near(double actual, double expected, double tolerance,
                 const char *message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message};
    }
}

void test_legacy_angle_wrapping() {
    const double two_pi = 2.0 * kLegacyPi;
    const double four_pi = 4.0 * kLegacyPi;

    expect(legacy_wrap_2pi(two_pi) == 0.0, "2-pi endpoint did not wrap to zero");
    expect(legacy_wrap_4pi(four_pi) == 0.0, "4-pi endpoint did not wrap to zero");
    expect_near(legacy_wrap_2pi(-0.25), two_pi - 0.25, 1.0e-15,
                "negative 2-pi wrapping changed");
    expect_near(legacy_wrap_4pi(-0.25), four_pi - 0.25, 1.0e-15,
                "negative 4-pi wrapping changed");
    expect_near(legacy_wrap_4pi(four_pi + 0.25), 0.25, 1.0e-15,
                "positive 4-pi wrapping changed");
    expect(legacy_positive_mod(-four_pi, four_pi) == 0.0,
           "negative full-cycle modulus did not produce zero");
}

void test_legacy_triangle_sampling() {
    constexpr std::array points{
        LegacyTrianglePoint{0.0, 0.0},
        LegacyTrianglePoint{100.0, 10.0},
        LegacyTrianglePoint{200.0, 30.0},
    };

    expect(legacy_triangle_sample({}, 50.0, 100.0) == 0.0,
           "empty triangle table did not sample as zero");
    expect(legacy_triangle_sample(points, -1.0, 100.0) == 0.0,
           "triangle table did not clamp below its domain");
    expect(legacy_triangle_sample(points, 201.0, 100.0) == 30.0,
           "triangle table did not clamp above its domain");
    expect(legacy_triangle_sample(points, 100.0, 100.0) == 10.0,
           "triangle table changed an authored point");
    expect_near(legacy_triangle_sample(points, 50.0, 100.0), 5.0, 1.0e-15,
                "triangle table changed its lower midpoint blend");
    expect_near(legacy_triangle_sample(points, 150.0, 100.0), 20.0, 1.0e-15,
                "triangle table changed its upper-tie midpoint blend");
    expect_near(legacy_triangle_sample(points, 125.0, 50.0), 10.0, 1.0e-15,
                "triangle table included a point outside its radius");
}

CenteredSliderCrankCylinder test_cylinder() {
    return {
        CylinderId{1}, 0.25, 0.01, 0.04, 0.14, 0.00005, 0.0,
    };
}

void test_centered_slider_crank_geometry() {
    const auto cylinder = test_cylinder();
    const auto tdc =
        evaluate_centered_slider_crank(cylinder, cylinder.geometric_tdc_rad, 200.0);
    expect(tdc.valid, "valid TDC slider-crank state was rejected");
    expect_near(tdc.phase_rad, 0.0, 0.0, "TDC phase is not zero");
    expect_near(tdc.piston_travel_m, 0.0, 1.0e-16, "TDC piston travel is not zero");
    expect_near(tdc.chamber_volume_m3, cylinder.clearance_volume_m3, 1.0e-16,
                "TDC chamber volume is not the clearance volume");
    expect_near(tdc.dx_dtheta_m_per_rad, 0.0, 1.0e-16,
                "TDC piston derivative is not zero");
    expect_near(tdc.piston_speed_abs_m_s, 0.0, 1.0e-14,
                "TDC absolute piston speed is not zero");

    const auto bdc = evaluate_centered_slider_crank(
        cylinder, cylinder.geometric_tdc_rad + kLegacyPi, -200.0);
    expect(bdc.valid, "valid BDC slider-crank state was rejected");
    expect_near(bdc.piston_travel_m, 2.0 * cylinder.crank_radius_m, 1.0e-15,
                "BDC piston travel is not twice the crank radius");
    expect_near(bdc.chamber_volume_m3,
                cylinder.clearance_volume_m3 +
                    cylinder.piston_area_m2 * 2.0 * cylinder.crank_radius_m,
                1.0e-16, "BDC chamber volume changed");
    expect_near(bdc.dvolume_dtheta_m3_per_rad, 0.0, 1.0e-15,
                "BDC volume derivative is not zero");

    const double quarter_phase = kLegacyPi / 2.0;
    const auto quarter = evaluate_centered_slider_crank(
        cylinder, cylinder.geometric_tdc_rad + quarter_phase, -200.0);
    expect(quarter.valid, "valid quarter-turn slider-crank state was rejected");
    const double sine = std::sin(quarter_phase);
    const double cosine = std::cos(quarter_phase);
    const double root =
        std::sqrt(cylinder.connecting_rod_length_m * cylinder.connecting_rod_length_m -
                  cylinder.crank_radius_m * cylinder.crank_radius_m * sine * sine);
    const double expected_travel = cylinder.crank_radius_m +
                                   cylinder.connecting_rod_length_m -
                                   (cylinder.crank_radius_m * cosine + root);
    const double expected_derivative =
        cylinder.crank_radius_m * sine +
        cylinder.crank_radius_m * cylinder.crank_radius_m * sine * cosine / root;
    expect_near(quarter.piston_travel_m, expected_travel, 1.0e-15,
                "quarter-turn slider position changed");
    expect_near(quarter.dx_dtheta_m_per_rad, expected_derivative, 1.0e-15,
                "quarter-turn slider derivative changed");
    expect_near(quarter.piston_speed_abs_m_s, std::abs(expected_derivative * -200.0),
                1.0e-13, "absolute piston speed changed");

    constexpr double h = 1.0e-6;
    const auto before = evaluate_centered_slider_crank(
        cylinder, cylinder.geometric_tdc_rad + quarter_phase - h, -200.0);
    const auto after = evaluate_centered_slider_crank(
        cylinder, cylinder.geometric_tdc_rad + quarter_phase + h, -200.0);
    expect(before.valid && after.valid,
           "finite-difference slider-crank samples were rejected");
    const double finite_difference =
        (after.chamber_volume_m3 - before.chamber_volume_m3) / (2.0 * h);
    expect_near(quarter.dvolume_dtheta_m3_per_rad, finite_difference, 1.0e-12,
                "analytic chamber-volume derivative disagrees with geometry");

    auto impossible = cylinder;
    impossible.crank_radius_m = 0.15;
    impossible.connecting_rod_length_m = 0.10;
    const auto invalid = evaluate_centered_slider_crank(
        impossible, impossible.geometric_tdc_rad + kLegacyPi / 2.0, 1.0);
    expect(!invalid.valid, "negative slider-crank radicand did not fail closed");
}

void test_ignition_crossing_half_open_intervals() {
    const auto increasing = evaluate_legacy_ignition_crossing(1.0, 2.0, 1.5, -1.0);
    expect(increasing.crossed,
           "increasing-cycle ignition crossing inside [saved,current) was missed");
    expect(evaluate_legacy_ignition_crossing(1.0, 2.0, 1.0, -1.0).crossed,
           "increasing-cycle ignition crossing excluded the saved endpoint");
    expect(!evaluate_legacy_ignition_crossing(1.0, 2.0, 2.0, -1.0).crossed,
           "increasing-cycle ignition crossing included the current endpoint");

    const double cycle = 4.0 * kLegacyPi;
    const auto increasing_wrap =
        evaluate_legacy_ignition_crossing(12.4, 0.2, 0.1, -1.0);
    expect(increasing_wrap.crossed,
           "increasing-cycle ignition crossing across wrap was missed");
    expect_near(increasing_wrap.adjusted_current_angle_rad, 0.2 + cycle, 1.0e-15,
                "wrapped increasing current angle changed");
    expect_near(increasing_wrap.adjusted_spark_angle_rad, 0.1 + cycle, 1.0e-15,
                "wrapped increasing spark angle changed");

    expect(evaluate_legacy_ignition_crossing(2.0, 1.0, 1.0, 1.0).crossed,
           "decreasing-cycle ignition crossing excluded the current endpoint");
    expect(!evaluate_legacy_ignition_crossing(2.0, 1.0, 2.0, 1.0).crossed,
           "decreasing-cycle ignition crossing included the saved endpoint");
    expect(evaluate_legacy_ignition_crossing(2.0, 1.0, 1.5, 0.0).crossed,
           "zero omega no longer follows decreasing-cycle semantics");

    const auto decreasing_wrap =
        evaluate_legacy_ignition_crossing(0.2, 12.4, 12.5, 1.0);
    expect(decreasing_wrap.crossed,
           "decreasing-cycle ignition crossing across wrap was missed");
    expect_near(decreasing_wrap.adjusted_current_angle_rad, 12.4 - cycle, 1.0e-15,
                "wrapped decreasing current angle changed");
    expect_near(decreasing_wrap.adjusted_spark_angle_rad, 12.5 - cycle, 1.0e-15,
                "wrapped decreasing spark angle changed");
}

void test_limiter_strict_threshold_and_timer_edges() {
    constexpr double limiter_rpm = 100.0;
    const double threshold = limiter_rpm * kLegacyRpmScale;

    const auto equality =
        update_legacy_limiter(0.0, 0.125, threshold, limiter_rpm, 0.5);
    expect(!equality.old_active && !equality.new_active &&
               !equality.overspeed_refreshed && equality.timer_s == 0.0,
           "limiter equality no longer uses a strict overspeed threshold");

    const auto activated =
        update_legacy_limiter(0.0, 0.125, threshold + 1.0, limiter_rpm, 0.5);
    expect(!activated.old_active && activated.new_active &&
               activated.overspeed_refreshed && activated.timer_s == 0.5,
           "limiter did not activate and refresh on strict overspeed");

    const auto decayed = update_legacy_limiter(0.25, 0.125, 0.0, limiter_rpm, 0.5);
    expect(decayed.old_active && decayed.new_active && !decayed.overspeed_refreshed &&
               decayed.timer_s == 0.125,
           "active limiter timer did not decay by exactly one step");

    const auto expired = update_legacy_limiter(0.125, 0.125, 0.0, limiter_rpm, 0.5);
    expect(expired.old_active && !expired.new_active && !expired.overspeed_refreshed &&
               expired.timer_s == 0.0,
           "limiter timer did not expire at exact zero");

    const auto refreshed =
        update_legacy_limiter(0.125, 0.125, -threshold - 1.0, limiter_rpm, 0.5);
    expect(refreshed.old_active && refreshed.new_active &&
               refreshed.overspeed_refreshed && refreshed.timer_s == 0.5,
           "active limiter did not refresh from negative overspeed");
}

struct MechanicsFixture {
    InputBuilder builder;
    EngineSpec engine;
    RenderScenario scenario;

    MechanicsFixture()
        : engine(make_engine(builder)), scenario(make_scenario(builder, engine)) {
        auto &profile = std::get<LegacyLowOrderV1Profile>(engine.physics_profile);
        profile.mechanism.cylinders[0].parameters.ignition_wire_angle_rad.value = 2.0;
        profile.ignition.limiter_speed_rpm.value = 300000.0;
        profile.ignition.limiter_hold_s.value = 0.0002;

        scenario.scenario_id = "mechanics-four-step";
        scenario.total_duration_s.value = 0.0004;
        scenario.audible_start_s.value = 0.0;
        scenario.audible_duration_s.value = 0.0004;
        scenario.operating_state.value = {
            {
                "fired",
                0.0,
                {true, true, false, true, true},
            },
            {
                "ignition-off",
                0.0002,
                {false, true, false, true, true},
            },
        };

        FixedRateRpmTrajectory rpm{
            {10000, 1},
            0,
            RpmSampleSemantics::post_step_rpm,
            {400000.0, 1000.0, 1000.0, 1000.0},
            {},
            builder.add_resolution("scenario.mode.trajectory.rpm"),
        };
        rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);

        RpmTrajectory trajectory{
            std::move(rpm),
            builder.resolved(0.0, "scenario.mode.trajectory.initial_theta_rad"),
            builder.resolved(method("fixed-rate-post-step-rpm-binary64-v1", 61),
                             "scenario.mode.trajectory.kinematic_resolution"),
        };
        ScalarTrajectory throttle{
            TrajectoryInterpolation::right_continuous_hold,
            {
                {0.0, 0.25},
                {0.0002, 0.75},
            },
            builder.add_resolution("scenario.mode.throttle_01"),
        };
        scenario.mode =
            PrescribedKinematicSweep{std::move(trajectory), std::move(throttle)};
    }
};

LegacyLowOrderMechanicsSession require_session(LegacyMechanicsCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "valid low-order mechanics session was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<LegacyLowOrderMechanicsSession>(std::move(result));
}

FixedRateRpmTrajectory &fixed_rpm(MechanicsFixture &fixture) {
    return std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(fixture.scenario.mode).trajectory.rpm);
}

void expect_compile_rejected(MechanicsFixture &fixture,
                             std::string_view expected_path) {
    auto result =
        compile_legacy_low_order_mechanics_session(fixture.engine, fixture.scenario);
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, "invalid mechanics request compiled successfully");
    const bool found = std::any_of(
        report->issues.begin(), report->issues.end(), [&](const auto &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    expect(found, "mechanics rejection omitted the expected issue path");
}

const LegacyMechanismStep &require_step(LegacyMechanicsAdvanceResult &result) {
    const auto *step =
        std::get_if<std::reference_wrapper<const LegacyMechanismStep>>(&result);
    expect(step != nullptr, "mechanics session did not produce an expected step");
    return step->get();
}

void test_mechanics_session_step_order_and_completion() {
    MechanicsFixture fixture;
    auto session = require_session(
        compile_legacy_low_order_mechanics_session(fixture.engine, fixture.scenario));
    expect(session.cylinder_models().size() == 1 &&
               session.cylinder_models()[0].cylinder_id == CylinderId{1},
           "compiled mechanics session lost its cylinder model");

    auto first_result = session.advance();
    const auto &first = require_step(first_result);
    const double expected_speed = 400000.0 * kLegacyRpmScale;
    expect(first.sample_index == 0 && first.step_end_index == 1 &&
               first.timestamp_tick == 1 && first.engine_speed_rpm == 400000.0,
           "first mechanics step has the wrong post-step clock or RPM");
    expect_near(first.angular_speed_rad_s, expected_speed, 0.0,
                "first mechanics angular speed changed");
    expect_near(first.theta_cycle_rad, expected_speed / 10000.0, 1.0e-15,
                "first mechanics cycle angle changed");
    expect(first.requested_throttle_01 == 0.25 &&
               first.resolved_engine_throttle_01 == 0.9375,
           "first mechanics throttle mapping changed");
    expect(first.cylinders.size() == 1 && first.cylinders[0].spark_crossed &&
               first.cylinders[0].cylinder_id == CylinderId{1} &&
               first.cylinders[0].chamber_volume_m3 > 0.0,
           "first mechanics cylinder state or spark crossing changed");
    expect(first.events.size() == 2 && first.events[0].ordinal_within_step == 0 &&
               std::holds_alternative<SparkCrossing>(first.events[0].payload) &&
               first.events[1].ordinal_within_step == 1 &&
               std::holds_alternative<LimiterStateChanged>(first.events[1].payload),
           "spark and limiter events are not in normative within-step order");
    const auto &limiter_started =
        std::get<LimiterStateChanged>(first.events[1].payload);
    expect(!limiter_started.old_active && limiter_started.new_active &&
               limiter_started.overspeed_refreshed &&
               limiter_started.resulting_timer_s == 0.0002 && first.limiter_cut_active,
           "first-step limiter activation evidence changed");

    auto second_result = session.advance();
    const auto &second = require_step(second_result);
    expect(second.sample_index == 1 && second.requested_throttle_01 == 0.25 &&
               second.operating_state.ignition_enabled && second.events.empty() &&
               second.limiter_cut_active,
           "second mechanics step changed before the control boundary");

    auto third_result = session.advance();
    const auto &third = require_step(third_result);
    expect(third.sample_index == 2 && third.requested_throttle_01 == 0.75 &&
               !third.operating_state.ignition_enabled && third.events.size() == 1 &&
               std::holds_alternative<LimiterStateChanged>(third.events[0].payload) &&
               !third.limiter_cut_active,
           "third mechanics step did not apply controls and limiter expiry together");
    const auto &limiter_stopped =
        std::get<LimiterStateChanged>(third.events[0].payload);
    expect(limiter_stopped.old_active && !limiter_stopped.new_active &&
               limiter_stopped.resulting_timer_s == 0.0,
           "limiter expiry evidence changed");

    auto fourth_result = session.advance();
    const auto &fourth = require_step(fourth_result);
    expect(fourth.sample_index == 3 && fourth.step_end_index == 4 &&
               fourth.events.empty() && session.completed(),
           "mechanics session did not produce and consume its final step");

    auto terminal = session.advance();
    const auto *completed = std::get_if<LegacyMechanicsCompleted>(&terminal);
    expect(completed != nullptr && completed->sample_count == 4,
           "mechanics session completed with the wrong sample count");
    auto stable_terminal = session.advance();
    const auto *stable_completed =
        std::get_if<LegacyMechanicsCompleted>(&stable_terminal);
    expect(stable_completed != nullptr && *stable_completed == *completed,
           "mechanics session completion is not terminal and stable");
}

void test_mechanics_compile_rejections() {
    {
        MechanicsFixture fixture;
        auto &profile =
            std::get<LegacyLowOrderV1Profile>(fixture.engine.physics_profile);
        profile.mechanism.cylinders[0].topology.exhaust_route_id = RouteId{999};
        expect_compile_rejected(fixture, "exhaust_route_id");
    }
    {
        MechanicsFixture fixture;
        auto &rpm = fixed_rpm(fixture);
        rpm.post_step_rpm[0] = 2000000.0;
        rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);
        expect_compile_rejected(fixture, "post_step_rpm[0]");
    }
    {
        MechanicsFixture fixture;
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.initial_theta_rad.value += 0.25;
        expect_compile_rejected(fixture, "initial_theta_rad.value");
    }
    {
        MechanicsFixture fixture;
        fixed_rpm(fixture).post_step_rpm[0] += 1.0;
        expect_compile_rejected(fixture, "samples_f64le_sha256");
    }
    {
        MechanicsFixture fixture;
        fixture.scenario.operating_state.value[0].state.limiter_enabled = false;
        expect_compile_rejected(fixture, "limiter_enabled");
    }
    {
        MechanicsFixture fixture;
        fixture.scenario.rates.physics = {12000, 1};
        expect_compile_rejected(fixture, "scenario.rates.physics");
    }
    {
        MechanicsFixture fixture;
        fixture.engine.methods.mechanism.value.version = 2;
        expect_compile_rejected(fixture, "engine.methods.mechanism");
    }
    {
        MechanicsFixture fixture;
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.kinematic_resolution.value.id = "unsupported-rpm-method";
        expect_compile_rejected(fixture, "kinematic_resolution");
    }
    {
        MechanicsFixture fixture;
        fixture.scenario.mode = HeldSpeed{{1000.0, {}}, {0.0, {}}, {0.75, {}}};
        expect_compile_rejected(fixture, "scenario.mode");
    }
}

void run_tests() {
    test_legacy_angle_wrapping();
    test_legacy_triangle_sampling();
    test_centered_slider_crank_geometry();
    test_ignition_crossing_half_open_intervals();
    test_limiter_strict_threshold_and_timer_edges();
    test_mechanics_session_step_order_and_completion();
    test_mechanics_compile_rejections();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Legacy low-order mechanics test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
