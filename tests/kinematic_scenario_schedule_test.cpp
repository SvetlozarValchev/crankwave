#include "contract_test_support.hpp"
#include "simulation/kinematic_scenario_schedule.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave::contract;
using namespace crankwave::contract::test;
using namespace crankwave::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool is_exact_positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) == std::bit_cast<std::uint64_t>(0.0);
}

KinematicScenarioSchedule require_schedule(KinematicScenarioScheduleResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "valid kinematic schedule was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<KinematicScenarioSchedule>(std::move(result));
}

ScenarioControlSchedule require_control_schedule(ScenarioControlScheduleResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "valid control schedule was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<ScenarioControlSchedule>(std::move(result));
}

LowOrderExecutionExtent finite_extent(const RenderScenario &scenario) {
    const auto frames =
        resolve_frame_index(scenario.total_duration_s.value, scenario.rates.physics);
    if (!frames.has_value()) {
        throw std::runtime_error{"test scenario has no integral finite horizon"};
    }
    return LowOrderExecutionExtent::finite_scenario(*frames);
}

void expect_rejected(const RenderScenario &scenario, std::string_view path) {
    const auto result = compile_kinematic_scenario_schedule(scenario);
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, "invalid kinematic scenario compiled successfully");
    if (std::ranges::any_of(report->issues, [&](const ContractIssue &issue) {
            return issue.path.find(path) != std::string::npos;
        })) {
        return;
    }
    std::string message = "kinematic rejection omitted path " + std::string(path);
    for (const auto &issue : report->issues) {
        message += "; actual: " + issue.path;
    }
    throw std::runtime_error{std::move(message)};
}

struct ScheduleFixture {
    InputBuilder builder;
    EngineSpec engine = make_engine(builder);
    RenderScenario scenario = make_scenario(builder, engine);
};

void configure_short_held_schedule(ScheduleFixture &fixture) {
    fixture.scenario.scenario_id = "held-schedule-six-step";
    fixture.scenario.rates.physics = {4, 1};
    fixture.scenario.total_duration_s.value = 1.5;
    fixture.scenario.operating_state.value = {
        {
            "fired",
            0.0,
            {true, true, false, true, true},
        },
        {
            "fuel-off",
            0.75,
            {true, false, false, true, true},
        },
    };
    fixture.scenario.mode = HeldSpeed{
        fixture.builder.resolved(2750.0, "held.engine_speed_rpm"),
        fixture.builder.resolved(0.25, "held.initial_theta_rad"),
        fixture.builder.resolved(0.625, "held.throttle_01"),
    };
}

void test_held_speed_snapshot_boundaries_and_completion() {
    ScheduleFixture fixture;
    configure_short_held_schedule(fixture);
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(fixture.scenario));

    expect(schedule.rate() == RationalRateHz{4, 1} &&
               schedule.first_step_index() == 0 &&
               schedule.sample_semantics() == RpmSampleSemantics::post_step_rpm &&
               schedule.sample_count() == 6 && schedule.initial_theta_rad() == 0.25,
           "held schedule has the wrong fixed-rate extent");
    expect(schedule.rpm_at_sample_offset(0) == 2750.0 &&
               schedule.rpm_at_sample_offset(5) == 2750.0 &&
               !schedule.rpm_at_sample_offset(6).has_value(),
           "held schedule did not expose a bounded constant-RPM lane");

    auto &held = std::get<HeldSpeed>(fixture.scenario.mode);
    held.engine_speed_rpm.value = 9000.0;
    held.initial_theta_rad.value = 9.0;
    held.throttle_01.value = 0.0;
    fixture.scenario.operating_state.value.clear();
    expect(schedule.initial_theta_rad() == 0.25,
           "held schedule retained the mutable initial-angle source");

    auto first_cursor = schedule.fresh_cursor();
    auto second_cursor = schedule.fresh_cursor();
    const auto first_a = first_cursor.next();
    const auto first_b = second_cursor.next();
    expect(first_a.has_value() && first_a == first_b,
           "fresh held-speed cursors did not begin independently");
    expect(first_a->sample_index == 0 && first_a->step_end_index == 1 &&
               first_a->rpm == 2750.0 && first_a->requested_throttle == 0.625 &&
               is_exact_positive_zero(first_a->external_resisting_torque_nm) &&
               first_a->operating_state.fuel_enabled,
           "held schedule retained mutable source-request state or invented an "
           "external resisting torque");

    const auto second = first_cursor.next();
    const auto third = first_cursor.next();
    const auto boundary = first_cursor.next();
    expect(second.has_value() && third.has_value() &&
               second->operating_state.fuel_enabled &&
               third->operating_state.fuel_enabled,
           "held operating-state boundary changed controls early");
    expect(boundary.has_value() && boundary->sample_index == 3 &&
               !boundary->operating_state.fuel_enabled &&
               boundary->requested_throttle == 0.625,
           "held operating-state boundary was not right-continuous");

    expect(first_cursor.next().has_value() && first_cursor.next().has_value(),
           "held cursor lost samples after its control boundary");
    expect(first_cursor.completed() && !first_cursor.next().has_value() &&
               !first_cursor.next().has_value(),
           "held cursor completion is not terminal and stable");
}

