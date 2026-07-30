#include "engine_sim_offline/contract.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract::test {
namespace {

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_issue(const ValidationReport &report, ContractIssueCode code,
               std::string_view path) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.code == code && issue.path == path;
    });
}

template <class CylinderRange, class PortRange, class VolumeRange, class EdgeRange,
          class RouteRange>
concept CanBorrowCaptureLayout =
    requires(CylinderRange &&cylinders, PortRange &&ports, VolumeRange &&volumes,
             EdgeRange &&edges, RouteRange &&routes) {
        CaptureLayoutView::borrow_for_callback(
            EngineId{1}, std::forward<CylinderRange>(cylinders),
            std::forward<PortRange>(ports), std::forward<VolumeRange>(volumes),
            std::forward<EdgeRange>(edges), std::forward<RouteRange>(routes));
    };

template <class SpeedRange, class CylinderRange>
concept CanBorrowReferenceParity =
    requires(SpeedRange &&speeds, CylinderRange &&cylinders) {
        ReferenceParityBlockView::borrow_for_callback(
            std::forward<SpeedRange>(speeds), std::forward<CylinderRange>(cylinders));
    };

template <class OffsetRange, class EventRange>
concept CanBorrowEventJournal = requires(OffsetRange &&offsets, EventRange &&events) {
    EventJournalView::borrow_for_callback(std::forward<OffsetRange>(offsets),
                                          std::forward<EventRange>(events));
};

template <class EngineRange, class CylinderRange, class PortRange, class VolumeRange,
          class EdgeRange, class RouteRange>
concept CanBorrowCaptureBlock =
    requires(CaptureLayoutView layout, EventJournalView journal, EngineRange &&engine,
             CylinderRange &&cylinders, PortRange &&ports, VolumeRange &&volumes,
             EdgeRange &&edges, RouteRange &&routes) {
        CaptureBlockView::borrow_for_callback(
            layout, CaptureClock{}, 1, 1, 1, std::forward<EngineRange>(engine),
            std::forward<CylinderRange>(cylinders), std::forward<PortRange>(ports),
            std::forward<VolumeRange>(volumes), std::forward<EdgeRange>(edges),
            std::forward<RouteRange>(routes), journal);
    };

using CylinderIds = std::array<CylinderId, 1>;
using PortIdentities = std::array<PortIdentity, 1>;
using VolumeIdentities = std::array<GasVolumeIdentity, 1>;
using EdgeIdentities = std::array<FlowEdgeIdentity, 1>;
using RouteIdentities = std::array<RouteIdentity, 1>;
using ParitySpeeds = std::array<double, 1>;
using ParityCylinders = std::array<ReferenceParityCylinderSample, 1>;
using EventOffsets = std::array<std::uint32_t, 2>;
using Events = std::array<EngineEvent, 0>;
using EngineSamples = std::array<EngineCaptureSample, 1>;
using CylinderSamples = std::array<CylinderCaptureSample, 1>;
using PortSamples = std::array<PortCaptureSample, 1>;
using VolumeSamples = std::array<GasVolumeCaptureSample, 1>;
using EdgeSamples = std::array<FlowEdgeCaptureSample, 1>;
using RouteSamples = std::array<SourceRouteCaptureSample, 1>;

static_assert(
    CanBorrowCaptureLayout<CylinderIds &, PortIdentities &, VolumeIdentities &,
                           EdgeIdentities &, RouteIdentities &>);
static_assert(
    !CanBorrowCaptureLayout<std::vector<CylinderId>, PortIdentities &,
                            VolumeIdentities &, EdgeIdentities &, RouteIdentities &>);
static_assert(
    !CanBorrowCaptureLayout<std::span<const CylinderId>, PortIdentities &,
                            VolumeIdentities &, EdgeIdentities &, RouteIdentities &>);
