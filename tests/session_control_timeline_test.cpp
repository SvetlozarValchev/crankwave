#include "contract_test_support.hpp"
#include "session/control_timeline.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

std::size_t allocation_count = 0;
bool count_allocations = false;

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noipa))
#elif defined(__clang__)
__attribute__((noinline))
#endif
void *allocate_test_storage(std::size_t size) noexcept {
    return std::malloc(size == 0 ? 1 : size);
}

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noipa))
#elif defined(__clang__)
__attribute__((noinline))
#endif
void release_test_storage(void *memory) noexcept {
    std::free(memory);
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

using engine_sim_offline::contract::RationalRateHz;
using engine_sim_offline::simulation::LiveControlOverrides;
using namespace engine_sim_offline::session;
namespace contract = engine_sim_offline::contract;
namespace contract_test = engine_sim_offline::contract::test;
namespace simulation = engine_sim_offline::simulation;

constexpr RationalRateHz kPhysicsRate{10000, 1};
constexpr RationalRateHz kDeliveryRate{192000, 1};

void test_exact_delivery_projection() {
    const auto zero =
        project_delivery_frame_to_physics_step(0, kPhysicsRate, kDeliveryRate);
    const auto on_grid =
        project_delivery_frame_to_physics_step(96, kPhysicsRate, kDeliveryRate);
    const auto off_grid =
        project_delivery_frame_to_physics_step(97, kPhysicsRate, kDeliveryRate);
    expect(zero && zero.physics_step == 0, "delivery frame zero did not map to step 0");
    expect(on_grid && on_grid.physics_step == 5,
           "delivery frame 96 did not map exactly to step 5");
    expect(off_grid && off_grid.physics_step == 6,
           "delivery frame 97 did not ceil-map to step 6");

    const auto rational = project_delivery_frame_to_physics_step(7, {3, 2}, {5, 2});
    expect(rational && rational.physics_step == 5,
           "generic rational-rate projection was not exact");

    const auto invalid =
        project_delivery_frame_to_physics_step(1, {10000, 2}, kDeliveryRate);
    expect(!invalid && invalid.error == ControlTimelineError::invalid_rate,
           "non-reduced rate was not rejected");

    const auto overflow = project_delivery_frame_to_physics_step(
        std::numeric_limits<std::uint64_t>::max(),
        {std::numeric_limits<std::uint64_t>::max(), 1}, {1, 1});
    expect(!overflow && overflow.error == ControlTimelineError::clock_overflow,
           "unrepresentable clock projection was not rejected");
}

void test_right_continuous_drain_and_sequence_order() {
    ControlTimeline timeline{8, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{0, 1, SetThrottle{0.25}},
        TimestampedControlCommand{96, 2, SetIgnitionEnabled{false}},
        TimestampedControlCommand{97, 3, SetThrottle{0.5}},
        TimestampedControlCommand{97, 4, SetThrottle{0.75}},
        TimestampedControlCommand{98, 5, SetFuelEnabled{false}},
    };
    expect(static_cast<bool>(timeline.enqueue(commands)),
           "valid ordered command batch was rejected");
    expect(timeline.queued_command_count() == commands.size(),
           "accepted batch did not occupy the fixed queue");

    auto step = timeline.drain_for_physics_step(0);
    expect(step && step.controls.applied_command_count == 1 &&
               step.controls.overrides ==
                   LiveControlOverrides{true, 0.25, false, false, false, false},
           "step-zero throttle was not applied right-continuously");
    for (std::uint64_t index = 1; index < 5; ++index) {
        step = timeline.drain_for_physics_step(index);
        expect(step && step.controls.applied_command_count == 0 &&
                   step.controls.overrides ==
                       LiveControlOverrides{true, 0.25, false, false, false, false},
               "future controls changed state before their projected step");
    }

    step = timeline.drain_for_physics_step(5);
    expect(step && step.controls.applied_command_count == 1 &&
               step.controls.overrides ==
                   LiveControlOverrides{true, 0.25, true, false, false, false},
           "delivery frame 96 was not applied at physics step 5");
    step = timeline.drain_for_physics_step(6);
    expect(step && step.controls.applied_command_count == 3 &&
               step.controls.overrides ==
                   LiveControlOverrides{true, 0.75, true, false, true, false},
           "same-step commands were not applied in caller sequence order");
    expect(timeline.queued_command_count() == 0,
           "drain retained commands after their projected step");

    const auto skipped = timeline.drain_for_physics_step(8);
    expect(!skipped &&
               skipped.error == ControlTimelineError::noncontiguous_physics_step &&
               timeline.next_physics_step() == 7,
           "noncontiguous drain mutated the physics cursor");
}

void test_atomic_batch_rejection() {
    ControlTimeline timeline{3, kPhysicsRate, kDeliveryRate};
    const std::array invalid_batch{
        TimestampedControlCommand{0, 10, SetThrottle{0.4}},
        TimestampedControlCommand{
            1, 11, SetThrottle{std::numeric_limits<double>::quiet_NaN()}},
    };
    const auto invalid = timeline.enqueue(invalid_batch);
    expect(!invalid && invalid.error == ControlTimelineError::invalid_payload &&
               invalid.command_index == 1 && timeline.queued_command_count() == 0,
           "invalid batch was not rejected atomically");

    const std::array accepted{
        TimestampedControlCommand{0, 10, SetThrottle{0.4}},
        TimestampedControlCommand{1, 11, SetFuelEnabled{false}},
    };
    expect(static_cast<bool>(timeline.enqueue(accepted)),
           "atomic rejection mutated sequence state before a corrected retry");

    const std::array too_many{
        TimestampedControlCommand{2, 12, SetThrottle{0.5}},
        TimestampedControlCommand{3, 13, SetThrottle{0.6}},
    };
    const auto full = timeline.enqueue(too_many);
    expect(!full && full.error == ControlTimelineError::capacity_exceeded &&
               timeline.queued_command_count() == accepted.size(),
           "full batch partially entered the command ring");

    const auto step0 = timeline.drain_for_physics_step(0);
    expect(step0 && step0.controls.applied_command_count == 1 &&
               step0.controls.overrides ==
                   LiveControlOverrides{true, 0.4, false, false, false, false},
           "atomic rejection changed the accepted command sequence");
    const auto step1 = timeline.drain_for_physics_step(1);
    expect(step1 && step1.controls.applied_command_count == 1 &&
               step1.controls.overrides ==
                   LiveControlOverrides{true, 0.4, false, false, true, false},
           "corrected batch did not remain intact");
}

void test_limiter_and_external_resistance_payloads() {
    ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{0, 1, SetLimiterEnabled{false}},
        TimestampedControlCommand{0, 2, SetExternalResistingTorque{17.5}},
        TimestampedControlCommand{19, 3, SetLimiterEnabled{true}},
        TimestampedControlCommand{19, 4, SetExternalResistingTorque{0.0}},
    };
    expect(static_cast<bool>(timeline.enqueue(commands)),
           "valid limiter/load command batch was rejected");

    const auto step0 = timeline.drain_for_physics_step(0);
    LiveControlOverrides expected_step0;
    expected_step0.has_limiter_enabled = true;
    expected_step0.limiter_enabled = false;
    expected_step0.has_external_resisting_torque_nm = true;
    expected_step0.external_resisting_torque_nm = 17.5;
    expect(step0 && step0.controls.applied_command_count == 2 &&
               step0.controls.overrides == expected_step0 &&
               step0.controls.overrides.any(),
           "step-zero limiter/load commands were not applied right-continuously");

    const auto step1 = timeline.drain_for_physics_step(1);
    auto expected_step1 = expected_step0;
    expected_step1.limiter_enabled = true;
    expected_step1.external_resisting_torque_nm = 0.0;
    expect(step1 && step1.controls.applied_command_count == 2 &&
               step1.controls.overrides == expected_step1,
           "later limiter/load commands did not replace the persistent snapshot");

    ControlTimeline invalid{2, kPhysicsRate, kDeliveryRate};
    const TimestampedControlCommand negative{0, 1, SetExternalResistingTorque{-1.0}};
    const auto negative_result = invalid.enqueue(std::span{&negative, 1});
    expect(!negative_result &&
               negative_result.error == ControlTimelineError::invalid_payload &&
               negative_result.command_index == 0 &&
               invalid.queued_command_count() == 0,
           "negative external resisting torque entered the timeline");

    const TimestampedControlCommand nonfinite{
        0, 1, SetExternalResistingTorque{std::numeric_limits<double>::infinity()}};
    const auto nonfinite_result = invalid.enqueue(std::span{&nonfinite, 1});
    expect(!nonfinite_result &&
               nonfinite_result.error == ControlTimelineError::invalid_payload &&
               nonfinite_result.command_index == 0 &&
               invalid.queued_command_count() == 0,
           "nonfinite external resisting torque entered the timeline");
}

