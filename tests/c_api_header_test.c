#include "engine_sim_offline/c_api.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(ESO_C_API_VERSION == 7, "unexpected C ABI version");
_Static_assert(sizeof(eso_engine_handle_t) == sizeof(uint64_t),
               "engine handle width changed");
_Static_assert(sizeof(eso_scenario_handle_t) == sizeof(uint64_t),
               "scenario handle width changed");
_Static_assert(sizeof(eso_session_handle_t) == sizeof(uint64_t),
               "session handle width changed");
_Static_assert(ESO_CONTROL_THROTTLE == 1 && ESO_CONTROL_IGNITION_ENABLED == 2 &&
                   ESO_CONTROL_FUEL_ENABLED == 3 && ESO_CONTROL_LIMITER_ENABLED == 4 &&
                   ESO_CONTROL_EXTERNAL_RESISTING_TORQUE == 5 &&
                   ESO_CONTROL_STARTER_ENABLED == 6 &&
                   ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED == 7 &&
                   ESO_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE == 8 &&
                   ESO_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE == 9 &&
                   ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR == 10 &&
                   ESO_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT == 11 &&
                   ESO_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION == 12,
               "control kind values changed");
_Static_assert(
    ESO_LIVE_CONTROL_CAPABILITY_THROTTLE == (UINT32_C(1) << 0U) &&
        ESO_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED == (UINT32_C(1) << 1U) &&
        ESO_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED == (UINT32_C(1) << 2U) &&
        ESO_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED == (UINT32_C(1) << 3U) &&
        ESO_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE == (UINT32_C(1) << 4U) &&
        ESO_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED == (UINT32_C(1) << 5U) &&
        ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_TARGET_ENGINE_SPEED ==
            (UINT32_C(1) << 6U) &&
        ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE ==
            (UINT32_C(1) << 7U) &&
        ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_DRIVING_TORQUE ==
            (UINT32_C(1) << 8U) &&
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SELECTED_FORWARD_GEAR ==
            (UINT32_C(1) << 9U) &&
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_CLUTCH_ENGAGEMENT == (UINT32_C(1) << 10U) &&
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SERVICE_BRAKE_APPLICATION ==
            (UINT32_C(1) << 11U),
    "live-control capability bits changed");
_Static_assert(ESO_MOTION_HELD_SPEED == 1 &&
                   ESO_MOTION_PRESCRIBED_KINEMATIC_SWEEP == 2 &&
                   ESO_MOTION_HELD_DYNO == 3 &&
                   ESO_MOTION_LOAD_TARGET_HELD_CAPTURE == 4 &&
                   ESO_MOTION_INERTIAL_DYNO == 5 && ESO_MOTION_FREE_ENGINE == 6 &&
                   ESO_MOTION_FREE_VEHICLE == 7,
               "motion-mode values changed");
_Static_assert(ESO_AUDIO_BUS_SOURCE_ROUTE_DRY == 1 &&
                   ESO_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER == 2 &&
                   ESO_AUDIO_BUS_SOURCE_ROUTE_SELECTED == 3 &&
                   ESO_AUDIO_BUS_ENGINE_RAW_MASTER == 4 &&
                   ESO_AUDIO_BUS_ENGINE_AUDITION_MASTER == 5,
               "audio-bus kind values changed");
_Static_assert(ESO_SOURCE_ROUTE_UNSPECIFIED == 0 &&
                   ESO_SOURCE_ROUTE_EXHAUST_OUTLET == 1 &&
                   ESO_SOURCE_ROUTE_INTAKE_INLET == 2 &&
                   ESO_SOURCE_ROUTE_MECHANICAL_ENGINE == 3 &&
                   ESO_SOURCE_ROUTE_MECHANICAL_STARTER == 4,
               "source-route kind values changed");
_Static_assert(ESO_AUDIO_SIGNAL_ACTIVE == 1 && ESO_AUDIO_SIGNAL_DECLARED_SILENT == 2,
               "audio-signal disposition values changed");
_Static_assert(sizeof(((eso_control_command_t *)0)->scalar_value) == sizeof(double),
               "control scalar width changed");
_Static_assert(sizeof(((eso_control_command_t *)0)->id_value) == sizeof(uint32_t),
               "control discrete-ID width changed");
