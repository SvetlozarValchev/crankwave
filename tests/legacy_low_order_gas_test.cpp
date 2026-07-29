#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::profiles;
using namespace engine_sim_offline::simulation;
using CoreRuntimeFactory =
    engine_sim_offline::simulation::detail::LowOrderEngineCoreV1RuntimeFactory;

inline constexpr std::size_t kShortRunStepCount = 4000U;
inline constexpr double kShortRunRpm = 2400.0;
inline constexpr double kShortRunDurationS =
    static_cast<double>(kShortRunStepCount) / 10000.0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
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

[[nodiscard]] FixedRateRpmTrajectory &fixed_rpm(RenderScenario &scenario) {
    auto *sweep = std::get_if<PrescribedKinematicSweep>(&scenario.mode);
    expect(sweep != nullptr, "short BMW scenario lost its prescribed sweep");
    auto *rpm = std::get_if<FixedRateRpmTrajectory>(&sweep->trajectory.rpm);
    expect(rpm != nullptr, "short BMW scenario lost its fixed-rate RPM lane");
    return *rpm;
}

[[nodiscard]] PrescribedKinematicSweep &prescribed_sweep(RenderScenario &scenario) {
    auto *sweep = std::get_if<PrescribedKinematicSweep>(&scenario.mode);
    expect(sweep != nullptr, "short BMW scenario lost its prescribed sweep");
    return *sweep;
}

[[nodiscard]] BmwM52b28ParityRequest make_short_bmw_request() {
    std::vector<double> rpm(kShortRunStepCount, kShortRunRpm);
    auto request = engine_sim_offline::profiles::detail::
        build_bmw_m52b28_parity_request_unvalidated(std::move(rpm));

    request.scenario.scenario_id = "bmw-m52b28-short-gas-integration";
    request.scenario.total_duration_s.value = kShortRunDurationS;
    request.scenario.audible_start_s.value = 0.0;
    request.scenario.audible_duration_s.value = kShortRunDurationS;
    auto *preparation = std::get_if<FixedSettling>(&request.scenario.preparation);
    expect(preparation != nullptr,
           "short BMW scenario lost its fixed preparation policy");
    preparation->warm_up_duration_s.value = 0.0;
    preparation->settling_duration_s.value = 0.0;
    request.scenario.operating_state.value = {
        {
            "short-run-fired",
            0.0,
            {true, true, false, true, true},
        },
    };

    auto &sweep = prescribed_sweep(request.scenario);
    sweep.throttle_01.points = {{0.0, 0.85}};
    auto &trajectory = fixed_rpm(request.scenario);
    trajectory.samples_f64le_sha256 =
        canonical_binary64_le_sha256(trajectory.post_step_rpm);
    return request;
}

[[nodiscard]] LegacyLowOrderMechanicsSession
require_mechanics(CoreRuntimeFactory::MechanicsCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short BMW mechanics request failed admission", *report);
    }
    return std::get<LegacyLowOrderMechanicsSession>(std::move(result));
}

[[nodiscard]] LegacyLowOrderGasSession
require_gas(CoreRuntimeFactory::GasCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short BMW gas request failed admission", *report);
    }
    return std::get<LegacyLowOrderGasSession>(std::move(result));
}

[[nodiscard]] KinematicScenarioSchedule
require_schedule(KinematicScenarioScheduleResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short BMW schedule failed admission", *report);
    }
    return std::get<KinematicScenarioSchedule>(std::move(result));
}

[[nodiscard]] const LowOrderEngineCoreV1 &
low_order_core(const BmwM52b28ParityRequest &request) {
    return std::get<LegacyLowOrderV1Profile>(request.engine.physics_profile).core;
}

struct CompiledSessions {
    LegacyLowOrderMechanicsSession mechanics;
    LegacyLowOrderGasSession gas;
};

[[nodiscard]] CompiledSessions compile_sessions(const BmwM52b28ParityRequest &request) {
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
        request.engine, low_order_core(request), request.scenario, schedule));
    auto gas = require_gas(CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario,
        schedule.control_schedule(), mechanics.cylinder_models()));
    return {std::move(mechanics), std::move(gas)};
}