void test_starter_level_is_sticky_until_release() {
    ControlTimeline timeline{2, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{0, 1, SetStarterEnabled{true}},
        TimestampedControlCommand{19, 2, SetStarterEnabled{false}},
    };
    expect(static_cast<bool>(timeline.enqueue(commands)),
           "valid starter press/release commands were rejected");

    const auto pressed = timeline.drain_for_physics_step(0);
    expect(pressed && pressed.controls.applied_command_count == 1 &&
               pressed.controls.overrides.has_starter_enabled &&
               pressed.controls.overrides.starter_enabled,
           "starter press did not become the right-continuous level");

    const auto released = timeline.drain_for_physics_step(1);
    expect(released && released.controls.applied_command_count == 1 &&
               released.controls.overrides.has_starter_enabled &&
               !released.controls.overrides.starter_enabled,
           "starter level did not persist until its explicit release");
}

void test_dyno_and_vehicle_levels_are_sticky() {
    ControlTimeline timeline{11, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{0, 1, SetDynoTargetEngineSpeed{2500.0}},
        TimestampedControlCommand{0, 2, SetDynoMaximumAbsorbingTorque{350.0}},
        TimestampedControlCommand{0, 3, SetDynoMaximumDrivingTorque{20.0}},
        TimestampedControlCommand{0, 4, SetVehicleSelectedForwardGear{2U}},
        TimestampedControlCommand{0, 5, SetVehicleClutchEngagement{0.25}},
        TimestampedControlCommand{0, 6, SetVehicleServiceBrakeApplication{0.75}},
        TimestampedControlCommand{19, 7, SetDynoTargetEngineSpeed{4500.0}},
        TimestampedControlCommand{19, 8, SetDynoMaximumDrivingTorque{0.0}},
        TimestampedControlCommand{19, 9, SetVehicleSelectedForwardGear{0U}},
        TimestampedControlCommand{19, 10, SetVehicleClutchEngagement{1.0}},
        TimestampedControlCommand{19, 11,
                                  SetVehicleServiceBrakeApplication{0.0}},
    };
    expect(static_cast<bool>(timeline.enqueue(commands)),
           "valid dyno/drivetrain command batch was rejected");

    const auto step0 = timeline.drain_for_physics_step(0);
    LiveControlOverrides expected_step0;
    expected_step0.has_dyno_target_engine_speed_rpm = true;
    expected_step0.dyno_target_engine_speed_rpm = 2500.0;
    expected_step0.has_dyno_maximum_absorbing_torque_nm = true;
    expected_step0.dyno_maximum_absorbing_torque_nm = 350.0;
    expected_step0.has_dyno_maximum_driving_torque_nm = true;
    expected_step0.dyno_maximum_driving_torque_nm = 20.0;
    expected_step0.has_vehicle_selected_forward_gear = true;
    expected_step0.vehicle_selected_forward_gear_ordinal = 2U;
    expected_step0.has_vehicle_clutch_engagement = true;
    expected_step0.vehicle_clutch_engagement_01 = 0.25;
    expected_step0.has_vehicle_service_brake_application = true;
    expected_step0.vehicle_service_brake_application_01 = 0.75;
    expect(step0 && step0.controls.applied_command_count == 6U &&
               step0.controls.overrides == expected_step0 &&
               step0.controls.overrides.any(),
           "step-zero dyno/drivetrain levels were not applied together");

    const auto step1 = timeline.drain_for_physics_step(1);
    auto expected_step1 = expected_step0;
    expected_step1.dyno_target_engine_speed_rpm = 4500.0;
    expected_step1.dyno_maximum_driving_torque_nm = 0.0;
    expected_step1.vehicle_selected_forward_gear_ordinal = 0U;
    expected_step1.vehicle_clutch_engagement_01 = 1.0;
    expected_step1.vehicle_service_brake_application_01 = 0.0;
    expect(step1 && step1.controls.applied_command_count == 5U &&
               step1.controls.overrides == expected_step1,
           "later dyno/drivetrain commands did not replace sticky levels");
}

