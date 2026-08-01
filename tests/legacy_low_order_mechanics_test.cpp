#include "contract_test_support.hpp"
#include "simulation/legacy_ignition_schedule.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
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
using CoreRuntimeFactory =
    engine_sim_offline::simulation::detail::LowOrderEngineCoreV1RuntimeFactory;

void expect_near(double actual, double expected, double tolerance,
                 const char *message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message};
    }
}

bool same_binary64(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
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

void test_source_governor_written_order_and_state() {
    constexpr LegacyGovernorControllerParameters parameters{
        4.0,
        12.0,
        -1.0,
        1.0,
        1.0 / 16.0,
        1.0,
        2.0,
    };
    constexpr double step_s = 1.0 / 4.0;
    LegacyGovernorControllerState state;
    const auto exact = [](double actual, double expected, const char *message) {
        expect(std::bit_cast<std::uint64_t>(actual) ==
                   std::bit_cast<std::uint64_t>(expected),
               message);
    };
    const auto advance = [&](double command, double speed,
                             double expected_target, double expected_velocity,
                             double expected_actuator,
                             double expected_resolved_closure) {
        const auto result = evaluate_legacy_governor_throttle(
            state, parameters, command, speed, step_s, 1.0);
        state = result.controller;
        exact(result.target_engine_speed_rad_s, expected_target,
              "governor changed target-speed interpolation order");
        exact(state.velocity_per_s, expected_velocity,
              "governor changed its written-order velocity update");
        exact(state.actuator_closure_01, expected_actuator,
              "governor changed its persistent actuator state");
        exact(result.throttle.resolved_engine_throttle_01,
              expected_resolved_closure,
              "governor changed its source gamma projection");
    };

    advance(0.5, 0.0, 8.0, 0.0, 1.0, 1.0);
    advance(0.5, 2.0, 8.0, -15.0 / 16.0, 49.0 / 64.0,
            3871.0 / 4096.0);
    advance(0.5, 4.0, 8.0, -1.0, 33.0 / 64.0,
            3135.0 / 4096.0);
    advance(0.5, 12.0, 8.0, 1.0 / 2.0, 41.0 / 64.0,
            3567.0 / 4096.0);
    advance(1.0, 12.0, 12.0, 3.0 / 8.0, 47.0 / 64.0,
            3807.0 / 4096.0);
    advance(0.0, 12.0, 4.0, 1.0, 63.0 / 64.0,
            4095.0 / 4096.0);
    advance(0.5, 1.0, 8.0, 0.0, 1.0, 1.0);
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
        auto &profile =
            std::get<LowOrderOperatingPointV1Profile>(engine.physics_profile);
        profile.core.mechanism.cylinders[0].parameters.ignition_wire_angle_rad.value =
            2.0;
        profile.core.ignition.limiter_speed_rpm.value = 300000.0;
        profile.core.ignition.limiter_hold_s.value = 0.0002;

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

void configure_radial_master_rod_twin(MechanicsFixture &fixture) {
    constexpr double slave_phase_rad = 72.0 * (kLegacyPi / 180.0);
    auto &profile =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile);
    auto &root = profile.core.mechanism.cylinders.front();
    root.parameters.deck_height_m.value = 0.25;

    auto slave_public = fixture.engine.cylinders.front();
    slave_public.id = CylinderId{2};
    slave_public.semantic_id.value = "cylinder-2";
    slave_public.journal_phase_rad.value = slave_phase_rad;
    slave_public.master_rod_attachment = MasterRodAttachmentSpec{
        CylinderId{1},
        fixture.builder.resolved(
            0.029,
            "engine.cylinders.cylinder-2.master_rod_attachment.throw_radius_m"),
    };
    fixture.engine.cylinders.push_back(std::move(slave_public));

    auto slave = root;
    slave.topology.cylinder_id = CylinderId{2};
    slave.parameters.ignition_wire_angle_rad.value = 3.0;
    slave.kinematics = LegacyMasterRodJournalKinematics{
        CylinderId{1},
        fixture.builder.resolved(
            0.029,
            "engine.physics.low-order-operating-point-v1.mechanism.cylinders."
            "cylinder-2.kinematics.throw_radius_m"),
        fixture.builder.resolved(
            slave_phase_rad,
            "engine.physics.low-order-operating-point-v1.mechanism.cylinders."
            "cylinder-2.kinematics.master_local_phase_rad"),
    };
    profile.core.mechanism.cylinders.push_back(std::move(slave));
    profile.core.ignition.firing_order.value = {CylinderId{1}, CylinderId{2}};
    fixture.scenario.scenario_id = "radial-mechanics-four-step";
}

void append_second_bank_head(MechanicsFixture &fixture,
                             double chamber_volume_m3) {
    auto second_bank = fixture.engine.banks.front();
    second_bank.id = BankId{2};
    second_bank.semantic_id.value = "bank-2";
    fixture.engine.banks.push_back(std::move(second_bank));

    auto &core =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile).core;
    auto second_head = core.gas_path.heads.front();
    second_head.bank_id = BankId{2};
    second_head.chamber_volume_m3.value = chamber_volume_m3;
    core.gas_path.heads.push_back(std::move(second_head));
}