void test_held_speed_horizon_is_not_materialized() {
    ScheduleFixture fixture;
    fixture.scenario.mode = HeldSpeed{
        fixture.builder.resolved(3000.0, "held-horizon.engine_speed_rpm"),
        fixture.builder.resolved(0.0, "held-horizon.initial_theta_rad"),
        fixture.builder.resolved(0.85, "held-horizon.throttle_01"),
    };
    fixture.scenario.total_duration_s.value = 100000.0;
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(fixture.scenario));

    constexpr std::uint64_t expected_steps = UINT64_C(2000000000);
    expect(schedule.sample_count() == expected_steps &&
               schedule.rpm_at_sample_offset(expected_steps - 1U) == 3000.0 &&
               !schedule.rpm_at_sample_offset(expected_steps).has_value(),
           "large held horizon was not represented as a constant lane");

    auto cursor = schedule.fresh_cursor();
    const auto first = cursor.next();
    expect(first.has_value() && first->rpm == 3000.0 &&
               first->requested_throttle == 0.85,
           "large held schedule did not remain directly iterable");
}

void configure_prescribed_schedule(ScheduleFixture &fixture) {
    fixture.scenario.scenario_id = "prescribed-schedule-eight-step";
    fixture.scenario.rates.physics = {20000, 1};
    fixture.scenario.total_duration_s.value = 0.0004;
    fixture.scenario.operating_state.value = {
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
        {20000, 1},
        0,
        RpmSampleSemantics::post_step_rpm,
        {400000.0, 400000.0, 1000.0, 1000.0,
         1000.0,   1000.0,   1000.0, 1000.0},
        {},
        fixture.builder.add_resolution("prescribed.rpm"),
    };
    rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);
    fixture.scenario.mode = PrescribedKinematicSweep{
        {
            std::move(rpm),
            fixture.builder.resolved(0.0, "prescribed.initial_theta_rad"),
            fixture.builder.resolved(method("fixed-rate-post-step-rpm-binary64-v1", 61),
                                     "prescribed.kinematic_resolution"),
        },
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.25}, {0.0002, 0.75}},
            fixture.builder.add_resolution("prescribed.throttle"),
        },
    };
}