void test_dyno_and_vehicle_payload_validation() {
    const std::array invalid_commands{
        TimestampedControlCommand{0, 1, SetThrottle{-0.0}},
        TimestampedControlCommand{0, 1, SetExternalResistingTorque{-0.0}},
        TimestampedControlCommand{0, 1, SetDynoTargetEngineSpeed{0.0}},
        TimestampedControlCommand{
            0, 1,
            SetDynoTargetEngineSpeed{std::numeric_limits<double>::infinity()}},
        TimestampedControlCommand{0, 1, SetDynoMaximumAbsorbingTorque{-0.0}},
        TimestampedControlCommand{0, 1, SetDynoMaximumDrivingTorque{-1.0}},
        TimestampedControlCommand{0, 1, SetVehicleClutchEngagement{-0.0}},
        TimestampedControlCommand{0, 1, SetVehicleClutchEngagement{1.01}},
        TimestampedControlCommand{0, 1,
                                  SetVehicleServiceBrakeApplication{-0.0}},
        TimestampedControlCommand{
            0, 1, SetVehicleServiceBrakeApplication{
                      std::numeric_limits<double>::quiet_NaN()}},
    };

    for (const auto &command : invalid_commands) {
        ControlTimeline timeline{1, kPhysicsRate, kDeliveryRate};
        const auto result = timeline.enqueue(std::span{&command, 1U});
        expect(!result && result.error == ControlTimelineError::invalid_payload &&
                   result.command_index == 0U &&
                   timeline.queued_command_count() == 0U &&
                   !timeline.current_overrides().any(),
               "noncanonical dyno/drivetrain payload entered the timeline");
    }
}

