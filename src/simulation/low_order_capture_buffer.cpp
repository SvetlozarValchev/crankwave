#include "simulation/low_order_capture_buffer.hpp"

#include "simulation/legacy_gas_primitives.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace engine_sim_offline::simulation::detail {
namespace {

constexpr auto kMechanism =
    contract::capture_validity_mask(contract::CaptureValidity::mechanism);
constexpr auto kThermodynamic =
    contract::capture_validity_mask(contract::CaptureValidity::thermodynamic_state);
constexpr auto kComposition =
    contract::capture_validity_mask(contract::CaptureValidity::composition);
constexpr auto kGasExchange =
    contract::capture_validity_mask(contract::CaptureValidity::gas_exchange);
constexpr auto kCombustion =
    contract::capture_validity_mask(contract::CaptureValidity::combustion);
constexpr auto kTorque =
    contract::capture_validity_mask(contract::CaptureValidity::torque);

[[nodiscard]] contract::MixtureFractions
capture_mixture(const LegacyGasMixture &mixture) noexcept {
    return {
        mixture.fuel_fraction,
        mixture.inert_fraction,
        mixture.oxygen_fraction,
    };
}

[[nodiscard]] LowOrderCaptureBufferFault shape_fault(std::string detail) {
    LowOrderCaptureBufferFault result;
    result.kind = contract::FailureKind::contract_violation;
    result.detail_code = "low-order-capture-step-shape-mismatch";
    result.state_summary = std::move(detail);
    return result;
}

[[nodiscard]] bool add_overflows(std::size_t left, std::size_t right) noexcept {
    return right > std::numeric_limits<std::size_t>::max() - left;
}

[[nodiscard]] double capture_mass_flow_kg_s(double signed_amount_mol) noexcept {
    // Operation order is part of M3: convert the outer-step amount to mass first,
    // then divide by the exact outer-step duration.
    return (signed_amount_mol * kLegacyAirMolarMassKgPerMol) / (1.0 / 10000.0);
}

} // namespace

LowOrderCaptureBuffer::LowOrderCaptureBuffer(LowOrderCaptureBufferPlan plan)
    : plan_(std::move(plan)) {
    const auto capacity =
        static_cast<std::size_t>(plan_.declared_block_capacity_frames);
    engine_.reserve(capacity);
    cylinders_.reserve(capacity * plan_.cylinders.size());
    ports_.reserve(capacity * plan_.ports.size());
    gas_volumes_.reserve(capacity * plan_.gas_volumes.size());
    flow_edges_.reserve(capacity * plan_.flow_edges.size());
    routes_.reserve(capacity * plan_.routes.size());
    event_offsets_.reserve(capacity + 1U);
    events_.reserve(plan_.declared_event_capacity_records);
    filtered_engine_speed_rpm_.reserve(capacity);
    parity_cylinders_.reserve(capacity * plan_.cylinders.size());
}

void LowOrderCaptureBuffer::begin_block(std::uint64_t first_sample_index) noexcept {
    first_sample_index_ = first_sample_index;
    frame_count_ = 0;
    engine_.clear();
    cylinders_.clear();
    ports_.clear();
    gas_volumes_.clear();
    flow_edges_.clear();
    routes_.clear();
    event_offsets_.clear();
    events_.clear();
    filtered_engine_speed_rpm_.clear();
    parity_cylinders_.clear();
    event_offsets_.push_back(0U);
}