LegacyLowOrderMechanicsSession
require_session(CoreRuntimeFactory::MechanicsCompileResult result) {
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

CoreRuntimeFactory::MechanicsCompileResult compile_fixture(MechanicsFixture &fixture) {
    auto schedule_result = compile_kinematic_scenario_schedule(fixture.scenario);
    if (auto *report = std::get_if<ValidationReport>(&schedule_result)) {
        return std::move(*report);
    }
    const auto &core =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile).core;
    auto mechanism_plan = compile_mechanism_kinematics_plan(fixture.engine, core);
    if (auto *report = std::get_if<ValidationReport>(&mechanism_plan)) {
        return std::move(*report);
    }
    return CoreRuntimeFactory::compile_mechanics(
        fixture.engine, core, fixture.scenario,
        std::get<SharedMechanismKinematicsPlan>(std::move(mechanism_plan)),
        std::get<KinematicScenarioSchedule>(schedule_result));
}

struct CompiledRadialMechanics {
    SharedMechanismKinematicsPlan mechanism_plan;
    LegacyLowOrderMechanicsSession session;
};

CompiledRadialMechanics compile_radial_mechanics(MechanicsFixture &fixture) {
    auto schedule_result = compile_kinematic_scenario_schedule(fixture.scenario);
    expect(std::holds_alternative<KinematicScenarioSchedule>(schedule_result),
           "valid radial fixture did not compile its kinematic schedule");
    auto schedule =
        std::get<KinematicScenarioSchedule>(std::move(schedule_result));
    const auto &core =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile).core;
    auto mechanism_plan_result =
        compile_mechanism_kinematics_plan(fixture.engine, core);
    expect(std::holds_alternative<SharedMechanismKinematicsPlan>(
               mechanism_plan_result),
           "valid radial fixture did not compile its certified mechanism plan");
    auto mechanism_plan = std::get<SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));
    auto session_result = CoreRuntimeFactory::compile_mechanics(
        fixture.engine, core, fixture.scenario, mechanism_plan, schedule);
    return {
        std::move(mechanism_plan),
        require_session(std::move(session_result)),
    };
}

void configure_inertial_controls(MechanicsFixture &fixture) {
    fixture.scenario.mode = InertialDyno{
        fixture.builder.resolved(1000.0, "scenario.mode.initial_engine_speed_rpm"),
        fixture.builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        fixture.builder.resolved(1.0, "scenario.mode.equivalent_inertia_kg_m2"),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.25}, {0.0002, 0.75}},
            fixture.builder.add_resolution("scenario.mode.throttle_01.dynamic"),
        },
        {},
        "piecewise-linear-brake-torque-v1",
        fixture.builder.resolved(method("inertial-crank-dynamics-v1", 1),
                                 "scenario.mode.crank_dynamics_method"),
        fixture.builder.resolved(6500.0, "scenario.mode.target_engine_speed_rpm"),
        fixture.builder.resolved(method("piecewise-linear-brake-torque-v1", 1),
                                 "scenario.mode.brake_torque_method"),
    };
}

CoreRuntimeFactory::MechanicsCompileResult
compile_dynamic_fixture(MechanicsFixture &fixture) {
    const auto frame_count = resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    if (!frame_count.has_value()) {
        ValidationReport report;
        report.add(ContractIssueCode::inconsistent_semantics,
                   "scenario.total_duration_s.value",
                   "test fixture has no integral finite physics horizon");
        return report;
    }
    auto schedule_result = compile_scenario_control_schedule(
        fixture.scenario, LowOrderExecutionExtent::finite_scenario(*frame_count));
    if (auto *report = std::get_if<ValidationReport>(&schedule_result)) {
        return std::move(*report);
    }
    const auto &core =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile).core;
    auto mechanism_plan = compile_mechanism_kinematics_plan(fixture.engine, core);
    if (auto *report = std::get_if<ValidationReport>(&mechanism_plan)) {
        return std::move(*report);
    }
    return CoreRuntimeFactory::compile_mechanics(
        fixture.engine, core, fixture.scenario,
        std::get<SharedMechanismKinematicsPlan>(std::move(mechanism_plan)),
        std::get<ScenarioControlSchedule>(schedule_result));
}

