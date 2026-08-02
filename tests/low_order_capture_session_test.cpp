#include "authored_engine_fixture_support.hpp"
#include "engine_sim_offline/artifacts/telemetry_encoder.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_capture_session.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::simulation;
using engine_sim_offline::artifacts::make_telemetry_encoder;
using engine_sim_offline::artifacts::TelemetryEncoder;
using engine_sim_offline::artifacts::TelemetryEncodingError;
using engine_sim_offline::artifacts::TelemetryStreamDescriptor;
using CoreRuntimeFactory =
    engine_sim_offline::simulation::detail::LowOrderEngineCoreV1RuntimeFactory;

inline constexpr double kOuterStepS = 1.0 / 10000.0;
inline constexpr double kOperatingHeldRpm = 3000.0;
inline constexpr double kOperatingCutoffTimeS = 0.22;
inline constexpr double kOperatingTotalDurationS = 0.3;
inline constexpr std::size_t kOperatingStepCount = 3000U;
inline constexpr std::uint32_t kOperatingCyclesPerBlock = 2U;
inline constexpr double kFreeEngineControlBoundaryS = 0.24;
inline constexpr double kFreeEngineTotalDurationS = 0.4;
inline constexpr double kFreeEngineAttachedInertiaKgM2 = 0.05;
inline constexpr double kFreeEngineInitialResistingTorqueNm = 50.0;
inline constexpr double kFreeEngineBoundaryResistingTorqueNm = 100.0;
inline constexpr CaptureValidityMask kMechanism =
    capture_validity_mask(CaptureValidity::mechanism);
inline constexpr CaptureValidityMask kThermodynamic =
    capture_validity_mask(CaptureValidity::thermodynamic_state);
inline constexpr CaptureValidityMask kComposition =
    capture_validity_mask(CaptureValidity::composition);
inline constexpr CaptureValidityMask kGasExchange =
    capture_validity_mask(CaptureValidity::gas_exchange);
inline constexpr CaptureValidityMask kCombustion =
    capture_validity_mask(CaptureValidity::combustion);
inline constexpr CaptureValidityMask kTorque =
    capture_validity_mask(CaptureValidity::torque);

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 const std::string &message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message + "; actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
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

[[nodiscard]] TelemetryEncoder
require_telemetry(engine_sim_offline::artifacts::TelemetryEncoderResult result) {
    if (const auto *failure = std::get_if<TelemetryEncodingError>(&result)) {
        throw std::runtime_error{"telemetry encoder rejected capture: " +
                                 failure->path + ": " + failure->message};
    }
    return std::get<TelemetryEncoder>(std::move(result));
}

[[nodiscard]] std::vector<std::byte>
encode_capture_block(const CaptureBlockView &block) {
    const TelemetryStreamDescriptor descriptor{
        block.clock(),
        block.frame_count(),
        block.reference_parity().has_value(),
        16U * 1024U,
    };
    auto encoder =
        require_telemetry(make_telemetry_encoder(block.layout(), descriptor));
    std::vector<std::byte> bytes;
    const auto consume = [&](std::uint64_t offset, std::span<const std::byte> chunk) {
        if (offset != bytes.size() || chunk.empty() || chunk.size() > 16U * 1024U) {
            return false;
        }
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return true;
    };
    const auto require_ok = [](const auto &status, std::string_view operation) {
        if (status.has_value()) {
            throw std::runtime_error{"telemetry " + std::string{operation} +
                                     " failed: " + status->path + ": " +
                                     status->message};
        }
    };
    require_ok(encoder.begin(consume), "begin");
    require_ok(encoder.write_block(block, consume), "block");
    require_ok(encoder.finish(consume), "finish");
    expect(encoder.finished() && encoder.frames_written() == block.frame_count() &&
               encoder.bytes_emitted() == bytes.size(),
           "single-block telemetry fingerprint is incomplete");
    return bytes;
}

[[nodiscard]] Sha256Digest nonzero_request_identity() {
    Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

[[nodiscard]] LowOrderExecutionExtent finite_extent(const RenderScenario &scenario) {
    const auto frame_count =
        resolve_frame_index(scenario.total_duration_s.value, scenario.rates.physics);
    return LowOrderExecutionExtent::finite_scenario(frame_count.value_or(1U));
}

[[nodiscard]] engine_sim_offline::test::AuthoredEngineFixture
make_operating_capture_request(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    auto request = canonical;
    auto &scenario = request.scenario;
    const auto *inertial = std::get_if<InertialDyno>(&scenario.mode);
    expect(inertial != nullptr,
           "canonical authored scenario lost inertial-dyno ownership");
    auto engine_speed = inertial->initial_engine_speed_rpm;
    engine_speed.value = kOperatingHeldRpm;
    const auto initial_theta = inertial->initial_theta_rad;
    auto throttle = ResolvedValue<double>{
        inertial->throttle_01.points.front().value,
        inertial->throttle_01.resolution_id,
    };
    throttle.value = 0.85;
    scenario.mode = HeldSpeed{std::move(engine_speed), initial_theta, throttle};
    scenario.scenario_id = "authored-held-capture-integration";
    auto *preparation = std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
    expect(preparation != nullptr,
           "canonical authored scenario lost fixed-horizon preparation");
    preparation->fixed_preparation_horizon_s.value = kOperatingCutoffTimeS;
    preparation->trailing_complete_cycle_count.value = kOperatingCyclesPerBlock;
    scenario.operating_state.value = {
        {
            "held-running",
            0.0,
            {true, true, false, true, false},
        },
    };
    scenario.total_duration_s.value = kOperatingTotalDurationS;
    scenario.audible_start_s.value = kOperatingCutoffTimeS;
    scenario.audible_duration_s.value =
        kOperatingTotalDurationS - kOperatingCutoffTimeS;
    scenario.rates.physics = {10000U, 1U};
    scenario.rates.capture = scenario.rates.physics;
    scenario.quality.value.capture_block_capacity_frames = 200U;
    scenario.quality.value.event_journal_capacity_records = 3800U;
    return request;
}

[[nodiscard]] engine_sim_offline::test::AuthoredEngineFixture
make_free_engine_capture_request(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical,
    double post_boundary_throttle, double initial_resisting_torque_nm,
    double boundary_resisting_torque_nm, double boundary_time_s,
    double total_duration_s = kFreeEngineTotalDurationS) {
    auto request = canonical;
    auto &scenario = request.scenario;
    const auto *inertial = std::get_if<InertialDyno>(&scenario.mode);
    expect(inertial != nullptr,
           "canonical authored scenario lost inertial-dyno ownership");

    auto initial_engine_speed = inertial->initial_engine_speed_rpm;
    initial_engine_speed.value = kOperatingHeldRpm;
    const auto &mechanism =
        engine_sim_offline::test::operating_profile(request.engine).core.mechanism;
    const auto inertia_calculation =
        calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto *derived_inertia =
        std::get_if<CenteredSliderCrankCycleMeanInertia>(&inertia_calculation);
    expect(derived_inertia != nullptr,
           "canonical authored mechanism could not derive its cycle-mean inertia");
    auto engine_baseline_inertia = inertial->equivalent_inertia_kg_m2;
    engine_baseline_inertia.value = derived_inertia->engine_equivalent_inertia_kg_m2;
    auto attached_inertia = inertial->equivalent_inertia_kg_m2;
    attached_inertia.value = kFreeEngineAttachedInertiaKgM2;
    auto total_equivalent_inertia = inertial->equivalent_inertia_kg_m2;
    total_equivalent_inertia.value =
        engine_baseline_inertia.value + attached_inertia.value;
    scenario.mode = FreeEngine{
        std::move(initial_engine_speed),
        inertial->initial_theta_rad,
        std::move(engine_baseline_inertia),
        std::move(attached_inertia),
        std::move(total_equivalent_inertia),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.85}, {boundary_time_s, post_boundary_throttle}},
            "free-engine-test.throttle",
        },
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, initial_resisting_torque_nm},
             {boundary_time_s, boundary_resisting_torque_nm}},
            "free-engine-test.external-resisting-torque",
        },
        {
            nonnegative_speed_free_engine_centered_slider_crank_method_identity(),
            "free-engine-test.crank-dynamics-method",
        },
    };
    scenario.scenario_id = "authored-free-engine-capture-integration";
    scenario.mode_resolution_id = "free-engine-test.mode";
    auto *preparation = std::get_if<FixedHorizonCycleSampling>(&scenario.preparation);
    expect(preparation != nullptr,
           "canonical authored scenario lost fixed-horizon preparation");
    preparation->fixed_preparation_horizon_s.value = kOperatingCutoffTimeS;
    preparation->trailing_complete_cycle_count.value = kOperatingCyclesPerBlock;
    scenario.operating_state.value = {
        {
            "free-engine-warm-running",
            0.0,
            {true, true, false, false, false},
        },
    };
    scenario.total_duration_s.value = total_duration_s;
    scenario.audible_start_s.value = kOperatingCutoffTimeS;
    scenario.audible_duration_s.value = total_duration_s - kOperatingCutoffTimeS;
    scenario.rates.physics = {10000U, 1U};
    scenario.rates.capture = scenario.rates.physics;
    scenario.quality.value.capture_block_capacity_frames = 200U;
    scenario.quality.value.event_journal_capacity_records = 3800U;
    return request;
}

[[nodiscard]] engine_sim_offline::test::AuthoredEngineFixture
make_stopped_free_engine_capture_request(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr double kStoppedDurationS = 0.04;
    auto request = make_free_engine_capture_request(canonical, 0.0, 0.0, 0.0, 0.0,
                                                    kStoppedDurationS);
    auto &scenario = request.scenario;
    auto &free_engine = std::get<FreeEngine>(scenario.mode);
    free_engine.initial_engine_speed_rpm.value = 0.0;
    free_engine.throttle_01.points = {{0.0, 0.0}};
    free_engine.external_resisting_torque_nm.points = {{0.0, 0.0}};
    scenario.scenario_id = "authored-stopped-free-engine-capture-integration";
    scenario.preparation = FixedSettling{
        {0.0, "stopped-free-engine-test.warm-up-duration"},
        {0.0, "stopped-free-engine-test.settling-duration"},
    };
    scenario.operating_state.value = {
        {
            "stopped",
            0.0,
            {false, false, false, false, false},
        },
    };
    scenario.audible_start_s.value = 0.0;
    scenario.audible_duration_s.value = kStoppedDurationS;
    return request;
}

[[nodiscard]] CenteredSliderCrankConfigurationInertiaPlan
configuration_inertia_plan(const EngineSpec &engine, double attached_inertia_kg_m2) {
    const auto &mechanism =
        engine_sim_offline::test::operating_profile(engine).core.mechanism;
    CenteredSliderCrankConfigurationInertiaPlan plan{
        mechanism.cranks.front().authored_crank_inertia_kg_m2.value,
        attached_inertia_kg_m2,
        {},
    };
    plan.cylinders.reserve(mechanism.cylinders.size());
    for (const auto &assembly : mechanism.cylinders) {
        const auto &parameters = assembly.parameters;
        const auto &direct =
            std::get<LegacyDirectJournalKinematics>(assembly.kinematics);
        plan.cylinders.push_back({
            legacy_wrap_2pi(mechanism.cranks.front().crank_tdc_reference_rad.value +
                            direct.journal_angle_rad.value - kLegacyPi / 2.0),
            direct.crank_radius_m.value,
            parameters.connecting_rod_length_m.value,
            parameters.connecting_rod_center_of_mass_from_crank_pin_m.value,
            parameters.piston_mass_kg.value,
            parameters.connecting_rod_mass_kg.value,
            parameters.connecting_rod_inertia_kg_m2.value,
        });
    }
    return plan;
}