_Static_assert(offsetof(eso_control_command_t, delivery_frame) == 0U &&
                   offsetof(eso_control_command_t, sequence) == 8U &&
                   offsetof(eso_control_command_t, kind) == 16U &&
                   offsetof(eso_control_command_t, enabled) == 20U &&
                   offsetof(eso_control_command_t, scalar_value) == 24U &&
                   offsetof(eso_control_command_t, id_value) == 32U &&
                   offsetof(eso_control_command_t, reserved) == 36U &&
                   sizeof(eso_control_command_t) == 40U,
               "control-command ABI layout changed");
_Static_assert(sizeof(((eso_session_descriptor_t *)0)->live_control_capabilities) ==
                   sizeof(uint32_t),
               "live-control capability mask width changed");
_Static_assert(offsetof(eso_session_descriptor_t,
                        maximum_cycle_evidence_per_process_call) == 12U,
               "session cycle-evidence capacity layout changed");
_Static_assert(offsetof(eso_session_descriptor_t, execution_kind) <
                       offsetof(eso_session_descriptor_t, motion_mode) &&
                   offsetof(eso_session_descriptor_t, motion_mode) <
                       offsetof(eso_session_descriptor_t, forward_gear_count),
               "session motion/inventory layout changed");
_Static_assert(offsetof(eso_audio_bus_descriptor_t, kind) == 0U &&
                   offsetof(eso_audio_bus_descriptor_t, has_route_id) == 24U &&
                   offsetof(eso_audio_bus_descriptor_t, route_id) == 28U &&
                   offsetof(eso_audio_bus_descriptor_t, source_route_kind) == 32U &&
                   offsetof(eso_audio_bus_descriptor_t, signal_disposition) == 36U &&
                   offsetof(eso_audio_bus_descriptor_t, id_utf8_bytes) == 40U &&
                   sizeof(eso_audio_bus_descriptor_t) == 48U,
               "audio-bus descriptor ABI layout changed");
_Static_assert(offsetof(eso_session_telemetry_t, physics_step_end) == 0U &&
                   offsetof(eso_session_telemetry_t, engine) == sizeof(uint64_t) &&
                   offsetof(eso_session_telemetry_t, has_held_dyno) <
                       offsetof(eso_session_telemetry_t, held_dyno) &&
                   offsetof(eso_session_telemetry_t, has_free_vehicle) <
                       offsetof(eso_session_telemetry_t, free_vehicle),
               "session-telemetry ABI layout changed");
_Static_assert(ESO_ENGINE_CYCLE_STATE_IGNITION_ENABLED == (UINT32_C(1) << 0U) &&
                   ESO_ENGINE_CYCLE_STATE_FUEL_ENABLED == (UINT32_C(1) << 1U) &&
                   ESO_ENGINE_CYCLE_STATE_STARTER_ENABLED == (UINT32_C(1) << 2U) &&
                   ESO_ENGINE_CYCLE_STATE_DYNO_ENABLED == (UINT32_C(1) << 3U) &&
                   ESO_ENGINE_CYCLE_STATE_LIMITER_ENABLED == (UINT32_C(1) << 4U) &&
                   ESO_ENGINE_CYCLE_STATE_LIMITER_CUT_ACTIVE == (UINT32_C(1) << 5U),
               "cycle-state flag bits changed");
_Static_assert(offsetof(eso_cycle_boundary_evidence_t, cycle_ordinal) == 0U &&
                   offsetof(eso_cycle_boundary_evidence_t, left_physics_frame) == 8U &&
                   offsetof(eso_cycle_boundary_evidence_t, right_physics_frame) ==
                       16U &&
                   offsetof(eso_cycle_boundary_evidence_t, fraction_from_left_01) ==
                       24U &&
                   offsetof(eso_cycle_boundary_evidence_t, delivery_frame) == 48U &&
                   sizeof(eso_cycle_boundary_evidence_t) == 56U,
               "cycle-boundary evidence ABI layout changed");
_Static_assert(offsetof(eso_cycle_control_evidence_t, change_count) == 24U &&
                   sizeof(eso_cycle_control_evidence_t) == 32U,
               "cycle-control evidence ABI layout changed");