void expect_compile_rejected(MechanicsFixture &fixture,
                             std::string_view expected_path) {
    auto result = compile_fixture(fixture);
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
    expect(std::holds_alternative<std::monostate>(
               MechanismCylinderSample{}.coordinates),
           "unpopulated mechanics sample fabricated direct coordinates");
    MechanicsFixture fixture;
    auto session = require_session(compile_fixture(fixture));

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
    const auto *first_coordinates =
        std::get_if<DirectCylinderCoordinates>(&first.cylinders[0].coordinates);
    expect(first_coordinates != nullptr,
           "direct mechanics did not emit tagged direct coordinates");
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

void test_radial_prescribed_mechanics_matches_pure_geometry_and_completes() {
    MechanicsFixture fixture;
    configure_radial_master_rod_twin(fixture);
    auto compiled = compile_radial_mechanics(fixture);
    const auto *radial_plan =
        one_level_master_rod_mechanism_kinematics_plan(compiled.mechanism_plan);
    expect(radial_plan != nullptr && radial_plan->cylinders.size() == 2U,
           "radial mechanics test lost its certified two-cylinder plan");

    for (std::uint64_t sample_index = 0; sample_index < 4U; ++sample_index) {
        auto result = compiled.session.advance();
        const auto &step = require_step(result);
        expect(step.sample_index == sample_index &&
                   step.step_end_index == sample_index + 1U &&
                   step.cylinders.size() == radial_plan->cylinders.size(),
               "radial mechanics changed its clock or cylinder shape");

        for (std::size_t index = 0; index < step.cylinders.size(); ++index) {
            const auto &actual = step.cylinders[index];
            const auto &planned = radial_plan->cylinders[index];
            const auto expected = evaluate_one_level_master_rod_plan(
                *radial_plan, index, step.body_angle_psi_rad,
                step.angular_speed_rad_s);
            const auto *coordinates =
                std::get_if<OneLevelMasterRodCoordinates>(&actual.coordinates);
            expect(expected.valid && coordinates != nullptr &&
                       actual.cylinder_id == CylinderId{
                                                 static_cast<std::uint32_t>(index + 1U)} &&
                       actual.exhaust_route_id == planned.exhaust_route_id &&
                       same_binary64(coordinates->piston_axis_position_m,
                                     expected.piston_axis_position_m) &&
                       same_binary64(
                           coordinates->piston_axis_derivative_m_per_rad,
                           expected.piston_axis_derivative_m_per_rad) &&
                       same_binary64(actual.chamber_volume_m3,
                                     expected.chamber_volume_m3) &&
                       same_binary64(actual.dvolume_dtheta_m3_per_rad,
                                     expected.dvolume_dtheta_m3_per_rad) &&
                       same_binary64(actual.piston_speed_abs_m_s,
                                     expected.piston_speed_abs_m_s),
                   "radial mechanics diverged from its pure evaluator or order");
        }

        if (sample_index == 0U) {
            expect(step.events.size() == 3U &&
                       std::holds_alternative<SparkCrossing>(
                           step.events[0].payload) &&
                       std::holds_alternative<SparkCrossing>(
                           step.events[1].payload) &&
                       std::get<SparkCrossing>(step.events[0].payload).cylinder_id ==
                           CylinderId{1} &&
                       std::get<SparkCrossing>(step.events[1].payload).cylinder_id ==
                           CylinderId{2} &&
                       std::holds_alternative<LimiterStateChanged>(
                           step.events[2].payload) &&
                       step.cylinders[0].spark_crossed &&
                       step.cylinders[1].spark_crossed,
                   "radial spark events lost cylinder order before the limiter");
        }
    }

    expect(compiled.session.completed(),
           "radial mechanics did not complete with its prescribed cursor");
    auto terminal = compiled.session.advance();
    const auto *completed = std::get_if<LegacyMechanicsCompleted>(&terminal);
    expect(completed != nullptr && completed->sample_count == 4U,
           "radial mechanics completed with the wrong exact sample count");

    auto terminal_with_motion =
        compiled.session.advance(PostStepCrankMotion{1000.0, 0.01});
    completed = std::get_if<LegacyMechanicsCompleted>(&terminal_with_motion);
    expect(completed != nullptr && completed->sample_count == 4U,
           "radial mechanics completion changed across advance overloads");
}

void test_mechanism_plan_requires_exact_bank_head_topology() {
    {
        MechanicsFixture fixture;
        auto &core = std::get<LowOrderOperatingPointV1Profile>(
                         fixture.engine.physics_profile)
                         .core;
        core.gas_path.heads.push_back(core.gas_path.heads.front());
        const auto result =
            compile_mechanism_kinematics_plan(fixture.engine, core);
        const auto *report = std::get_if<ValidationReport>(&result);
        expect(report != nullptr &&
                   std::ranges::any_of(report->issues, [](const auto &issue) {
                       return issue.path ==
                              "engine.physics_profile.gas_path.heads";
                   }),
               "direct mechanism plan admitted duplicate bank-head coverage");
    }

    {
        MechanicsFixture fixture;
        configure_radial_master_rod_twin(fixture);
        constexpr double second_head_chamber_volume_m3 = 0.000052;
        append_second_bank_head(fixture, second_head_chamber_volume_m3);
        fixture.engine.cylinders[1].bank_id = BankId{2};
        auto &core = std::get<LowOrderOperatingPointV1Profile>(
                         fixture.engine.physics_profile)
                         .core;

        const auto result =
            compile_mechanism_kinematics_plan(fixture.engine, core);
        const auto *plan = std::get_if<SharedMechanismKinematicsPlan>(&result);
        const auto *radial =
            plan == nullptr ? nullptr
                            : one_level_master_rod_mechanism_kinematics_plan(*plan);
        expect(radial != nullptr && radial->cylinders.size() == 2U,
               "two-bank radial mechanism plan was rejected");
        const double compiled_slave_head_volume = std::visit(
            [](const auto &kinematics) {
                return kinematics.cylinder.head_chamber_volume_m3;
            },
            radial->cylinders[1].kinematics);
        expect(same_binary64(compiled_slave_head_volume,
                             second_head_chamber_volume_m3),
               "radial slave did not bind its bank-local chamber volume");
        expect(mechanism_kinematics_plan_matches_source(*plan, fixture.engine, core),
               "fresh two-bank radial plan did not match its source topology");

        std::reverse(core.gas_path.heads.begin(), core.gas_path.heads.end());
        expect(!mechanism_kinematics_plan_matches_source(*plan, fixture.engine, core),
               "compiled radial plan accepted reordered bank-head source topology");
        const auto reordered =
            compile_mechanism_kinematics_plan(fixture.engine, core);
        const auto *report = std::get_if<ValidationReport>(&reordered);
        expect(report != nullptr &&
                   std::ranges::any_of(report->issues, [](const auto &issue) {
                       return issue.path ==
                              "engine.physics_profile.gas_path.heads";
                   }),
               "radial mechanism plan admitted reordered bank-head coverage");
    }
}

void test_radial_mechanics_requires_kinematic_schedule_and_rejects_external_motion() {
    MechanicsFixture missing_cursor_fixture;
    configure_radial_master_rod_twin(missing_cursor_fixture);
    auto schedule_result =
        compile_kinematic_scenario_schedule(missing_cursor_fixture.scenario);
    expect(std::holds_alternative<KinematicScenarioSchedule>(schedule_result),
           "radial missing-cursor fixture lost its source schedule");
    auto schedule =
        std::get<KinematicScenarioSchedule>(std::move(schedule_result));
    const auto &core = std::get<LowOrderOperatingPointV1Profile>(
                           missing_cursor_fixture.engine.physics_profile)
                           .core;
    auto mechanism_plan_result = compile_mechanism_kinematics_plan(
        missing_cursor_fixture.engine, core);
    expect(std::holds_alternative<SharedMechanismKinematicsPlan>(
               mechanism_plan_result),
           "radial missing-cursor fixture lost its certified plan");
    auto mechanism_plan = std::get<SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));
    auto missing_cursor_result = CoreRuntimeFactory::compile_mechanics(
        missing_cursor_fixture.engine, core, missing_cursor_fixture.scenario,
        mechanism_plan, schedule.control_schedule());
    const auto *missing_cursor_report =
        std::get_if<ValidationReport>(&missing_cursor_result);
    expect(missing_cursor_report != nullptr &&
               std::ranges::any_of(missing_cursor_report->issues,
                                   [](const auto &issue) {
                                       return issue.path == "mechanism_plan" &&
                                              issue.message.find(
                                                  "prescribed kinematic schedule") !=
                                                  std::string::npos;
                                   }),
           "radial mechanics admitted a control schedule without a kinematic "
           "cursor");

    MechanicsFixture external_motion_fixture;
    configure_radial_master_rod_twin(external_motion_fixture);
    auto compiled = compile_radial_mechanics(external_motion_fixture);
    auto result = compiled.session.advance(PostStepCrankMotion{1000.0, 0.01});
    const auto *failure = std::get_if<FailureContext>(&result);
    expect(failure != nullptr && failure->kind == FailureKind::contract_violation &&
               failure->detail_code ==
                   "legacy-mechanics-radial-external-motion-not-admitted",
           "radial mechanics admitted external post-step crank motion");
}