[[nodiscard]] double expected_configuration_dependent_alpha(
    const EngineSpec &engine, const EngineCaptureSample &left_boundary,
    double attached_inertia_kg_m2, double applied_net_engine_torque_nm,
    double applied_resisting_torque_nm) {
    const auto calculation = evaluate_centered_slider_crank_configuration_inertia(
        configuration_inertia_plan(engine, attached_inertia_kg_m2),
        left_boundary.theta_rad);
    const auto *inertia =
        std::get_if<CenteredSliderCrankConfigurationInertia>(&calculation);
    expect(inertia != nullptr,
           "valid capture fixture configuration inertia was rejected");
    const double velocity_inertia_torque_nm =
        0.5 * inertia->total_derivative_kg_m2_per_rad *
        left_boundary.angular_speed_rad_s * left_boundary.angular_speed_rad_s;
    return (applied_net_engine_torque_nm - applied_resisting_torque_nm -
            velocity_inertia_torque_nm) /
           inertia->total_inertia_kg_m2;
}

[[nodiscard]] LegacyLowOrderMechanicsSession
require_mechanics(CoreRuntimeFactory::MechanicsCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("authored mechanics request failed admission", *report);
    }
    return std::get<LegacyLowOrderMechanicsSession>(std::move(result));
}

[[nodiscard]] LegacyLowOrderGasSession
require_gas(CoreRuntimeFactory::GasCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("authored gas request failed admission", *report);
    }
    return std::get<LegacyLowOrderGasSession>(std::move(result));
}

[[nodiscard]] LowOrderCaptureSession
require_simulation(LowOrderCaptureCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("authored capture request failed admission", *report);
    }
    return std::get<LowOrderCaptureSession>(std::move(result));
}

[[nodiscard]] RandomPlan
fixture_random_plan(const engine_sim_offline::test::AuthoredEngineFixture &authored,
                    const EngineSpec &engine, const RenderScenario &scenario) {
    return engine_sim_offline::test::compile_fixture_random_plan(authored, engine,
                                                                 scenario);
}

struct FreeEngineCaptureTrace {
    std::vector<EngineCaptureSample> frames;
    std::vector<std::byte> encoded_capture;
    LowOrderCaptureCompleted completion;
};

struct FreeEngineLiveControlContext {
    std::uint64_t next_step = 0;
    std::uint64_t release_step = 0;
    double external_resisting_torque_nm = 0.0;
};

[[nodiscard]] engine_sim_offline::simulation::detail::LowOrderLiveControlStep
drain_free_engine_live_controls(void *context, std::uint64_t physics_step) noexcept {
    auto &controls = *static_cast<FreeEngineLiveControlContext *>(context);
    if (physics_step != controls.next_step) {
        return {};
    }
    ++controls.next_step;

    LiveControlOverrides overrides;
    if (physics_step >= controls.release_step) {
        overrides.has_limiter_enabled = true;
        overrides.limiter_enabled = true;
        overrides.has_external_resisting_torque_nm = true;
        overrides.external_resisting_torque_nm = controls.external_resisting_torque_nm;
    }
    return {true, overrides};
}

[[nodiscard]] FreeEngineCaptureTrace
run_free_engine_capture(const engine_sim_offline::test::AuthoredEngineFixture &request,
                        const bool retain_encoded_capture = false) {
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));

    FreeEngineCaptureTrace trace;
    while (true) {
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            const auto report = validate(block, request.engine, request.scenario);
            if (!report.ok()) {
                fail_report("free-engine capture block failed validation", report);
            }
            expect(block.clock().first_sample_index == trace.frames.size(),
                   "free-engine capture blocks lost contiguous frame order");
            trace.frames.insert(trace.frames.end(), block.engine().begin(),
                                block.engine().end());
            if (retain_encoded_capture) {
                const auto bytes = encode_capture_block(block);
                trace.encoded_capture.insert(trace.encoded_capture.end(), bytes.begin(),
                                             bytes.end());
            }
            return true;
        });
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "free-engine capture faulted: " + failure->detail_code + "; " +
                failure->state_summary +
                "; time-s=" + std::to_string(failure->scenario_time_s)};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            trace.completion = *completed;
            return trace;
        }
    }
}

[[nodiscard]] const LegacyMechanismStep &
require_mechanics_step(LegacyMechanicsAdvanceResult &result,
                       std::uint64_t expected_sample_index) {
    if (const auto *fault = std::get_if<FailureContext>(&result)) {
        throw std::runtime_error{"independent mechanics fault at frame " +
                                 std::to_string(expected_sample_index) + ": " +
                                 fault->detail_code};
    }
    const auto *step =
        std::get_if<std::reference_wrapper<const LegacyMechanismStep>>(&result);
    expect(step != nullptr,
           "independent mechanics completed before the capture horizon");
    return step->get();
}

[[nodiscard]] const LegacyLowOrderGasStep &
require_gas_step(LegacyGasAdvanceResult &result, std::uint64_t expected_sample_index) {
    if (const auto *fault = std::get_if<FailureContext>(&result)) {
        throw std::runtime_error{"independent gas fault at frame " +
                                 std::to_string(expected_sample_index) + ": " +
                                 fault->detail_code};
    }
    const auto *step =
        std::get_if<std::reference_wrapper<const LegacyLowOrderGasStep>>(&result);
    expect(step != nullptr,
           "independent gas session did not publish the expected step");
    return step->get();
}

template <class Id, class Range, class Projection>
[[nodiscard]] const typename Range::value_type &
find_by_id(const Range &range, Id id, Projection projection,
           std::string_view entity_name) {
    const auto found = std::ranges::find(range, id, projection);
    expect(found != range.end(),
           std::string{"missing independent "} + std::string{entity_name});
    return *found;
}

[[nodiscard]] const LegacyGasVolumeStepState &
find_volume(const LegacyLowOrderGasStep &gas, GasVolumeId id) {
    return find_by_id(gas.gas_volumes, id, &LegacyGasVolumeStepState::gas_volume_id,
                      "gas volume");
}

[[nodiscard]] const LegacyFlowEdgeStepState &find_edge(const LegacyLowOrderGasStep &gas,
                                                       FlowEdgeId id) {
    return find_by_id(gas.flow_edges, id, &LegacyFlowEdgeStepState::flow_edge_id,
                      "flow edge");
}

[[nodiscard]] const LegacyFlowEdgeStepState &find_edge(const LegacyLowOrderGasStep &gas,
                                                       GasVolumeId endpoint_0,
                                                       GasVolumeId endpoint_1) {
    const auto found = std::ranges::find_if(gas.flow_edges, [&](const auto &edge) {
        return edge.endpoint_0_volume_id == endpoint_0 &&
               edge.endpoint_1_volume_id == endpoint_1;
    });
    expect(found != gas.flow_edges.end(),
           "missing independent flow edge for declared endpoint pair");
    return *found;
}

[[nodiscard]] const LegacyCylinderGasStepState &
find_cylinder(const LegacyLowOrderGasStep &gas, CylinderId id) {
    return find_by_id(gas.cylinders, id, &LegacyCylinderGasStepState::cylinder_id,
                      "cylinder");
}

[[nodiscard]] const MechanismCylinderSample &
find_cylinder(const LegacyMechanismStep &mechanics, CylinderId id) {
    return find_by_id(mechanics.cylinders, id, &MechanismCylinderSample::cylinder_id,
                      "mechanism cylinder");
}

[[nodiscard]] double mass_flow_kg_s(
    double signed_amount_mol,
    RationalRateHz rate = RationalRateHz{10000U, 1U}) noexcept {
    const double step_s = static_cast<double>(rate.denominator) /
                          static_cast<double>(rate.numerator);
    return signed_amount_mol * kLegacyAirMolarMassKgPerMol / step_s;
}

[[nodiscard]] MixtureFractions mixture(const LegacyGasMixture &value) noexcept {
    return {value.fuel_fraction, value.inert_fraction, value.oxygen_fraction};
}

void test_capture_projection_canonicalizes_legacy_mixture_weights() {
    const LegacyGasMixture exact{0.05, 0.74, 0.21};
    expect(engine_sim_offline::simulation::detail::capture_mixture_for_contract(
               exact, 1.0) == mixture(exact),
           "capture projection changed an already admitted legacy mixture");

    // Exact plenum state from the reported M52TUB28 open-ended rev/lift failure.
    const LegacyGasMixture drifted{
        0.023264048640971431,
        0.74480183102433828,
        0.23193412033569039,
    };
    const double raw_sum =
        drifted.fuel_fraction + drifted.inert_fraction + drifted.oxygen_fraction;
    expect(std::abs(raw_sum - 1.0) > kMixtureFractionUnityTolerance,
           "roundoff regression fixture no longer exceeds the public tolerance");

    const auto projected =
        engine_sim_offline::simulation::detail::capture_mixture_for_contract(drifted,
                                                                             1.0);
    const double projected_sum = projected.fuel + projected.inert + projected.oxygen;
    expect(std::abs(projected_sum - 1.0) <= kMixtureFractionUnityTolerance &&
               projected.fuel == drifted.fuel_fraction / raw_sum &&
               projected.inert == drifted.inert_fraction / raw_sum &&
               projected.oxygen == drifted.oxygen_fraction / raw_sum,
           "capture projection did not remove common-scale legacy mixture drift");

    // The legacy solver admits nonnegative species weights and uses their ratios.
    // Their common scale is not public composition semantics, so observation must
    // remain valid after any duration rather than relying on a finite drift window.
    const LegacyGasMixture scaled{0.2, 0.2, 0.2};
    const auto projected_scaled =
        engine_sim_offline::simulation::detail::capture_mixture_for_contract(scaled,
                                                                             1.0);
    expect(projected_scaled == MixtureFractions{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0},
           "capture projection retained a noncanonical common species scale");

    expect(engine_sim_offline::simulation::detail::capture_mixture_for_contract(
               exact, 0.0) == MixtureFractions{},
           "empty gas cell retained undefined legacy mixture weights");

    const LegacyGasMixture negative{-0.1, 0.8, 0.3};
    expect(engine_sim_offline::simulation::detail::capture_mixture_for_contract(
               negative, 1.0) == mixture(negative),
           "capture projection masked a negative legacy mixture weight");

    const LegacyGasMixture zero_sum{};
    expect(engine_sim_offline::simulation::detail::capture_mixture_for_contract(
               zero_sum, 1.0) == mixture(zero_sum),
           "capture projection masked a positive cell with zero species weight");
}

void expect_positive_zero(double value, std::string_view field) {
    expect(value == 0.0 && !std::signbit(value),
           std::string{field} + " is not canonical positive zero");
}

[[nodiscard]] bool same_event_payload(const EngineEventPayload &left,
                                      const EngineEventPayload &right) {
    if (left.index() != right.index()) {
        return false;
    }
    return std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            const auto *other = std::get_if<T>(&right);
            if (other == nullptr) {
                return false;
            }
            if constexpr (std::is_same_v<T, SparkCrossing>) {
                return value.cylinder_id == other->cylinder_id &&
                       value.raw_saved_angle_rad == other->raw_saved_angle_rad &&
                       value.raw_current_angle_rad == other->raw_current_angle_rad &&
                       value.adjusted_current_angle_rad ==
                           other->adjusted_current_angle_rad &&
                       value.adjusted_spark_angle_rad ==
                           other->adjusted_spark_angle_rad &&
                       value.timing_advance_rad == other->timing_advance_rad;
            } else if constexpr (std::is_same_v<T, LimiterStateChanged>) {
                return value.old_active == other->old_active &&
                       value.new_active == other->new_active &&
                       value.overspeed_refreshed == other->overspeed_refreshed &&
                       value.resulting_timer_s == other->resulting_timer_s;
            } else if constexpr (std::is_same_v<T, IgnitionAccepted>) {
                return value.cylinder_id == other->cylinder_id &&
                       value.efficiency_01 == other->efficiency_01 &&
                       value.flame_speed_m_s == other->flame_speed_m_s;
            } else if constexpr (std::is_same_v<T, IgnitionRejected>) {
                return value.cylinder_id == other->cylinder_id &&
                       value.reason == other->reason;
            } else {
                return value.cylinder_id == other->cylinder_id &&
                       value.gas_substep_index == other->gas_substep_index &&
                       value.reason == other->reason;
            }
        },
        left);
}