[[nodiscard]] const LegacyMechanismStep &
require_mechanics_step(LegacyMechanicsAdvanceResult &result,
                       std::uint64_t expected_sample_index) {
    if (const auto *fault = std::get_if<FailureContext>(&result)) {
        std::ostringstream message;
        message << "mechanics fault at short-run frame " << expected_sample_index
                << ": " << fault->detail_code << " (" << fault->state_summary << ')';
        throw std::runtime_error{message.str()};
    }
    const auto *step =
        std::get_if<std::reference_wrapper<const LegacyMechanismStep>>(&result);
    expect(step != nullptr,
           "mechanics completed before the short BMW scenario horizon");
    return step->get();
}

[[nodiscard]] const LegacyLowOrderGasStep &
require_gas_step(LegacyGasAdvanceResult &result, std::uint64_t expected_sample_index) {
    if (const auto *fault = std::get_if<FailureContext>(&result)) {
        std::ostringstream message;
        message << "gas fault at short-run frame " << expected_sample_index << ": "
                << fault->detail_code << " (" << fault->state_summary << ')';
        throw std::runtime_error{message.str()};
    }
    const auto *step =
        std::get_if<std::reference_wrapper<const LegacyLowOrderGasStep>>(&result);
    expect(step != nullptr, "gas session did not publish a short-run step");
    return step->get();
}

[[nodiscard]] bool finite_nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