void test_moved_from_mechanics_session_fails_stably() {
    MechanicsFixture fixture;
    auto source = require_session(compile_fixture(fixture));
    auto destination = std::move(source);

    auto first_result = source.advance();
    const auto *first_failure = std::get_if<FailureContext>(&first_result);
    expect(first_failure != nullptr &&
               first_failure->kind == FailureKind::contract_violation &&
               first_failure->detail_code ==
                   "legacy-mechanics-mechanism-plan-unavailable",
           "moved-from mechanics session did not fail closed with stable context");
    const FailureContext expected_failure = *first_failure;

    auto repeated_result = source.advance();
    const auto *repeated_failure =
        std::get_if<FailureContext>(&repeated_result);
    expect(repeated_failure != nullptr && *repeated_failure == expected_failure,
           "moved-from mechanics failure was not terminal and repeatable");

    auto destination_result = destination.advance();
    require_step(destination_result);
}

void test_mechanics_accepts_compiled_held_speed_schedule() {
    MechanicsFixture fixture;
    fixture.scenario.mode = HeldSpeed{{1000.0, {}}, {0.0, {}}, {0.75, {}}};

    auto session = require_session(compile_fixture(fixture));
    auto result = session.advance();
    const auto &step = require_step(result);
    expect(step.engine_speed_rpm == 1000.0 && step.requested_throttle_01 == 0.75,
           "held-speed schedule changed while entering mechanics");
}