struct Activity {
    bool nonzero_edge_flow = false;
    bool positive_edge_flow = false;
    bool negative_edge_flow = false;
    bool nonzero_directional_pressure = false;
    bool combustion_heat = false;
    bool event = false;
};

void verify_layout(const CaptureLayoutView &layout, const EngineSpec &engine) {
    expect(layout.engine_id() == engine.id, "capture layout lost the engine identity");
    expect(layout.cylinders().size() == engine.cylinders.size() &&
               layout.ports().size() == engine.ports.size() &&
               layout.gas_volumes().size() == engine.gas_volumes.size() &&
               layout.flow_edges().size() == engine.flow_edges.size() &&
               layout.routes().size() == engine.routes.size(),
           "capture layout has the wrong canonical entity counts");

    for (std::size_t index = 0; index < layout.cylinders().size(); ++index) {
        expect(layout.cylinders()[index] == engine.cylinders[index].id,
               "capture cylinder identity/order changed");
    }
    for (std::size_t index = 0; index < layout.ports().size(); ++index) {
        const auto &actual = layout.ports()[index];
        const auto &declared = engine.ports[index];
        expect(actual.id == declared.id && actual.cylinder_id == declared.cylinder_id &&
                   actual.kind == declared.kind.value,
               "capture port identity/order changed");
    }
    for (std::size_t index = 0; index < layout.gas_volumes().size(); ++index) {
        expect(layout.gas_volumes()[index] ==
                   GasVolumeIdentity{engine.gas_volumes[index].id,
                                     engine.gas_volumes[index].kind.value},
               "capture gas-volume identity/order changed");
    }
    for (std::size_t index = 0; index < layout.flow_edges().size(); ++index) {
        const auto &declared = engine.flow_edges[index];
        expect(layout.flow_edges()[index] ==
                   FlowEdgeIdentity{declared.id, declared.endpoint_0_volume_id,
                                    declared.endpoint_1_volume_id},
               "capture flow-edge identity/order changed");
    }
    for (std::size_t index = 0; index < layout.routes().size(); ++index) {
        const auto &actual = layout.routes()[index];
        const auto &declared = engine.routes[index];
        const auto anchor =
            declared.emitter_anchor_id.has_value()
                ? std::optional<std::string>{declared.emitter_anchor_id->value}
                : std::nullopt;
        expect(actual == RouteIdentity{declared.id, declared.kind.value,
                                       declared.source_volume_id,
                                       declared.default_parent_route_id, anchor},
               "capture route identity/order changed");
    }
}

void verify_frame(const CaptureBlockView &block, std::size_t frame,
                  const EngineSpec &engine_spec, const LegacyMechanismStep &mechanics,
                  const LegacyLowOrderGasStep &gas,
                  const TorqueTelemetry &expected_torque, Activity &activity) {
    const auto *engine = block.engine_sample(frame);
    expect(engine != nullptr, "frame-major engine accessor rejected a valid frame");
    expect(engine->validity == (kMechanism | kGasExchange | kTorque) &&
               engine->step_end_index == mechanics.step_end_index &&
               engine->theta_rad == mechanics.theta_unwrapped_rad &&
               engine->theta_cycle_rad == mechanics.theta_cycle_rad &&
               engine->angular_speed_rad_s == mechanics.angular_speed_rad_s &&
               engine->angular_acceleration_rad_s2 ==
                   mechanics.angular_acceleration_rad_s2 &&
               engine->engine_speed_rpm == mechanics.engine_speed_rpm &&
               engine->requested_throttle_01 == mechanics.requested_throttle_01 &&
               engine->resolved_engine_throttle_01 ==
                   mechanics.resolved_engine_throttle_01 &&
               engine->intake_plate_position_01 == mechanics.intake_plate_position_01 &&
               engine->main_flow_multiplier_01 == mechanics.main_flow_multiplier_01 &&
               engine->ignition_enabled == mechanics.operating_state.ignition_enabled &&
               engine->fuel_enabled == mechanics.operating_state.fuel_enabled &&
               engine->starter_enabled == mechanics.operating_state.starter_enabled &&
               engine->dyno_enabled == mechanics.operating_state.dyno_enabled &&
               engine->limiter_enabled == mechanics.operating_state.limiter_enabled &&
               engine->limiter_cut_active == mechanics.limiter_cut_active &&
               engine->requested_external_resisting_torque_nm ==
                   mechanics.external_resisting_torque_nm,
           "engine observable mapping changed");
    expect(engine->torque == expected_torque,
           "operating-policy torque mapping changed");

    for (std::size_t index = 0; index < engine_spec.cylinders.size(); ++index) {
        const auto id = engine_spec.cylinders[index].id;
        const auto &mechanism = find_cylinder(mechanics, id);
        const auto &gas_cylinder = find_cylinder(gas, id);
        const auto &chamber = find_volume(gas, gas_cylinder.chamber_volume_id).cell;
        const auto *actual = block.cylinder_sample(frame, index);
        expect(actual != nullptr &&
                   actual->validity == (kMechanism | kThermodynamic | kComposition |
                                        kCombustion | kTorque) &&
                   actual->chamber_volume_m3 == mechanism.chamber_volume_m3 &&
                   actual->chamber_dvolume_dtheta_m3_per_rad ==
                       mechanism.dvolume_dtheta_m3_per_rad &&
                   actual->piston_velocity_m_s == mechanism.piston_speed_abs_m_s &&
                   actual->pressure_pa_abs == legacy_gas_pressure_pa(chamber) &&
                   actual->temperature_k == legacy_gas_temperature_k(chamber) &&
                   actual->amount_mol == chamber.amount_mol &&
                   actual->composition == mixture(chamber.mixture) &&
                   actual->combustion_heat_release_j ==
                       gas_cylinder.outer_step_combustion_heat_release_j &&
                   actual->flame_radius_m == gas_cylinder.flame.radial_travel_m &&
                   actual->flame_axial_travel_m == gas_cylinder.flame.axial_travel_m &&
                   actual->flame_active == gas_cylinder.flame.active,
               "cylinder observable mapping changed");
        expect(actual->indicated_gas_torque ==
                   TorqueValueNm{gas_cylinder.indicated_gas_torque_nm,
                                 Availability::available, Completeness::complete,
                                 QuantityUnavailableReason::none,
                                 indicated_gas_torque_term_mask(), 0U},
               "per-cylinder indicated torque mapping changed");
        activity.combustion_heat =
            activity.combustion_heat ||
            gas_cylinder.outer_step_combustion_heat_release_j > 0.0;
    }

    for (std::size_t index = 0; index < engine_spec.ports.size(); ++index) {
        const auto &port = engine_spec.ports[index];
        const auto &cylinder = find_cylinder(gas, port.cylinder_id);
        const bool intake = port.kind.value == PortKind::intake;
        const auto duct_id = intake ? cylinder.intake_runner_volume_id
                                    : cylinder.exhaust_primary_volume_id;
        const auto &duct = find_volume(gas, duct_id).cell;
        const auto &edge = intake ? find_edge(gas, cylinder.intake_runner_volume_id,
                                              cylinder.chamber_volume_id)
                                  : find_edge(gas, cylinder.chamber_volume_id,
                                              cylinder.exhaust_primary_volume_id);
        const auto *actual = block.port_sample(frame, index);
        expect(actual != nullptr && actual->validity == kGasExchange &&
                   actual->pressure_pa_abs == legacy_gas_pressure_pa(duct) &&
                   actual->temperature_k == legacy_gas_temperature_k(duct) &&
                   actual->signed_mass_flow_kg_s ==
                       mass_flow_kg_s(edge.signed_amount_mol) &&
                   actual->effective_flow_area_m2 == 0.0 &&
                   !std::signbit(actual->effective_flow_area_m2) &&
                   actual->effective_molar_flow_conductance_m2_sqrt_mol_per_kg ==
                       (intake ? cylinder.valves.intake_valve_k
                               : cylinder.valves.exhaust_valve_k) &&
                   actual->valve_lift_m == (intake ? cylinder.valves.intake_lift_m
                                                   : cylinder.valves.exhaust_lift_m),
               "port observable mapping or valve-edge direction changed");
    }

    for (std::size_t index = 0; index < gas.gas_volumes.size(); ++index) {
        const auto &source = gas.gas_volumes[index];
        const auto *actual = block.gas_volume_sample(frame, index);
        expect(actual != nullptr, "frame-major gas-volume shape changed");
        if (!source.physically_resolved) {
            expect(actual->validity == 0U && actual->composition == MixtureFractions{},
                   "unresolved atmosphere gained validity or composition");
            expect_positive_zero(actual->volume_m3, "atmosphere volume");
            expect_positive_zero(actual->pressure_pa_abs, "atmosphere pressure");
            expect_positive_zero(actual->temperature_k, "atmosphere temperature");
            expect_positive_zero(actual->amount_mol, "atmosphere amount");
            expect_positive_zero(actual->thermal_energy_j, "atmosphere thermal energy");
            expect_positive_zero(actual->momentum_x_kg_m_s, "atmosphere x momentum");
            expect_positive_zero(actual->momentum_y_kg_m_s, "atmosphere y momentum");
            continue;
        }
        const auto &cell = source.cell;
        expect(actual->validity == (kThermodynamic | kComposition) &&
                   actual->volume_m3 == cell.volume_m3 &&
                   actual->pressure_pa_abs == legacy_gas_pressure_pa(cell) &&
                   actual->temperature_k == legacy_gas_temperature_k(cell) &&
                   actual->amount_mol == cell.amount_mol &&
                   actual->thermal_energy_j == cell.thermal_energy_j &&
                   actual->momentum_x_kg_m_s == cell.momentum_x_kg_m_s &&
                   actual->momentum_y_kg_m_s == cell.momentum_y_kg_m_s &&
                   actual->composition == mixture(cell.mixture),
               "finite gas-volume observable mapping changed");
    }

    for (std::size_t index = 0; index < gas.flow_edges.size(); ++index) {
        const auto *actual = block.flow_edge_sample(frame, index);
        const double expected = mass_flow_kg_s(gas.flow_edges[index].signed_amount_mol);
        expect(actual != nullptr && actual->validity == kGasExchange &&
                   actual->signed_mass_flow_kg_s == expected,
               "flow-edge mass conversion or declared direction changed");
        activity.nonzero_edge_flow = activity.nonzero_edge_flow || expected != 0.0;
        activity.positive_edge_flow = activity.positive_edge_flow || expected > 0.0;
        activity.negative_edge_flow = activity.negative_edge_flow || expected < 0.0;
    }

    for (std::size_t index = 0; index < gas.exhaust_routes.size(); ++index) {
        const auto &route = gas.exhaust_routes[index];
        const auto &collector = find_volume(gas, route.collector_volume_id).cell;
        const auto &outlet = find_edge(gas, route.collector_outlet_edge_id);
        const auto *actual = block.gas_source_route_sample(frame, index);
        expect(actual != nullptr &&
                   actual->validity == (kThermodynamic | kGasExchange) &&
                   actual->pressure_pa_abs == legacy_gas_pressure_pa(collector) &&
                   actual->temperature_k == legacy_gas_temperature_k(collector) &&
                   actual->signed_mass_flow_kg_s ==
                       -mass_flow_kg_s(outlet.signed_amount_mol) &&
                   actual->effective_area_m2 == route.collector_cross_section_area_m2,
               "exhaust source-route mapping or outward sign changed");
    }

    expect(block.reference_parity().has_value(),
           "M3 capture omitted its narrow reference-parity observables");
    const auto &parity = *block.reference_parity();
    expect(parity.filtered_engine_speed_rpm()[frame] ==
               mechanics.filtered_engine_speed_rpm,
           "reference filtered-RPM mapping changed");
    for (std::size_t index = 0; index < gas.cylinders.size(); ++index) {
        const auto &primary =
            find_volume(gas, gas.cylinders[index].exhaust_primary_volume_id).cell;
        const auto &actual = parity.cylinders()[frame * gas.cylinders.size() + index];
        const double forward =
            legacy_gas_directional_dynamic_pressure_pa(primary, 1.0, 0.0);
        const double reverse =
            legacy_gas_directional_dynamic_pressure_pa(primary, -1.0, 0.0);
        expect(actual.exhaust_primary_static_pressure_pa_abs ==
                       legacy_gas_pressure_pa(primary) &&
                   actual.dynamic_pressure_forward_pa == forward &&
                   actual.dynamic_pressure_reverse_pa == reverse,
               "reference primary static/directional pressure mapping changed");
        activity.nonzero_directional_pressure =
            activity.nonzero_directional_pressure || forward > 0.0 || reverse > 0.0;
    }
}

