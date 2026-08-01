#include "authored_engine_fixture_support.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
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
using namespace engine_sim_offline::simulation;
using engine_sim_offline::test::AuthoredEngineFixture;
using CoreRuntimeFactory =
    engine_sim_offline::simulation::detail::LowOrderEngineCoreV1RuntimeFactory;

inline constexpr std::size_t kShortRunStepCount = 4000U;
inline constexpr double kShortRunRpm = 2400.0;
inline constexpr double kShortRunDurationS =
    static_cast<double>(kShortRunStepCount) / 10000.0;
inline constexpr std::size_t kRadialGasStepCount = 2U;
inline constexpr double kRadialGasRpm = 1800.0;

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
    expect(sweep != nullptr, "short authored scenario lost its prescribed sweep");
    auto *rpm = std::get_if<FixedRateRpmTrajectory>(&sweep->trajectory.rpm);
    expect(rpm != nullptr, "short authored scenario lost its fixed-rate RPM lane");
    return *rpm;
}

[[nodiscard]] PrescribedKinematicSweep &prescribed_sweep(RenderScenario &scenario) {
    auto *sweep = std::get_if<PrescribedKinematicSweep>(&scenario.mode);
    expect(sweep != nullptr, "short authored scenario lost its prescribed sweep");
    return *sweep;
}

[[nodiscard]] AuthoredEngineFixture
make_short_request(const AuthoredEngineFixture &canonical) {
    std::vector<double> rpm(kShortRunStepCount, kShortRunRpm);
    auto request =
        engine_sim_offline::test::make_prescribed_fixture(canonical, std::move(rpm));

    request.scenario.scenario_id = "authored-short-gas-integration";
    request.scenario.total_duration_s.value = kShortRunDurationS;
    request.scenario.audible_start_s.value = 0.0;
    request.scenario.audible_duration_s.value = kShortRunDurationS;
    request.scenario.preparation = FixedSettling{
        {0.0, "authored-fixture.no-warm-up"},
        {0.0, "authored-fixture.no-settling"},
    };
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

[[nodiscard]] AuthoredEngineFixture
make_radial_gas_request(const AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    auto &trajectory = fixed_rpm(request.scenario);
    trajectory.post_step_rpm.assign(kRadialGasStepCount, kRadialGasRpm);
    trajectory.samples_f64le_sha256 =
        canonical_binary64_le_sha256(trajectory.post_step_rpm);
    request.scenario.scenario_id = "internal-radial-gas-prescribed";
    request.scenario.total_duration_s.value =
        static_cast<double>(kRadialGasStepCount) / 10000.0;
    request.scenario.audible_duration_s.value =
        request.scenario.total_duration_s.value;
    request.scenario.operating_state.value = {
        {
            "radial-motored",
            0.0,
            {false, false, false, false, false},
        },
    };
    prescribed_sweep(request.scenario).throttle_01.points = {{0.0, 0.35}};

    auto &core = engine_sim_offline::test::low_order_core(request.engine);
    expect(request.engine.cylinders.size() >= 2U &&
               core.mechanism.cylinders.size() == request.engine.cylinders.size(),
           "radial gas fixture requires at least two ordered cylinders");
    const auto root_id = request.engine.cylinders.front().id;
    auto &public_slave = request.engine.cylinders[1];
    auto &slave_assembly = core.mechanism.cylinders[1];
    const double local_phase_rad = kLegacyPi / 3.0;
    auto throw_radius = public_slave.bore_m;
    throw_radius.value = 0.025;
    public_slave.journal_phase_rad.value = local_phase_rad;
    public_slave.master_rod_attachment =
        MasterRodAttachmentSpec{root_id, throw_radius};
    auto local_phase = public_slave.journal_phase_rad;
    local_phase.value = local_phase_rad;
    slave_assembly.parameters.deck_height_m.value = 0.25;
    slave_assembly.kinematics = LegacyMasterRodJournalKinematics{
        root_id,
        throw_radius,
        local_phase,
    };
    return request;
}

[[nodiscard]] LegacyLowOrderMechanicsSession
require_mechanics(CoreRuntimeFactory::MechanicsCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short authored mechanics request failed admission", *report);
    }
    return std::get<LegacyLowOrderMechanicsSession>(std::move(result));
}

