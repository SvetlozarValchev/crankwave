#include "authored_engine_fixture_support.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/free_vehicle_method_registry.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_capture_session.hpp"
#include "simulation/low_order_dynamic_crank_runtime.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::simulation;
using engine_sim_offline::test::AuthoredEngineFixture;

constexpr double kPreparationEndS = 0.22;
constexpr double kBrakeReleaseS = 0.221;
constexpr double kFifthGearSelectionS = 0.26;
constexpr double kTotalDurationS = 0.30;
constexpr std::uint64_t kPreparationEndFrame = 2200U;
constexpr std::uint64_t kBrakeReleaseFrame = 2210U;
constexpr std::uint64_t kFifthGearSelectionFrame = 2600U;
constexpr std::uint64_t kTotalFrameCount = 3000U;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[noreturn]] void fail_report(std::string_view context,
                              const ValidationReport &report) {
    std::ostringstream message;
    message << context;
    for (const auto &issue : report.issues) {
        message << "\n  " << issue.path << ": " << issue.message;
    }
    throw std::runtime_error{message.str()};
}

template <typename T>
[[nodiscard]] ResolvedValue<T> resolved(T value, std::string resolution_id) {
    return {std::move(value), std::move(resolution_id)};
}

[[nodiscard]] Sha256Digest nonzero_request_identity() {
    Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

[[nodiscard]] AuthoredEngineFixture
make_free_vehicle_request(const AuthoredEngineFixture &canonical) {
    auto request = canonical;
    auto &scenario = request.scenario;
    const auto *inertial = std::get_if<InertialDyno>(&scenario.mode);
    expect(inertial != nullptr,
           "canonical authored scenario lost inertial-dyno ownership");

    const auto inertia_calculation = calculate_centered_slider_crank_cycle_mean_inertia(
        engine_sim_offline::test::operating_profile(request.engine).core.mechanism);
    const auto *engine_inertia =
        std::get_if<CenteredSliderCrankCycleMeanInertia>(&inertia_calculation);
    expect(engine_inertia != nullptr,
           "canonical BMW mechanism did not derive cycle-mean inertia");

    FreeVehicleRig rig;
    rig.id = RigId{1001U};
    rig.semantic_id = resolved(std::string{"test-bmw-e36-rig"}, "test.rig.id");
    rig.vehicle.id = VehicleId{1002U};
    rig.vehicle.semantic_id = resolved(std::string{"test-bmw-e36"}, "test.vehicle.id");
    rig.vehicle.mass_kg = resolved(1399.78741682, "test.vehicle.mass");
    rig.vehicle.drag_coefficient = resolved(0.32, "test.vehicle.drag");
    rig.vehicle.frontal_area_m2 = resolved(2.368253328, "test.vehicle.area");
    rig.vehicle.differential_ratio = resolved(3.15, "test.vehicle.diff");
    rig.vehicle.tire_radius_m = resolved(0.30988, "test.vehicle.tire");
    rig.vehicle.rolling_resistance_force_n =
        resolved(205.978517819613, "test.vehicle.rolling");
    rig.vehicle.maximum_service_brake_force_n =
        resolved(50000.0, "test.vehicle.service-brake");
    rig.transmission.id = TransmissionId{1003U};
    rig.transmission.semantic_id =
        resolved(std::string{"test-e36-five-speed"}, "test.transmission.id");
    rig.transmission.maximum_clutch_torque_nm =
        resolved(406.745598, "test.transmission.clutch");
    rig.transmission.gears = {
        {GearId{1011U}, resolved<std::uint32_t>(1U, "test.gear-1.ordinal"),
         resolved(std::string{"gear-1"}, "test.gear-1.id"),
         resolved(4.20, "test.gear-1.ratio")},
        {GearId{1012U}, resolved<std::uint32_t>(2U, "test.gear-2.ordinal"),
         resolved(std::string{"gear-2"}, "test.gear-2.id"),
         resolved(2.49, "test.gear-2.ratio")},
        {GearId{1013U}, resolved<std::uint32_t>(3U, "test.gear-3.ordinal"),
         resolved(std::string{"gear-3"}, "test.gear-3.id"),
         resolved(1.66, "test.gear-3.ratio")},
        {GearId{1014U}, resolved<std::uint32_t>(4U, "test.gear-4.ordinal"),
         resolved(std::string{"gear-4"}, "test.gear-4.id"),
         resolved(1.24, "test.gear-4.ratio")},
        {GearId{1015U}, resolved<std::uint32_t>(5U, "test.gear-5.ordinal"),
         resolved(std::string{"gear-5"}, "test.gear-5.id"),
         resolved(1.00, "test.gear-5.ratio")},
    };

    auto initial_engine_speed = inertial->initial_engine_speed_rpm;
    initial_engine_speed.value = 3000.0;
    scenario.mode = FreeVehicle{
        std::move(initial_engine_speed),
        inertial->initial_theta_rad,
        resolved(engine_inertia->engine_equivalent_inertia_kg_m2,
                 "test.engine-baseline-inertia"),
        resolved(0.0, "test.initial-vehicle-speed"),
        std::move(rig),
        {TrajectoryInterpolation::right_continuous_hold,
         {{0.0, 0.85}},
         "test.free-vehicle.throttle"},
        resolved(
            std::vector<GearSelectionPoint>{
                {"select-first", 0.0, GearId{1011U}},
                {"select-fifth", kFifthGearSelectionS, GearId{1015U}},
            },
            "test.free-vehicle.selected-gear"),
        resolved(std::vector<ScalarControlPoint>{{"clutch-engaged", 0.0, 1.0}},
                 "test.free-vehicle.clutch"),
        resolved(
            std::vector<ScalarControlPoint>{
                {"service-brake-held", 0.0, 1.0},
                {"service-brake-released", kBrakeReleaseS, 0.0},
            },
            "test.free-vehicle.service-brake"),
        resolved(nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
                 "test.free-vehicle.crank-method"),
        resolved(forward_vehicle_road_load_method_identity(),
                 "test.free-vehicle.road-load-method"),
        resolved(bounded_clutch_coupling_method_identity(),
                 "test.free-vehicle.clutch-method"),
        resolved(bounded_forward_vehicle_drivetrain_method_identity(),
                 "test.free-vehicle.drivetrain-method"),
    };
    scenario.scenario_id = "bmw-free-vehicle-runtime-integration";
    scenario.mode_resolution_id = "test.free-vehicle.mode";
    auto *preparation = std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
    expect(preparation != nullptr,
           "canonical authored scenario lost fixed-horizon preparation");
    preparation->fixed_preparation_horizon_s.value = kPreparationEndS;
    preparation->trailing_complete_cycle_count.value = 2U;
    scenario.operating_state.value = {
        {"warm-running", 0.0, {true, true, false, false, false}},
    };
    scenario.total_duration_s.value = kTotalDurationS;
    scenario.audible_start_s.value = kPreparationEndS;
    scenario.audible_duration_s.value = kTotalDurationS - kPreparationEndS;
    scenario.rates.physics = {10000U, 1U};
    scenario.rates.capture = scenario.rates.physics;
    scenario.quality.value.capture_block_capacity_frames = 200U;
    scenario.quality.value.event_journal_capacity_records = 3800U;
    return request;
}

[[nodiscard]] LowOrderCapturePlan
require_capture_plan(LowOrderCapturePlanCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("FreeVehicle capture plan failed admission", *report);
    }
    return std::get<LowOrderCapturePlan>(std::move(result));
}