void verify_fresh_layout_and_first_state(const BmwM52b28ParityRequest &request,
                                         const LegacyMechanismStep &mechanics,
                                         const LegacyLowOrderGasStep &gas) {
    expect(gas.rate == RationalRateHz{10000, 1} && gas.sample_index == 0 &&
               gas.step_end_index == 1 && gas.timestamp_tick == 1,
           "fresh gas step has the wrong fixed-rate clock");
    expect(gas.gas_volumes.size() == 22U && gas.flow_edges.size() == 34U &&
               gas.cylinders.size() == 6U && gas.exhaust_routes.size() == 2U,
           "fresh BMW gas layout has the wrong canonical entity counts");
    expect(gas.gas_volumes.size() == request.engine.gas_volumes.size() &&
               gas.flow_edges.size() == request.engine.flow_edges.size() &&
               gas.cylinders.size() == request.engine.cylinders.size() &&
               gas.exhaust_routes.size() == request.engine.routes.size(),
           "compiled gas layout diverged from engine topology");

    for (std::size_t index = 0; index < gas.gas_volumes.size(); ++index) {
        const auto &actual = gas.gas_volumes[index];
        const auto &declared = request.engine.gas_volumes[index];
        expect(actual.gas_volume_id == declared.id &&
                   actual.kind == declared.kind.value,
               "gas-volume identity/order changed at canonical index " +
                   std::to_string(index));
        if (declared.kind.value == GasVolumeKind::atmosphere) {
            expect(!actual.physically_resolved && actual.cell == LegacyGasCell{} &&
                       actual.geometry == LegacyGasCellGeometry{},
                   "aliased atmosphere unexpectedly published physical state");
            continue;
        }

        expect(actual.physically_resolved && std::isfinite(actual.cell.amount_mol) &&
                   actual.cell.amount_mol > 0.0 &&
                   std::isfinite(actual.cell.thermal_energy_j) &&
                   actual.cell.thermal_energy_j > 0.0 &&
                   std::isfinite(actual.cell.volume_m3) && actual.cell.volume_m3 > 0.0,
               "fresh finite gas volume has invalid thermodynamic state");
        expect(finite_nonnegative(actual.cell.mixture.fuel_fraction) &&
                   finite_nonnegative(actual.cell.mixture.inert_fraction) &&
                   finite_nonnegative(actual.cell.mixture.oxygen_fraction),
               "fresh finite gas volume has invalid composition");
        expect(
            std::isfinite(actual.geometry.width_m) && actual.geometry.width_m > 0.0 &&
                std::isfinite(actual.geometry.height_m) &&
                actual.geometry.height_m > 0.0 && actual.geometry.direction_x == 1.0 &&
                actual.geometry.direction_y == 0.0,
            "fresh finite gas volume has invalid fixed planar geometry");
    }

    for (std::size_t index = 0; index < gas.flow_edges.size(); ++index) {
        const auto &actual = gas.flow_edges[index];
        const auto &declared = request.engine.flow_edges[index];
        expect(actual.flow_edge_id == declared.id &&
                   actual.endpoint_0_volume_id == declared.endpoint_0_volume_id &&
                   actual.endpoint_1_volume_id == declared.endpoint_1_volume_id &&
                   std::isfinite(actual.signed_amount_mol),
               "flow-edge identity/order or fresh value changed");
    }

    for (std::size_t index = 0; index < gas.cylinders.size(); ++index) {
        const std::uint32_t number = static_cast<std::uint32_t>(index + 1U);
        const auto &cylinder = gas.cylinders[index];
        expect(cylinder.cylinder_id == CylinderId{number} &&
                   cylinder.intake_port_id == PortId{2U * number - 1U} &&
                   cylinder.exhaust_port_id == PortId{2U * number} &&
                   cylinder.intake_runner_volume_id ==
                       GasVolumeId{3U + 3U * static_cast<std::uint32_t>(index)} &&
                   cylinder.chamber_volume_id ==
                       GasVolumeId{4U + 3U * static_cast<std::uint32_t>(index)} &&
                   cylinder.exhaust_primary_volume_id ==
                       GasVolumeId{5U + 3U * static_cast<std::uint32_t>(index)} &&
                   cylinder.exhaust_route_id == RouteId{number % 2U == 0U ? 1U : 2U},
               "fresh cylinder gas binding/order changed");
        expect(cylinder.valves.cylinder_id == cylinder.cylinder_id &&
                   cylinder.valves.intake_port_id == cylinder.intake_port_id &&
                   cylinder.valves.exhaust_port_id == cylinder.exhaust_port_id &&
                   finite_nonnegative(cylinder.valves.intake_lift_m) &&
                   finite_nonnegative(cylinder.valves.exhaust_lift_m) &&
                   finite_nonnegative(cylinder.valves.intake_valve_k) &&
                   finite_nonnegative(cylinder.valves.exhaust_valve_k),
               "fresh cylinder valve binding/state changed");
        expect(!cylinder.flame.active &&
                   cylinder.outer_step_combustion_heat_release_j == 0.0 &&
                   cylinder.cumulative_burned_fuel_mass_kg == 0.0 &&
                   std::isfinite(cylinder.peak_temperature_k) &&
                   cylinder.peak_temperature_k > 0.0 &&
                   std::isfinite(cylinder.indicated_gas_torque_nm),
               "fresh cylinder diagnostics did not begin unburned and finite");
        expect(mechanics.cylinders[index].cylinder_id == cylinder.cylinder_id,
               "fresh gas cylinder order diverged from mechanics order");
    }

    for (std::size_t index = 0; index < gas.exhaust_routes.size(); ++index) {
        const auto &route = gas.exhaust_routes[index];
        const auto &declared = request.engine.routes[index];
        expect(route.route_id == declared.id && declared.source_volume_id.has_value() &&
                   route.collector_volume_id == *declared.source_volume_id &&
                   route.collector_outlet_edge_id ==
                       FlowEdgeId{static_cast<std::uint32_t>(33U + index)} &&
                   std::isfinite(route.collector_cross_section_area_m2) &&
                   route.collector_cross_section_area_m2 > 0.0,
               "fresh exhaust-route identity/order changed");
    }

    expect(std::isfinite(gas.indicated_gas_torque_nm),
           "fresh aggregate indicated torque state is nonfinite");
}

enum class EventStage : std::uint8_t {
    spark,
    limiter,
    ignition,
    extinction,
};

[[nodiscard]] EventStage event_stage(const EngineEventPayload &payload) {
    if (std::holds_alternative<SparkCrossing>(payload)) {
        return EventStage::spark;
    }
    if (std::holds_alternative<LimiterStateChanged>(payload)) {
        return EventStage::limiter;
    }
    if (std::holds_alternative<IgnitionAccepted>(payload) ||
        std::holds_alternative<IgnitionRejected>(payload)) {
        return EventStage::ignition;
    }
    expect(std::holds_alternative<FlameExtinguished>(payload),
           "gas event journal contains an unknown payload");
    return EventStage::extinction;
}