void verify_events(const CaptureBlockView &block,
                   const std::vector<std::vector<ScheduledMechanismEvent>> &expected,
                   Activity &activity) {
    const auto offsets = block.event_journal().offsets();
    const auto events = block.event_journal().events();
    expect(offsets.size() == block.frame_count() + 1U && offsets.front() == 0U &&
               offsets.back() == events.size(),
           "event journal is not a complete frame CSR");
    std::size_t expected_total = 0;
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
        expect(offsets[frame] == expected_total,
               "event CSR offset does not begin at the frame's first event");
        for (std::size_t ordinal = 0; ordinal < expected[frame].size(); ++ordinal) {
            const auto &source = expected[frame][ordinal];
            const auto &actual = events[expected_total + ordinal];
            expect(actual.frame_offset == frame &&
                       actual.ordinal_within_step == source.ordinal_within_step &&
                       same_event_payload(actual.payload, source.payload),
                   "compressed capture event changed frame, ordinal, or payload");
        }
        expected_total += expected[frame].size();
        expect(offsets[frame + 1U] == expected_total,
               "event CSR offset does not end after the frame's events");
    }
    expect(expected_total == events.size(),
           "event CSR retained extra or omitted source events");
    activity.event = activity.event || !events.empty();
}

[[nodiscard]] LowOrderCapturePlan
require_capture_plan(LowOrderCapturePlanCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("authored capture plan failed admission", *report);
    }
    return std::get<LowOrderCapturePlan>(std::move(result));
}

[[nodiscard]] LowOrderOperatingPointV1Runtime
require_operating_runtime(LowOrderOperatingPointV1CompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("authored operating policy failed admission", *report);
    }
    return std::get<LowOrderOperatingPointV1Runtime>(std::move(result));
}

[[nodiscard]] const TorqueTelemetry &
require_operating_torque(LowOrderOperatingPointV1AdvanceResult &result) {
    if (const auto *failure = std::get_if<FailureContext>(&result)) {
        throw std::runtime_error{"independent operating policy faulted: " +
                                 failure->detail_code + "; " + failure->state_summary};
    }
    return std::get<LowOrderOperatingPointV1Step>(result).capture_torque;
}

void test_authored_capture_mapping_and_completion(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    const auto request_identity = nonzero_request_identity();
    const auto random_plan =
        fixture_random_plan(request, request.engine, request.scenario);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario, random_plan, request_identity,
        finite_extent(request.scenario)));
    auto schedule_result = compile_kinematic_scenario_schedule(request.scenario);
    if (const auto *report = std::get_if<ValidationReport>(&schedule_result)) {
        fail_report("authored held-speed schedule failed admission", *report);
    }
    const auto &schedule = std::get<KinematicScenarioSchedule>(schedule_result);
    const auto &core = engine_sim_offline::test::low_order_core(request.engine);
    auto mechanism_plan_result =
        compile_mechanism_kinematics_plan(request.engine, core);
    if (const auto *report = std::get_if<ValidationReport>(&mechanism_plan_result)) {
        fail_report("authored mechanism plan failed admission", *report);
    }
    auto mechanism_plan =
        std::get<SharedMechanismKinematicsPlan>(std::move(mechanism_plan_result));
    auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
        request.engine, core, request.scenario, mechanism_plan, schedule));
    auto gas = require_gas(CoreRuntimeFactory::compile_gas(
        request.engine, core, request.scenario, random_plan,
        schedule.control_schedule(), std::move(mechanism_plan)));
    const auto capture_plan = require_capture_plan(compile_low_order_capture_plan(
        request.engine, request.scenario, finite_extent(request.scenario)));
    auto operating =
        require_operating_runtime(compile_low_order_operating_point_v1_runtime(
            request.engine, request.scenario, capture_plan, request_identity));

    Activity activity;
    std::uint64_t next_sample_index = 0U;
    std::uint64_t published_sample_count = 0U;
    std::uint64_t block_ordinal = 0U;
    while (next_sample_index < kOperatingStepCount) {
        std::size_t callback_count = 0U;
        std::uint32_t callback_frame_count = 0U;
        const std::uint64_t expected_first_sample = next_sample_index;
        std::exception_ptr callback_error;
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            try {
                ++callback_count;
                callback_frame_count = block.frame_count();
                expect(block.clock() == CaptureClock{{10000, 1},
                                                     expected_first_sample,
                                                     expected_first_sample + 1U,
                                                     SamplePhase::post_step} &&
                           block.declared_block_capacity_frames() == 200U &&
                           block.declared_event_journal_capacity_records() == 3800U,
                       "capture block clock or declared bounds changed");
                expect(block.frame_count() ==
                           std::min<std::uint64_t>(200U, kOperatingStepCount -
                                                             expected_first_sample),
                       "capture block did not use the bounded declared partition");
                expect(block.engine().size() == block.frame_count() &&
                           block.cylinders().size() ==
                               block.frame_count() * request.engine.cylinders.size() &&
                           block.ports().size() ==
                               block.frame_count() * request.engine.ports.size() &&
                           block.gas_volumes().size() ==
                               block.frame_count() *
                                   request.engine.gas_volumes.size() &&
                           block.flow_edges().size() ==
                               block.frame_count() * request.engine.flow_edges.size() &&
                           block.source_routes().size() ==
                               block.frame_count() * request.engine.routes.size(),
                       "capture arrays are not declared frame-major shapes");
                verify_layout(block.layout(), request.engine);
                const auto report = validate(block, request.engine, request.scenario);
                if (!report.ok()) {
                    fail_report("published capture failed request-aware validation",
                                report);
                }

                std::vector<std::vector<ScheduledMechanismEvent>> expected_events;
                expected_events.reserve(block.frame_count());
                for (std::size_t frame = 0; frame < block.frame_count(); ++frame) {
                    auto mechanics_result = mechanics.advance();
                    const auto &mechanics_step =
                        require_mechanics_step(mechanics_result, next_sample_index);
                    auto gas_result = gas.advance(mechanics_step);
                    const auto &gas_step =
                        require_gas_step(gas_result, next_sample_index);
                    auto operating_result = operating.advance(mechanics_step, gas_step);
                    const auto &expected_torque =
                        require_operating_torque(operating_result);
                    expect(mechanics_step.sample_index == next_sample_index &&
                               gas_step.sample_index == next_sample_index,
                           "independent source sessions left the capture clock");
                    verify_frame(block, frame, request.engine, mechanics_step, gas_step,
                                 expected_torque, activity);
                    expected_events.push_back(gas_step.events);
                    ++next_sample_index;
                }
                verify_events(block, expected_events, activity);
                expect(block.engine_sample(block.frame_count()) == nullptr &&
                           block.cylinder_sample(
                               0U, block.layout().cylinders().size()) == nullptr &&
                           block.flow_edge_sample(block.frame_count(), 0U) == nullptr,
                       "capture frame-major accessors accepted an out-of-range index");
                return true;
            } catch (...) {
                callback_error = std::current_exception();
                return false;
            }
        });
        if (callback_error != nullptr) {
            std::rethrow_exception(callback_error);
        }

        expect(callback_count == 1U,
               "publish_next_block did not call its consumer exactly once");
        const auto *published = std::get_if<LowOrderCaptureBlockPublished>(&result);
        expect(published != nullptr && published->block_ordinal == block_ordinal &&
                   published->first_sample_index == expected_first_sample &&
                   published->frame_count == callback_frame_count &&
                   published->published_sample_count == next_sample_index,
               "published-block progress record changed");
        published_sample_count += callback_frame_count;
        expect(published_sample_count == next_sample_index &&
                   capture.published_sample_count() == next_sample_index &&
                   capture.published_block_count() == block_ordinal + 1U &&
                   !capture.completed() && !capture.faulted(),
               "capture session progress getters changed");
        ++block_ordinal;
    }

    expect(block_ordinal == 15U && published_sample_count == kOperatingStepCount,
           "held capture did not end with fifteen complete bounded blocks");
    std::size_t completion_callback_count = 0U;
    auto completion = capture.publish_next_block([&](const CaptureBlockView &) {
        ++completion_callback_count;
        return true;
    });
    const auto *completed = std::get_if<LowOrderCaptureCompleted>(&completion);
    expect(completed != nullptr && completed->sample_count == kOperatingStepCount &&
               completed->block_count == 15U &&
               completed->held_speed_operating_point.has_value() &&
               !completed->inertial_dyno.has_value() &&
               operating.operating_point_result().has_value() &&
               *completed->held_speed_operating_point ==
                   *operating.operating_point_result() &&
               completion_callback_count == 0U && capture.completed() &&
               !capture.faulted(),
           "held capture completion is not terminal, callback-free, or bound to "
           "the independently evaluated operating result");

    auto repeated = capture.publish_next_block([&](const CaptureBlockView &) {
        ++completion_callback_count;
        return true;
    });
    const auto *repeated_completion = std::get_if<LowOrderCaptureCompleted>(&repeated);
    expect(repeated_completion != nullptr && *repeated_completion == *completed &&
               completion_callback_count == 0U,
           "capture completion result is not stable");

    expect(activity.nonzero_edge_flow && activity.positive_edge_flow &&
               activity.negative_edge_flow && activity.nonzero_directional_pressure &&
               activity.combustion_heat && activity.event,
           "held capture did not exercise bidirectional flow, pressure, combustion, "
           "and events");
}