void test_ordering_lateness_and_cursor_rejections() {
    {
        ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
        const std::array duplicate{
            TimestampedControlCommand{10, 1, SetThrottle{0.1}},
            TimestampedControlCommand{10, 1, SetThrottle{0.2}},
        };
        const auto result = timeline.enqueue(duplicate);
        expect(!result && result.error == ControlTimelineError::duplicate_sequence &&
                   result.command_index == 1 && timeline.queued_command_count() == 0,
               "duplicate sequence was not rejected atomically");
    }
    {
        ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
        const std::array unordered_sequence{
            TimestampedControlCommand{10, 2, SetThrottle{0.1}},
            TimestampedControlCommand{10, 1, SetThrottle{0.2}},
        };
        const auto result = timeline.enqueue(unordered_sequence);
        expect(!result && result.error == ControlTimelineError::unordered_sequence &&
                   timeline.queued_command_count() == 0,
               "decreasing sequence was not rejected");
    }
    {
        ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
        const std::array unordered_frames{
            TimestampedControlCommand{11, 1, SetThrottle{0.1}},
            TimestampedControlCommand{10, 2, SetThrottle{0.2}},
        };
        const auto result = timeline.enqueue(unordered_frames);
        expect(!result &&
                   result.error == ControlTimelineError::unordered_delivery_frame &&
                   timeline.queued_command_count() == 0,
               "decreasing delivery timestamp was not rejected");
    }
    {
        ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
        expect(timeline.advance_delivery_cursor(10) == ControlTimelineError::none,
               "delivery cursor did not advance");
        expect(timeline.advance_delivery_cursor(9) ==
                       ControlTimelineError::delivery_cursor_regression &&
                   timeline.generated_delivery_frame() == 10,
               "delivery cursor regression mutated the cursor");
        const TimestampedControlCommand late{9, 1, SetThrottle{0.1}};
        const auto late_result = timeline.enqueue(std::span{&late, 1});
        expect(!late_result &&
                   late_result.error == ControlTimelineError::late_command &&
                   timeline.queued_command_count() == 0,
               "command targeting generated PCM was not rejected as late");
        const TimestampedControlCommand current{10, 1, SetThrottle{0.2}};
        expect(static_cast<bool>(timeline.enqueue(std::span{&current, 1})),
               "command at the ungenerated delivery cursor was rejected");
    }
    {
        ControlTimeline timeline{1, kPhysicsRate, kDeliveryRate};
        expect(static_cast<bool>(timeline.drain_for_physics_step(0)),
               "empty step-zero drain was rejected");
        const TimestampedControlCommand already_simulated{0, 1, SetThrottle{0.1}};
        const auto result = timeline.enqueue(std::span{&already_simulated, 1});
        expect(!result && result.error == ControlTimelineError::late_command,
               "command targeting an already-simulated physics step was accepted");
    }
}