void test_prescribed_sweep_behavior_is_preserved() {
    ScheduleFixture fixture;
    configure_prescribed_schedule(fixture);
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(fixture.scenario));
    expect(schedule.rate() == RationalRateHz{20000, 1} &&
               schedule.first_step_index() == 0 && schedule.sample_count() == 8,
           "sampled schedule changed its fixed-rate extent");

    auto &source_rpm = std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(fixture.scenario.mode).trajectory.rpm);
    source_rpm.post_step_rpm.assign(1U, -1.0);

    auto cursor = schedule.fresh_cursor();
    const auto first = cursor.next();
    const auto second = cursor.next();
    const auto third = cursor.next();
    const auto fourth = cursor.next();
    const auto boundary = cursor.next();
    const auto sixth = cursor.next();
    const auto seventh = cursor.next();
    const auto eighth = cursor.next();
    expect(first.has_value() && first->sample_index == 0 &&
               first->step_end_index == 1 && first->rpm == 400000.0 &&
               first->requested_throttle == 0.25 &&
               is_exact_positive_zero(first->external_resisting_torque_nm) &&
               first->operating_state.ignition_enabled,
           "sampled schedule changed its first post-step controls or invented an "
           "external resisting torque");
    expect(second.has_value() && second->sample_index == 1 &&
               second->requested_throttle == 0.25 &&
               second->operating_state.ignition_enabled,
           "sampled schedule changed controls before a boundary");
    expect(third.has_value() && fourth.has_value() &&
               third->requested_throttle == 0.25 &&
               fourth->requested_throttle == 0.25 &&
               boundary.has_value() && boundary->sample_index == 4 &&
               boundary->rpm == 1000.0 && boundary->requested_throttle == 0.75 &&
               !boundary->operating_state.ignition_enabled,
           "sampled schedule changed right-continuous boundary behavior");
    expect(sixth.has_value() && seventh.has_value() && eighth.has_value() &&
               eighth->sample_index == 7 && eighth->step_end_index == 8 &&
               cursor.completed() &&
               !cursor.next().has_value(),
           "sampled schedule changed final-step completion");
}

void test_inertial_dyno_compiles_controls_without_a_fake_rpm_lane() {
    ScheduleFixture fixture;
    configure_short_held_schedule(fixture);
    fixture.scenario.mode = InertialDyno{
        fixture.builder.resolved(1500.0, "dyno.initial_engine_speed_rpm"),
        fixture.builder.resolved(0.25, "dyno.initial_theta_rad"),
        fixture.builder.resolved(1.0, "dyno.equivalent_inertia_kg_m2"),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.4}, {0.5, 0.9}},
            fixture.builder.add_resolution("dyno.throttle"),
        },
        {},
        "piecewise-linear-brake-torque-v1",
        fixture.builder.resolved(method("inertial-crank-dynamics-v1", 1),
                                 "dyno.crank_dynamics_method"),
        fixture.builder.resolved(6500.0, "dyno.target_engine_speed_rpm"),
        fixture.builder.resolved(method("piecewise-linear-brake-torque-v1", 1),
                                 "dyno.brake_torque_method"),
    };

    auto controls = require_control_schedule(compile_scenario_control_schedule(
        fixture.scenario, finite_extent(fixture.scenario)));
    const auto finite_frames = controls.execution_extent().finite_physics_frame_count();
    expect(controls.rate() == RationalRateHz{4, 1} &&
               controls.first_step_index() == 0U && finite_frames == 6U &&
               controls.initial_theta_rad() == 0.25,
           "inertial controls have the wrong fixed-rate extent or initial angle");

    auto &source = std::get<InertialDyno>(fixture.scenario.mode);
    source.initial_theta_rad.value = 9.0;
    source.throttle_01.points.clear();
    fixture.scenario.operating_state.value.clear();

    auto cursor = controls.fresh_cursor();
    const auto first = cursor.next();
    const auto second = cursor.next();
    const auto throttle_boundary = cursor.next();
    const auto operating_boundary = cursor.next();
    expect(first.has_value() && first->sample_index == 0U &&
               first->step_end_index == 1U && first->requested_throttle == 0.4 &&
               is_exact_positive_zero(first->external_resisting_torque_nm) &&
               first->operating_state.fuel_enabled && second.has_value() &&
               second->requested_throttle == 0.4,
           "inertial control cursor lost its immutable step-zero controls or "
           "invented an external resisting torque");
    expect(throttle_boundary.has_value() && throttle_boundary->sample_index == 2U &&
               throttle_boundary->requested_throttle == 0.9 &&
               throttle_boundary->operating_state.fuel_enabled,
           "inertial throttle boundary was not right-continuous");
    expect(operating_boundary.has_value() && operating_boundary->sample_index == 3U &&
               !operating_boundary->operating_state.fuel_enabled,
           "inertial operating-state boundary was not right-continuous");

    const auto rejected = compile_kinematic_scenario_schedule(fixture.scenario);
    expect(std::holds_alternative<ValidationReport>(rejected),
           "inertial controls were silently materialized as a kinematic RPM lane");
}

