#include "telemetry_encoder_support.hpp"

#include <type_traits>

namespace engine_sim_offline::artifacts::detail {
namespace {

std::uint8_t port_kind_code(contract::PortKind value) noexcept {
    switch (value) {
    case contract::PortKind::intake:
        return 1;
    case contract::PortKind::exhaust:
        return 2;
    case contract::PortKind::unspecified:
        return 0;
    }
    return 0;
}

std::uint8_t volume_kind_code(contract::GasVolumeKind value) noexcept {
    switch (value) {
    case contract::GasVolumeKind::atmosphere:
        return 1;
    case contract::GasVolumeKind::intake_plenum:
        return 2;
    case contract::GasVolumeKind::intake_runner:
        return 3;
    case contract::GasVolumeKind::cylinder:
        return 4;
    case contract::GasVolumeKind::exhaust_primary:
        return 5;
    case contract::GasVolumeKind::exhaust_collector:
        return 6;
    case contract::GasVolumeKind::unspecified:
        return 0;
    }
    return 0;
}

std::uint8_t route_kind_code(contract::SourceRouteKind value) noexcept {
    switch (value) {
    case contract::SourceRouteKind::exhaust_outlet:
        return 1;
    case contract::SourceRouteKind::intake_inlet:
        return 2;
    case contract::SourceRouteKind::mechanical_engine:
        return 3;
    case contract::SourceRouteKind::mechanical_starter:
        return 4;
    case contract::SourceRouteKind::unspecified:
        return 0;
    }
    return 0;
}

std::uint8_t availability_code(contract::Availability value) noexcept {
    switch (value) {
    case contract::Availability::available:
        return 1;
    case contract::Availability::unavailable:
        return 2;
    }
    return 0;
}

std::uint8_t completeness_code(contract::Completeness value) noexcept {
    switch (value) {
    case contract::Completeness::complete:
        return 1;
    case contract::Completeness::incomplete:
        return 2;
    }
    return 0;
}

std::uint8_t
unavailable_reason_code(contract::QuantityUnavailableReason value) noexcept {
    switch (value) {
    case contract::QuantityUnavailableReason::none:
        return 0;
    case contract::QuantityUnavailableReason::scenario_not_applicable:
        return 1;
    case contract::QuantityUnavailableReason::model_not_admitted:
        return 2;
    case contract::QuantityUnavailableReason::equivalent_inertia_missing:
        return 3;
    case contract::QuantityUnavailableReason::cycle_integration_not_admitted:
        return 4;
    case contract::QuantityUnavailableReason::not_settled:
        return 5;
    case contract::QuantityUnavailableReason::required_input_missing:
        return 6;
    }
    return 255;
}

std::uint8_t ignition_rejection_code(contract::IgnitionRejection value) noexcept {
    switch (value) {
    case contract::IgnitionRejection::active_flame:
        return 1;
    case contract::IgnitionRejection::no_fuel:
        return 2;
    case contract::IgnitionRejection::mixture_low:
        return 3;
    case contract::IgnitionRejection::mixture_high:
        return 4;
    case contract::IgnitionRejection::unspecified:
        return 0;
    }
    return 0;
}

std::uint8_t flame_extinction_code(contract::FlameExtinctionReason value) noexcept {
    switch (value) {
    case contract::FlameExtinctionReason::intake_transfer:
        return 1;
    case contract::FlameExtinctionReason::no_geometric_progress:
        return 2;
    case contract::FlameExtinctionReason::unspecified:
        return 0;
    }
    return 0;
}

bool emit_optional_id(TelemetryByteEmitter &emitter,
                      const std::optional<contract::GasVolumeId> &value) {
    return emitter.append_bool(value.has_value()) &&
           (!value.has_value() || emitter.append_u32(value->value));
}

bool emit_optional_id(TelemetryByteEmitter &emitter,
                      const std::optional<contract::RouteId> &value) {
    return emitter.append_bool(value.has_value()) &&
           (!value.has_value() || emitter.append_u32(value->value));
}

bool emit_mixture(TelemetryByteEmitter &emitter,
                  const contract::MixtureFractions &value) {
    return emitter.append_f64(value.fuel) && emitter.append_f64(value.inert) &&
           emitter.append_f64(value.oxygen);
}

bool emit_quantity(TelemetryByteEmitter &emitter,
                   const contract::QuantityValue &value) {
    return emitter.append_f64(value.value) &&
           emitter.append_u8(availability_code(value.availability)) &&
           emitter.append_u8(completeness_code(value.completeness)) &&
           emitter.append_u8(unavailable_reason_code(value.unavailable_reason));
}

bool emit_torque(TelemetryByteEmitter &emitter, const contract::TorqueValueNm &value) {
    return emitter.append_f64(value.value_nm) &&
           emitter.append_u8(availability_code(value.availability)) &&
           emitter.append_u8(completeness_code(value.completeness)) &&
           emitter.append_u8(unavailable_reason_code(value.unavailable_reason)) &&
           emitter.append_u64(value.included_terms) &&
           emitter.append_u64(value.omitted_terms);
}

bool emit_torque_telemetry(TelemetryByteEmitter &emitter,
                           const contract::TorqueTelemetry &value) {
    return emit_torque(emitter, value.instantaneous_indicated_gas) &&
           emit_torque(emitter, value.pumping_partition) &&
           emit_torque(emitter, value.friction_pump_and_accessory) &&
           emit_torque(emitter, value.starter) &&
           emit_torque(emitter, value.instantaneous_net_shaft) &&
           emit_torque(emitter, value.cycle_mean_net_shaft) &&
           emit_torque(emitter, value.actuator) &&
           emit_torque(emitter, value.dyno_reaction) &&
           emit_quantity(emitter, value.cycle_work_j) &&
           emit_quantity(emitter, value.net_bmep_pa) &&
           emit_quantity(emitter, value.instantaneous_power_w) &&
           emit_quantity(emitter, value.cycle_mean_power_w);
}

bool emit_engine(TelemetryByteEmitter &emitter,
                 const contract::EngineCaptureSample &value) {
    return emitter.append_u64(value.step_end_index) &&
           emitter.append_u32(value.validity) && emitter.append_f64(value.theta_rad) &&
           emitter.append_f64(value.theta_cycle_rad) &&
           emitter.append_f64(value.angular_speed_rad_s) &&
           emitter.append_f64(value.angular_acceleration_rad_s2) &&
           emitter.append_f64(value.engine_speed_rpm) &&
           emitter.append_f64(value.requested_throttle_01) &&
           emitter.append_f64(value.resolved_engine_throttle_01) &&
           emitter.append_f64(value.intake_plate_position_01) &&
           emitter.append_f64(value.main_flow_multiplier_01) &&
           emitter.append_bool(value.ignition_enabled) &&
           emitter.append_bool(value.fuel_enabled) &&
           emitter.append_bool(value.starter_enabled) &&
           emitter.append_bool(value.dyno_enabled) &&
           emitter.append_bool(value.limiter_cut_active) &&
           emit_torque_telemetry(emitter, value.torque);
}

bool emit_cylinder(TelemetryByteEmitter &emitter,
                   const contract::CylinderCaptureSample &value) {
    return emitter.append_u32(value.validity) &&
           emitter.append_f64(value.chamber_volume_m3) &&
           emitter.append_f64(value.chamber_dvolume_dtheta_m3_per_rad) &&
           emitter.append_f64(value.piston_velocity_m_s) &&
           emitter.append_f64(value.pressure_pa_abs) &&
           emitter.append_f64(value.temperature_k) &&
           emitter.append_f64(value.amount_mol) &&
           emit_mixture(emitter, value.composition) &&
           emitter.append_f64(value.combustion_heat_release_j) &&
           emitter.append_f64(value.flame_radius_m) &&
           emitter.append_f64(value.flame_axial_travel_m) &&
           emitter.append_bool(value.flame_active) &&
           emit_torque(emitter, value.indicated_gas_torque);
}

bool emit_port(TelemetryByteEmitter &emitter,
               const contract::PortCaptureSample &value) {
    return emitter.append_u32(value.validity) &&
           emitter.append_f64(value.pressure_pa_abs) &&
           emitter.append_f64(value.temperature_k) &&
           emitter.append_f64(value.signed_mass_flow_kg_s) &&
           emitter.append_f64(value.effective_flow_area_m2) &&
           emitter.append_f64(
               value.effective_molar_flow_conductance_m2_sqrt_mol_per_kg) &&
           emitter.append_f64(value.valve_lift_m);
}

bool emit_gas_volume(TelemetryByteEmitter &emitter,
                     const contract::GasVolumeCaptureSample &value) {
    return emitter.append_u32(value.validity) && emitter.append_f64(value.volume_m3) &&
           emitter.append_f64(value.pressure_pa_abs) &&
           emitter.append_f64(value.temperature_k) &&
           emitter.append_f64(value.amount_mol) &&
           emitter.append_f64(value.thermal_energy_j) &&
           emitter.append_f64(value.momentum_x_kg_m_s) &&
           emitter.append_f64(value.momentum_y_kg_m_s) &&
           emit_mixture(emitter, value.composition);
}

bool emit_flow_edge(TelemetryByteEmitter &emitter,
                    const contract::FlowEdgeCaptureSample &value) {
    return emitter.append_u32(value.validity) &&
           emitter.append_f64(value.signed_mass_flow_kg_s);
}

bool emit_source_route(TelemetryByteEmitter &emitter,
                       const contract::SourceRouteCaptureSample &value) {
    return std::visit(
        [&](const auto &payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload,
                                         contract::GasSourceRouteCaptureSample>) {
                return emitter.append_u8(1) && emitter.append_u32(payload.validity) &&
                       emitter.append_f64(payload.pressure_pa_abs) &&
                       emitter.append_f64(payload.temperature_k) &&
                       emitter.append_f64(payload.signed_mass_flow_kg_s) &&
                       emitter.append_f64(payload.effective_area_m2);
            } else {
                bool emitted =
                    emitter.append_u8(2) && emitter.append_u32(payload.validity);
                for (const auto component : payload.force_xyz_n) {
                    emitted = emitted && emitter.append_f64(component);
                }
                for (const auto component : payload.torque_xyz_nm) {
                    emitted = emitted && emitter.append_f64(component);
                }
                return emitted;
            }
        },
        value);
}

bool emit_event(TelemetryByteEmitter &emitter, const contract::EngineEvent &event) {
    return emitter.append_u8(event.ordinal_within_step) &&
           std::visit(
               [&](const auto &payload) {
                   using Payload = std::decay_t<decltype(payload)>;
                   if constexpr (std::is_same_v<Payload, contract::SparkCrossing>) {
                       return emitter.append_u8(1) &&
                              emitter.append_u32(payload.cylinder_id.value) &&
                              emitter.append_f64(payload.raw_saved_angle_rad) &&
                              emitter.append_f64(payload.raw_current_angle_rad) &&
                              emitter.append_f64(payload.adjusted_current_angle_rad) &&
                              emitter.append_f64(payload.adjusted_spark_angle_rad) &&
                              emitter.append_f64(payload.timing_advance_rad);
                   } else if constexpr (std::is_same_v<Payload,
                                                       contract::LimiterStateChanged>) {
                       return emitter.append_u8(2) &&
                              emitter.append_bool(payload.old_active) &&
                              emitter.append_bool(payload.new_active) &&
                              emitter.append_bool(payload.overspeed_refreshed) &&
                              emitter.append_f64(payload.resulting_timer_s);
                   } else if constexpr (std::is_same_v<Payload,
                                                       contract::IgnitionAccepted>) {
                       return emitter.append_u8(3) &&
                              emitter.append_u32(payload.cylinder_id.value) &&
                              emitter.append_f64(payload.efficiency_01) &&
                              emitter.append_f64(payload.flame_speed_m_s);
                   } else if constexpr (std::is_same_v<Payload,
                                                       contract::IgnitionRejected>) {
                       return emitter.append_u8(4) &&
                              emitter.append_u32(payload.cylinder_id.value) &&
                              emitter.append_u8(
                                  ignition_rejection_code(payload.reason));
                   } else {
                       return emitter.append_u8(5) &&
                              emitter.append_u32(payload.cylinder_id.value) &&
                              emitter.append_u8(payload.gas_substep_index) &&
                              emitter.append_u8(flame_extinction_code(payload.reason));
                   }
               },
               event.payload);
}

} // namespace