static_assert(CanBorrowReferenceParity<ParitySpeeds &, ParityCylinders &>);
static_assert(!CanBorrowReferenceParity<std::vector<double>, ParityCylinders &>);
static_assert(CanBorrowEventJournal<EventOffsets &, Events &>);
static_assert(!CanBorrowEventJournal<std::vector<std::uint32_t>, Events &>);
static_assert(CanBorrowCaptureBlock<EngineSamples &, CylinderSamples &, PortSamples &,
                                    VolumeSamples &, EdgeSamples &, RouteSamples &>);
static_assert(!CanBorrowCaptureBlock<std::vector<EngineCaptureSample>,
                                     CylinderSamples &, PortSamples &, VolumeSamples &,
                                     EdgeSamples &, RouteSamples &>);

} // namespace

void run_capture_contract_tests() {
    const std::array cylinders{CylinderId{1}, CylinderId{2}};
    const std::array ports{
        PortIdentity{PortId{1}, CylinderId{1}, PortKind::exhaust},
    };
    const std::array volumes{
        GasVolumeIdentity{GasVolumeId{1}, GasVolumeKind::cylinder},
        GasVolumeIdentity{GasVolumeId{2}, GasVolumeKind::exhaust_primary},
    };
    const std::array edges{
        FlowEdgeIdentity{
            FlowEdgeId{1},
            GasVolumeId{1},
            GasVolumeId{2},
        },
    };
    const std::array routes{
        RouteIdentity{
            RouteId{1},
            SourceRouteKind::exhaust_outlet,
            GasVolumeId{2},
            std::nullopt,
            std::nullopt,
        },
    };
    const auto layout = CaptureLayoutView::borrow_for_callback(
        EngineId{1}, cylinders, ports, volumes, edges, routes);

    const auto mechanism = capture_validity_mask(CaptureValidity::mechanism);
    const auto thermodynamic =
        capture_validity_mask(CaptureValidity::thermodynamic_state);
    const auto composition = capture_validity_mask(CaptureValidity::composition);
    const auto gas_exchange = capture_validity_mask(CaptureValidity::gas_exchange);
    const auto torque = capture_validity_mask(CaptureValidity::torque);

    std::array<EngineCaptureSample, 2> engine{};
    for (std::size_t index = 0; index < engine.size(); ++index) {
        engine[index].step_end_index = index + 1;
        engine[index].validity = mechanism;
        engine[index].theta_rad = 0.1 * static_cast<double>(index + 1);
        engine[index].theta_cycle_rad = engine[index].theta_rad;
        engine[index].angular_speed_rad_s = 100.0;
        engine[index].engine_speed_rpm = 954.9;
        engine[index].requested_throttle_01 = 0.5;
        engine[index].resolved_engine_throttle_01 = 0.75;
        engine[index].intake_plate_position_01 = 0.75;
        engine[index].main_flow_multiplier_01 = 0.5;
        engine[index].limiter_enabled = true;
        engine[index].external_resisting_torque_nm = 15.0;
    }

    std::array<CylinderCaptureSample, 4> cylinder_samples{};
    for (auto &sample : cylinder_samples) {
        sample.validity = mechanism | thermodynamic | composition | torque;
        sample.chamber_volume_m3 = 0.0005;
        sample.pressure_pa_abs = 101325.0;
        sample.temperature_k = 400.0;
        sample.amount_mol = 0.01;
        sample.composition = {0.05, 0.74, 0.21};
        sample.indicated_gas_torque = {
            25.0,
            Availability::available,
            Completeness::complete,
            QuantityUnavailableReason::none,
            torque_term_mask(TorqueTerm::indicated_gas),
            0,
        };
    }

    std::array<PortCaptureSample, 2> port_samples{};
    for (auto &sample : port_samples) {
        sample.validity = gas_exchange;
        sample.pressure_pa_abs = 101325.0;
        sample.temperature_k = 500.0;
        sample.signed_mass_flow_kg_s = -0.01;
        sample.effective_flow_area_m2 = 0.0001;
        sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg =
            0.00002748668227937587;
        sample.valve_lift_m = 0.001;
    }

    std::array<GasVolumeCaptureSample, 4> volume_samples{};
    for (std::size_t index = 0; index < volume_samples.size(); ++index) {
        auto &sample = volume_samples[index];
        sample.validity = thermodynamic | composition;
        sample.volume_m3 = 0.001;
        sample.pressure_pa_abs = 101325.0 + static_cast<double>(index);
        sample.temperature_k = 400.0;
        sample.amount_mol = 0.02;
        sample.thermal_energy_j = 100.0;
        sample.composition = {0.05, 0.74, 0.21};
    }

    std::array<FlowEdgeCaptureSample, 2> edge_samples{};
    for (auto &sample : edge_samples) {
        sample.validity = gas_exchange;
        sample.signed_mass_flow_kg_s = -0.01;
    }

    std::array<SourceRouteCaptureSample, 2> route_samples{};
    for (auto &sample : route_samples) {
        sample = GasSourceRouteCaptureSample{
            gas_exchange, 101325.0, 500.0, 0.01, 0.001,
        };
    }

    const std::array<std::uint32_t, 3> no_event_offsets{0, 0, 0};
    const std::vector<EngineEvent> no_events;
    const auto empty_journal =
        EventJournalView::borrow_for_callback(no_event_offsets, no_events);
    const std::array filtered_rpm{954.9, 954.9};
    const std::array parity_cylinders{
        ReferenceParityCylinderSample{101325.0, 10.0, 5.0},
        ReferenceParityCylinderSample{101325.0, 9.0, 4.0},
        ReferenceParityCylinderSample{101325.0, 11.0, 4.0},
        ReferenceParityCylinderSample{101325.0, 12.0, 3.0},
    };
    const auto parity =
        ReferenceParityBlockView::borrow_for_callback(filtered_rpm, parity_cylinders);
    const CaptureClock parity_clock{
        {10000, 1},
        0,
        1,
        SamplePhase::post_step,
    };

    const auto make_block =
        [&](CaptureClock clock, std::uint32_t capacity, std::uint32_t event_capacity,
            EventJournalView journal,
            std::optional<ReferenceParityBlockView> reference_parity) {
            return CaptureBlockView::borrow_for_callback(
                layout, clock, 2, capacity, event_capacity, engine, cylinder_samples,
                port_samples, volume_samples, edge_samples, route_samples, journal,
                reference_parity);
        };

    const auto valid_block = make_block(parity_clock, 256, 38, empty_journal, parity);
    expect(validate(valid_block).ok(), "valid bounded CaptureBlock was rejected");
    expect(valid_block.declared_event_journal_capacity_records() == 38,
           "CaptureBlock lost its declared event-journal capacity");
    expect(valid_block.clock().timestamp_s(0) == 1.0 / 10000.0 &&
               valid_block.clock().timestamp_s(1) == 2.0 / 10000.0,
           "M3 post-step integer time grid is wrong");

    const auto *frame_one_volume_zero = valid_block.gas_volume_sample(1, 0);
    expect(frame_one_volume_zero == &volume_samples[2],
           "frame-major gas-volume accessor used the wrong flatten order");
    expect(valid_block.gas_volume_sample(2, 0) == nullptr &&
               valid_block.gas_volume_sample(0, 2) == nullptr,
           "frame-major accessor accepted an out-of-range coordinate");
    expect(valid_block.gas_source_route_sample(0, 0) != nullptr &&
               valid_block.mechanical_source_route_sample(0, 0) == nullptr,
           "typed source-route accessor confused gas and mechanical samples");

    EngineSpec bound_engine;
    bound_engine.id = EngineId{1};
    bound_engine.profile_id.value = "capture-binding-profile";
    bound_engine.cylinders.resize(2);
    bound_engine.cylinders[0].id = CylinderId{1};
    bound_engine.cylinders[1].id = CylinderId{2};
    bound_engine.ports.resize(1);
    bound_engine.ports[0].id = PortId{1};
    bound_engine.ports[0].cylinder_id = CylinderId{1};
    bound_engine.ports[0].kind.value = PortKind::exhaust;
    bound_engine.gas_volumes.resize(2);
    bound_engine.gas_volumes[0].id = GasVolumeId{1};
    bound_engine.gas_volumes[0].kind.value = GasVolumeKind::cylinder;
    bound_engine.gas_volumes[1].id = GasVolumeId{2};
    bound_engine.gas_volumes[1].kind.value = GasVolumeKind::exhaust_primary;
    bound_engine.flow_edges.resize(1);
    bound_engine.flow_edges[0].id = FlowEdgeId{1};
    bound_engine.flow_edges[0].endpoint_0_volume_id = GasVolumeId{1};
    bound_engine.flow_edges[0].endpoint_1_volume_id = GasVolumeId{2};
    bound_engine.routes.resize(1);
    bound_engine.routes[0].id = RouteId{1};
    bound_engine.routes[0].kind.value = SourceRouteKind::exhaust_outlet;
    bound_engine.routes[0].source_volume_id = GasVolumeId{2};

    RenderScenario bound_scenario;
    bound_scenario.engine_profile_id = "capture-binding-profile";
    bound_scenario.rates.capture = {10000, 1};
    bound_scenario.quality.value.capture_block_capacity_frames = 256;
    bound_scenario.quality.value.event_journal_capacity_records = 38;
    bound_scenario.total_duration_s.value = 1.0;

    expect(validate(valid_block, bound_engine, bound_scenario).ok(),
           "CaptureBlock rejected an exact engine/scenario binding");

    auto mismatched_engine = bound_engine;
    mismatched_engine.id = EngineId{2};
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics, "layout.engine_id"),
           "CaptureBlock accepted the wrong engine identity");

    mismatched_engine = bound_engine;
    std::swap(mismatched_engine.cylinders[0], mismatched_engine.cylinders[1]);
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics, "layout.cylinders[0]"),
           "CaptureBlock accepted a different cylinder order");

    mismatched_engine = bound_engine;
    mismatched_engine.ports[0].kind.value = PortKind::intake;
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics, "layout.ports[0].kind"),
           "CaptureBlock accepted a different port kind");

    mismatched_engine = bound_engine;
    mismatched_engine.gas_volumes[0].kind.value = GasVolumeKind::intake_runner;
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "layout.gas_volumes[0].kind"),
           "CaptureBlock accepted a different gas-volume kind");

    mismatched_engine = bound_engine;
    mismatched_engine.flow_edges[0].endpoint_0_volume_id = GasVolumeId{2};
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "layout.flow_edges[0].endpoint_0_volume_id"),
           "CaptureBlock accepted different flow-edge endpoints");

    mismatched_engine = bound_engine;
    mismatched_engine.routes[0].source_volume_id = GasVolumeId{1};
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "layout.routes[0].source_volume_id"),
           "CaptureBlock accepted a different physical-route source endpoint");

    mismatched_engine = bound_engine;
    mismatched_engine.routes[0].default_parent_route_id = RouteId{2};
    expect(has_issue(validate(valid_block, mismatched_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "layout.routes[0].default_parent_route_id"),
           "CaptureBlock accepted a different physical-route parent");

    auto mismatched_scenario = bound_scenario;
    mismatched_scenario.rates.capture = {20000, 1};
    expect(has_issue(validate(valid_block, bound_engine, mismatched_scenario),
                     ContractIssueCode::inconsistent_semantics, "clock.rate"),
           "CaptureBlock accepted a clock rate different from its scenario");

    mismatched_scenario = bound_scenario;
    mismatched_scenario.quality.value.capture_block_capacity_frames = 512;
    expect(has_issue(validate(valid_block, bound_engine, mismatched_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "declared_block_capacity_frames"),
           "CaptureBlock accepted a capacity different from scenario quality");

    mismatched_scenario = bound_scenario;
    mismatched_scenario.quality.value.event_journal_capacity_records = 39;
    expect(has_issue(validate(valid_block, bound_engine, mismatched_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "declared_event_journal_capacity_records"),
           "CaptureBlock accepted an event capacity different from scenario quality");

    mismatched_scenario = bound_scenario;
    mismatched_scenario.total_duration_s.value = 0.0001;
    expect(has_issue(validate(valid_block, bound_engine, mismatched_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "clock.first_sample_index"),
           "CaptureBlock accepted timestamps beyond the scenario duration");

    const CaptureClock final_post_step_clock{
        {10000, 1},
        9998,
        9999,
        SamplePhase::post_step,
    };
    expect(validate(
               make_block(final_post_step_clock, 256, 38, empty_journal, std::nullopt),
               bound_engine, bound_scenario)
               .ok(),
           "post-step block ending exactly at the scenario boundary was rejected");

    const CaptureClock exclusive_end_pre_step_clock{
        {10000, 1},
        10000,
        10000,
        SamplePhase::pre_step,
    };
    expect(has_issue(validate(make_block(exclusive_end_pre_step_clock, 256, 38,
                                         empty_journal, std::nullopt),
                              bound_engine, bound_scenario),
                     ContractIssueCode::inconsistent_semantics,
                     "clock.first_sample_index"),
           "pre-step sample at the half-open scenario end was accepted");

    const auto capacity_report =
        validate(make_block(parity_clock, 1, 38, empty_journal, parity));
    expect(
        capacity_report.issues.size() == 1 &&
            has_issue(capacity_report, ContractIssueCode::invalid_value, "frame_count"),
        "CaptureBlock capacity regression did not fail specifically on "
        "declared capacity");

    const auto zero_event_capacity_report =
        validate(make_block(parity_clock, 256, 0, empty_journal, parity));
    expect(has_issue(zero_event_capacity_report, ContractIssueCode::invalid_value,
                     "declared_event_journal_capacity_records"),
           "CaptureBlock accepted a zero event-journal capacity");

    engine[0].torque.instantaneous_indicated_gas = {
        25.0,
        Availability::available,
        Completeness::incomplete,
        QuantityUnavailableReason::none,
        torque_term_mask(TorqueTerm::indicated_gas),
        0,
    };
    expect(has_issue(validate(valid_block), ContractIssueCode::inconsistent_semantics,
                     "engine[0].torque"),
           "available engine torque without torque validity was accepted");
    engine[0].torque.instantaneous_indicated_gas = {};

    engine[0].validity |= torque;
    expect(has_issue(validate(valid_block), ContractIssueCode::inconsistent_semantics,
                     "engine[0].torque"),
           "engine torque validity without an available quantity was accepted");
    engine[0].torque.instantaneous_indicated_gas = {
        25.0,
        Availability::available,
        Completeness::incomplete,
        QuantityUnavailableReason::none,
        torque_term_mask(TorqueTerm::indicated_gas),
        0,
    };
    expect(validate(valid_block).ok(),
           "engine torque validity rejected meaningful available telemetry");
    engine[0].torque.instantaneous_indicated_gas = {};
    engine[0].validity = mechanism;

    engine[0].torque.starter = {
        12.0,
        Availability::available,
        Completeness::complete,
        QuantityUnavailableReason::none,
        torque_term_mask(TorqueTerm::starter),
        0,
    };
    expect(has_issue(validate(valid_block), ContractIssueCode::inconsistent_semantics,
                     "engine[0].torque"),
           "available starter torque without torque validity was ignored");
    engine[0].validity |= torque;
    expect(validate(valid_block).ok(),
           "starter-only torque telemetry was not recognized as meaningful");
    engine[0].torque.starter = {};
    engine[0].validity = mechanism;

    engine[0].external_resisting_torque_nm = -1.0;
    expect(has_issue(validate(valid_block), ContractIssueCode::invalid_value,
                     "engine[0].external_resisting_torque_nm"),
           "negative captured external resisting torque was accepted");
    engine[0].external_resisting_torque_nm = std::numeric_limits<double>::quiet_NaN();
    expect(
        has_issue(validate(valid_block), ContractIssueCode::invalid_value, "engine[0]"),
        "nonfinite captured external resisting torque was accepted");
    engine[0].external_resisting_torque_nm = 15.0;

    cylinder_samples[0].composition.oxygen = 0.20;
    expect(has_issue(validate(valid_block), ContractIssueCode::invalid_value,
                     "cylinders[0].composition"),
           "positive mixture whose fractions do not sum to one was accepted");
    cylinder_samples[0].composition.oxygen = 0.21;

    cylinder_samples[0].amount_mol = 0.0;
    expect(has_issue(validate(valid_block), ContractIssueCode::invalid_value,
                     "cylinders[0].composition"),
           "zero-amount mixture with nonzero fractions was accepted");
    cylinder_samples[0].amount_mol = 0.01;

    port_samples[0].effective_molar_flow_conductance_m2_sqrt_mol_per_kg = -1.0;
    expect(
        has_issue(validate(valid_block), ContractIssueCode::invalid_value, "ports[0]"),
        "negative effective valve conductance was accepted");
    port_samples[0].effective_molar_flow_conductance_m2_sqrt_mol_per_kg =
        0.00002748668227937587;

    route_samples[0] = MechanicalSourceRouteCaptureSample{};
    expect(has_issue(validate(valid_block), ContractIssueCode::inconsistent_semantics,
                     "source_routes[0]"),
           "mechanical payload masquerading as a gas route was accepted");
    route_samples[0] = GasSourceRouteCaptureSample{
        gas_exchange, 101325.0, 500.0, 0.01, 0.001,
    };

    engine[1].step_end_index = 3;
    expect(has_issue(validate(valid_block), ContractIssueCode::inconsistent_semantics,
                     "engine[1].step_end_index"),
           "M3 sample-to-step mapping drift was accepted");
    engine[1].step_end_index = 2;

    auto wrong_clock = parity_clock;
    wrong_clock.rate = {20000, 1};
    expect(has_issue(validate(make_block(wrong_clock, 256, 38, empty_journal, parity)),
                     ContractIssueCode::inconsistent_semantics, "clock"),
           "non-10k M3 reference-parity clock was accepted");

    wrong_clock = parity_clock;
    wrong_clock.first_timestamp_tick = 0;
    expect(has_issue(validate(make_block(wrong_clock, 256, 38, empty_journal, parity)),
                     ContractIssueCode::inconsistent_semantics,
                     "clock.first_timestamp_tick"),
           "pre-step timestamp masquerading as M3 post-step capture was "
           "accepted");

    auto invalid_pressure_samples = parity_cylinders;
    invalid_pressure_samples[0].dynamic_pressure_reverse_pa = -1.0;
    const auto invalid_pressure_parity = ReferenceParityBlockView::borrow_for_callback(
        filtered_rpm, invalid_pressure_samples);
    expect(has_issue(validate(make_block(parity_clock, 256, 38, empty_journal,
                                         invalid_pressure_parity)),
                     ContractIssueCode::invalid_value, "reference_parity.cylinders[0]"),
           "negative M3 directional dynamic pressure was accepted");

    const std::array no_change_event{
        EngineEvent{
            0,
            0,
            LimiterStateChanged{false, false, false, 0.0},
        },
    };
    const std::array<std::uint32_t, 3> one_event_offsets{0, 1, 1};
    const auto no_change_journal =
        EventJournalView::borrow_for_callback(one_event_offsets, no_change_event);
    expect(has_issue(
               validate(make_block(parity_clock, 256, 38, no_change_journal, parity)),
               ContractIssueCode::inconsistent_semantics, "event_journal.events[0]"),
           "limiter transition with unchanged state was accepted");

    const std::array out_of_order_events{
        EngineEvent{
            0,
            0,
            IgnitionAccepted{CylinderId{1}, 0.8, 1.0},
        },
        EngineEvent{
            0,
            1,
            SparkCrossing{CylinderId{1}, 0.0, 0.1, 0.1, 0.2, 0.1},
        },
    };
    const std::array<std::uint32_t, 3> two_event_offsets{0, 2, 2};
    const auto out_of_order_journal =
        EventJournalView::borrow_for_callback(two_event_offsets, out_of_order_events);
    expect(has_issue(validate(make_block(parity_clock, 256, 38, out_of_order_journal,
                                         parity)),
                     ContractIssueCode::inconsistent_semantics,
                     "event_journal.events[1].payload"),
           "out-of-category-order M3 journal was accepted");
    expect(has_issue(
               validate(make_block(parity_clock, 256, 1, out_of_order_journal, parity)),
               ContractIssueCode::inconsistent_shape, "event_journal.events"),
           "CaptureBlock accepted more events than its declared journal capacity");

    const std::array wrong_cylinder_order_events{
        EngineEvent{
            0,
            0,
            SparkCrossing{CylinderId{2}, 0.0, 0.1, 0.1, 0.2, 0.1},
        },
        EngineEvent{
            0,
            1,
            SparkCrossing{CylinderId{1}, 0.0, 0.1, 0.1, 0.2, 0.1},
        },
    };
    const auto wrong_cylinder_order_journal = EventJournalView::borrow_for_callback(
        two_event_offsets, wrong_cylinder_order_events);
    expect(has_issue(validate(make_block(parity_clock, 256, 38,
                                         wrong_cylinder_order_journal, parity)),
                     ContractIssueCode::inconsistent_semantics,
                     "event_journal.events[1].cylinder_id"),
           "out-of-cylinder-order M3 spark journal was accepted");

    const std::array bad_substep_event{
        EngineEvent{
            0,
            0,
            FlameExtinguished{
                CylinderId{1},
                8,
                FlameExtinctionReason::intake_transfer,
            },
        },
    };
    const auto bad_substep_journal =
        EventJournalView::borrow_for_callback(one_event_offsets, bad_substep_event);
    expect(has_issue(
               validate(make_block(parity_clock, 256, 38, bad_substep_journal, parity)),
               ContractIssueCode::invalid_value,
               "event_journal.events[0].gas_substep_index"),
           "M3 flame extinction outside gas substeps 0..7 was accepted");

    std::array<EngineEvent, 2> duplicate_limiter_events{
        EngineEvent{
            0,
            0,
            LimiterStateChanged{false, true, true, 0.5},
        },
        EngineEvent{
            0,
            1,
            LimiterStateChanged{true, false, false, 0.0},
        },
    };
    const auto duplicate_limiter_journal = EventJournalView::borrow_for_callback(
        two_event_offsets, duplicate_limiter_events);
    expect(has_issue(validate(make_block(parity_clock, 256, 38,
                                         duplicate_limiter_journal, parity)),
                     ContractIssueCode::inconsistent_shape, "event_journal"),
           "more than one M3 limiter transition per frame was accepted");

    const std::array cyclic_routes{
        RouteIdentity{
            RouteId{1},
            SourceRouteKind::exhaust_outlet,
            GasVolumeId{2},
            RouteId{2},
            std::nullopt,
        },
        RouteIdentity{
            RouteId{2},
            SourceRouteKind::intake_inlet,
            GasVolumeId{1},
            RouteId{1},
            std::nullopt,
        },
    };
    const auto cyclic_layout = CaptureLayoutView::borrow_for_callback(
        EngineId{1}, cylinders, ports, volumes, edges, cyclic_routes);
    expect(has_issue(validate(cyclic_layout), ContractIssueCode::inconsistent_semantics,
                     "routes"),
           "cyclic source-route parent graph was accepted");

    const std::array unknown_route{
        RouteIdentity{
            RouteId{1},
            static_cast<SourceRouteKind>(255),
            std::nullopt,
            std::nullopt,
            std::nullopt,
        },
    };
    const auto unknown_route_layout = CaptureLayoutView::borrow_for_callback(
        EngineId{1}, cylinders, ports, volumes, edges, unknown_route);
    expect(has_issue(validate(unknown_route_layout),
                     ContractIssueCode::unsupported_value, "routes[0].kind"),
           "unknown route kind entered CaptureBlock layout");
}

} // namespace engine_sim_offline::contract::test