void test_twenty_khz_mechanics_gas_and_capture_share_one_clock(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr RationalRateHz kRate{20000U, 1U};
    constexpr std::uint32_t kFrameCount = 400U;
    constexpr double kDurationS = 0.02;
    constexpr double kRpm = 2400.0;

    auto request = engine_sim_offline::test::make_prescribed_fixture(
        canonical, std::vector<double>(kFrameCount, kRpm));
    auto &scenario = request.scenario;
    scenario.scenario_id = "authored-20khz-lower-layer-rate-proof";
    scenario.rates.physics = kRate;
    scenario.rates.capture = kRate;
    scenario.total_duration_s.value = kDurationS;
    scenario.audible_start_s.value = 0.0;
    scenario.audible_duration_s.value = kDurationS;
    scenario.preparation = FixedSettling{
        {0.0, "20khz-rate-proof.warm-up"},
        {0.0, "20khz-rate-proof.settling"},
    };
    scenario.operating_state.value = {
        {
            "20khz-rate-proof-running",
            0.0,
            {true, true, false, true, false},
        },
    };
    scenario.quality.value.capture_block_capacity_frames = kFrameCount;
    scenario.quality.value.event_journal_capacity_records = kFrameCount * 19U;
    auto &sweep = std::get<PrescribedKinematicSweep>(scenario.mode);
    auto &rpm = std::get<FixedRateRpmTrajectory>(sweep.trajectory.rpm);
    rpm.rate = kRate;
    rpm.samples_f64le_sha256 = canonical_binary64_le_sha256(rpm.post_step_rpm);
    sweep.throttle_01.points = {{0.0, 0.85}};
    const double initial_theta_rad = sweep.trajectory.initial_theta_rad.value;

    const auto clock_report = validate_clock_grid(scenario);
    if (!clock_report.ok()) {
        fail_report("20 kHz lower-layer fixture is not on its declared clocks",
                    clock_report);
    }
    const auto engine_report = validate_for_engine(scenario, request.engine);
    if (!engine_report.ok()) {
        fail_report("20 kHz lower-layer fixture is not admitted for its engine",
                    engine_report);
    }

    const auto random_plan =
        fixture_random_plan(request, request.engine, scenario);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, scenario, random_plan, nonzero_request_identity(),
        finite_extent(scenario)));

    auto schedule_result = compile_kinematic_scenario_schedule(scenario);
    if (const auto *report = std::get_if<ValidationReport>(&schedule_result)) {
        fail_report("20 kHz kinematic schedule failed admission", *report);
    }
    const auto &schedule = std::get<KinematicScenarioSchedule>(schedule_result);
    const auto &core = engine_sim_offline::test::low_order_core(request.engine);
    auto mechanism_plan_result =
        compile_mechanism_kinematics_plan(request.engine, core);
    if (const auto *report =
            std::get_if<ValidationReport>(&mechanism_plan_result)) {
        fail_report("20 kHz mechanism plan failed admission", *report);
    }
    auto mechanism_plan = std::get<SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));
    auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
        request.engine, core, scenario, mechanism_plan, schedule));
    auto gas = require_gas(CoreRuntimeFactory::compile_gas(
        request.engine, core, scenario, random_plan, schedule.control_schedule(),
        std::move(mechanism_plan)));

    bool saw_nonzero_flow = false;
    std::size_t callback_count = 0U;
    auto published = capture.publish_next_block([&](const CaptureBlockView &block) {
        ++callback_count;
        expect(block.clock() ==
                       CaptureClock{kRate, 0U, 1U, SamplePhase::post_step} &&
                   block.frame_count() == kFrameCount &&
                   block.declared_block_capacity_frames() == kFrameCount &&
                   block.declared_event_journal_capacity_records() ==
                       kFrameCount * 19U,
               "20 kHz capture did not use one exact 20 ms clock block");
        const auto report = validate(block, request.engine, scenario);
        if (!report.ok()) {
            fail_report("20 kHz capture block failed validation", report);
        }

        for (std::uint32_t frame = 0U; frame < kFrameCount; ++frame) {
            auto mechanics_result = mechanics.advance();
            const auto &mechanics_step =
                require_mechanics_step(mechanics_result, frame);
            auto gas_result = gas.advance(mechanics_step);
            const auto &gas_step = require_gas_step(gas_result, frame);
            expect(mechanics_step.rate == kRate && gas_step.rate == kRate &&
                       mechanics_step.sample_index == frame &&
                       gas_step.sample_index == frame,
                   "20 kHz mechanics and gas left their shared declared clock");
            if (frame == 0U) {
                const double expected_theta_rad =
                    initial_theta_rad + mechanics_step.angular_speed_rad_s / 20000.0;
                const double stale_theta_rad =
                    initial_theta_rad + mechanics_step.angular_speed_rad_s / 10000.0;
                expect(mechanics_step.theta_unwrapped_rad == expected_theta_rad &&
                           mechanics_step.theta_unwrapped_rad != stale_theta_rad,
                       "20 kHz mechanics did not integrate its first crank step over "
                       "the declared 50 us duration");
            }

            for (std::size_t edge_index = 0U;
                 edge_index < gas_step.flow_edges.size(); ++edge_index) {
                const auto signed_amount_mol =
                    gas_step.flow_edges[edge_index].signed_amount_mol;
                const auto *actual = block.flow_edge_sample(frame, edge_index);
                const double expected = mass_flow_kg_s(signed_amount_mol, kRate);
                expect(actual != nullptr &&
                           actual->signed_mass_flow_kg_s == expected,
                       "20 kHz capture mass flow did not use its declared step time");
                if (signed_amount_mol != 0.0) {
                    saw_nonzero_flow = true;
                    expect(actual->signed_mass_flow_kg_s !=
                               mass_flow_kg_s(signed_amount_mol),
                           "20 kHz nonzero flow retained the stale 10 kHz conversion");
                }
            }
        }
        return true;
    });
    const auto *published_block =
        std::get_if<LowOrderCaptureBlockPublished>(&published);
    expect(published_block != nullptr && callback_count == 1U &&
               published_block->frame_count == kFrameCount &&
               published_block->published_sample_count == kFrameCount &&
               saw_nonzero_flow,
           "20 kHz lower-layer session did not publish one active complete block");

    auto completion =
        capture.publish_next_block([](const CaptureBlockView &) { return true; });
    const auto *completed = std::get_if<LowOrderCaptureCompleted>(&completion);
    expect(completed != nullptr && completed->sample_count == kFrameCount &&
               completed->block_count == 1U && capture.completed() &&
               !capture.faulted(),
           "20 kHz lower-layer session did not complete at exactly 20 ms");
}

void test_operating_capture_publishes_request_bound_completion_evidence(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    const auto request_identity = nonzero_request_identity();
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        request_identity, finite_extent(request.scenario)));

    std::uint64_t callback_count = 0U;
    std::optional<LowOrderCaptureCompleted> completion;
    while (!completion.has_value()) {
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            ++callback_count;
            const auto report = validate(block, request.engine, request.scenario);
            if (!report.ok()) {
                fail_report("operating capture block failed request validation",
                            report);
            }
            return true;
        });
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "operating capture faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            completion = *completed;
        }
    }

    const auto expected_frames = resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);
    expect(expected_frames.has_value() &&
               completion->sample_count == *expected_frames &&
               completion->block_count == 15U && callback_count == 15U &&
               completion->held_speed_operating_point.has_value() &&
               capture.completed() && !capture.faulted(),
           "operating capture did not publish one complete typed held result");
    const auto report = validate(*completion->held_speed_operating_point,
                                 request.scenario, request.engine, request_identity);
    if (!report.ok()) {
        fail_report("operating completion evidence failed request validation", report);
    }
}

void test_operating_capture_rejects_zero_request_identity(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    const auto result = compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario), Sha256Digest{},
        finite_extent(request.scenario));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(report->issues,
                                   [](const ContractIssue &issue) {
                                       return issue.path ==
                                              "simulation_request_identity_v7_sha256";
                                   }),
           "operating capture admitted a zero simulation-request identity");
}

void test_free_engine_capture_holds_preparation_and_executes_authored_controls(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto high_throttle_request = make_free_engine_capture_request(
        canonical, 0.85, kFreeEngineInitialResistingTorqueNm,
        kFreeEngineBoundaryResistingTorqueNm, kFreeEngineControlBoundaryS);
    const auto low_throttle_request = make_free_engine_capture_request(
        canonical, 0.15, kFreeEngineInitialResistingTorqueNm,
        kFreeEngineBoundaryResistingTorqueNm, kFreeEngineControlBoundaryS);
    const auto high = run_free_engine_capture(high_throttle_request);
    const auto low = run_free_engine_capture(low_throttle_request);

    const auto release_frame = *resolve_frame_index(
        kOperatingCutoffTimeS, high_throttle_request.scenario.rates.capture);
    const auto control_boundary_frame = *resolve_frame_index(
        kFreeEngineControlBoundaryS, high_throttle_request.scenario.rates.capture);
    const auto expected_frame_count = *resolve_frame_index(
        kFreeEngineTotalDurationS, high_throttle_request.scenario.rates.capture);
    const auto expected_block_count =
        expected_frame_count /
        high_throttle_request.scenario.quality.value.capture_block_capacity_frames;
    expect(high.frames.size() == expected_frame_count &&
               low.frames.size() == expected_frame_count &&
               high.completion.sample_count == expected_frame_count &&
               low.completion.sample_count == expected_frame_count &&
               high.completion.block_count == expected_block_count &&
               low.completion.block_count == expected_block_count &&
               !high.completion.held_speed_operating_point.has_value() &&
               !high.completion.inertial_dyno.has_value() &&
               !low.completion.held_speed_operating_point.has_value() &&
               !low.completion.inertial_dyno.has_value(),
           "free-engine captures did not complete their exact fixed horizon");

    const auto held_rpm_bits = std::bit_cast<std::uint64_t>(kOperatingHeldRpm);
    for (std::uint64_t index = 0; index < release_frame; ++index) {
        expect(std::bit_cast<std::uint64_t>(high.frames[index].engine_speed_rpm) ==
                       held_rpm_bits &&
                   std::bit_cast<std::uint64_t>(low.frames[index].engine_speed_rpm) ==
                       held_rpm_bits,
               "free-engine preparation was not held at the exact initial RPM");
    }

    const auto &released = high.frames[release_frame];
    const auto &released_net = released.torque.instantaneous_net_shaft;
    const auto &released_friction = released.torque.friction_pump_and_accessory;
    const auto &released_actuator = released.torque.actuator;
    const auto &released_reaction = released.torque.dyno_reaction;
    const double expected_crank_friction_nm =
        -engine_sim_offline::test::operating_profile(high_throttle_request.engine)
             .core.mechanism.cranks.front()
             .running_friction_torque_magnitude_nm.value;
    const auto expected_source_friction_terms =
        torque_term_mask(TorqueTerm::crank_friction) |
        torque_term_mask(TorqueTerm::piston_ring_friction);
    expect(!released.limiter_enabled &&
               released.requested_external_resisting_torque_nm ==
                   kFreeEngineInitialResistingTorqueNm &&
               released_friction.availability == Availability::available &&
               released_friction.completeness == Completeness::incomplete &&
               released_friction.included_terms == expected_source_friction_terms &&
               released_friction.value_nm < expected_crank_friction_nm &&
               released_net.availability == Availability::available &&
               released_net.value_nm ==
                   released.torque.instantaneous_indicated_gas.value_nm +
                       released_friction.value_nm &&
               released_actuator.availability == Availability::available &&
               released_reaction.availability == Availability::available &&
               released_actuator.value_nm == -kFreeEngineInitialResistingTorqueNm &&
               released_reaction.value_nm == kFreeEngineInitialResistingTorqueNm,
           "first released free-engine frame did not expose its source crank plus "
           "piston-wall friction and applied load");
    const double expected_release_alpha = expected_configuration_dependent_alpha(
        high_throttle_request.engine, high.frames[release_frame - 1U],
        kFreeEngineAttachedInertiaKgM2, released_net.value_nm,
        kFreeEngineInitialResistingTorqueNm);
    expect_near(released.angular_acceleration_rad_s2, expected_release_alpha,
                std::max(1.0, std::abs(expected_release_alpha)) * 1e-8,
                "first released free-engine motion disagreed with torque and inertia");

    const auto &before_boundary = low.frames[control_boundary_frame - 1U];
    const auto &at_boundary = low.frames[control_boundary_frame];
    expect(before_boundary.requested_throttle_01 == 0.85 &&
               before_boundary.torque.actuator.value_nm ==
                   -kFreeEngineInitialResistingTorqueNm &&
               before_boundary.torque.dyno_reaction.value_nm ==
                   kFreeEngineInitialResistingTorqueNm &&
               before_boundary.requested_external_resisting_torque_nm ==
                   kFreeEngineInitialResistingTorqueNm &&
               at_boundary.requested_throttle_01 == 0.15 &&
               at_boundary.requested_external_resisting_torque_nm ==
                   kFreeEngineBoundaryResistingTorqueNm &&
               at_boundary.torque.actuator.value_nm ==
                   -kFreeEngineBoundaryResistingTorqueNm &&
               at_boundary.torque.dyno_reaction.value_nm ==
                   kFreeEngineBoundaryResistingTorqueNm &&
               high.frames[control_boundary_frame].requested_throttle_01 == 0.85,
           "free-engine authored controls did not change on their exact RCH frame");

    for (std::uint64_t index = 0; index <= control_boundary_frame; ++index) {
        expect(std::bit_cast<std::uint64_t>(high.frames[index].engine_speed_rpm) ==
                   std::bit_cast<std::uint64_t>(low.frames[index].engine_speed_rpm),
               "current-frame throttle rewrote prior committed crank motion");
    }
    const auto first_rpm_divergence = std::find_if(
        high.frames.begin() + static_cast<std::ptrdiff_t>(control_boundary_frame + 1U),
        high.frames.end(), [&](const EngineCaptureSample &high_frame) {
            const auto index =
                static_cast<std::size_t>(&high_frame - high.frames.data());
            return std::bit_cast<std::uint64_t>(high_frame.engine_speed_rpm) !=
                   std::bit_cast<std::uint64_t>(low.frames[index].engine_speed_rpm);
        });
    expect(first_rpm_divergence != high.frames.end() &&
               high.frames.back().engine_speed_rpm > low.frames.back().engine_speed_rpm,
           "released free-engine motion did not respond to authored throttle");
}