[[nodiscard]] LegacyLowOrderGasSession
require_gas(CoreRuntimeFactory::GasCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short authored gas request failed admission", *report);
    }
    return std::get<LegacyLowOrderGasSession>(std::move(result));
}

[[nodiscard]] RandomPlan require_random_plan(const AuthoredEngineFixture &request) {
    return engine_sim_offline::test::compile_fixture_random_plan(request);
}

[[nodiscard]] KinematicScenarioSchedule
require_schedule(KinematicScenarioScheduleResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short authored schedule failed admission", *report);
    }
    return std::get<KinematicScenarioSchedule>(std::move(result));
}

[[nodiscard]] const LowOrderEngineCoreV1 &
low_order_core(const AuthoredEngineFixture &request) {
    return engine_sim_offline::test::low_order_core(request.engine);
}

[[nodiscard]] SharedMechanismKinematicsPlan
require_mechanism_plan(const AuthoredEngineFixture &request) {
    auto result = compile_mechanism_kinematics_plan(request.engine,
                                                    low_order_core(request));
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        fail_report("short authored mechanism plan failed admission", *report);
    }
    return std::get<SharedMechanismKinematicsPlan>(std::move(result));
}

[[nodiscard]] LegacyMechanismStep make_radial_mechanism_step(
    const OneLevelMasterRodMechanismKinematicsPlan &plan,
    const std::uint64_t sample_index) {
    constexpr double step_s = 1.0 / 10000.0;
    const double angular_speed_rad_s = kRadialGasRpm * kLegacyRpmScale;
    const double angular_displacement_rad = angular_speed_rad_s * step_s;
    const double body_angle_psi_rad =
        -static_cast<double>(sample_index + 1U) * angular_displacement_rad;

    LegacyMechanismStep step;
    step.rate = {10000U, 1U};
    step.sample_index = sample_index;
    step.step_end_index = sample_index + 1U;
    step.timestamp_tick = step.step_end_index;
    step.operating_state = {false, false, false, false, false};
    step.requested_throttle_01 = 0.35;
    step.resolved_engine_throttle_01 = 0.35;
    step.intake_plate_position_01 = 0.35;
    step.main_flow_multiplier_01 = 0.35;
    step.engine_speed_rpm = kRadialGasRpm;
    step.omega_legacy_rad_s = -angular_speed_rad_s;
    step.angular_speed_rad_s = angular_speed_rad_s;
    step.angular_acceleration_rad_s2 =
        sample_index == 0U ? angular_speed_rad_s / step_s : 0.0;
    step.body_angle_psi_rad = body_angle_psi_rad;
    step.theta_cycle_rad = legacy_positive_mod(
        -(body_angle_psi_rad - plan.crank_tdc_reference_rad), 4.0 * kLegacyPi);
    step.theta_unwrapped_rad =
        plan.crank_tdc_reference_rad +
        static_cast<double>(sample_index + 1U) * angular_displacement_rad;
    step.filtered_engine_speed_rpm = kRadialGasRpm;
    step.timing_advance_rad = 0.0;
    step.cylinders.resize(plan.cylinders.size());

    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto evaluated = evaluate_one_level_master_rod_plan(
            plan, index, body_angle_psi_rad, angular_speed_rad_s);
        expect(evaluated.valid,
               "radial gas mechanics fixture produced invalid analytic geometry");
        const auto &planned = plan.cylinders[index];
        const auto *geometry = std::visit(
            [](const auto &kinematics) { return &kinematics.cylinder; },
            planned.kinematics);
        step.cylinders[index] = {
            geometry->cylinder_id,
            planned.exhaust_route_id,
            OneLevelMasterRodCoordinates{
                evaluated.piston_axis_position_m,
                evaluated.piston_axis_derivative_m_per_rad,
            },
            planned.ignition_wire_angle_rad,
            evaluated.chamber_volume_m3,
            evaluated.dvolume_dtheta_m3_per_rad,
            evaluated.piston_speed_abs_m_s,
            false,
        };
    }
    return step;
}