void test_mechanics_uses_authored_direct_throttle_transform() {
    MechanicsFixture fixture;
    auto &profile =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile);
    std::get<DirectThrottleControllerV1>(profile.core.throttle_controller)
        .gamma.value = 1.65;
    profile.core.gas_path.intake.idle_throttle_plate_position_01.value = 0.99715;

    auto session = require_session(compile_fixture(fixture));
    auto result = session.advance();
    const auto &step = require_step(result);
    const auto expected = evaluate_legacy_direct_throttle(0.25, 1.65, 0.99715);
    expect_near(step.resolved_engine_throttle_01, expected.resolved_engine_throttle_01,
                0.0, "mechanics ignored the authored direct-throttle gamma");
    expect_near(step.intake_plate_position_01, expected.intake_plate_position_01, 0.0,
                "mechanics ignored the authored idle plate position");
    expect_near(step.main_flow_multiplier_01, expected.main_flow_multiplier_01, 0.0,
                "mechanics changed the authored intake flow attenuation");
}

void test_mechanics_executes_governor_with_persistent_state() {
    MechanicsFixture fixture;
    auto &profile =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile);
    const auto prototype =
        std::get<DirectThrottleControllerV1>(profile.core.throttle_controller).gamma;
    const auto resolved = [&](double value) {
        auto field = prototype;
        field.value = value;
        return field;
    };
    profile.core.throttle_controller = GovernorThrottleControllerV1{
        resolved(100.0),
        resolved(200.0),
        resolved(-5.0),
        resolved(5.0),
        resolved(0.0006),
        resolved(200.0),
        resolved(2.0),
    };
    auto &rpm = fixed_rpm(fixture);
    rpm.post_step_rpm = {1000.0, 1000.0, 1000.0, 1000.0};
    rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);

    auto session = require_session(compile_fixture(fixture));
    LegacyGovernorControllerState expected_state;
    const LegacyGovernorControllerParameters parameters{
        100.0, 200.0, -5.0, 5.0, 0.0006, 200.0, 2.0,
    };
    for (std::size_t index = 0; index < rpm.post_step_rpm.size(); ++index) {
        auto result = session.advance();
        const auto &step = require_step(result);
        const auto expected = evaluate_legacy_governor_throttle(
            expected_state, parameters, index < 2U ? 0.25 : 0.75,
            1000.0 * kLegacyRpmScale, 1.0 / 10000.0,
            profile.core.gas_path.intake.idle_throttle_plate_position_01.value);
        expected_state = expected.controller;
        expect(std::bit_cast<std::uint64_t>(step.resolved_engine_throttle_01) ==
                       std::bit_cast<std::uint64_t>(
                           expected.throttle.resolved_engine_throttle_01) &&
                   std::bit_cast<std::uint64_t>(step.intake_plate_position_01) ==
                       std::bit_cast<std::uint64_t>(
                           expected.throttle.intake_plate_position_01) &&
                   std::bit_cast<std::uint64_t>(step.main_flow_multiplier_01) ==
                       std::bit_cast<std::uint64_t>(
                           expected.throttle.main_flow_multiplier_01),
               "mechanics changed the governor state update or intake projection");
    }
}