_Static_assert(offsetof(eso_cycle_net_shaft_evidence_t, included_terms) == 32U &&
                   offsetof(eso_cycle_net_shaft_evidence_t, omitted_terms) == 40U &&
                   sizeof(eso_cycle_net_shaft_evidence_t) == 48U,
               "cycle-net-shaft evidence ABI layout changed");
_Static_assert(
    offsetof(eso_completed_cycle_evidence_t, start_boundary) == 8U &&
        offsetof(eso_completed_cycle_evidence_t, end_boundary) == 64U &&
        offsetof(eso_completed_cycle_evidence_t, duration_s) == 120U &&
        offsetof(eso_completed_cycle_evidence_t, requested_throttle) == 136U &&
        offsetof(eso_completed_cycle_evidence_t, instantaneous_net_shaft) == 232U &&
        offsetof(eso_completed_cycle_evidence_t, start_state_flags) == 280U &&
        sizeof(eso_completed_cycle_evidence_t) == 296U,
    "completed-cycle evidence ABI layout changed");
_Static_assert(offsetof(eso_process_info_t, telemetry_written) <
                       offsetof(eso_process_info_t, cycle_evidence_written) &&
                   offsetof(eso_process_info_t, cycle_evidence_written) <
                       offsetof(eso_process_info_t, completed_physics_frame_count),
               "process cycle-evidence extent layout changed");

int main(void) {
    eso_context_t *context = (eso_context_t *)(uintptr_t)1;
    if (eso_context_create(ESO_C_API_VERSION - 1U, &context) !=
            ESO_STATUS_ABI_VERSION_MISMATCH ||
        context != NULL) {
        return 1;
    }
    if (eso_context_create(ESO_C_API_VERSION, &context) != ESO_STATUS_OK ||
        context == NULL) {
        return 2;
    }

    eso_abi_layout_t layout = {0};
    if (eso_get_abi_layout(&layout) != ESO_STATUS_OK ||
        layout.api_version != ESO_C_API_VERSION ||
        layout.pointer_size_bytes != sizeof(void *) ||
        layout.size_type_size_bytes != sizeof(size_t) ||
        layout.float_size_bytes != sizeof(float) ||
        layout.double_size_bytes != sizeof(double) ||
        layout.control_command_size_bytes != sizeof(eso_control_command_t) ||
        layout.session_descriptor_size_bytes != sizeof(eso_session_descriptor_t) ||
        layout.forward_gear_descriptor_size_bytes !=
            sizeof(eso_forward_gear_descriptor_t) ||
        layout.audio_bus_descriptor_size_bytes != sizeof(eso_audio_bus_descriptor_t) ||
        layout.session_telemetry_size_bytes != sizeof(eso_session_telemetry_t) ||
        layout.completed_cycle_evidence_size_bytes !=
            sizeof(eso_completed_cycle_evidence_t)) {
        return 3;
    }

    {
        static const char malformed[] = "{";
        const eso_utf8_view_t json = {malformed, sizeof(malformed) - 1U};
        eso_engine_handle_t engine = ESO_INVALID_HANDLE;
        if (eso_compile_engine_json(context, json, NULL, 0U, &engine) !=
                ESO_STATUS_ENGINE_PARSE_FAILED ||
            engine != ESO_INVALID_HANDLE) {
            return 4;
        }

        eso_error_info_t error = {0};
        if (eso_context_get_last_error(context, &error) != ESO_STATUS_OK ||
            error.status != ESO_STATUS_ENGINE_PARSE_FAILED ||
            error.stage != ESO_ERROR_STAGE_ENGINE_PARSE ||
            error.diagnostic_count == 0U) {
            return 5;
        }

        eso_diagnostic_info_t diagnostic = {0};
        if (eso_context_get_diagnostic(context, 0U, &diagnostic) != ESO_STATUS_OK ||
            diagnostic.severity != ESO_DIAGNOSTIC_ERROR) {
            return 6;
        }
    }

    if (eso_context_destroy(context) != ESO_STATUS_OK ||
        eso_context_destroy(NULL) != ESO_STATUS_OK) {
        return 7;
    }
    return 0;
}
