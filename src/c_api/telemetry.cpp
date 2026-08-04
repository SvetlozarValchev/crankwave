#include "c_api/c_api_internal.hpp"

#include <cstring>

namespace engine_sim_offline::c_api {
namespace {

[[nodiscard]] eso_availability_t
availability(const contract::Availability value) noexcept {
    return value == contract::Availability::available ? ESO_AVAILABLE : ESO_UNAVAILABLE;
}

[[nodiscard]] eso_completeness_t
completeness(const contract::Completeness value) noexcept {
    return value == contract::Completeness::complete ? ESO_COMPLETE : ESO_INCOMPLETE;
}

[[nodiscard]] eso_quantity_unavailable_reason_t
unavailable_reason(const contract::QuantityUnavailableReason reason) noexcept {
    using Reason = contract::QuantityUnavailableReason;
    switch (reason) {
    case Reason::none:
        return ESO_QUANTITY_UNAVAILABLE_NONE;
    case Reason::scenario_not_applicable:
        return ESO_QUANTITY_UNAVAILABLE_SCENARIO_NOT_APPLICABLE;
    case Reason::model_not_admitted:
        return ESO_QUANTITY_UNAVAILABLE_MODEL_NOT_ADMITTED;
    case Reason::equivalent_inertia_missing:
        return ESO_QUANTITY_UNAVAILABLE_EQUIVALENT_INERTIA_MISSING;
    case Reason::cycle_integration_not_admitted:
        return ESO_QUANTITY_UNAVAILABLE_CYCLE_INTEGRATION_NOT_ADMITTED;
    case Reason::not_settled:
        return ESO_QUANTITY_UNAVAILABLE_NOT_SETTLED;
    case Reason::required_input_missing:
        return ESO_QUANTITY_UNAVAILABLE_REQUIRED_INPUT_MISSING;
    }
    return ESO_QUANTITY_UNAVAILABLE_MODEL_NOT_ADMITTED;
}

[[nodiscard]] eso_torque_telemetry_t
torque_telemetry(const contract::TorqueTelemetry &value) noexcept {
    return {
        torque_value(value.instantaneous_indicated_gas),
        torque_value(value.pumping_partition),
        torque_value(value.friction_pump_and_accessory),
        torque_value(value.starter),
        torque_value(value.instantaneous_net_shaft),
        torque_value(value.cycle_mean_net_shaft),
        torque_value(value.actuator),
        torque_value(value.dyno_reaction),
        quantity_value(value.cycle_work_j),
        quantity_value(value.net_bmep_pa),
        quantity_value(value.instantaneous_power_w),
        quantity_value(value.cycle_mean_power_w),
    };
}

[[nodiscard]] eso_held_dyno_disposition_t
held_dyno_disposition(const EngineHeldDynoDisposition value) noexcept {
    switch (value) {
    case EngineHeldDynoDisposition::tracking:
        return ESO_HELD_DYNO_TRACKING;
    case EngineHeldDynoDisposition::absorbing_torque_limited:
        return ESO_HELD_DYNO_ABSORBING_TORQUE_LIMITED;
    case EngineHeldDynoDisposition::driving_torque_limited:
        return ESO_HELD_DYNO_DRIVING_TORQUE_LIMITED;
    }
    return ESO_HELD_DYNO_TRACKING;
}

[[nodiscard]] eso_clutch_disposition_t
clutch_disposition(const EngineClutchDisposition value) noexcept {
    switch (value) {
    case EngineClutchDisposition::neutral:
        return ESO_CLUTCH_NEUTRAL;
    case EngineClutchDisposition::disengaged:
        return ESO_CLUTCH_DISENGAGED;
    case EngineClutchDisposition::engine_driving_torque_limited:
        return ESO_CLUTCH_ENGINE_DRIVING_TORQUE_LIMITED;
    case EngineClutchDisposition::vehicle_backdrive_torque_limited:
        return ESO_CLUTCH_VEHICLE_BACKDRIVE_TORQUE_LIMITED;
    case EngineClutchDisposition::tracking:
        return ESO_CLUTCH_TRACKING;
    }
    return ESO_CLUTCH_NEUTRAL;
}

[[nodiscard]] eso_road_load_disposition_t
road_load_disposition(const EngineRoadLoadDisposition value) noexcept {
    switch (value) {
    case EngineRoadLoadDisposition::moving:
        return ESO_ROAD_LOAD_MOVING;
    case EngineRoadLoadDisposition::stopped_within_step:
        return ESO_ROAD_LOAD_STOPPED_WITHIN_STEP;
    case EngineRoadLoadDisposition::held_at_rest:
        return ESO_ROAD_LOAD_HELD_AT_REST;
    }
    return ESO_ROAD_LOAD_HELD_AT_REST;
}

[[nodiscard]] eso_held_dyno_telemetry_t
held_dyno_telemetry(const EngineHeldDynoTelemetry &value) noexcept {
    return {
        value.target_engine_speed_rpm,    value.maximum_absorbing_torque_nm,
        value.maximum_driving_torque_nm,  value.required_actuator_torque_nm,
        value.applied_actuator_torque_nm, held_dyno_disposition(value.disposition),
    };
}

[[nodiscard]] eso_free_vehicle_telemetry_t
free_vehicle_telemetry(const EngineFreeVehicleTelemetry &value) noexcept {
    return {
        value.vehicle_speed_m_s,
        value.vehicle_distance_m,
        value.selected_forward_gear_ordinal.has_value() ? 1U : 0U,
        value.selected_forward_gear_ordinal.value_or(0U),
        value.clutch_engagement_01,
        value.service_brake_application_01,
        clutch_disposition(value.clutch_disposition),
        value.final_clutch_slip_rad_s.has_value() ? 1U : 0U,
        value.clutch_torque_capacity_nm,
        value.applied_average_clutch_torque_on_engine_nm,
        value.final_clutch_slip_rad_s.value_or(0.0),
        road_load_disposition(value.road_load_disposition),
        value.requested_road_load_force_n,
        value.applied_average_road_load_force_n,
    };
}

[[nodiscard]] eso_cycle_boundary_evidence_t
cycle_boundary_evidence(const EngineCycleBoundaryEvidence &value) noexcept {
    return {
        value.cycle_ordinal,       value.left_physics_frame,
        value.right_physics_frame, value.fraction_from_left_01,
        value.theta_unwrapped_rad, value.time_s,
        value.delivery_frame,
    };
}

[[nodiscard]] eso_cycle_control_evidence_t
cycle_control_evidence(const EngineCycleControlEvidence &value) noexcept {
    return {
        value.time_weighted_mean_01,
        value.minimum_01,
        value.maximum_01,
        value.change_count,
    };
}

[[nodiscard]] eso_cycle_net_shaft_evidence_t
cycle_net_shaft_evidence(const EngineCycleNetShaftEvidence &value) noexcept {
    return {
        value.angular_work_j,
        value.cycle_mean_torque_nm,
        availability(value.availability),
        completeness(value.completeness),
        unavailable_reason(value.unavailable_reason),
        value.included_terms,
        value.omitted_terms,
    };
}

} // namespace

eso_quantity_value_t quantity_value(const contract::QuantityValue &value) noexcept {
    return {
        value.value,
        availability(value.availability),
        completeness(value.completeness),
        unavailable_reason(value.unavailable_reason),
    };
}

eso_torque_value_nm_t torque_value(const contract::TorqueValueNm &value) noexcept {
    return {
        value.value_nm,
        availability(value.availability),
        completeness(value.completeness),
        unavailable_reason(value.unavailable_reason),
        value.included_terms,
        value.omitted_terms,
    };
}

eso_engine_telemetry_t
engine_telemetry(const contract::EngineCaptureSample &engine) noexcept {
    return {
        engine.step_end_index,
        engine.validity,
        engine.ignition_enabled ? 1U : 0U,
        engine.fuel_enabled ? 1U : 0U,
        engine.starter_enabled ? 1U : 0U,
        engine.dyno_enabled ? 1U : 0U,
        engine.limiter_enabled ? 1U : 0U,
        engine.limiter_cut_active ? 1U : 0U,
        engine.theta_rad,
        engine.theta_cycle_rad,
        engine.angular_speed_rad_s,
        engine.angular_acceleration_rad_s2,
        engine.engine_speed_rpm,
        engine.requested_throttle_01,
        engine.resolved_engine_throttle_01,
        engine.intake_plate_position_01,
        engine.main_flow_multiplier_01,
        engine.requested_external_resisting_torque_nm,
        torque_telemetry(engine.torque),
    };
}

eso_session_telemetry_t session_telemetry(const EngineTelemetryFrame &frame) noexcept {
    eso_session_telemetry_t result;
    std::memset(&result, 0, sizeof(result));
    result.physics_step_end = frame.physics_step_end;
    result.engine = engine_telemetry(frame.engine);
    if (frame.held_dyno.has_value()) {
        result.has_held_dyno = 1U;
        result.held_dyno = held_dyno_telemetry(*frame.held_dyno);
    }
    if (frame.free_vehicle.has_value()) {
        result.has_free_vehicle = 1U;
        result.free_vehicle = free_vehicle_telemetry(*frame.free_vehicle);
    }
    return result;
}

eso_completed_cycle_evidence_t
completed_cycle_evidence(const EngineCompletedCycleEvidence &cycle) noexcept {
    return {
        cycle.completed_cycle_ordinal,
        cycle_boundary_evidence(cycle.start_boundary),
        cycle_boundary_evidence(cycle.end_boundary),
        cycle.duration_s,
        cycle.mean_engine_speed_rpm,
        cycle_control_evidence(cycle.requested_throttle),
        cycle_control_evidence(cycle.resolved_engine_throttle),
        cycle_control_evidence(cycle.intake_plate_position),
        cycle_net_shaft_evidence(cycle.instantaneous_net_shaft),
        cycle.start_state_flags,
        cycle.end_state_flags,
        cycle.state_transition_flags,
    };
}

} // namespace engine_sim_offline::c_api