bool emit_layout(TelemetryByteEmitter &emitter, contract::EngineId engine_id,
                 std::span<const contract::CylinderId> cylinders,
                 std::span<const contract::PortIdentity> ports,
                 std::span<const contract::GasVolumeIdentity> gas_volumes,
                 std::span<const contract::FlowEdgeIdentity> flow_edges,
                 std::span<const contract::RouteIdentity> routes) {
    bool emitted = emitter.append_u32(engine_id.value) &&
                   emitter.append_u32(static_cast<std::uint32_t>(cylinders.size())) &&
                   emitter.append_u32(static_cast<std::uint32_t>(ports.size())) &&
                   emitter.append_u32(static_cast<std::uint32_t>(gas_volumes.size())) &&
                   emitter.append_u32(static_cast<std::uint32_t>(flow_edges.size())) &&
                   emitter.append_u32(static_cast<std::uint32_t>(routes.size()));
    for (const auto cylinder : cylinders) {
        emitted = emitted && emitter.append_u32(cylinder.value);
    }
    for (const auto &port : ports) {
        emitted = emitted && emitter.append_u32(port.id.value) &&
                  emitter.append_u32(port.cylinder_id.value) &&
                  emitter.append_u8(port_kind_code(port.kind));
    }
    for (const auto &volume : gas_volumes) {
        emitted = emitted && emitter.append_u32(volume.id.value) &&
                  emitter.append_u8(volume_kind_code(volume.kind));
    }
    for (const auto &edge : flow_edges) {
        emitted = emitted && emitter.append_u32(edge.id.value) &&
                  emitter.append_u32(edge.endpoint_0_volume_id.value) &&
                  emitter.append_u32(edge.endpoint_1_volume_id.value);
    }
    for (const auto &route : routes) {
        emitted = emitted && emitter.append_u32(route.id.value) &&
                  emitter.append_u8(route_kind_code(route.kind)) &&
                  emit_optional_id(emitter, route.source_volume_id) &&
                  emit_optional_id(emitter, route.default_parent_route_id) &&
                  emitter.append_bool(route.emitter_anchor_id.has_value()) &&
                  (!route.emitter_anchor_id.has_value() ||
                   emitter.append_string(*route.emitter_anchor_id));
    }
    return emitted;
}