void test_mechanics_accepts_external_post_step_motion_for_inertial_controls() {
    MechanicsFixture fixture;
    configure_inertial_controls(fixture);
    auto session = require_session(compile_dynamic_fixture(fixture));

    auto first_result = session.advance(PostStepCrankMotion{1250.0, 0.02});
    const auto &first = require_step(first_result);
    expect(first.sample_index == 0U && first.step_end_index == 1U &&
               first.engine_speed_rpm == 1250.0 &&
               first.requested_throttle_01 == 0.25 && first.theta_unwrapped_rad == 0.02,
           "external post-step motion lost the first dynamic controls");
    expect_near(first.angular_speed_rad_s, 1250.0 * kLegacyRpmScale, 0.0,
                "external post-step RPM changed during mechanics resolution");

    auto second_result = session.advance(PostStepCrankMotion{1500.0, 0.021});
    const auto &second = require_step(second_result);
    expect(second.sample_index == 1U && second.engine_speed_rpm == 1500.0 &&
               second.requested_throttle_01 == 0.25 &&
               second.angular_acceleration_rad_s2 > 0.0,
           "external motion did not advance as one coherent dynamic step");

    auto third_result = session.advance(PostStepCrankMotion{1750.0, 0.022});
    const auto &third = require_step(third_result);
    expect(third.sample_index == 2U && third.requested_throttle_01 == 0.75 &&
               !third.operating_state.ignition_enabled,
           "dynamic mechanics did not apply its right-continuous controls");
    auto fourth_result = session.advance(PostStepCrankMotion{2000.0, 0.023});
    require_step(fourth_result);
    expect(session.completed(),
           "externally driven mechanics did not complete at the control horizon");

    MechanicsFixture missing_motion_fixture;
    configure_inertial_controls(missing_motion_fixture);
    auto missing_motion =
        require_session(compile_dynamic_fixture(missing_motion_fixture));
    const auto failure = missing_motion.advance();
    const auto *context = std::get_if<FailureContext>(&failure);
    const auto repeated = missing_motion.advance(PostStepCrankMotion{1250.0, 0.02});
    const auto *repeated_context = std::get_if<FailureContext>(&repeated);
    expect(context != nullptr &&
               context->detail_code == "legacy-mechanics-external-motion-required" &&
               repeated_context != nullptr &&
               repeated_context->detail_code == context->detail_code,
           "dynamic mechanics did not fail closed when motion was omitted");
}

void test_mechanics_accepts_canonical_external_zero_motion() {
    MechanicsFixture fixture;
    configure_inertial_controls(fixture);
    auto session = require_session(compile_dynamic_fixture(fixture));

    auto stopped_result = session.advance(PostStepCrankMotion{0.0, 0.0});
    const auto &stopped = require_step(stopped_result);
    expect(stopped.sample_index == 0U && stopped.step_end_index == 1U &&
               stopped.requested_throttle_01 == 0.25 &&
               std::bit_cast<std::uint64_t>(stopped.engine_speed_rpm) ==
                   std::bit_cast<std::uint64_t>(0.0) &&
               std::bit_cast<std::uint64_t>(stopped.angular_speed_rad_s) ==
                   std::bit_cast<std::uint64_t>(0.0),
           "canonical stopped motion did not advance one complete mechanics step");
    expect(std::bit_cast<std::uint64_t>(stopped.body_angle_psi_rad) ==
                   std::bit_cast<std::uint64_t>(0.0) &&
               std::bit_cast<std::uint64_t>(stopped.theta_cycle_rad) ==
                   std::bit_cast<std::uint64_t>(0.0) &&
               std::bit_cast<std::uint64_t>(stopped.theta_unwrapped_rad) ==
                   std::bit_cast<std::uint64_t>(0.0),
           "canonical stopped motion changed the crank angle");
    expect(stopped.events.empty() && !stopped.cylinders.front().spark_crossed,
           "canonical stopped motion emitted a spark crossing");
    const double stopped_theta_cycle_rad = stopped.theta_cycle_rad;
    const double stopped_theta_unwrapped_rad = stopped.theta_unwrapped_rad;

    auto resolved_speed_result = session.advance(PostStepCrankMotion{1250.0, 0.0});
    const auto &resolved_speed = require_step(resolved_speed_result);
    expect(resolved_speed.sample_index == 1U &&
               resolved_speed.engine_speed_rpm == 1250.0 &&
               resolved_speed.theta_cycle_rad == stopped_theta_cycle_rad &&
               resolved_speed.theta_unwrapped_rad == stopped_theta_unwrapped_rad &&
               resolved_speed.events.empty() &&
               !resolved_speed.cylinders.front().spark_crossed,
           "zero displacement with positive post-step RPM changed crank phase");

    auto moving_result = session.advance(PostStepCrankMotion{1250.0, 0.02});
    const auto &moving = require_step(moving_result);
    expect(moving.sample_index == 2U && moving.theta_unwrapped_rad == 0.02,
           "positive external motion changed after admitting stopped motion");
}