std::optional<LowOrderCaptureBufferFault>
LowOrderCaptureBuffer::append(const LegacyMechanismStep &mechanics,
                              const LegacyLowOrderGasStep &gas,
                              const contract::TorqueTelemetry &torque) {
    const auto expected_sample_index =
        first_sample_index_ + static_cast<std::uint64_t>(frame_count_);
    if (frame_count_ >= plan_.declared_block_capacity_frames ||
        mechanics.rate != plan_.rate || gas.rate != plan_.rate ||
        mechanics.sample_index != expected_sample_index ||
        gas.sample_index != expected_sample_index ||
        mechanics.step_end_index != expected_sample_index + 1U ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick ||
        mechanics.cylinders.size() != plan_.cylinders.size() ||
        gas.cylinders.size() != plan_.cylinders.size() ||
        gas.gas_volumes.size() != plan_.gas_volumes.size() ||
        gas.flow_edges.size() != plan_.flow_edges.size() ||
        gas.exhaust_routes.size() != plan_.route_bindings.size() ||
        plan_.cylinder_bindings.size() != plan_.cylinders.size() ||
        plan_.port_bindings.size() != plan_.ports.size() ||
        plan_.route_bindings.size() != plan_.routes.size()) {
        return shape_fault("mechanics, gas, capture clock, or entity shape diverged "
                           "from the compiled capture plan");
    }
    if (gas.events.size() > plan_.maximum_events_per_frame ||
        add_overflows(events_.size(), gas.events.size()) ||
        events_.size() + gas.events.size() > plan_.declared_event_capacity_records) {
        LowOrderCaptureBufferFault failure;
        failure.kind = contract::FailureKind::event_schedule_violation;
        failure.detail_code = "low-order-capture-event-capacity-exceeded";
        failure.state_summary =
            "composed events exceed the per-frame or block event-journal capacity";
        return failure;
    }

    for (std::size_t index = 0; index < plan_.cylinders.size(); ++index) {
        const auto expected = plan_.cylinders[index];
        if (mechanics.cylinders[index].cylinder_id != expected ||
            gas.cylinders[index].cylinder_id != expected) {
            auto failure = shape_fault(
                "mechanics and gas cylinder identity/order differ from capture");
            failure.cylinder_id = expected;
            return failure;
        }
    }
    for (std::size_t index = 0; index < plan_.gas_volumes.size(); ++index) {
        const auto &expected = plan_.gas_volumes[index];
        const auto &actual = gas.gas_volumes[index];
        if (actual.gas_volume_id != expected.id || actual.kind != expected.kind ||
            actual.physically_resolved ==
                (expected.kind == contract::GasVolumeKind::atmosphere)) {
            auto failure = shape_fault(
                "gas-volume identity, order, kind, or physical resolution differs "
                "from capture");
            failure.gas_volume_id = expected.id;
            return failure;
        }
    }
    for (std::size_t index = 0; index < plan_.flow_edges.size(); ++index) {
        const auto &expected = plan_.flow_edges[index];
        const auto &actual = gas.flow_edges[index];
        if (actual.flow_edge_id != expected.id ||
            actual.endpoint_0_volume_id != expected.endpoint_0_volume_id ||
            actual.endpoint_1_volume_id != expected.endpoint_1_volume_id) {
            auto failure = shape_fault(
                "gas edge identity, endpoint direction, or order differs from "
                "capture");
            failure.flow_edge_id = expected.id;
            return failure;
        }
    }
    for (std::size_t index = 0; index < gas.events.size(); ++index) {
        if (gas.events[index].ordinal_within_step != static_cast<std::uint8_t>(index)) {
            LowOrderCaptureBufferFault failure;
            failure.kind = contract::FailureKind::event_schedule_violation;
            failure.detail_code = "low-order-capture-event-order-invalid";
            failure.state_summary =
                "gas event ordinals are not contiguous execution order";
            return failure;
        }
    }

    contract::EngineCaptureSample engine;
    engine.step_end_index = mechanics.step_end_index;
    engine.validity = kMechanism | kGasExchange | kTorque;
    engine.theta_rad = mechanics.theta_unwrapped_rad;
    engine.theta_cycle_rad = mechanics.theta_cycle_rad;
    engine.angular_speed_rad_s = mechanics.angular_speed_rad_s;
    engine.angular_acceleration_rad_s2 = mechanics.angular_acceleration_rad_s2;
    engine.engine_speed_rpm = mechanics.engine_speed_rpm;
    engine.requested_throttle_01 = mechanics.requested_throttle_01;
    engine.resolved_engine_throttle_01 = mechanics.resolved_engine_throttle_01;
    engine.intake_plate_position_01 = mechanics.intake_plate_position_01;
    engine.main_flow_multiplier_01 = mechanics.main_flow_multiplier_01;
    engine.ignition_enabled = mechanics.operating_state.ignition_enabled;
    engine.fuel_enabled = mechanics.operating_state.fuel_enabled;
    engine.starter_enabled = mechanics.operating_state.starter_enabled;
    engine.dyno_enabled = mechanics.operating_state.dyno_enabled;
    engine.limiter_cut_active = mechanics.limiter_cut_active;
    engine.torque = torque;
    engine_.push_back(engine);

    for (std::size_t index = 0; index < plan_.cylinders.size(); ++index) {
        const auto &mechanism = mechanics.cylinders[index];
        const auto &gas_cylinder = gas.cylinders[index];
        const auto &binding = plan_.cylinder_bindings[index];
        if (binding.chamber_volume_index >= gas.gas_volumes.size() ||
            binding.exhaust_primary_volume_index >= gas.gas_volumes.size()) {
            auto failure = shape_fault("compiled cylinder capture binding is invalid");
            failure.cylinder_id = plan_.cylinders[index];
            return failure;
        }
        const auto &chamber = gas.gas_volumes[binding.chamber_volume_index].cell;
        cylinders_.push_back({
            kMechanism | kThermodynamic | kComposition | kCombustion | kTorque,
            mechanism.chamber_volume_m3,
            mechanism.dvolume_dtheta_m3_per_rad,
            // The legacy observable is piston-speed magnitude, as used by its
            // combustion-history model; capture does not invent a sign.
            mechanism.piston_speed_abs_m_s,
            legacy_gas_pressure_pa(chamber),
            legacy_gas_temperature_k(chamber),
            chamber.amount_mol,
            capture_mixture(chamber.mixture),
            gas_cylinder.outer_step_combustion_heat_release_j,
            gas_cylinder.flame.radial_travel_m,
            gas_cylinder.flame.axial_travel_m,
            gas_cylinder.flame.active,
            contract::TorqueValueNm{
                gas_cylinder.indicated_gas_torque_nm,
                contract::Availability::available,
                contract::Completeness::complete,
                contract::QuantityUnavailableReason::none,
                contract::indicated_gas_torque_term_mask(),
                0,
            },
        });

        parity_cylinders_.push_back({
            gas_cylinder.experimental_primary_audio.static_pressure_pa_abs,
            gas_cylinder.experimental_primary_audio.dynamic_pressure_forward_pa,
            gas_cylinder.experimental_primary_audio.dynamic_pressure_reverse_pa,
        });
    }

    for (std::size_t index = 0; index < plan_.ports.size(); ++index) {
        const auto &binding = plan_.port_bindings[index];
        if (binding.cylinder_index >= gas.cylinders.size() ||
            binding.duct_volume_index >= gas.gas_volumes.size() ||
            binding.valve_edge_index >= gas.flow_edges.size()) {
            auto failure = shape_fault("compiled port capture binding is invalid");
            failure.port_id = plan_.ports[index].id;
            return failure;
        }
        const auto &duct = gas.gas_volumes[binding.duct_volume_index].cell;
        const auto &edge = gas.flow_edges[binding.valve_edge_index];
        const auto &valves = gas.cylinders[binding.cylinder_index].valves;
        const bool intake = binding.kind == contract::PortKind::intake;
        ports_.push_back({
            kGasExchange,
            legacy_gas_pressure_pa(duct),
            legacy_gas_temperature_k(duct),
            capture_mass_flow_kg_s(edge.signed_amount_mol),
            // K is a molar-flow conductance, not a geometric area. M3 does not
            // resolve a separate effective valve area, so its canonical value is +0.
            0.0,
            intake ? valves.intake_valve_k : valves.exhaust_valve_k,
            intake ? valves.intake_lift_m : valves.exhaust_lift_m,
        });
    }

    for (const auto &volume : gas.gas_volumes) {
        if (!volume.physically_resolved) {
            gas_volumes_.push_back({});
            continue;
        }
        gas_volumes_.push_back({
            kThermodynamic | kComposition,
            volume.cell.volume_m3,
            legacy_gas_pressure_pa(volume.cell),
            legacy_gas_temperature_k(volume.cell),
            volume.cell.amount_mol,
            volume.cell.thermal_energy_j,
            volume.cell.momentum_x_kg_m_s,
            volume.cell.momentum_y_kg_m_s,
            capture_mixture(volume.cell.mixture),
        });
    }

    for (const auto &edge : gas.flow_edges) {
        flow_edges_.push_back({
            kGasExchange,
            capture_mass_flow_kg_s(edge.signed_amount_mol),
        });
    }

    for (std::size_t index = 0; index < plan_.route_bindings.size(); ++index) {
        const auto &binding = plan_.route_bindings[index];
        if (binding.gas_route_index >= gas.exhaust_routes.size() ||
            binding.source_volume_index >= gas.gas_volumes.size() ||
            binding.outlet_edge_index >= gas.flow_edges.size()) {
            auto failure = shape_fault("compiled route capture binding is invalid");
            failure.route_id = plan_.routes[index].id;
            return failure;
        }
        const auto &route = gas.exhaust_routes[binding.gas_route_index];
        const auto &source = gas.gas_volumes[binding.source_volume_index].cell;
        const auto &edge = gas.flow_edges[binding.outlet_edge_index];
        if (route.route_id != plan_.routes[index].id ||
            route.collector_volume_id != plan_.routes[index].source_volume_id ||
            route.collector_outlet_edge_id != edge.flow_edge_id) {
            auto failure = shape_fault(
                "gas route identity, source, outlet, or order differs from capture");
            failure.route_id = plan_.routes[index].id;
            return failure;
        }
        routes_.push_back(contract::GasSourceRouteCaptureSample{
            kThermodynamic | kGasExchange,
            legacy_gas_pressure_pa(source),
            legacy_gas_temperature_k(source),
            // The edge is declared atmosphere -> collector. A route is oriented
            // source-volume -> exterior, so its public flow uses the opposite sign.
            -capture_mass_flow_kg_s(edge.signed_amount_mol),
            route.collector_cross_section_area_m2,
        });
    }

    const auto frame_offset = frame_count_;
    for (const auto &event : gas.events) {
        events_.push_back({
            frame_offset,
            event.ordinal_within_step,
            event.payload,
        });
    }
    event_offsets_.push_back(static_cast<std::uint32_t>(events_.size()));
    filtered_engine_speed_rpm_.push_back(mechanics.filtered_engine_speed_rpm);
    ++frame_count_;
    return std::nullopt;
}