struct ActivityCoverage {
    bool intake_valve_open = false;
    bool exhaust_valve_open = false;
    bool nonzero_flow = false;
    bool accepted_ignition = false;
    bool combustion_heat = false;
    std::uint64_t spark_count = 0;
    std::uint64_t ignition_result_count = 0;
};

void verify_event_order(const LegacyLowOrderGasStep &step, ActivityCoverage &coverage) {
    EventStage previous_stage = EventStage::spark;
    std::vector<CylinderId> spark_cylinders;
    std::vector<CylinderId> result_cylinders;
    for (std::size_t index = 0; index < step.events.size(); ++index) {
        const auto &event = step.events[index];
        expect(event.ordinal_within_step == static_cast<std::uint8_t>(index),
               "gas event ordinal is not contiguous append order");
        const EventStage stage = event_stage(event.payload);
        expect(static_cast<std::uint8_t>(stage) >=
                   static_cast<std::uint8_t>(previous_stage),
               "gas event journal left normative execution order");
        previous_stage = stage;

        if (const auto *spark = std::get_if<SparkCrossing>(&event.payload)) {
            spark_cylinders.push_back(spark->cylinder_id);
            ++coverage.spark_count;
        } else if (const auto *accepted =
                       std::get_if<IgnitionAccepted>(&event.payload)) {
            result_cylinders.push_back(accepted->cylinder_id);
            ++coverage.ignition_result_count;
            coverage.accepted_ignition = true;
            expect(finite_nonnegative(accepted->efficiency_01) &&
                       std::isfinite(accepted->flame_speed_m_s) &&
                       accepted->flame_speed_m_s > 0.0,
                   "accepted ignition event has invalid flame parameters");
        } else if (const auto *rejected =
                       std::get_if<IgnitionRejected>(&event.payload)) {
            result_cylinders.push_back(rejected->cylinder_id);
            ++coverage.ignition_result_count;
            expect(rejected->reason != IgnitionRejection::unspecified,
                   "rejected ignition event omitted its classification");
        } else if (const auto *extinguished =
                       std::get_if<FlameExtinguished>(&event.payload)) {
            expect(extinguished->gas_substep_index < kLegacyGasSubstepCount &&
                       extinguished->reason != FlameExtinctionReason::unspecified,
                   "flame-extinction event has invalid substep or reason");
        }
    }
    expect(spark_cylinders == result_cylinders,
           "spark crossings and cylinder-order ignition results diverged");
}

void accumulate_activity(const LegacyLowOrderGasStep &step,
                         ActivityCoverage &coverage) {
    for (const auto &edge : step.flow_edges) {
        coverage.nonzero_flow = coverage.nonzero_flow || edge.signed_amount_mol != 0.0;
    }
    for (const auto &cylinder : step.cylinders) {
        coverage.intake_valve_open =
            coverage.intake_valve_open || cylinder.valves.intake_lift_m > 0.0;
        coverage.exhaust_valve_open =
            coverage.exhaust_valve_open || cylinder.valves.exhaust_lift_m > 0.0;
        coverage.combustion_heat = coverage.combustion_heat ||
                                   cylinder.outer_step_combustion_heat_release_j > 0.0;
    }
}