void test_success_path_does_not_allocate() {
    ControlTimeline timeline{4, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{0, 1, SetThrottle{0.5}},
        TimestampedControlCommand{1, 2, SetIgnitionEnabled{false}},
    };

    const auto before = allocation_count;
    count_allocations = true;
    const auto enqueue_result = timeline.enqueue(commands);
    const auto step0 = timeline.drain_for_physics_step(0);
    const auto step1 = timeline.drain_for_physics_step(1);
    const auto cursor_result = timeline.advance_delivery_cursor(32);
    count_allocations = false;

    expect(enqueue_result && step0 && step1 &&
               cursor_result == ControlTimelineError::none,
           "measured success path unexpectedly failed");
    expect(allocation_count == before,
           "successful enqueue/drain allocated after timeline construction");
}

[[nodiscard]] simulation::LowOrderEngineCoreV1Runtime
require_core_runtime(simulation::LowOrderEngineCoreV1CompileResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message = "valid live-control core fixture was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<simulation::LowOrderEngineCoreV1Runtime>(std::move(result));
}

[[nodiscard]] contract::RandomPlan
require_random_plan(contract::RandomPlanCompilationResult result) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message = "valid live-control random plan was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<contract::RandomPlan>(std::move(result));
}

struct SimulationControlFixture {
    contract_test::InputBuilder builder;
    contract::EngineSpec engine = contract_test::make_engine(builder);
    contract::PresentationCalibration presentation =
        contract_test::make_presentation(builder, engine);
    contract::RenderScenario scenario = contract_test::make_scenario(builder, engine);
    contract::ResolvedRandomnessPolicy randomness =
        contract_test::make_randomness_policy(builder);

