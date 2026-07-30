#include "c_api/c_api_internal.hpp"

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

eso_engine_telemetry_t engine_telemetry(const EngineTelemetryFrame &frame) noexcept {
    const auto &engine = frame.engine;
    return {
        frame.physics_step_end,
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

} // namespace engine_sim_offline::c_api