[[nodiscard]] bool same_event_payload(const EngineEventPayload &left,
                                      const EngineEventPayload &right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto *value = std::get_if<SparkCrossing>(&left)) {
        const auto &other = std::get<SparkCrossing>(right);
        return value->cylinder_id == other.cylinder_id &&
               value->raw_saved_angle_rad == other.raw_saved_angle_rad &&
               value->raw_current_angle_rad == other.raw_current_angle_rad &&
               value->adjusted_current_angle_rad == other.adjusted_current_angle_rad &&
               value->adjusted_spark_angle_rad == other.adjusted_spark_angle_rad &&
               value->timing_advance_rad == other.timing_advance_rad;
    }
    if (const auto *value = std::get_if<LimiterStateChanged>(&left)) {
        const auto &other = std::get<LimiterStateChanged>(right);
        return value->old_active == other.old_active &&
               value->new_active == other.new_active &&
               value->overspeed_refreshed == other.overspeed_refreshed &&
               value->resulting_timer_s == other.resulting_timer_s;
    }
    if (const auto *value = std::get_if<IgnitionAccepted>(&left)) {
        const auto &other = std::get<IgnitionAccepted>(right);
        return value->cylinder_id == other.cylinder_id &&
               value->efficiency_01 == other.efficiency_01 &&
               value->flame_speed_m_s == other.flame_speed_m_s;
    }
    if (const auto *value = std::get_if<IgnitionRejected>(&left)) {
        const auto &other = std::get<IgnitionRejected>(right);
        return value->cylinder_id == other.cylinder_id && value->reason == other.reason;
    }
    const auto &value = std::get<FlameExtinguished>(left);
    const auto &other = std::get<FlameExtinguished>(right);
    return value.cylinder_id == other.cylinder_id &&
           value.gas_substep_index == other.gas_substep_index &&
           value.reason == other.reason;
}

[[nodiscard]] bool same_events(const std::vector<ScheduledMechanismEvent> &left,
                               const std::vector<ScheduledMechanismEvent> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].ordinal_within_step != right[index].ordinal_within_step ||
            !same_event_payload(left[index].payload, right[index].payload)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_mechanics_step(const LegacyMechanismStep &left,
                                       const LegacyMechanismStep &right) {
    return left.rate == right.rate && left.sample_index == right.sample_index &&
           left.step_end_index == right.step_end_index &&
           left.timestamp_tick == right.timestamp_tick &&
           left.operating_state == right.operating_state &&
           left.requested_throttle_01 == right.requested_throttle_01 &&
           left.resolved_engine_throttle_01 == right.resolved_engine_throttle_01 &&
           left.intake_plate_position_01 == right.intake_plate_position_01 &&
           left.main_flow_multiplier_01 == right.main_flow_multiplier_01 &&
           left.engine_speed_rpm == right.engine_speed_rpm &&
           left.omega_legacy_rad_s == right.omega_legacy_rad_s &&
           left.angular_speed_rad_s == right.angular_speed_rad_s &&
           left.angular_acceleration_rad_s2 == right.angular_acceleration_rad_s2 &&
           left.body_angle_psi_rad == right.body_angle_psi_rad &&
           left.theta_cycle_rad == right.theta_cycle_rad &&
           left.theta_unwrapped_rad == right.theta_unwrapped_rad &&
           left.filtered_engine_speed_rpm == right.filtered_engine_speed_rpm &&
           left.timing_advance_rad == right.timing_advance_rad &&
           left.limiter_timer_s == right.limiter_timer_s &&
           left.limiter_cut_active == right.limiter_cut_active &&
           left.cylinders == right.cylinders && same_events(left.events, right.events);
}

[[nodiscard]] bool same_gas_step(const LegacyLowOrderGasStep &left,
                                 const LegacyLowOrderGasStep &right) {
    return left.rate == right.rate && left.sample_index == right.sample_index &&
           left.step_end_index == right.step_end_index &&
           left.timestamp_tick == right.timestamp_tick &&
           left.gas_volumes == right.gas_volumes &&
           left.flow_edges == right.flow_edges && left.cylinders == right.cylinders &&
           left.exhaust_routes == right.exhaust_routes &&
           left.exhaust_port_substeps == right.exhaust_port_substeps &&
           same_events(left.events, right.events) &&
           left.indicated_gas_torque_nm == right.indicated_gas_torque_nm;
}