    SimulationControlFixture() {
        const auto legacy_method = contract::legacy_low_order_v1_method_identity();
        engine.methods.mechanism.value = legacy_method;
        engine.methods.valvetrain.value = legacy_method;
        engine.methods.gas_exchange.value = legacy_method;
        engine.methods.ignition.value = legacy_method;
        engine.methods.combustion.value = legacy_method;
        engine.methods.heat_transfer.value = legacy_method;
        engine.methods.excitation.value = legacy_method;
        auto &physics_profile =
            std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
        physics_profile.core.gas_path.intake.idle_throttle_plate_position_01.value =
            0.994;

        constexpr std::size_t kStepCount = 8;
        scenario.scenario_id = "session-live-control-eight-step";
        scenario.total_duration_s.value = static_cast<double>(kStepCount) / 10000.0;
        scenario.audible_start_s.value = 0.0;
        scenario.audible_duration_s.value = scenario.total_duration_s.value;
        scenario.preparation = contract::FixedSettling{
            {0.0, "session-test.no-warm-up"},
            {0.0, "session-test.no-settling"},
        };
        scenario.operating_state.value = {
            {
                "fired",
                0.0,
                {true, true, false, true, false},
            },
            {
                "ignition-off",
                0.0003,
                {false, true, false, true, false},
            },
            {
                "fuel-off",
                0.0005,
                {false, false, false, true, false},
            },
        };

        std::vector<double> rpm(kStepCount, 1800.0);
        contract::FixedRateRpmTrajectory rpm_lane{
            kPhysicsRate,   0,  contract::RpmSampleSemantics::post_step_rpm,
            std::move(rpm), {}, builder.add_resolution("session-test.rpm-lane"),
        };
        rpm_lane.samples_f64le_sha256 =
            contract::canonical_binary64_le_sha256(rpm_lane.post_step_rpm);
        scenario.mode = contract::PrescribedKinematicSweep{
            {
                std::move(rpm_lane),
                builder.resolved(0.0, "session-test.initial-theta"),
                builder.resolved(contract_test::fixed_rate_rpm_method(),
                                 "session-test.kinematic-method"),
            },
            {
                contract::TrajectoryInterpolation::right_continuous_hold,
                {{0.0, 0.25}, {0.0004, 0.75}},
                builder.add_resolution("session-test.throttle"),
            },
        };
    }
};

[[nodiscard]] simulation::LowOrderExecutionExtent
finite_extent(const contract::RenderScenario &scenario) {
    const auto frame_count = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    expect(frame_count.has_value(),
           "test scenario did not resolve to an integral physics horizon");
    return simulation::LowOrderExecutionExtent::finite_scenario(*frame_count);
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] const simulation::LowOrderEngineCoreV1StepView &
require_core_step(simulation::LowOrderEngineCoreV1AdvanceResult &result) {
    const auto *step = std::get_if<simulation::LowOrderEngineCoreV1StepView>(&result);
    expect(step != nullptr, "live-control fixture did not produce a core step");
    return *step;
}