void test_free_engine_capture_runs_dynamic_pre_audible_acquisition(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr double kAudibleStartS = 0.30;
    const auto request = [&] {
        auto value = make_free_engine_capture_request(
            canonical, 0.15, kFreeEngineInitialResistingTorqueNm,
            kFreeEngineBoundaryResistingTorqueNm, kFreeEngineControlBoundaryS);
        value.scenario.audible_start_s.value = kAudibleStartS;
        value.scenario.audible_duration_s.value =
            value.scenario.total_duration_s.value - kAudibleStartS;
        return value;
    }();
    const auto trace = run_free_engine_capture(request, true);
    auto earlier_audition = request;
    earlier_audition.scenario.audible_start_s.value = kAudibleStartS - kOuterStepS;
    earlier_audition.scenario.audible_duration_s.value =
        earlier_audition.scenario.total_duration_s.value -
        earlier_audition.scenario.audible_start_s.value;
    const auto earlier_audition_trace = run_free_engine_capture(earlier_audition, true);
    const auto release_frame =
        *resolve_frame_index(kOperatingCutoffTimeS, request.scenario.rates.capture);
    const auto control_boundary_frame = *resolve_frame_index(
        kFreeEngineControlBoundaryS, request.scenario.rates.capture);
    const auto audible_start_frame =
        *resolve_frame_index(kAudibleStartS, request.scenario.rates.capture);

    expect(release_frame < control_boundary_frame &&
               control_boundary_frame < audible_start_frame &&
               trace.frames[release_frame - 1U].engine_speed_rpm == kOperatingHeldRpm &&
               trace.frames[release_frame].angular_acceleration_rad_s2 != 0.0 &&
               trace.frames[control_boundary_frame].requested_throttle_01 == 0.15 &&
               trace.frames[control_boundary_frame]
                       .requested_external_resisting_torque_nm ==
                   kFreeEngineBoundaryResistingTorqueNm &&
               trace.frames[audible_start_frame - 1U].engine_speed_rpm !=
                   kOperatingHeldRpm &&
               trace.encoded_capture == earlier_audition_trace.encoded_capture,
           "FreeEngine did not release, execute authored controls, and evolve before "
           "the independent audible-start boundary without resetting or changing "
           "the physical trace");

    auto late_release = request;
    std::get<FixedHorizonCycleSampling>(late_release.scenario.preparation)
        .fixed_preparation_horizon_s.value = kAudibleStartS + kOuterStepS;
    const auto rejected = compile_low_order_capture_session(
        late_release.engine, late_release.scenario,
        fixture_random_plan(late_release, late_release.engine, late_release.scenario),
        nonzero_request_identity(), finite_extent(late_release.scenario));
    const auto *report = std::get_if<ValidationReport>(&rejected);
    expect(report != nullptr && !report->ok() &&
               std::ranges::any_of(
                   report->issues,
                   [](const ContractIssue &issue) {
                       return issue.path ==
                              "scenario.preparation.fixed_preparation_horizon_s.value";
                   }),
           "FreeEngine admitted a held-preparation release after audible start");
}

void test_free_engine_capture_applies_live_limiter_and_resistance_after_release(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr double kLiveResistingTorqueNm = 7.5;
    constexpr double kShortHorizonS = 0.24;
    constexpr double kAuthoredLoadBoundaryS = 0.23;
    const auto request = make_free_engine_capture_request(
        canonical, 0.85, kFreeEngineInitialResistingTorqueNm,
        kFreeEngineBoundaryResistingTorqueNm, kAuthoredLoadBoundaryS, kShortHorizonS);
    const auto release_frame =
        *resolve_frame_index(kOperatingCutoffTimeS, request.scenario.rates.physics);
    const auto expected_frame_count =
        *resolve_frame_index(kShortHorizonS, request.scenario.rates.physics);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));

    FreeEngineLiveControlContext context{
        0,
        release_frame,
        kLiveResistingTorqueNm,
    };
    const engine_sim_offline::simulation::detail::LowOrderLiveControlProvider provider{
        &context,
        request.scenario.rates.physics,
        &drain_free_engine_live_controls,
    };
    std::vector<EngineCaptureSample> frames;
    std::optional<LowOrderCaptureCompleted> completion;
    while (!completion.has_value()) {
        auto result = capture.publish_next_block(
            [&](const CaptureBlockView &block) {
                const auto report = validate(block, request.engine, request.scenario);
                if (!report.ok()) {
                    fail_report("live free-engine capture failed validation", report);
                }
                frames.insert(frames.end(), block.engine().begin(),
                              block.engine().end());
                return true;
            },
            provider);
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "live free-engine capture faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            completion = *completed;
        }
    }

    expect(frames.size() == expected_frame_count &&
               context.next_step == expected_frame_count &&
               completion->sample_count == expected_frame_count,
           "live free-engine capture did not consume its exact control horizon");
    const auto &prepared = frames[release_frame - 1U];
    const auto &released = frames[release_frame];
    const auto &final = frames.back();
    expect(!prepared.limiter_enabled &&
               prepared.requested_external_resisting_torque_nm ==
                   kFreeEngineInitialResistingTorqueNm &&
               released.limiter_enabled && !released.limiter_cut_active &&
               released.requested_external_resisting_torque_nm ==
                   kLiveResistingTorqueNm &&
               released.torque.actuator.value_nm == -kLiveResistingTorqueNm &&
               released.torque.dyno_reaction.value_nm == kLiveResistingTorqueNm &&
               final.limiter_enabled &&
               final.requested_external_resisting_torque_nm == kLiveResistingTorqueNm &&
               final.torque.dyno_reaction.value_nm == kLiveResistingTorqueNm,
           "released FreeEngine did not use and publish live limiter/load state");
    const double expected_release_alpha = expected_configuration_dependent_alpha(
        request.engine, prepared, kFreeEngineAttachedInertiaKgM2,
        released.torque.instantaneous_net_shaft.value_nm, kLiveResistingTorqueNm);
    expect_near(released.angular_acceleration_rad_s2, expected_release_alpha,
                std::max(1.0, std::abs(expected_release_alpha)) * 1e-8,
                "live resistance did not drive released crank acceleration");
}

void test_open_free_engine_capture_runs_past_authored_horizon_in_full_blocks(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr double kReleaseThrottle = 0.60;
    constexpr double kReleaseResistanceNm = 10.0;
    constexpr double kIgnoredBoundaryS = 0.24;
    const auto request = [&] {
        auto value = make_free_engine_capture_request(
            canonical, kReleaseThrottle, kFreeEngineInitialResistingTorqueNm,
            kReleaseResistanceNm, kOperatingCutoffTimeS);
        auto &free_engine = std::get<FreeEngine>(value.scenario.mode);
        free_engine.throttle_01.points.push_back({kIgnoredBoundaryS, 0.05});
        free_engine.external_resisting_torque_nm.points.push_back(
            {kIgnoredBoundaryS, 200.0});
        return value;
    }();
    const auto release_frame =
        *resolve_frame_index(kOperatingCutoffTimeS, request.scenario.rates.physics);
    const auto ignored_boundary_frame =
        *resolve_frame_index(kIgnoredBoundaryS, request.scenario.rates.physics);
    const auto authored_horizon = *resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.physics);
    const auto block_capacity =
        request.scenario.quality.value.capture_block_capacity_frames;
    const std::uint64_t block_count = authored_horizon / block_capacity + 1U;

    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), LowOrderExecutionExtent::open_ended()));

    std::optional<EngineCaptureSample> release;
    std::optional<EngineCaptureSample> ignored_boundary;
    std::optional<EngineCaptureSample> last;
    for (std::uint64_t block_ordinal = 0; block_ordinal < block_count;
         ++block_ordinal) {
        const auto result =
            capture.publish_next_block([&](const CaptureBlockView &block) {
                expect(block.frame_count() == block_capacity &&
                           block.clock().first_sample_index ==
                               block_ordinal * block_capacity,
                       "open capture did not publish one contiguous full block");
                for (std::uint32_t offset = 0; offset < block.frame_count(); ++offset) {
                    const auto sample_index = block.clock().first_sample_index + offset;
                    if (sample_index == release_frame) {
                        release = block.engine()[offset];
                    }
                    if (sample_index == ignored_boundary_frame) {
                        ignored_boundary = block.engine()[offset];
                    }
                    last = block.engine()[offset];
                }
                return true;
            });
        const auto *published = std::get_if<LowOrderCaptureBlockPublished>(&result);
        expect(published != nullptr && published->block_ordinal == block_ordinal &&
                   published->frame_count == block_capacity,
               "open capture completed or faulted at its authored horizon");
    }

    const std::uint64_t expected_frames = block_count * block_capacity;
    expect(expected_frames > authored_horizon &&
               capture.published_sample_count() == expected_frames &&
               capture.published_block_count() == block_count && !capture.completed() &&
               !capture.faulted(),
           "open capture did not remain active beyond the finite recipe");
    expect(release.has_value() && ignored_boundary.has_value() && last.has_value() &&
               release->requested_throttle_01 == kReleaseThrottle &&
               release->requested_external_resisting_torque_nm ==
                   kReleaseResistanceNm &&
               release->fuel_enabled &&
               ignored_boundary->requested_throttle_01 == kReleaseThrottle &&
               ignored_boundary->requested_external_resisting_torque_nm ==
                   kReleaseResistanceNm &&
               ignored_boundary->fuel_enabled &&
               last->requested_throttle_01 == kReleaseThrottle &&
               last->requested_external_resisting_torque_nm == kReleaseResistanceNm &&
               last->fuel_enabled,
           "open capture did not hold the exact release snapshot after release");
}