bool emit_frame(TelemetryByteEmitter &emitter, const contract::CaptureBlockView &block,
                std::uint32_t frame_offset, std::uint64_t global_sample_index,
                std::uint64_t timestamp_tick) {
    bool emitted = emitter.append_fourcc({'F', 'R', 'M', '1'}) &&
                   emitter.append_u64(global_sample_index) &&
                   emitter.append_u64(timestamp_tick) &&
                   emit_engine(emitter, *block.engine_sample(frame_offset));

    for (std::size_t index = 0; emitted && index < block.layout().cylinders().size();
         ++index) {
        emitted = emit_cylinder(emitter, *block.cylinder_sample(frame_offset, index));
    }
    for (std::size_t index = 0; emitted && index < block.layout().ports().size();
         ++index) {
        emitted = emit_port(emitter, *block.port_sample(frame_offset, index));
    }
    for (std::size_t index = 0; emitted && index < block.layout().gas_volumes().size();
         ++index) {
        emitted =
            emit_gas_volume(emitter, *block.gas_volume_sample(frame_offset, index));
    }
    for (std::size_t index = 0; emitted && index < block.layout().flow_edges().size();
         ++index) {
        emitted = emit_flow_edge(emitter, *block.flow_edge_sample(frame_offset, index));
    }
    for (std::size_t index = 0; emitted && index < block.layout().routes().size();
         ++index) {
        emitted =
            emit_source_route(emitter, *block.source_route_sample(frame_offset, index));
    }

    if (emitted && block.reference_parity().has_value()) {
        const auto &parity = *block.reference_parity();
        emitted = emitter.append_f64(parity.filtered_engine_speed_rpm()[frame_offset]);
        const auto cylinder_count = block.layout().cylinders().size();
        for (std::size_t index = 0; emitted && index < cylinder_count; ++index) {
            const auto &sample =
                parity.cylinders()[static_cast<std::size_t>(frame_offset) *
                                       cylinder_count +
                                   index];
            emitted =
                emitter.append_f64(sample.exhaust_primary_static_pressure_pa_abs) &&
                emitter.append_f64(sample.dynamic_pressure_forward_pa) &&
                emitter.append_f64(sample.dynamic_pressure_reverse_pa);
        }
    }

    const auto offsets = block.event_journal().offsets();
    const auto begin = offsets[frame_offset];
    const auto end = offsets[frame_offset + 1];
    emitted = emitted && emitter.append_u32(end - begin);
    const auto events = block.event_journal().events();
    for (auto index = begin; emitted && index < end; ++index) {
        emitted = emit_event(emitter, events[index]);
    }
    return emitted;
}

} // namespace engine_sim_offline::artifacts::detail