void expect_core_step_bits_equal(
    const simulation::LowOrderEngineCoreV1StepView &left,
    const simulation::LowOrderEngineCoreV1StepView &right) {
    const auto &left_mechanics = left.mechanics.get();
    const auto &right_mechanics = right.mechanics.get();
    const auto &left_gas = left.gas.get();
    const auto &right_gas = right.gas.get();
    const auto same_event_shape = [](const auto &left_events,
                                     const auto &right_events) {
        if (left_events.size() != right_events.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left_events.size(); ++index) {
            if (left_events[index].ordinal_within_step !=
                    right_events[index].ordinal_within_step ||
                left_events[index].payload.index() !=
                    right_events[index].payload.index()) {
                return false;
            }
        }
        return true;
    };
    expect(left_mechanics.rate == right_mechanics.rate &&
               left_mechanics.sample_index == right_mechanics.sample_index &&
               left_mechanics.step_end_index == right_mechanics.step_end_index &&
               left_mechanics.timestamp_tick == right_mechanics.timestamp_tick &&
               left_mechanics.operating_state == right_mechanics.operating_state &&
               same_binary64(left_mechanics.requested_throttle_01,
                             right_mechanics.requested_throttle_01) &&
               same_binary64(left_mechanics.resolved_engine_throttle_01,
                             right_mechanics.resolved_engine_throttle_01) &&
               same_binary64(left_mechanics.intake_plate_position_01,
                             right_mechanics.intake_plate_position_01) &&
               same_binary64(left_mechanics.main_flow_multiplier_01,
                             right_mechanics.main_flow_multiplier_01) &&
               same_binary64(left_mechanics.theta_unwrapped_rad,
                             right_mechanics.theta_unwrapped_rad) &&
               same_binary64(left_mechanics.engine_speed_rpm,
                             right_mechanics.engine_speed_rpm) &&
               same_binary64(left_mechanics.omega_legacy_rad_s,
                             right_mechanics.omega_legacy_rad_s) &&
               same_binary64(left_mechanics.angular_speed_rad_s,
                             right_mechanics.angular_speed_rad_s) &&
               same_binary64(left_mechanics.angular_acceleration_rad_s2,
                             right_mechanics.angular_acceleration_rad_s2) &&
               same_binary64(left_mechanics.body_angle_psi_rad,
                             right_mechanics.body_angle_psi_rad) &&
               same_binary64(left_mechanics.theta_cycle_rad,
                             right_mechanics.theta_cycle_rad) &&
               same_binary64(left_mechanics.filtered_engine_speed_rpm,
                             right_mechanics.filtered_engine_speed_rpm) &&
               same_binary64(left_mechanics.timing_advance_rad,
                             right_mechanics.timing_advance_rad) &&
               same_binary64(left_mechanics.external_resisting_torque_nm,
                             right_mechanics.external_resisting_torque_nm) &&
               same_binary64(left_mechanics.limiter_timer_s,
                             right_mechanics.limiter_timer_s) &&
               left_mechanics.limiter_cut_active ==
                   right_mechanics.limiter_cut_active &&
               left_mechanics.cylinders == right_mechanics.cylinders &&
               same_event_shape(left_mechanics.events, right_mechanics.events) &&
               left_gas.rate == right_gas.rate &&
               left_gas.sample_index == right_gas.sample_index &&
               left_gas.step_end_index == right_gas.step_end_index &&
               left_gas.timestamp_tick == right_gas.timestamp_tick &&
               left_gas.gas_volumes == right_gas.gas_volumes &&
               left_gas.flow_edges == right_gas.flow_edges &&
               left_gas.cylinders == right_gas.cylinders &&
               left_gas.exhaust_routes == right_gas.exhaust_routes &&
               same_event_shape(left_gas.events, right_gas.events) &&
               same_binary64(left_gas.indicated_gas_torque_nm,
                             right_gas.indicated_gas_torque_nm),
           "empty live overrides changed core mechanics or gas bits");
}