struct CompiledSessions {
    LegacyLowOrderMechanicsSession mechanics;
    LegacyLowOrderGasSession gas;
};

[[nodiscard]] CompiledSessions compile_sessions(const AuthoredEngineFixture &request) {
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanism_plan = require_mechanism_plan(request);
    auto mechanics = require_mechanics(CoreRuntimeFactory::compile_mechanics(
        request.engine, low_order_core(request), request.scenario, mechanism_plan,
        schedule));
    const auto random_plan = require_random_plan(request);
    auto gas = require_gas(CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario, random_plan,
        schedule.control_schedule(), std::move(mechanism_plan)));
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
           "mechanics completed before the short authored scenario horizon");
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

void verify_fresh_layout_and_first_state(const AuthoredEngineFixture &request,
                                         const LegacyMechanismStep &mechanics,
                                         const LegacyLowOrderGasStep &gas) {
    expect(gas.rate == RationalRateHz{10000, 1} && gas.sample_index == 0 &&
               gas.step_end_index == 1 && gas.timestamp_tick == 1,
           "fresh gas step has the wrong fixed-rate clock");
    expect(gas.gas_volumes.size() == 22U && gas.flow_edges.size() == 34U &&
               gas.cylinders.size() == 6U && gas.exhaust_routes.size() == 2U,
           "fresh authored gas layout has the wrong canonical entity counts");
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

    const auto &core = low_order_core(request);
    for (std::size_t index = 0; index < gas.cylinders.size(); ++index) {
        const auto &cylinder = gas.cylinders[index];
        const auto &declared = core.mechanism.cylinders[index].topology;
        expect(cylinder.cylinder_id == declared.cylinder_id &&
                   cylinder.intake_port_id == declared.intake_port_id &&
                   cylinder.exhaust_port_id == declared.exhaust_port_id &&
                   cylinder.intake_runner_volume_id ==
                       declared.intake_runner_volume_id &&
                   cylinder.chamber_volume_id == declared.chamber_volume_id &&
                   cylinder.exhaust_primary_volume_id ==
                       declared.exhaust_primary_volume_id &&
                   cylinder.exhaust_route_id == declared.exhaust_route_id,
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
        const auto &declared = core.gas_path.exhaust_routes[index].topology;
        expect(route.route_id == declared.route_id &&
                   route.collector_volume_id == declared.collector_volume_id &&
                   route.collector_outlet_edge_id ==
                       declared.collector_outlet_edge_id &&
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
           same_events(left.events, right.events) &&
           left.indicated_gas_torque_nm == right.indicated_gas_torque_nm;
}

void offset_cam_advance(LegacyCamshaftProfile &camshaft, double offset_rad) {
    std::visit([&](auto &shape) { shape.advance_rad.value += offset_rad; },
               camshaft.shape);
}

void configure_vtec_alternate(AuthoredEngineFixture &request, bool distinct_alternate,
                              bool force_alternate_active) {
    auto &core = engine_sim_offline::test::low_order_core(request.engine);
    LegacyVtecAlternateCamProfile alternate;
    alternate.intake = core.valvetrain.intake;
    alternate.exhaust = core.valvetrain.exhaust;
    if (distinct_alternate) {
        offset_cam_advance(alternate.intake, 0.35);
        offset_cam_advance(alternate.exhaust, 0.35);
    }

    alternate.activation.minimum_engine_speed_rad_s =
        core.ignition.timing_curve_triangle_radius_rad_s;
    alternate.activation.minimum_engine_speed_rad_s.value = 0.0;
    alternate.activation.minimum_mean_manifold_pressure_pa_abs =
        core.gas_path.intake.plenum_volume_m3;
    alternate.activation.minimum_mean_manifold_pressure_pa_abs.value = 1.0;
    alternate.activation.minimum_throttle_linkage_opening_01 =
        std::get<DirectThrottleControllerV1>(core.throttle_controller).gamma;
    alternate.activation.minimum_throttle_linkage_opening_01.value =
        force_alternate_active ? 0.0 : 0.8;
    core.valvetrain.alternate.emplace(std::move(alternate));
}

[[nodiscard]] const LegacyMechanismStep &advance_mechanics(CompiledSessions &sessions,
                                                           std::uint64_t sample_index) {
    auto result = sessions.mechanics.advance();
    return require_mechanics_step(result, sample_index);
}

[[nodiscard]] const LegacyLowOrderGasStep &
advance_gas(CompiledSessions &sessions, const LegacyMechanismStep &mechanics,
            std::uint64_t sample_index) {
    auto result = sessions.gas.advance(mechanics);
    return require_gas_step(result, sample_index);
}

void test_vtec_selects_one_coherent_immutable_cam_pair(
    const AuthoredEngineFixture &canonical) {
    auto base_request = make_short_request(canonical);

    auto forced_base_request = base_request;
    configure_vtec_alternate(forced_base_request, true, false);

    auto equal_active_request = base_request;
    configure_vtec_alternate(equal_active_request, false, true);

    auto alternate_active_request = base_request;
    configure_vtec_alternate(alternate_active_request, true, true);

    auto alternate_fixed_request = base_request;
    auto &alternate_fixed_core =
        engine_sim_offline::test::low_order_core(alternate_fixed_request.engine);
    const auto &alternate_source =
        *engine_sim_offline::test::low_order_core(alternate_active_request.engine)
             .valvetrain.alternate;
    alternate_fixed_core.valvetrain.intake = alternate_source.intake;
    alternate_fixed_core.valvetrain.exhaust = alternate_source.exhaust;

    auto base = compile_sessions(base_request);
    auto forced_base = compile_sessions(forced_base_request);
    auto equal_active = compile_sessions(equal_active_request);
    auto alternate_active = compile_sessions(alternate_active_request);
    auto alternate_fixed = compile_sessions(alternate_fixed_request);

    bool observed_intake_difference = false;
    bool observed_exhaust_difference = false;
    constexpr std::uint64_t kProofFrameCount = 512U;
    for (std::uint64_t sample_index = 0; sample_index < kProofFrameCount;
         ++sample_index) {
        const auto &base_mechanics = advance_mechanics(base, sample_index);
        const auto &forced_base_mechanics =
            advance_mechanics(forced_base, sample_index);
        const auto &equal_active_mechanics =
            advance_mechanics(equal_active, sample_index);
        const auto &alternate_active_mechanics =
            advance_mechanics(alternate_active, sample_index);
        const auto &alternate_fixed_mechanics =
            advance_mechanics(alternate_fixed, sample_index);
        expect(same_mechanics_step(base_mechanics, forced_base_mechanics) &&
                   same_mechanics_step(base_mechanics, equal_active_mechanics) &&
                   same_mechanics_step(base_mechanics, alternate_active_mechanics) &&
                   same_mechanics_step(base_mechanics, alternate_fixed_mechanics),
               "VTEC-only fixture changed mechanics before gas selection");
        expect(base_mechanics.requested_throttle_01 > 0.8 &&
                   1.0 - base_mechanics.resolved_engine_throttle_01 < 0.8,
               "forced-base fixture no longer distinguishes requested throttle from "
               "pristine throttle-linkage opening");

        const auto &base_gas = advance_gas(base, base_mechanics, sample_index);
        const auto &forced_base_gas =
            advance_gas(forced_base, forced_base_mechanics, sample_index);
        const auto &equal_active_gas =
            advance_gas(equal_active, equal_active_mechanics, sample_index);
        const auto &alternate_active_gas =
            advance_gas(alternate_active, alternate_active_mechanics, sample_index);
        const auto &alternate_fixed_gas =
            advance_gas(alternate_fixed, alternate_fixed_mechanics, sample_index);

        expect(same_gas_step(base_gas, forced_base_gas),
               "failed VTEC gate changed the standard base-cam gas transaction");
        expect(same_gas_step(base_gas, equal_active_gas),
               "active equal VTEC cams changed the base-cam gas transaction");
        expect(same_gas_step(alternate_fixed_gas, alternate_active_gas),
               "active VTEC did not use one coherent alternate pair for the entire "
               "gas transaction");

        for (std::size_t cylinder_index = 0; cylinder_index < base_gas.cylinders.size();
             ++cylinder_index) {
            const auto &base_valves = base_gas.cylinders[cylinder_index].valves;
            const auto &alternate_valves =
                alternate_active_gas.cylinders[cylinder_index].valves;
            observed_intake_difference =
                observed_intake_difference ||
                base_valves.intake_lift_m != alternate_valves.intake_lift_m;
            observed_exhaust_difference =
                observed_exhaust_difference ||
                base_valves.exhaust_lift_m != alternate_valves.exhaust_lift_m;
        }
    }

    expect(observed_intake_difference && observed_exhaust_difference,
           "distinct alternate intake and exhaust cams never diverged from the "
           "base pair");
}

void test_short_authored_fresh_state_and_deterministic_activity(
    const AuthoredEngineFixture &canonical) {
    const auto request = make_short_request(canonical);
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
           "short authored run did not exercise both valvetrains and real gas flow");
    expect(coverage.spark_count > 0U &&
               coverage.ignition_result_count == coverage.spark_count,
           "short authored run did not exercise paired spark and ignition events");
    expect(coverage.accepted_ignition && coverage.combustion_heat,
           "short authored run did not reach accepted, heat-releasing combustion");
}

void test_certified_radial_gas_uses_common_mechanism_coordinates(
    const AuthoredEngineFixture &canonical) {
    auto request = make_radial_gas_request(canonical);
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanism_plan = require_mechanism_plan(request);
    const auto *radial_plan =
        one_level_master_rod_mechanism_kinematics_plan(mechanism_plan);
    expect(radial_plan != nullptr,
           "radial gas fixture did not compile a master-rod mechanism plan");
    const auto random_plan = require_random_plan(request);
    auto gas = require_gas(CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario, random_plan,
        schedule.control_schedule(), mechanism_plan));

    bool observed_chamber_work = false;
    for (std::uint64_t sample_index = 0U; sample_index < kRadialGasStepCount;
         ++sample_index) {
        const auto mechanics = make_radial_mechanism_step(*radial_plan, sample_index);
        auto result = gas.advance(mechanics);
        const auto &step = require_gas_step(result, sample_index);

        for (const auto &volume : step.gas_volumes) {
            if (!volume.physically_resolved) {
                continue;
            }
            expect(std::isfinite(volume.cell.amount_mol) &&
                       volume.cell.amount_mol > 0.0 &&
                       std::isfinite(volume.cell.thermal_energy_j) &&
                       volume.cell.thermal_energy_j > 0.0 &&
                       std::isfinite(volume.cell.volume_m3) &&
                       volume.cell.volume_m3 > 0.0 &&
                       std::isfinite(legacy_gas_pressure_pa(volume.cell)) &&
                       legacy_gas_pressure_pa(volume.cell) > 0.0 &&
                       std::isfinite(legacy_gas_temperature_k(volume.cell)) &&
                       legacy_gas_temperature_k(volume.cell) > 0.0,
                   "radial gas advance published a nonfinite physical volume");
        }

        double expected_torque_sum_nm = 0.0;
        for (std::size_t index = 0; index < mechanics.cylinders.size(); ++index) {
            const auto &mechanism_cylinder = mechanics.cylinders[index];
            const auto chamber = std::ranges::find_if(
                step.gas_volumes, [&](const LegacyGasVolumeStepState &volume) {
                    return volume.gas_volume_id ==
                           step.cylinders[index].chamber_volume_id;
                });
            expect(chamber != step.gas_volumes.end(),
                   "radial gas cylinder lost its chamber volume");
            expect(chamber->cell.volume_m3 ==
                       mechanism_cylinder.chamber_volume_m3,
                   "radial chamber work did not apply the evaluator volume");

            const auto initial = evaluate_one_level_master_rod_plan(
                *radial_plan, index, 0.0, 0.0);
            expect(initial.valid,
                   "certified radial plan lost its initial chamber sample");
            if (initial.chamber_volume_m3 !=
                mechanism_cylinder.chamber_volume_m3) {
                const auto initial_cell = legacy_initialize_gas_cell(
                    request.scenario.ambient.pressure_pa_abs.value,
                    initial.chamber_volume_m3,
                    request.scenario.initial_thermal_state.gas_temperature_k.value,
                    {0.0, 1.0, 0.0});
                observed_chamber_work =
                    observed_chamber_work ||
                    chamber->cell.thermal_energy_j != initial_cell.thermal_energy_j;
            }

            const double expected_torque_nm =
                (legacy_gas_pressure_pa(chamber->cell) -
                 request.scenario.ambient.pressure_pa_abs.value) *
                mechanism_cylinder.dvolume_dtheta_m3_per_rad;
            expect(step.cylinders[index].indicated_gas_torque_nm ==
                       expected_torque_nm,
                   "radial cylinder torque did not use common dV/dtheta");
            expected_torque_sum_nm += expected_torque_nm;
        }
        expect(std::isfinite(step.indicated_gas_torque_nm) &&
                   step.indicated_gas_torque_nm == expected_torque_sum_nm,
               "radial aggregate indicated torque changed order or became "
               "nonfinite");
    }
    expect(observed_chamber_work,
           "radial gas run never changed chamber energy from its fresh state");
    expect(!gas.faulted() && gas.produced_sample_count() == kRadialGasStepCount,
           "radial gas session did not reach its finite prescribed horizon");

    auto non_prescribed = make_radial_gas_request(canonical);
    const double initial_theta_rad =
        low_order_core(non_prescribed)
            .mechanism.crank.crank_tdc_reference_rad.value;
    non_prescribed.scenario.mode =
        HeldSpeed{{kRadialGasRpm, {}}, {initial_theta_rad, {}}, {0.35, {}}};
    auto held_schedule = require_schedule(
        compile_kinematic_scenario_schedule(non_prescribed.scenario));
    auto held_plan = require_mechanism_plan(non_prescribed);
    const auto held_random_plan = require_random_plan(non_prescribed);
    auto held_result = CoreRuntimeFactory::compile_gas(
        non_prescribed.engine, low_order_core(non_prescribed),
        non_prescribed.scenario, held_random_plan, held_schedule.control_schedule(),
        std::move(held_plan));
    const auto *held_report = std::get_if<ValidationReport>(&held_result);
    expect(held_report != nullptr &&
               std::ranges::any_of(
                   held_report->issues,
                   [](const ContractIssue &issue) {
                       return issue.code == ContractIssueCode::unsupported_value &&
                              issue.path == "scenario.mode";
                   }),
           "radial gas admitted motion outside finite prescribed kinematics");
}

void expect_gas_compile_rejected(const AuthoredEngineFixture &request,
                                 std::string_view expected_path,
                                 std::string_view context) {
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanism_plan = require_mechanism_plan(request);
    const auto random_plan = require_random_plan(request);
    auto result = CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario, random_plan,
        schedule.control_schedule(), std::move(mechanism_plan));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, std::string{context} + " compiled successfully");
    const bool has_expected_issue = std::any_of(
        report->issues.begin(), report->issues.end(), [&](const ContractIssue &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    expect(has_expected_issue,
           std::string{context} + " rejection omitted the responsible path");
}

void expect_random_plan_rejected(const AuthoredEngineFixture &request,
                                 const RandomPlan &random_plan,
                                 std::string_view expected_path,
                                 std::string_view context) {
    auto schedule =
        require_schedule(compile_kinematic_scenario_schedule(request.scenario));
    auto mechanism_plan = require_mechanism_plan(request);
    auto result = CoreRuntimeFactory::compile_gas(
        request.engine, low_order_core(request), request.scenario, random_plan,
        schedule.control_schedule(), std::move(mechanism_plan));
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr, std::string{context} + " compiled successfully");
    const bool has_expected_issue =
        std::ranges::any_of(report->issues, [&](const ContractIssue &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    expect(has_expected_issue,
           std::string{context} + " rejection omitted the responsible path");
}

void test_gas_method_admission_rejection(const AuthoredEngineFixture &canonical) {
    {
        auto request = make_short_request(canonical);
        request.engine.methods.gas_exchange.value.version += 1U;
        expect_gas_compile_rejected(request, "engine.methods.gas_exchange",
                                    "unsupported gas method");
    }

    {
        auto request = make_short_request(canonical);
        engine_sim_offline::test::low_order_core(request.engine)
            .fuel.lbv_multiplier.value = 0.0;
        expect_gas_compile_rejected(request, "engine.physics_profile.fuel",
                                    "zero flame-speed multiplier");
    }

    {
        const auto request = make_short_request(canonical);
        auto random_plan = require_random_plan(request);
        ++random_plan.public_seed;
        expect_random_plan_rejected(request, random_plan, "random_plan.public_seed",
                                    "foreign-scenario random plan");
    }

    {
        const auto request = make_short_request(canonical);
        auto random_plan = require_random_plan(request);
        random_plan.component_seeds.erase(random_plan.component_seeds.begin());
        expect_random_plan_rejected(request, random_plan, "random_plan.component_seeds",
                                    "incomplete combustion random plan");
    }

    {
        auto request = make_short_request(canonical);
        auto mechanism_plan = require_mechanism_plan(request);
        const double initial_theta_rad =
            low_order_core(request).mechanism.crank.crank_tdc_reference_rad.value;
        request.scenario.mode =
            HeldSpeed{{kShortRunRpm, {}}, {initial_theta_rad, {}}, {0.85, {}}};
        auto schedule =
            require_schedule(compile_kinematic_scenario_schedule(request.scenario));
        const auto random_plan = require_random_plan(request);
        auto gas = require_gas(CoreRuntimeFactory::compile_gas(
            request.engine, low_order_core(request), request.scenario, random_plan,
            schedule.control_schedule(), std::move(mechanism_plan)));
        expect(gas.produced_sample_count() == 0U,
               "fresh held-speed gas session published samples during admission");
    }
}

void test_length_authored_collector_geometry_admission(
    const AuthoredEngineFixture &canonical) {
    auto request = make_short_request(canonical);
    auto &route = engine_sim_offline::test::low_order_core(request.engine)
                      .gas_path.exhaust_routes.front()
                      .parameters;
    route.collector_cross_section_area_m2.value = 0.0040715040790526395;
    route.exhaust_system_length_m.value = 1.97;
    route.collector_volume_m3.value = route.exhaust_system_length_m.value *
                                      route.collector_cross_section_area_m2.value;
    expect(route.collector_volume_m3.value /
                   route.collector_cross_section_area_m2.value !=
               route.exhaust_system_length_m.value,
           "length-authored collector fixture unexpectedly round-tripped through "
           "division");

    static_cast<void>(compile_sessions(request));
}

void run_tests(const AuthoredEngineFixture &canonical) {
    test_vtec_selects_one_coherent_immutable_cam_pair(canonical);
    test_short_authored_fresh_state_and_deterministic_activity(canonical);
    test_certified_radial_gas_uses_common_mechanism_coordinates(canonical);
    test_length_authored_collector_geometry_admission(canonical);
    test_gas_method_admission_rejection(canonical);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const auto canonical =
            engine_sim_offline::test::load_canonical_authored_engine_fixture(
                std::filesystem::canonical(argv[1]));
        run_tests(canonical);
    } catch (const std::exception &error) {
        std::cerr << "legacy_low_order_gas_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