void test_free_engine_capture_advances_canonical_stopped_state(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_stopped_free_engine_capture_request(canonical);
    const auto trace = run_free_engine_capture(request);
    const auto &free_engine = std::get<FreeEngine>(request.scenario.mode);
    const auto expected_frame_count = *resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);

    expect(trace.frames.size() == expected_frame_count &&
               trace.completion.sample_count == expected_frame_count &&
               !trace.completion.held_speed_operating_point.has_value() &&
               !trace.completion.inertial_dyno.has_value(),
           "stopped FreeEngine did not complete its exact finite horizon");
    for (std::size_t index = 0; index < trace.frames.size(); ++index) {
        const auto &frame = trace.frames[index];
        expect(frame.step_end_index == index + 1U &&
                   frame.theta_rad == free_engine.initial_theta_rad.value &&
                   frame.theta_cycle_rad == free_engine.initial_theta_rad.value &&
                   std::bit_cast<std::uint64_t>(frame.angular_speed_rad_s) ==
                       std::bit_cast<std::uint64_t>(0.0) &&
                   std::bit_cast<std::uint64_t>(frame.angular_acceleration_rad_s2) ==
                       std::bit_cast<std::uint64_t>(0.0) &&
                   std::bit_cast<std::uint64_t>(frame.engine_speed_rpm) ==
                       std::bit_cast<std::uint64_t>(0.0) &&
                   !frame.ignition_enabled && !frame.fuel_enabled &&
                   !frame.starter_enabled && !frame.dyno_enabled &&
                   frame.torque.starter.value_nm == 0.0,
               "stopped FreeEngine changed crank state, enabled an actuator, or "
               "published starter torque");
    }
}

void test_free_engine_capture_commits_stable_stop_without_reverse(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    constexpr double kStallResistingTorqueNm = 1.0e9;
    const auto request = make_free_engine_capture_request(
        canonical, 0.85, kFreeEngineInitialResistingTorqueNm, kStallResistingTorqueNm,
        kOperatingCutoffTimeS);
    const auto release_frame =
        *resolve_frame_index(kOperatingCutoffTimeS, request.scenario.rates.capture);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));

    std::size_t callback_count = 0U;
    std::uint64_t observed_frame_count = 0U;
    std::optional<LowOrderCaptureCompleted> completion;
    std::optional<std::uint64_t> first_stopped_frame;
    std::optional<double> stopped_theta_rad;
    while (!completion.has_value()) {
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            ++callback_count;
            const auto report = validate(block, request.engine, request.scenario);
            if (!report.ok()) {
                fail_report("free-engine stall preparation failed validation", report);
            }
            expect(block.clock().first_sample_index == observed_frame_count,
                   "stall probe capture blocks lost contiguous frame order");
            for (std::uint32_t offset = 0; offset < block.frame_count(); ++offset) {
                const auto frame_index = block.clock().first_sample_index + offset;
                const auto &frame = block.engine()[offset];
                expect(std::isfinite(frame.engine_speed_rpm) &&
                           frame.engine_speed_rpm >= 0.0 &&
                           !(frame.engine_speed_rpm == 0.0 &&
                             std::signbit(frame.engine_speed_rpm)),
                       "stall probe published reverse or noncanonical crank motion");
                if (frame_index < release_frame) {
                    expect(frame.engine_speed_rpm == kOperatingHeldRpm,
                           "stall probe changed held preparation speed");
                } else if (!first_stopped_frame.has_value()) {
                    expect(frame.engine_speed_rpm == 0.0,
                           "extreme release load did not stop on its first dynamic "
                           "frame");
                    first_stopped_frame = frame_index;
                    stopped_theta_rad = frame.theta_rad;
                } else {
                    expect(frame.engine_speed_rpm == 0.0 &&
                               frame.theta_rad == *stopped_theta_rad,
                           "stopped FreeEngine did not hold canonical zero speed and "
                           "the exact committed stall angle");
                }
            }
            observed_frame_count += block.frame_count();
            return true;
        });
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "stopped FreeEngine faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            completion = *completed;
        }
    }

    const auto expected_frame_count = *resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);
    const auto expected_callback_count =
        expected_frame_count /
        request.scenario.quality.value.capture_block_capacity_frames;
    expect(first_stopped_frame == release_frame && stopped_theta_rad.has_value() &&
               callback_count == expected_callback_count &&
               observed_frame_count == expected_frame_count &&
               capture.published_sample_count() == expected_frame_count &&
               capture.published_block_count() == expected_callback_count &&
               !capture.faulted() && capture.completed(),
           "free-engine stall did not become a stable published stopped state");
}

void test_inertial_capture_publishes_dynamic_motion_and_energy_evidence(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    auto request = canonical;
    auto &dyno = std::get<InertialDyno>(request.scenario.mode);
    dyno.throttle_01.points = {
        {0.0, 0.85},
        {6.45, 0.80},
        {6.46, 0.85},
    };
    const auto request_identity = nonzero_request_identity();
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        request_identity, finite_extent(request.scenario)));

    std::optional<LowOrderCaptureCompleted> completion;
    double first_released_rpm = 0.0;
    double last_rpm = 0.0;
    double throttle_before_boundary = 0.0;
    double throttle_at_boundary = 0.0;
    double throttle_after_boundary = 0.0;
    const auto release_frame = *resolve_frame_index(
        request.scenario.audible_start_s.value, request.scenario.rates.capture);
    const auto throttle_boundary_frame =
        *resolve_frame_index(6.45, request.scenario.rates.capture);
    const auto throttle_restore_frame =
        *resolve_frame_index(6.46, request.scenario.rates.capture);
    while (!completion.has_value()) {
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            const auto report = validate(block, request.engine, request.scenario);
            if (!report.ok()) {
                fail_report("inertial capture block failed validation", report);
            }
            for (const auto &frame : block.engine()) {
                const auto frame_index =
                    block.clock().first_sample_index +
                    static_cast<std::uint64_t>(&frame - block.engine().data());
                if (frame_index == release_frame) {
                    first_released_rpm = frame.engine_speed_rpm;
                }
                if (frame_index + 1U == throttle_boundary_frame) {
                    throttle_before_boundary = frame.requested_throttle_01;
                } else if (frame_index == throttle_boundary_frame) {
                    throttle_at_boundary = frame.requested_throttle_01;
                } else if (frame_index == throttle_restore_frame) {
                    throttle_after_boundary = frame.requested_throttle_01;
                }
                last_rpm = frame.engine_speed_rpm;
            }
            return true;
        });
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "inertial capture faulted: " + failure->detail_code + "; " +
                failure->state_summary +
                "; time-s=" + std::to_string(failure->scenario_time_s)};
        }
        if (const auto *completed = std::get_if<LowOrderCaptureCompleted>(&result)) {
            completion = *completed;
        }
    }

    expect(completion->inertial_dyno.has_value() &&
               !completion->held_speed_operating_point.has_value() &&
               first_released_rpm > 1500.0 && last_rpm > first_released_rpm &&
               throttle_before_boundary == 0.85 && throttle_at_boundary == 0.80 &&
               throttle_after_boundary == 0.85,
           "inertial capture did not execute its authored throttle timeline on the "
           "dynamic motion lane");
    const auto report =
        validate(*completion->inertial_dyno, request.scenario, request_identity);
    if (!report.ok()) {
        fail_report("inertial completion evidence failed request validation", report);
    }
}

void test_inertial_capture_rejects_throttle_transition_during_preparation(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    auto request = canonical;
    auto &dyno = std::get<InertialDyno>(request.scenario.mode);
    dyno.throttle_01.points.push_back(
        {request.scenario.audible_start_s.value - kOuterStepS, 0.80});
    const auto result = compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(
        report != nullptr && !report->ok() &&
            std::ranges::any_of(report->issues,
                                [](const ContractIssue &issue) {
                                    return issue.path ==
                                           "scenario.mode.throttle_01.points[1].time_s";
                                }),
        "inertial capture admitted a throttle transition during held "
        "preparation");
}

void test_consumer_rejection_is_a_stable_terminal_fault(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));
    std::size_t callback_count = 0U;
    auto rejected = capture.publish_next_block([&](const CaptureBlockView &block) {
        ++callback_count;
        expect(block.frame_count() == 200U,
               "rejection probe did not receive one complete bounded block");
        return false;
    });
    const auto *fault = std::get_if<FailureContext>(&rejected);
    expect(fault != nullptr && callback_count == 1U && capture.faulted() &&
               !capture.completed() && capture.published_sample_count() == 0U &&
               capture.published_block_count() == 0U,
           "consumer rejection did not become a terminal unpublished fault");
    const FailureContext first_fault = *fault;

    auto repeated = capture.publish_next_block([&](const CaptureBlockView &) {
        ++callback_count;
        return true;
    });
    const auto *repeated_fault = std::get_if<FailureContext>(&repeated);
    expect(repeated_fault != nullptr && callback_count == 1U &&
               repeated_fault->kind == first_fault.kind &&
               repeated_fault->detail_code == first_fault.detail_code &&
               repeated_fault->state_summary == first_fault.state_summary,
           "consumer-rejection fault was not stable and callback-free");
}

void test_consumer_exception_is_a_stable_terminal_fault(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));
    std::size_t callback_count = 0U;
    auto rejected = capture.publish_next_block([&](const CaptureBlockView &) -> bool {
        ++callback_count;
        throw std::runtime_error{"intentional capture-consumer failure"};
    });
    const auto *fault = std::get_if<FailureContext>(&rejected);
    expect(fault != nullptr && callback_count == 1U && capture.faulted() &&
               !capture.completed() && capture.published_sample_count() == 0U &&
               capture.published_block_count() == 0U,
           "consumer exception escaped or published a rejected block");
    const FailureContext first_fault = *fault;

    auto repeated = capture.publish_next_block([&](const CaptureBlockView &) {
        ++callback_count;
        return true;
    });
    const auto *repeated_fault = std::get_if<FailureContext>(&repeated);
    expect(repeated_fault != nullptr && callback_count == 1U &&
               repeated_fault->kind == first_fault.kind &&
               repeated_fault->detail_code == first_fault.detail_code &&
               repeated_fault->state_summary == first_fault.state_summary,
           "consumer-exception fault was not stable and callback-free");
}

void test_reentrant_publication_preserves_outer_view_and_faults(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_operating_capture_request(canonical);
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));
    std::size_t outer_callback_count = 0U;
    std::size_t nested_callback_count = 0U;
    std::optional<FailureContext> nested_fault;
    std::exception_ptr callback_error;

    auto outer = capture.publish_next_block([&](const CaptureBlockView &block) {
        try {
            ++outer_callback_count;
            const auto before = encode_capture_block(block);
            auto nested = capture.publish_next_block([&](const CaptureBlockView &) {
                ++nested_callback_count;
                return true;
            });
            const auto *failure = std::get_if<FailureContext>(&nested);
            expect(failure != nullptr,
                   "nested publication was not rejected before entering a consumer");
            nested_fault = *failure;
            const auto after = encode_capture_block(block);
            expect(after == before,
                   "nested publication mutated the borrowed outer CaptureBlock");
            return true;
        } catch (...) {
            callback_error = std::current_exception();
            return false;
        }
    });
    if (callback_error != nullptr) {
        std::rethrow_exception(callback_error);
    }

    const auto *outer_fault = std::get_if<FailureContext>(&outer);
    expect(nested_fault.has_value() && outer_fault != nullptr &&
               outer_callback_count == 1U && nested_callback_count == 0U &&
               nested_fault->detail_code == "low-order-capture-consumer-reentrant" &&
               outer_fault->kind == nested_fault->kind &&
               outer_fault->detail_code == nested_fault->detail_code &&
               outer_fault->state_summary == nested_fault->state_summary &&
               capture.faulted() && !capture.completed() &&
               capture.published_sample_count() == 0U &&
               capture.published_block_count() == 0U,
           "reentrant publication did not preserve one stable unpublished fault");

    auto repeated = capture.publish_next_block([&](const CaptureBlockView &) {
        ++outer_callback_count;
        return true;
    });
    const auto *repeated_fault = std::get_if<FailureContext>(&repeated);
    expect(repeated_fault != nullptr && outer_callback_count == 1U &&
               repeated_fault->kind == outer_fault->kind &&
               repeated_fault->detail_code == outer_fault->detail_code &&
               repeated_fault->state_summary == outer_fault->state_summary,
           "reentrant-publication fault was not stable and callback-free");
}