void test_short_bmw_fresh_state_and_deterministic_activity() {
    const BmwM52b28ParityRequest request = make_short_bmw_request();
    CompiledSessions first = compile_sessions(request);
    CompiledSessions second = compile_sessions(request);
    ActivityCoverage coverage;

    for (std::uint64_t sample_index = 0;
         sample_index < static_cast<std::uint64_t>(kShortRunStepCount);
         ++sample_index) {
        auto first_mechanics_result = first.mechanics.advance();
        auto second_mechanics_result = second.mechanics.advance();
        const auto &first_mechanics =
            require_mechanics_step(first_mechanics_result, sample_index);
        const auto &second_mechanics =
            require_mechanics_step(second_mechanics_result, sample_index);
        expect(same_mechanics_step(first_mechanics, second_mechanics),
               "two fresh mechanics sessions diverged at frame " +
                   std::to_string(sample_index));

        auto first_gas_result = first.gas.advance(first_mechanics);
        auto second_gas_result = second.gas.advance(second_mechanics);
        const auto &first_gas = require_gas_step(first_gas_result, sample_index);
        const auto &second_gas = require_gas_step(second_gas_result, sample_index);
        expect(same_gas_step(first_gas, second_gas),
               "two fresh gas sessions diverged at frame " +
                   std::to_string(sample_index));
        expect(first_gas.sample_index == sample_index &&
                   first_gas.step_end_index == sample_index + 1U &&
                   first_gas.timestamp_tick == sample_index + 1U,
               "gas session published the wrong frame identity");

        if (sample_index == 0U) {
            verify_fresh_layout_and_first_state(request, first_mechanics, first_gas);
        }
        verify_event_order(first_gas, coverage);
        accumulate_activity(first_gas, coverage);
    }

    expect(first.mechanics.completed() && second.mechanics.completed() &&
               !first.gas.faulted() && !second.gas.faulted() &&
               first.gas.produced_sample_count() == kShortRunStepCount &&
               second.gas.produced_sample_count() == kShortRunStepCount,
           "fresh deterministic sessions did not reach the short horizon cleanly");
    expect(coverage.intake_valve_open && coverage.exhaust_valve_open &&
               coverage.nonzero_flow,
           "short BMW run did not exercise both valvetrains and real gas flow");
    expect(coverage.spark_count > 0U &&
               coverage.ignition_result_count == coverage.spark_count,
           "short BMW run did not exercise paired spark and ignition events");
    expect(coverage.accepted_ignition && coverage.combustion_heat,
           "short BMW run did not reach accepted, heat-releasing combustion");
}

void expect_gas_compile_rejected(const BmwM52b28ParityRequest &request,
                                 std::string_view expected_path,
                                 std::string_view context) {
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
        request.engine, low_order_core(request), request.scenario, schedule));
    auto result = CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario,
        schedule.control_schedule(), mechanics.cylinder_models());
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, std::string{context} + " compiled successfully");
    const bool has_expected_issue = std::any_of(
        report->issues.begin(), report->issues.end(), [&](const ContractIssue &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    expect(has_expected_issue,
           std::string{context} + " rejection omitted the responsible path");
}

void test_gas_method_admission_rejection() {
    {
        BmwM52b28ParityRequest request = make_short_bmw_request();
        request.engine.methods.gas_exchange.value.version += 1U;
        expect_gas_compile_rejected(request, "engine.methods.gas_exchange",
                                    "unsupported gas method");
    }

    {
        BmwM52b28ParityRequest request = make_short_bmw_request();
        auto &profile =
            std::get<LegacyLowOrderV1Profile>(request.engine.physics_profile);
        profile.core.fuel.lbv_multiplier.value = 0.0;
        expect_gas_compile_rejected(request, "engine.physics_profile.fuel",
                                    "zero flame-speed multiplier");
    }

    {
        BmwM52b28ParityRequest request = make_short_bmw_request();
        auto sweep_schedule =
            require_schedule(compile_kinematic_scenario_schedule(request.scenario));
        auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
            request.engine, low_order_core(request), request.scenario, sweep_schedule));
        const double initial_theta_rad =
            low_order_core(request).mechanism.crank.crank_tdc_reference_rad.value;
        request.scenario.mode =
            HeldSpeed{{kShortRunRpm, {}}, {initial_theta_rad, {}}, {0.85, {}}};
        auto schedule =
            require_schedule(compile_kinematic_scenario_schedule(request.scenario));
        auto gas = require_gas(CoreRuntimeFactory::compile_gas(
            request.engine, low_order_core(request), request.scenario,
            schedule.control_schedule(), mechanics.cylinder_models()));
        expect(gas.produced_sample_count() == 0U,
               "fresh held-speed gas session published samples during admission");
    }
}

void run_tests() {
    test_short_bmw_fresh_state_and_deterministic_activity();
    test_gas_method_admission_rejection();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "legacy_low_order_gas_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