contract::CaptureBlockView LowOrderCaptureBuffer::view() const noexcept {
    const auto layout = contract::CaptureLayoutView::borrow_for_callback(
        plan_.engine_id, plan_.cylinders, plan_.ports, plan_.gas_volumes,
        plan_.flow_edges, plan_.routes);
    const auto journal =
        contract::EventJournalView::borrow_for_callback(event_offsets_, events_);
    const auto parity = contract::ReferenceParityBlockView::borrow_for_callback(
        filtered_engine_speed_rpm_, parity_cylinders_);
    return contract::CaptureBlockView::borrow_for_callback(
        layout,
        contract::CaptureClock{
            plan_.rate,
            first_sample_index_,
            first_sample_index_ + 1U,
            contract::SamplePhase::post_step,
        },
        frame_count_, plan_.declared_block_capacity_frames,
        plan_.declared_event_capacity_records, engine_, cylinders_, ports_,
        gas_volumes_, flow_edges_, routes_, journal, parity);
}

std::uint32_t LowOrderCaptureBuffer::frame_count() const noexcept {
    return frame_count_;
}

std::uint32_t LowOrderCaptureBuffer::block_capacity_frames() const noexcept {
    return plan_.declared_block_capacity_frames;
}

std::uint64_t LowOrderCaptureBuffer::first_sample_index() const noexcept {
    return first_sample_index_;
}

} // namespace engine_sim_offline::simulation::detail