[[nodiscard]] LowOrderDynamicCrankRuntime
require_dynamic_runtime(LowOrderDynamicCrankCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("FreeVehicle dynamic runtime failed admission", *report);
    }
    return std::get<LowOrderDynamicCrankRuntime>(std::move(result));
}

[[nodiscard]] LowOrderEngineCoreV1Runtime
require_core_runtime(LowOrderEngineCoreV1CompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("FreeVehicle core runtime failed admission", *report);
    }
    return std::get<LowOrderEngineCoreV1Runtime>(std::move(result));
}

[[nodiscard]] LowOrderCaptureSession
require_capture_session(LowOrderCaptureCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("FreeVehicle capture session failed admission", *report);
    }
    return std::get<LowOrderCaptureSession>(std::move(result));
}

void test_bmw_launch_shift_and_internal_state(const AuthoredEngineFixture &canonical) {
    const auto request = make_free_vehicle_request(canonical);
    const auto extent = LowOrderExecutionExtent::finite_scenario(kTotalFrameCount);
    auto capture_plan = require_capture_plan(
        compile_low_order_capture_plan(request.engine, request.scenario, extent));
    const auto random_plan = engine_sim_offline::test::compile_fixture_random_plan(
        request, request.engine, request.scenario);
    auto runtime = require_dynamic_runtime(compile_low_order_dynamic_crank_runtime(
        request.engine, request.scenario, capture_plan, nonzero_request_identity(),
        extent));
    auto core = require_core_runtime(compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario,
        engine_sim_offline::test::operating_profile(request.engine).core, random_plan,
        extent));

    const auto initial = runtime.free_vehicle_state();
    expect(
        initial.has_value() && std::abs(initial->engine_speed_rpm - 3000.0) < 1e-10 &&
            initial->vehicle_speed_m_s == 0.0 && initial->vehicle_distance_m == 0.0 &&
            initial->selected_forward_gear_ordinal == 1U &&
            initial->clutch_engagement_01 == 1.0 &&
            initial->service_brake_application_01 == 1.0,
        "initial simulation-internal FreeVehicle snapshot is incomplete");

    bool checked_held_release = false;
    bool checked_launch = false;
    bool checked_fifth = false;
    for (std::uint64_t frame = 0U; frame < kTotalFrameCount; ++frame) {
        auto result = runtime.advance(core);
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "FreeVehicle runtime faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
        const auto *step = std::get_if<LowOrderDynamicCrankStepView>(&result);
        expect(step != nullptr, "FreeVehicle runtime completed before its horizon");
        const auto state = runtime.free_vehicle_state();
        expect(state.has_value(),
               "FreeVehicle runtime lost its internal state snapshot");
        expect(std::isfinite(state->engine_speed_rpm) &&
                   std::isfinite(state->vehicle_speed_m_s) &&
                   std::isfinite(state->vehicle_distance_m) &&
                   state->vehicle_speed_m_s >= 0.0 && state->vehicle_distance_m >= 0.0,
               "FreeVehicle runtime published nonfinite or reverse state");

        const auto accepted = runtime.accepted_sample_count();
        if (accepted <= kPreparationEndFrame) {
            expect(state->vehicle_speed_m_s == 0.0 && state->vehicle_distance_m == 0.0,
                   "held preparation advanced the vehicle");
        }
        if (accepted == kPreparationEndFrame + 1U) {
            expect(state->vehicle_speed_m_s == 0.0 &&
                       state->vehicle_distance_m == 0.0 &&
                       state->applied_average_clutch_torque_on_engine_nm < 0.0 &&
                       state->applied_average_road_load_force_n > 0.0,
                   "service brake did not hold the first released clutch step");
            expect(step->capture_torque.actuator.availability ==
                           Availability::unavailable &&
                       step->capture_torque.actuator.unavailable_reason ==
                           QuantityUnavailableReason::scenario_not_applicable &&
                       step->capture_torque.dyno_reaction.availability ==
                           Availability::unavailable &&
                       step->capture_torque.dyno_reaction.unavailable_reason ==
                           QuantityUnavailableReason::scenario_not_applicable,
                   "FreeVehicle mislabeled drivetrain reaction as dyno telemetry");
            checked_held_release = true;
        }
        if (accepted == kBrakeReleaseFrame + 200U) {
            expect(state->service_brake_application_01 == 0.0 &&
                       state->vehicle_speed_m_s > 0.0 &&
                       state->vehicle_distance_m > 0.0,
                   "BMW did not launch after the authored service-brake release");
            checked_launch = true;
        }
        if (accepted == kFifthGearSelectionFrame + 1U) {
            expect(state->selected_forward_gear_ordinal == 5U &&
                       state->clutch_engagement_01 == 1.0 &&
                       state->service_brake_application_01 == 0.0,
                   "right-continuous fifth-gear event missed its left boundary");
            checked_fifth = true;
        }
    }

    expect(checked_held_release && checked_launch && checked_fifth,
           "FreeVehicle integration did not reach every verification boundary");
    expect(runtime.finalized() && core.completed(),
           "FreeVehicle runtime and core did not complete the same finite horizon");
    auto completion = runtime.advance(core);
    const auto *completed = std::get_if<LowOrderEngineCoreV1Completed>(&completion);
    expect(completed != nullptr && completed->sample_count == kTotalFrameCount,
           "FreeVehicle completion was not terminal and stable");
}