void test_simulation_preserves_schedule_bits_until_a_field_is_overridden() {
    SimulationControlFixture fixture;
    const auto random_plan = require_random_plan(contract::compile_random_plan(
        fixture.randomness, fixture.engine, fixture.presentation, fixture.scenario));
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        fixture.engine.physics_profile);
    auto mechanism_plan_result = simulation::compile_mechanism_kinematics_plan(
        fixture.engine, profile.core);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&mechanism_plan_result)) {
        std::string message = "valid live-control mechanism plan was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    const auto mechanism_plan =
        std::get<simulation::SharedMechanismKinematicsPlan>(
            std::move(mechanism_plan_result));

    auto old_call_shape =
        require_core_runtime(simulation::compile_low_order_engine_core_v1_runtime(
            fixture.engine, fixture.scenario, profile.core, random_plan,
            mechanism_plan, finite_extent(fixture.scenario)));
    auto explicit_empty =
        require_core_runtime(simulation::compile_low_order_engine_core_v1_runtime(
            fixture.engine, fixture.scenario, profile.core, random_plan,
            mechanism_plan, finite_extent(fixture.scenario)));
    for (std::uint64_t step_index = 0; step_index < 8; ++step_index) {
        auto old_result = old_call_shape.advance();
        auto empty_result = explicit_empty.advance(LiveControlOverrides{});
        expect_core_step_bits_equal(require_core_step(old_result),
                                    require_core_step(empty_result));
    }

    auto authored =
        require_core_runtime(simulation::compile_low_order_engine_core_v1_runtime(
            fixture.engine, fixture.scenario, profile.core, random_plan,
            mechanism_plan, finite_extent(fixture.scenario)));
    auto controlled =
        require_core_runtime(simulation::compile_low_order_engine_core_v1_runtime(
            fixture.engine, fixture.scenario, profile.core, random_plan,
            mechanism_plan, finite_extent(fixture.scenario)));
    ControlTimeline timeline{3, kPhysicsRate, kDeliveryRate};
    const std::array commands{
        TimestampedControlCommand{97, 1, SetThrottle{0.5}},
        TimestampedControlCommand{97, 2, SetIgnitionEnabled{true}},
        TimestampedControlCommand{97, 3, SetFuelEnabled{true}},
    };
    expect(static_cast<bool>(timeline.enqueue(commands)),
           "simulation live-control batch was rejected");

    for (std::uint64_t step_index = 0; step_index < 8; ++step_index) {
        const auto controls = timeline.drain_for_physics_step(step_index);
        expect(static_cast<bool>(controls),
               "timeline did not resolve a contiguous simulation step");
        auto authored_result = authored.advance();
        auto controlled_result = controlled.advance(controls.controls.overrides);
        const auto &authored_step = require_core_step(authored_result);
        const auto &controlled_step = require_core_step(controlled_result);
        if (step_index < 6) {
            expect_core_step_bits_equal(authored_step, controlled_step);
            if (step_index == 4) {
                expect(controlled_step.mechanics.get().requested_throttle_01 == 0.75,
                       "authored throttle boundary stopped before a live override");
            }
            if (step_index == 5) {
                expect(
                    !controlled_step.mechanics.get().operating_state.ignition_enabled &&
                        !controlled_step.mechanics.get().operating_state.fuel_enabled,
                    "authored operating-state boundaries stopped before override");
            }
            continue;
        }

        const auto &mechanics = controlled_step.mechanics.get();
        const auto &authored_mechanics = authored_step.mechanics.get();
        expect(mechanics.requested_throttle_01 == 0.5 &&
                   mechanics.operating_state.ignition_enabled &&
                   mechanics.operating_state.fuel_enabled &&
                   mechanics.operating_state.starter_enabled ==
                       authored_mechanics.operating_state.starter_enabled &&
                   mechanics.operating_state.dyno_enabled ==
                       authored_mechanics.operating_state.dyno_enabled &&
                   mechanics.operating_state.limiter_enabled ==
                       authored_mechanics.operating_state.limiter_enabled,
               "mapped live commands changed the wrong mechanics fields");
    }
}

void run_tests() {
    test_exact_delivery_projection();
    test_right_continuous_drain_and_sequence_order();
    test_atomic_batch_rejection();
    test_limiter_and_external_resistance_payloads();
    test_starter_level_is_sticky_until_release();
    test_dyno_and_vehicle_levels_are_sticky();
    test_dyno_and_vehicle_payload_validation();
    test_ordering_lateness_and_cursor_rejections();
    test_success_path_does_not_allocate();
    test_simulation_preserves_schedule_bits_until_a_field_is_overridden();
}

} // namespace

void *operator new(std::size_t size) {
    if (count_allocations) {
        ++allocation_count;
    }
    if (auto *memory = allocate_test_storage(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *memory) noexcept {
    release_test_storage(memory);
}

void operator delete[](void *memory) noexcept {
    release_test_storage(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    release_test_storage(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    release_test_storage(memory);
}

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Session control timeline test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