void configure_free_engine_control_schedule(ScheduleFixture &fixture) {
    configure_short_held_schedule(fixture);
    fixture.scenario.scenario_id = "free-engine-control-schedule-six-step";
    for (auto &point : fixture.scenario.operating_state.value) {
        point.state.dyno_enabled = false;
    }
    fixture.scenario.mode = FreeEngine{
        fixture.builder.resolved(1500.0, "free-engine.initial_engine_speed_rpm"),
        fixture.builder.resolved(0.25, "free-engine.initial_theta_rad"),
        fixture.builder.resolved(0.20, "free-engine.engine_baseline_inertia_kg_m2"),
        fixture.builder.resolved(0.05, "free-engine.attached_inertia_kg_m2"),
        fixture.builder.resolved(0.25, "free-engine.total_equivalent_inertia_kg_m2"),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.2}, {0.5, 0.8}},
            fixture.builder.add_resolution("free-engine.throttle"),
        },
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 12.0}, {1.0, 4.0}},
            fixture.builder.add_resolution("free-engine.external_resisting_torque"),
        },
        fixture.builder.resolved(method("inertial-crank-dynamics-v1", 1),
                                 "free-engine.crank_dynamics_method"),
    };
}

void test_free_engine_control_schedule_exposes_external_resisting_torque() {
    ScheduleFixture fixture;
    configure_free_engine_control_schedule(fixture);
    auto controls = require_control_schedule(compile_scenario_control_schedule(
        fixture.scenario, finite_extent(fixture.scenario)));
    const auto finite_frames = controls.execution_extent().finite_physics_frame_count();
    expect(controls.rate() == RationalRateHz{4, 1} &&
               controls.first_step_index() == 0U && finite_frames == 6U &&
               controls.initial_theta_rad() == 0.25,
           "free-engine controls have the wrong fixed-rate extent or initial angle");

    auto &source = std::get<FreeEngine>(fixture.scenario.mode);
    source.initial_theta_rad.value = 9.0;
    source.throttle_01.points.clear();
    source.external_resisting_torque_nm.points.clear();
    fixture.scenario.operating_state.value.clear();

    auto cursor = controls.fresh_cursor();
    const auto first = cursor.next();
    const auto second = cursor.next();
    const auto throttle_boundary = cursor.next();
    const auto operating_boundary = cursor.next();
    const auto torque_boundary = cursor.next();
    const auto final = cursor.next();
    expect(first.has_value() && first->sample_index == 0U &&
               first->step_end_index == 1U && first->requested_throttle == 0.2 &&
               first->external_resisting_torque_nm == 12.0 &&
               first->operating_state.fuel_enabled && second.has_value() &&
               second->requested_throttle == 0.2 &&
               second->external_resisting_torque_nm == 12.0,
           "free-engine cursor lost its immutable step-zero control snapshot");
    expect(throttle_boundary.has_value() && throttle_boundary->sample_index == 2U &&
               throttle_boundary->requested_throttle == 0.8 &&
               throttle_boundary->external_resisting_torque_nm == 12.0,
           "free-engine throttle boundary was not right-continuous");
    expect(operating_boundary.has_value() && operating_boundary->sample_index == 3U &&
               !operating_boundary->operating_state.fuel_enabled &&
               operating_boundary->external_resisting_torque_nm == 12.0,
           "free-engine operating-state boundary was not right-continuous");
    expect(torque_boundary.has_value() && torque_boundary->sample_index == 4U &&
               torque_boundary->requested_throttle == 0.8 &&
               torque_boundary->external_resisting_torque_nm == 4.0,
           "free-engine resisting-torque boundary was not right-continuous");
    expect(final.has_value() && final->sample_index == 5U &&
               final->external_resisting_torque_nm == 4.0 && cursor.completed() &&
               !cursor.next().has_value(),
           "free-engine control cursor did not complete at its fixed horizon");

    const auto rejected_kinematic =
        compile_kinematic_scenario_schedule(fixture.scenario);
    expect(std::holds_alternative<ValidationReport>(rejected_kinematic),
           "free-engine controls were silently materialized as a kinematic RPM lane");

    ScheduleFixture invalid;
    configure_free_engine_control_schedule(invalid);
    std::get<FreeEngine>(invalid.scenario.mode)
        .external_resisting_torque_nm.interpolation = TrajectoryInterpolation::linear;
    const auto rejected_controls = compile_scenario_control_schedule(
        invalid.scenario, finite_extent(invalid.scenario));
    expect(std::holds_alternative<ValidationReport>(rejected_controls),
           "linear free-engine resisting torque was admitted as an RCH schedule");
}