void expect_simulation_compile_rejected(
    const engine_sim_offline::test::AuthoredEngineFixture &request,
    std::string_view mutation) {
    auto result = compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr && !report->ok(),
           std::string{mutation} + " was admitted by the top-level compiler");
}

void test_declared_capture_capacity_drives_publication(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    auto request = make_operating_capture_request(canonical);
    request.scenario.quality.value.capture_block_capacity_frames = 37U;
    request.scenario.quality.value.event_journal_capacity_records = 37U * 19U;
    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));

    std::size_t callback_count = 0U;
    const auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
        ++callback_count;
        expect(block.frame_count() == 37U &&
                   block.declared_block_capacity_frames() == 37U &&
                   block.declared_event_journal_capacity_records() == 37U * 19U,
               "session did not use the compiled capture transport bounds");
        const auto report = validate(block, request.engine, request.scenario);
        if (!report.ok()) {
            fail_report("dynamic-capacity block failed validation", report);
        }
        return true;
    });
    const auto *published = std::get_if<LowOrderCaptureBlockPublished>(&result);
    expect(published != nullptr && published->frame_count == 37U &&
               published->published_sample_count == 37U && callback_count == 1U,
           "dynamic-capacity block was not published atomically");
}

[[nodiscard]] bool is_unavailable(const TorqueValueNm &value,
                                  QuantityUnavailableReason reason) noexcept {
    return value ==
           TorqueValueNm{
               0.0, Availability::unavailable, Completeness::incomplete, reason, 0U,
               0U};
}

[[nodiscard]] bool is_unavailable(const QuantityValue &value,
                                  QuantityUnavailableReason reason) noexcept {
    return value == QuantityValue{0.0, Availability::unavailable,
                                  Completeness::incomplete, reason};
}

void verify_prescribed_torque(const CaptureBlockView &block) {
    for (std::size_t frame = 0; frame < block.frame_count(); ++frame) {
        const auto *engine = block.engine_sample(frame);
        expect(engine != nullptr, "prescribed capture lost an engine frame");

        double aggregate_indicated_gas_torque_nm = 0.0;
        for (std::size_t cylinder = 0; cylinder < block.layout().cylinders().size();
             ++cylinder) {
            const auto *sample = block.cylinder_sample(frame, cylinder);
            expect(sample != nullptr,
                   "prescribed capture lost a cylinder torque sample");
            aggregate_indicated_gas_torque_nm += sample->indicated_gas_torque.value_nm;
        }

        const auto &torque = engine->torque;
        expect(torque.instantaneous_indicated_gas ==
                   TorqueValueNm{aggregate_indicated_gas_torque_nm,
                                 Availability::available, Completeness::complete,
                                 QuantityUnavailableReason::none,
                                 indicated_gas_torque_term_mask(), 0U},
               "prescribed capture did not publish the exact aggregate "
               "indicated-gas torque");
        expect(is_unavailable(torque.pumping_partition,
                              QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(torque.friction_pump_and_accessory,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(torque.starter,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(torque.instantaneous_net_shaft,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(
                       torque.cycle_mean_net_shaft,
                       QuantityUnavailableReason::cycle_integration_not_admitted) &&
                   is_unavailable(torque.actuator,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(torque.dyno_reaction,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(
                       torque.cycle_work_j,
                       QuantityUnavailableReason::cycle_integration_not_admitted) &&
                   is_unavailable(
                       torque.net_bmep_pa,
                       QuantityUnavailableReason::cycle_integration_not_admitted) &&
                   is_unavailable(torque.instantaneous_power_w,
                                  QuantityUnavailableReason::model_not_admitted) &&
                   is_unavailable(
                       torque.cycle_mean_power_w,
                       QuantityUnavailableReason::cycle_integration_not_admitted) &&
                   validate(torque).ok(),
               "prescribed capture invented an unavailable torque, power, work, "
               "or BMEP quantity");
    }
}

struct PrescribedLiveControlContext {
    std::uint64_t next_step = 0U;
};

[[nodiscard]] engine_sim_offline::simulation::detail::LowOrderLiveControlStep
drain_prescribed_live_control(void *context, std::uint64_t physics_step) noexcept {
    auto &state = *static_cast<PrescribedLiveControlContext *>(context);
    if (physics_step != state.next_step) {
        return {};
    }
    ++state.next_step;
    LiveControlOverrides overrides;
    overrides.has_throttle = true;
    overrides.throttle_01 = 0.25;
    return {true, overrides};
}

[[nodiscard]] engine_sim_offline::test::AuthoredEngineFixture
make_prescribed_capture_request(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    auto request = engine_sim_offline::test::make_prescribed_fixture(
        canonical, std::vector<double>(kOperatingStepCount, kOperatingHeldRpm));
    request.scenario.preparation = FixedSettling{
        {0.1, "prescribed-capture-test.warm-up"},
        {0.12, "prescribed-capture-test.settling"},
    };
    request.scenario.total_duration_s.value = kOperatingTotalDurationS;
    request.scenario.audible_start_s.value = kOperatingCutoffTimeS;
    request.scenario.audible_duration_s.value =
        kOperatingTotalDurationS - kOperatingCutoffTimeS;
    return request;
}

void test_prescribed_capture_is_finite_and_gas_torque_only(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    const auto request = make_prescribed_capture_request(canonical);
    expect(validate_clock_grid(request.scenario).ok(),
           "happy prescribed capture fixture is not on its declared clocks");
    expect(validate_for_engine(request.scenario, request.engine).ok(),
           "happy prescribed capture fixture is not admitted for its engine");

    auto capture = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));

    std::uint64_t published_frames = 0U;
    std::uint64_t published_blocks = 0U;
    while (published_frames < kOperatingStepCount) {
        std::exception_ptr callback_error;
        auto result = capture.publish_next_block([&](const CaptureBlockView &block) {
            try {
                verify_prescribed_torque(block);
                published_frames += block.frame_count();
                ++published_blocks;
                return true;
            } catch (...) {
                callback_error = std::current_exception();
                return false;
            }
        });
        if (callback_error != nullptr) {
            std::rethrow_exception(callback_error);
        }
        expect(std::holds_alternative<LowOrderCaptureBlockPublished>(result),
               "prescribed session failed before its finite horizon");
    }

    auto completion =
        capture.publish_next_block([](const CaptureBlockView &) { return true; });
    const auto *completed = std::get_if<LowOrderCaptureCompleted>(&completion);
    expect(completed != nullptr && completed->sample_count == kOperatingStepCount &&
               completed->block_count == published_blocks &&
               !completed->held_speed_operating_point.has_value() &&
               !completed->inertial_dyno.has_value() && capture.completed() &&
               !capture.faulted(),
           "prescribed session did not finish without invented result evidence");

    auto controlled = require_simulation(compile_low_order_capture_session(
        request.engine, request.scenario,
        fixture_random_plan(request, request.engine, request.scenario),
        nonzero_request_identity(), finite_extent(request.scenario)));
    PrescribedLiveControlContext context;
    const engine_sim_offline::simulation::detail::LowOrderLiveControlProvider provider{
        &context, request.scenario.rates.physics, &drain_prescribed_live_control};
    std::size_t callback_count = 0U;
    auto rejected = controlled.publish_next_block(
        [&](const CaptureBlockView &) {
            ++callback_count;
            return true;
        },
        provider);
    const auto *failure = std::get_if<FailureContext>(&rejected);
    expect(failure != nullptr &&
               failure->detail_code ==
                   "prescribed-kinematic-live-controls-not-admitted" &&
               controlled.faulted() && !controlled.completed() &&
               controlled.published_sample_count() == 0U &&
               controlled.published_block_count() == 0U && callback_count == 0U,
           "prescribed session applied or published a live control override");
}

void test_capture_partition_admission_rejection(
    const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    {
        auto request = make_operating_capture_request(canonical);
        request.scenario.rates.capture = {9999, 1};
        expect_simulation_compile_rejected(request, "noncanonical capture clock");
    }
    {
        auto request = make_operating_capture_request(canonical);
        request.scenario.rates.physics = {9999, 1};
        expect_simulation_compile_rejected(request, "noncanonical physics clock");
    }
    {
        auto request = make_operating_capture_request(canonical);
        request.scenario.quality.value.event_journal_capacity_records = 3799U;
        expect_simulation_compile_rejected(request,
                                           "undersized event-journal capacity");
    }
    {
        auto request = make_operating_capture_request(canonical);
        auto &preparation =
            std::get<FixedHorizonCycleSampling>(request.scenario.preparation);
        preparation.method.value.configuration_sha256.bytes[0] ^= 0xffU;
        expect_simulation_compile_rejected(request,
                                           "wrong fixed-horizon method configuration");
    }
    {
        auto request = make_prescribed_capture_request(canonical);
        auto fixed_horizon =
            std::get<FixedHorizonCycleSampling>(canonical.scenario.preparation);
        fixed_horizon.fixed_preparation_horizon_s.value = kOperatingCutoffTimeS;
        request.scenario.preparation = std::move(fixed_horizon);
        expect_simulation_compile_rejected(
            request, "fixed-horizon preparation for prescribed motion");
    }
    {
        auto request = make_prescribed_capture_request(canonical);
        request.scenario.operating_state.value.front().state.dyno_enabled = false;
        expect_simulation_compile_rejected(
            request, "non-held operating state for prescribed motion");
    }
}

void run_tests(const engine_sim_offline::test::AuthoredEngineFixture &canonical) {
    test_capture_projection_canonicalizes_legacy_mixture_weights();
    test_authored_capture_mapping_and_completion(canonical);
    test_twenty_khz_mechanics_gas_and_capture_share_one_clock(canonical);
    test_operating_capture_publishes_request_bound_completion_evidence(canonical);
    test_operating_capture_rejects_zero_request_identity(canonical);
    test_free_engine_capture_holds_preparation_and_executes_authored_controls(
        canonical);
    test_free_engine_capture_runs_dynamic_pre_audible_acquisition(canonical);
    test_free_engine_capture_applies_live_limiter_and_resistance_after_release(
        canonical);
    test_open_free_engine_capture_runs_past_authored_horizon_in_full_blocks(canonical);
    test_free_engine_capture_advances_canonical_stopped_state(canonical);
    test_free_engine_capture_commits_stable_stop_without_reverse(canonical);
    test_inertial_capture_publishes_dynamic_motion_and_energy_evidence(canonical);
    test_inertial_capture_rejects_throttle_transition_during_preparation(canonical);
    test_declared_capture_capacity_drives_publication(canonical);
    test_consumer_rejection_is_a_stable_terminal_fault(canonical);
    test_consumer_exception_is_a_stable_terminal_fault(canonical);
    test_reentrant_publication_preserves_outer_view_and_faults(canonical);
    test_prescribed_capture_is_finite_and_gas_torque_only(canonical);
    test_capture_partition_admission_rejection(canonical);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{
                "usage: low_order_capture_session_test <repository-root>"};
        }
        const auto canonical =
            engine_sim_offline::test::load_canonical_authored_engine_fixture(
                std::filesystem::path{argv[1]});
        run_tests(canonical);
    } catch (const std::exception &error) {
        std::cerr << "low_order_capture_session_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
