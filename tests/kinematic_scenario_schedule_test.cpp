#include "contract_test_support.hpp"
#include "simulation/kinematic_scenario_schedule.hpp"

#include <algorithm>
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

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;
using namespace engine_sim_offline::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
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

void expect_rejected(const RenderScenario &scenario, std::string_view path) {
    const auto result = compile_kinematic_scenario_schedule(scenario);
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, "invalid kinematic scenario compiled successfully");
    expect(std::ranges::any_of(report->issues,
                               [&](const ContractIssue &issue) {
                                   return issue.path.find(path) != std::string::npos;
                               }),
           "kinematic rejection omitted the responsible path");
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
               first_a->operating_state.fuel_enabled,
           "held schedule retained mutable source-request state");

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
    fixture.scenario.total_duration_s.value = 100000.0;
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(fixture.scenario));

    constexpr std::uint64_t expected_steps = UINT64_C(1000000000);
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
    fixture.scenario.scenario_id = "prescribed-schedule-four-step";
    fixture.scenario.rates.physics = {10000, 1};
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
        {10000, 1},
        0,
        RpmSampleSemantics::post_step_rpm,
        {400000.0, 1000.0, 1000.0, 1000.0},
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
    expect(schedule.rate() == RationalRateHz{10000, 1} &&
               schedule.first_step_index() == 0 && schedule.sample_count() == 4,
           "sampled schedule changed its fixed-rate extent");

    auto &source_rpm = std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(fixture.scenario.mode).trajectory.rpm);
    source_rpm.post_step_rpm.assign(1U, -1.0);

    auto cursor = schedule.fresh_cursor();
    const auto first = cursor.next();
    const auto second = cursor.next();
    const auto boundary = cursor.next();
    const auto fourth = cursor.next();
    expect(first.has_value() && first->sample_index == 0 &&
               first->step_end_index == 1 && first->rpm == 400000.0 &&
               first->requested_throttle == 0.25 &&
               first->operating_state.ignition_enabled,
           "sampled schedule changed its first post-step controls");
    expect(second.has_value() && second->sample_index == 1 &&
               second->requested_throttle == 0.25 &&
               second->operating_state.ignition_enabled,
           "sampled schedule changed controls before a boundary");
    expect(boundary.has_value() && boundary->sample_index == 2 &&
               boundary->rpm == 1000.0 && boundary->requested_throttle == 0.75 &&
               !boundary->operating_state.ignition_enabled,
           "sampled schedule changed right-continuous boundary behavior");
    expect(fourth.has_value() && fourth->sample_index == 3 &&
               fourth->step_end_index == 4 && cursor.completed() &&
               !cursor.next().has_value(),
           "sampled schedule changed final-step completion");
}

void test_admission_rejections() {
    {
        ScheduleFixture fixture;
        std::get<HeldSpeed>(fixture.scenario.mode).engine_speed_rpm.value = 0.0;
        expect_rejected(fixture.scenario, "engine_speed_rpm.value");
    }
    {
        ScheduleFixture fixture;
        std::get<HeldSpeed>(fixture.scenario.mode).initial_theta_rad.value =
            std::numeric_limits<double>::quiet_NaN();
        expect_rejected(fixture.scenario, "initial_theta_rad.value");
    }
    {
        ScheduleFixture fixture;
        std::get<HeldSpeed>(fixture.scenario.mode).throttle_01.value = 1.01;
        expect_rejected(fixture.scenario, "throttle_01.value");
    }
    {
        ScheduleFixture fixture;
        fixture.scenario.operating_state.value.push_back(
            {"off-grid", 0.00015, {false, false, false, true, true}});
        expect_rejected(fixture.scenario, "operating_state.value[1].time_s");
    }
    {
        ScheduleFixture fixture;
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