void test_mechanics_rejects_noncanonical_external_motion() {
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::array invalid_motions{
        PostStepCrankMotion{-0.0, 0.0},     PostStepCrankMotion{-1.0, 0.0},
        PostStepCrankMotion{infinity, 0.0}, PostStepCrankMotion{nan, 0.0},
        PostStepCrankMotion{0.0, -0.0},     PostStepCrankMotion{0.0, -1.0e-12},
        PostStepCrankMotion{0.0, infinity}, PostStepCrankMotion{0.0, nan},
    };

    for (const auto motion : invalid_motions) {
        MechanicsFixture fixture;
        configure_inertial_controls(fixture);
        auto session = require_session(compile_dynamic_fixture(fixture));
        const auto result = session.advance(motion);
        const auto *failure = std::get_if<FailureContext>(&result);
        expect(failure != nullptr &&
                   failure->detail_code == "legacy-mechanics-invalid-post-step-motion",
               "external mechanics admitted negative zero, a negative value, or a "
               "nonfinite motion scalar");
    }
}

void test_mechanics_uniform_limiter_disabled_policy() {
    MechanicsFixture fixture;
    for (auto &point : fixture.scenario.operating_state.value) {
        point.state.limiter_enabled = false;
    }

    auto session = require_session(compile_fixture(fixture));
    for (std::uint64_t index = 0; index < 4U; ++index) {
        auto result = session.advance();
        const auto &step = require_step(result);
        expect(step.sample_index == index && !step.operating_state.limiter_enabled &&
                   !step.limiter_cut_active &&
                   std::bit_cast<std::uint64_t>(step.limiter_timer_s) ==
                       std::bit_cast<std::uint64_t>(0.0),
               "disabled limiter did not remain canonical inactive positive zero");
        expect(std::none_of(step.events.begin(), step.events.end(),
                            [](const auto &event) {
                                return std::holds_alternative<LimiterStateChanged>(
                                    event.payload);
                            }),
               "disabled limiter emitted a limiter-state transition");
        if (index == 0U) {
            expect(
                step.cylinders.front().spark_crossed && step.events.size() == 1U &&
                    std::holds_alternative<SparkCrossing>(step.events.front().payload),
                "disabled limiter suppressed the admitted overspeed spark");
        }
    }
}

void test_mechanics_applies_live_limiter_and_external_resistance() {
    MechanicsFixture fixture;
    auto &rpm = fixed_rpm(fixture);
    std::ranges::fill(rpm.post_step_rpm, 400000.0);
    rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);
    auto session = require_session(compile_fixture(fixture));

    LiveControlOverrides disabled;
    disabled.has_limiter_enabled = true;
    disabled.limiter_enabled = false;
    disabled.has_external_resisting_torque_nm = true;
    disabled.external_resisting_torque_nm = 12.5;
    auto first_result = session.advance(disabled);
    const auto &first = require_step(first_result);
    expect(!first.operating_state.limiter_enabled && !first.limiter_cut_active &&
               first.limiter_timer_s == 0.0 &&
               first.external_resisting_torque_nm == 12.5 &&
               std::none_of(first.events.begin(), first.events.end(),
                            [](const auto &event) {
                                return std::holds_alternative<LimiterStateChanged>(
                                    event.payload);
                            }),
           "live disabled limiter/load policy was not resolved into mechanics");

    LiveControlOverrides enabled = disabled;
    enabled.limiter_enabled = true;
    enabled.external_resisting_torque_nm = 20.0;
    auto second_result = session.advance(enabled);
    const auto &second = require_step(second_result);
    expect(second.operating_state.limiter_enabled && second.limiter_cut_active &&
               second.limiter_timer_s == 0.0002 &&
               second.external_resisting_torque_nm == 20.0 &&
               std::ranges::any_of(
                   second.events,
                   [](const auto &event) {
                       const auto *transition =
                           std::get_if<LimiterStateChanged>(&event.payload);
                       return transition != nullptr && !transition->old_active &&
                              transition->new_active;
                   }),
           "live limiter enable did not activate at overspeed");

    disabled.external_resisting_torque_nm = 0.0;
    auto third_result = session.advance(disabled);
    const auto &third = require_step(third_result);
    expect(!third.operating_state.limiter_enabled && !third.limiter_cut_active &&
               third.limiter_timer_s == 0.0 &&
               third.external_resisting_torque_nm == 0.0 &&
               std::ranges::any_of(
                   third.events,
                   [](const auto &event) {
                       const auto *transition =
                           std::get_if<LimiterStateChanged>(&event.payload);
                       return transition != nullptr && transition->old_active &&
                              !transition->new_active &&
                              transition->resulting_timer_s == 0.0;
                   }),
           "live limiter disable did not publish the active-to-inactive edge");

    MechanicsFixture invalid_fixture;
    auto invalid_session = require_session(compile_fixture(invalid_fixture));
    LiveControlOverrides invalid;
    invalid.has_external_resisting_torque_nm = true;
    invalid.external_resisting_torque_nm = -1.0;
    const auto invalid_result = invalid_session.advance(invalid);
    const auto *failure = std::get_if<FailureContext>(&invalid_result);
    expect(failure != nullptr &&
               failure->detail_code ==
                   "legacy-mechanics-invalid-live-external-resisting-torque",
           "direct mechanics entry admitted negative live resistance");
}