void test_open_free_engine_holds_the_exact_release_snapshot() {
    ScheduleFixture fixture;
    configure_free_engine_control_schedule(fixture);
    fixture.scenario.audible_start_s.value = 0.5;
    auto controls = require_control_schedule(compile_scenario_control_schedule(
        fixture.scenario, LowOrderExecutionExtent::open_ended()));
    expect(controls.execution_extent().is_open_ended(),
           "open FreeEngine controls were compiled as finite");

    auto cursor = controls.fresh_cursor();
    std::optional<ScheduledScenarioControls> latest;
    for (std::uint64_t frame = 0; frame < 12U; ++frame) {
        latest = cursor.next();
        expect(latest.has_value() && latest->sample_index == frame,
               "open FreeEngine cursor stopped at the authored horizon");
        if (frame >= 2U) {
            expect(latest->requested_throttle == 0.8 &&
                       latest->external_resisting_torque_nm == 12.0 &&
                       latest->operating_state.fuel_enabled,
                   "open FreeEngine did not hold the exact release-frame RCH "
                   "snapshot");
        }
    }
    expect(!cursor.completed() && !cursor.clock_overflowed(),
           "open FreeEngine cursor reported a finite terminal state");
}

void test_admission_rejections() {
    {
        ScheduleFixture fixture;
        configure_short_held_schedule(fixture);
        std::get<HeldSpeed>(fixture.scenario.mode).engine_speed_rpm.value = 0.0;
        expect_rejected(fixture.scenario, "engine_speed_rpm.value");
    }
    {
        ScheduleFixture fixture;
        configure_short_held_schedule(fixture);
        std::get<HeldSpeed>(fixture.scenario.mode).initial_theta_rad.value =
            std::numeric_limits<double>::quiet_NaN();
        expect_rejected(fixture.scenario, "initial_theta_rad.value");
    }
    {
        ScheduleFixture fixture;
        configure_short_held_schedule(fixture);
        std::get<HeldSpeed>(fixture.scenario.mode).throttle_01.value = 1.01;
        expect_rejected(fixture.scenario, "throttle_01.value");
    }
    {
        ScheduleFixture fixture;
        configure_short_held_schedule(fixture);
        fixture.scenario.operating_state.value.push_back(
            {"off-grid", 0.00015, {false, false, false, true, true}});
        expect_rejected(fixture.scenario, "operating_state.value[2].time_s");
    }
    {
        ScheduleFixture fixture;
        configure_short_held_schedule(fixture);
        fixture.scenario.total_duration_s.value = 0.0;
        expect_rejected(fixture.scenario, "total_duration_s.value");
    }
    {
        ScheduleFixture fixture;
        fixture.scenario.mode = LoadTargetHeldCapture{};
        expect_rejected(fixture.scenario, "scenario.mode");
    }
    {
        ScheduleFixture fixture;
        configure_prescribed_schedule(fixture);
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.rpm = ScalarTrajectory{};
        expect_rejected(fixture.scenario, "trajectory.rpm");
    }
    {
        ScheduleFixture fixture;
        configure_prescribed_schedule(fixture);
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.kinematic_resolution.value.id = "wrong-method";
        expect_rejected(fixture.scenario, "kinematic_resolution");
    }
    {
        ScheduleFixture fixture;
        configure_prescribed_schedule(fixture);
        auto &sweep = std::get<PrescribedKinematicSweep>(fixture.scenario.mode);
        sweep.trajectory.kinematic_resolution.value.version = 2U;
        expect_rejected(fixture.scenario, "kinematic_resolution");
    }
}

void run_tests() {
    test_held_speed_snapshot_boundaries_and_completion();
    test_held_speed_horizon_is_not_materialized();
    test_prescribed_sweep_behavior_is_preserved();
    test_inertial_dyno_compiles_controls_without_a_fake_rpm_lane();
    test_free_engine_control_schedule_exposes_external_resisting_torque();
    test_open_free_engine_holds_the_exact_release_snapshot();
    test_admission_rejections();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Kinematic scenario schedule test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