void test_capture_session_selects_free_vehicle(const AuthoredEngineFixture &canonical) {
    const auto request = make_free_vehicle_request(canonical);
    auto session = require_capture_session(compile_low_order_capture_session(
        request.engine, request.scenario,
        engine_sim_offline::test::compile_fixture_random_plan(request, request.engine,
                                                              request.scenario),
        nonzero_request_identity(),
        LowOrderExecutionExtent::finite_scenario(kTotalFrameCount)));

    std::uint64_t captured_frames = 0U;
    while (true) {
        auto result = session.publish_next_block([&](const CaptureBlockView &block) {
            expect(block.clock().first_sample_index == captured_frames,
                   "FreeVehicle capture blocks lost contiguous frame order");
            captured_frames += block.frame_count();
            return true;
        });
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "FreeVehicle capture session faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            expect(completed->sample_count == kTotalFrameCount &&
                       captured_frames == kTotalFrameCount,
                   "FreeVehicle capture completion disagreed with its blocks");
            return;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{
                "usage: low_order_free_vehicle_runtime_test <repository-root>"};
        }
        const auto canonical =
            engine_sim_offline::test::load_canonical_authored_engine_fixture(
                std::filesystem::path{argv[1]});
        test_bmw_launch_shift_and_internal_state(canonical);
        test_capture_session_selects_free_vehicle(canonical);
    } catch (const std::exception &error) {
        std::cerr << "low_order_free_vehicle_runtime_test: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