void test_mechanics_compile_rejections() {
    {
        MechanicsFixture fixture;
        auto &profile =
            std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile);
        profile.core.mechanism.cylinders[0].topology.exhaust_route_id = RouteId{999};
        expect_compile_rejected(fixture, "exhaust_route_id");
    }
    {
        MechanicsFixture fixture;
        auto &rpm = fixed_rpm(fixture);
        rpm.post_step_rpm[0] = 2000000.0;
        rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);
        expect_compile_rejected(fixture, "schedule.rpm[0]");
    }
    {
        MechanicsFixture fixture;
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.initial_theta_rad.value += 0.25;
        expect_compile_rejected(fixture, "initial_theta_rad");
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
        fixture.scenario.total_duration_s.value = 0.0005;
        fixture.scenario.audible_duration_s.value = 0.0005;
        fixture.scenario.operating_state.value[1].time_s = 0.00025;
        fixture.scenario.mode = HeldSpeed{{1000.0, {}}, {0.0, {}}, {0.75, {}}};
        expect_compile_rejected(fixture, "scenario.rates.physics");
    }
    {
        MechanicsFixture fixture;
        fixture.engine.methods.mechanism.value.version = 2;
        expect_compile_rejected(fixture, "engine.methods.mechanism");
    }
}

void test_mechanics_rejects_stale_plan_with_unchanged_identity() {
    MechanicsFixture fixture;
    auto &core =
        std::get<LowOrderOperatingPointV1Profile>(fixture.engine.physics_profile).core;
    auto mechanism_plan_result =
        compile_mechanism_kinematics_plan(fixture.engine, core);
    expect(std::holds_alternative<SharedMechanismKinematicsPlan>(
               mechanism_plan_result),
           "valid mechanism source did not compile before stale-plan test");
    auto mechanism_plan = std::get<SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));

    auto schedule_result = compile_kinematic_scenario_schedule(fixture.scenario);
    expect(std::holds_alternative<KinematicScenarioSchedule>(schedule_result),
           "valid kinematic schedule did not compile before stale-plan test");

    const auto engine_id = fixture.engine.id;
    const auto profile_id = fixture.engine.profile_id.value;
    std::get<LegacyDirectJournalKinematics>(
        core.mechanism.cylinders[0].kinematics)
        .journal_angle_rad.value += 0.125;
    expect(fixture.engine.id == engine_id &&
               fixture.engine.profile_id.value == profile_id,
           "stale-plan test accidentally changed engine identity");

    auto result = CoreRuntimeFactory::compile_mechanics(
        fixture.engine, core, fixture.scenario, std::move(mechanism_plan),
        std::get<KinematicScenarioSchedule>(std::move(schedule_result)));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr,
           "mechanics admitted a stale plan after a consumed source field changed");
    expect(std::ranges::any_of(report->issues, [](const auto &issue) {
               return issue.path == "mechanism_plan";
           }),
           "stale mechanism plan rejection omitted its centralized binding path");
}

void run_tests() {
    test_legacy_angle_wrapping();
    test_legacy_triangle_sampling();
    test_source_governor_written_order_and_state();
    test_centered_slider_crank_geometry();
    test_ignition_crossing_half_open_intervals();
    test_limiter_strict_threshold_and_timer_edges();
    test_mechanics_session_step_order_and_completion();
    test_radial_prescribed_mechanics_matches_pure_geometry_and_completes();
    test_mechanism_plan_requires_exact_bank_head_topology();
    test_radial_mechanics_requires_kinematic_schedule_and_rejects_external_motion();
    test_moved_from_mechanics_session_fails_stably();
    test_mechanics_accepts_compiled_held_speed_schedule();
    test_mechanics_uses_authored_direct_throttle_transform();
    test_mechanics_executes_governor_with_persistent_state();
    test_mechanics_accepts_external_post_step_motion_for_inertial_controls();
    test_mechanics_accepts_canonical_external_zero_motion();
    test_mechanics_rejects_noncanonical_external_motion();
    test_mechanics_uniform_limiter_disabled_policy();
    test_mechanics_applies_live_limiter_and_external_resistance();
    test_mechanics_compile_rejections();
    test_mechanics_rejects_stale_plan_with_unchanged_identity();
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
