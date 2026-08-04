#ifndef ENGINE_SIM_OFFLINE_C_API_H
#define ENGINE_SIM_OFFLINE_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define ESO_C_API_NOEXCEPT noexcept
extern "C" {
#else
#define ESO_C_API_NOEXCEPT
#endif

/*
 * This is the only engine-sim-offline C ABI. It is a greenfield, exact-version
 * contract rather than a compatibility family.
 */
#define ESO_C_API_VERSION UINT32_C(9)
#define ESO_INVALID_HANDLE UINT64_C(0)
#define ESO_SHA256_DIGEST_SIZE UINT32_C(32)

typedef struct eso_context eso_context_t;
typedef uint64_t eso_engine_handle_t;
typedef uint64_t eso_scenario_handle_t;
typedef uint64_t eso_session_handle_t;

typedef uint32_t eso_status_t;
enum {
    ESO_STATUS_OK = 0,
    ESO_STATUS_ABI_VERSION_MISMATCH = 1,
    ESO_STATUS_INVALID_ARGUMENT = 2,
    ESO_STATUS_INVALID_HANDLE = 3,
    ESO_STATUS_NOT_AVAILABLE = 4,
    ESO_STATUS_BUFFER_TOO_SMALL = 5,
    ESO_STATUS_RESOURCE_EXHAUSTED = 6,
    ESO_STATUS_ENGINE_PARSE_FAILED = 7,
    ESO_STATUS_ENGINE_COMPILE_FAILED = 8,
    ESO_STATUS_SCENARIO_PARSE_FAILED = 9,
    ESO_STATUS_SCENARIO_COMPILE_FAILED = 10,
    ESO_STATUS_SESSION_CREATE_FAILED = 11,
    ESO_STATUS_CONTROL_REJECTED = 12,
    ESO_STATUS_PROCESS_FAILED = 13,
    ESO_STATUS_INTERNAL_ERROR = 14
};

typedef uint32_t eso_error_stage_t;
enum {
    ESO_ERROR_STAGE_NONE = 0,
    ESO_ERROR_STAGE_ARGUMENT = 1,
    ESO_ERROR_STAGE_HANDLE = 2,
    ESO_ERROR_STAGE_ENGINE_PARSE = 3,
    ESO_ERROR_STAGE_ENGINE_COMPILE = 4,
    ESO_ERROR_STAGE_SCENARIO_PARSE = 5,
    ESO_ERROR_STAGE_SCENARIO_COMPILE = 6,
    ESO_ERROR_STAGE_SESSION_CREATE = 7,
    ESO_ERROR_STAGE_CONTROL = 8,
    ESO_ERROR_STAGE_PROCESS = 9,
    ESO_ERROR_STAGE_ABI = 10
};

/*
 * Domain codes are stable values carried by eso_error_info_t. Authoring
 * diagnostics have their own code in eso_diagnostic_info_t.
 */
typedef uint32_t eso_error_code_t;
enum {
    ESO_ERROR_NONE = 0,
    ESO_ERROR_INVALID_POINTER = 1,
    ESO_ERROR_INVALID_COUNT = 2,
    ESO_ERROR_INVALID_ENUM = 3,
    ESO_ERROR_INVALID_HANDLE = 4,
    ESO_ERROR_BUFFER_CAPACITY = 5,
    ESO_ERROR_AUTHORING_DIAGNOSTICS = 6,
    ESO_ERROR_RENDERER_SOURCE_STAMP_UNAVAILABLE = 7,
    ESO_ERROR_SESSION_INVALID_COMPILED_SCENARIO = 100,
    ESO_ERROR_SESSION_UNSUPPORTED_CONFIGURATION = 101,
    ESO_ERROR_SESSION_RESOURCE_EXHAUSTED = 102,
    ESO_ERROR_SESSION_PROCESSING_FAILED = 103,
    ESO_ERROR_SESSION_CONSUMER_STATE_INVALID = 104,
    ESO_ERROR_SESSION_INTERNAL = 105,
    ESO_ERROR_CONTROL_CAPACITY_EXCEEDED = 200,
    ESO_ERROR_CONTROL_LATE_COMMAND = 201,
    ESO_ERROR_CONTROL_INVALID_PAYLOAD = 202,
    ESO_ERROR_CONTROL_UNORDERED_DELIVERY_FRAME = 203,
    ESO_ERROR_CONTROL_DUPLICATE_SEQUENCE = 204,
    ESO_ERROR_CONTROL_UNORDERED_SEQUENCE = 205,
    ESO_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE = 206,
    ESO_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION = 207,
    ESO_ERROR_CONTROL_OUTSIDE_SESSION_HORIZON = 208,
    ESO_ERROR_CONTROL_SESSION_TERMINAL = 209,
    ESO_ERROR_CONTROL_INTERNAL_CLOCK = 210
};

typedef struct eso_utf8_view {
    const char *data;
    size_t size;
} eso_utf8_view_t;

typedef struct eso_byte_view {
    const uint8_t *data;
    size_t size;
} eso_byte_view_t;

typedef struct eso_mutable_utf8_buffer {
    char *data;
    size_t capacity;
} eso_mutable_utf8_buffer_t;

typedef struct eso_sha256_digest {
    uint8_t bytes[ESO_SHA256_DIGEST_SIZE];
} eso_sha256_digest_t;

typedef uint32_t eso_asset_kind_t;
enum { ESO_ASSET_AUDIO = 1, ESO_ASSET_ACCESSORY_CONFIGURATION = 2 };

typedef struct eso_asset_payload {
    eso_asset_kind_t kind;
    eso_utf8_view_t asset_id;
    eso_byte_view_t bytes;
} eso_asset_payload_t;

typedef uint32_t eso_diagnostic_severity_t;
enum { ESO_DIAGNOSTIC_ERROR = 1, ESO_DIAGNOSTIC_WARNING = 2 };

typedef uint32_t eso_diagnostic_code_t;
enum {
    ESO_DIAGNOSTIC_MALFORMED_DOCUMENT = 1,
    ESO_DIAGNOSTIC_UNSUPPORTED_SCHEMA = 2,
    ESO_DIAGNOSTIC_MISSING_VALUE = 3,
    ESO_DIAGNOSTIC_UNKNOWN_FIELD = 4,
    ESO_DIAGNOSTIC_INVALID_TYPE = 5,
    ESO_DIAGNOSTIC_INVALID_UNIT = 6,
    ESO_DIAGNOSTIC_INVALID_VALUE = 7,
    ESO_DIAGNOSTIC_OUT_OF_RANGE = 8,
    ESO_DIAGNOSTIC_DUPLICATE_ID = 9,
    ESO_DIAGNOSTIC_DANGLING_REFERENCE = 10,
    ESO_DIAGNOSTIC_FORBIDDEN_CYCLE = 11,
    ESO_DIAGNOSTIC_DISCONNECTED_OBJECT = 12,
    ESO_DIAGNOSTIC_INCONSISTENT_VALUE = 13,
    ESO_DIAGNOSTIC_UNSUPPORTED_CAPABILITY = 14,
    ESO_DIAGNOSTIC_MISSING_ASSET = 15,
    ESO_DIAGNOSTIC_ASSET_HASH_MISMATCH = 16,
    ESO_DIAGNOSTIC_RESOURCE_LIMIT = 17,
    ESO_DIAGNOSTIC_INTERNAL_FAILURE = 18
};

typedef struct eso_error_info {
    eso_status_t status;
    eso_error_stage_t stage;
    eso_error_code_t code;
    size_t detail_code_utf8_bytes;
    size_t message_utf8_bytes;
    size_t diagnostic_count;
} eso_error_info_t;

typedef struct eso_error_text_buffers {
    eso_mutable_utf8_buffer_t detail_code;
    eso_mutable_utf8_buffer_t message;
} eso_error_text_buffers_t;

typedef struct eso_diagnostic_info {
    eso_diagnostic_severity_t severity;
    eso_diagnostic_code_t code;
    uint32_t has_subject;
    uint32_t has_source_position;
    uint64_t source_byte_offset;
    uint32_t source_line;
    uint32_t source_column;
    size_t json_pointer_utf8_bytes;
    size_t subject_kind_utf8_bytes;
    size_t subject_id_utf8_bytes;
    size_t message_utf8_bytes;
    size_t related_count;
} eso_diagnostic_info_t;

typedef struct eso_diagnostic_text_buffers {
    eso_mutable_utf8_buffer_t json_pointer;
    eso_mutable_utf8_buffer_t subject_kind;
    eso_mutable_utf8_buffer_t subject_id;
    eso_mutable_utf8_buffer_t message;
} eso_diagnostic_text_buffers_t;

typedef struct eso_related_diagnostic_info {
    uint32_t has_subject;
    size_t json_pointer_utf8_bytes;
    size_t subject_kind_utf8_bytes;
    size_t subject_id_utf8_bytes;
    size_t message_utf8_bytes;
} eso_related_diagnostic_info_t;

typedef struct eso_abi_layout {
    uint32_t api_version;
    uint32_t pointer_size_bytes;
    uint32_t size_type_size_bytes;
    uint32_t float_size_bytes;
    uint32_t double_size_bytes;
    uint32_t little_endian;
    uint32_t control_command_size_bytes;
    uint32_t session_descriptor_size_bytes;
    uint32_t forward_gear_descriptor_size_bytes;
    uint32_t audio_bus_descriptor_size_bytes;
    uint32_t session_telemetry_size_bytes;
    uint32_t completed_cycle_evidence_size_bytes;
} eso_abi_layout_t;

typedef uint32_t eso_live_control_capability_mask_t;
enum {
    ESO_LIVE_CONTROL_CAPABILITY_THROTTLE = UINT32_C(1) << 0U,
    ESO_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED = UINT32_C(1) << 1U,
    ESO_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED = UINT32_C(1) << 2U,
    ESO_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED = UINT32_C(1) << 3U,
    ESO_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE = UINT32_C(1) << 4U,
    ESO_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED = UINT32_C(1) << 5U,
    ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_TARGET_ENGINE_SPEED = UINT32_C(1) << 6U,
    ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE = UINT32_C(1) << 7U,
    ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_DRIVING_TORQUE = UINT32_C(1) << 8U,
    ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SELECTED_FORWARD_GEAR = UINT32_C(1) << 9U,
    ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_CLUTCH_ENGAGEMENT = UINT32_C(1) << 10U,
    ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SERVICE_BRAKE_APPLICATION = UINT32_C(1) << 11U
};

typedef uint32_t eso_session_execution_kind_t;
enum {
    ESO_SESSION_EXECUTION_FINITE_SCENARIO = 1,
    ESO_SESSION_EXECUTION_OPEN_ENDED = 2
};

typedef uint32_t eso_motion_mode_t;
enum {
    ESO_MOTION_HELD_SPEED = 1,
    ESO_MOTION_PRESCRIBED_KINEMATIC_SWEEP = 2,
    ESO_MOTION_HELD_DYNO = 3,
    ESO_MOTION_LOAD_TARGET_HELD_CAPTURE = 4,
    ESO_MOTION_INERTIAL_DYNO = 5,
    ESO_MOTION_FREE_ENGINE = 6,
    ESO_MOTION_FREE_VEHICLE = 7
};

typedef struct eso_session_descriptor {
    uint32_t maximum_delivery_frames_per_process_call;
    uint32_t control_command_queue_capacity;
    uint32_t maximum_telemetry_frames_per_process_call;
    uint32_t maximum_cycle_evidence_per_process_call;
    uint64_t physics_rate_numerator_hz;
    uint64_t physics_rate_denominator;
    uint64_t delivery_rate_numerator_hz;
    uint64_t delivery_rate_denominator;
    uint32_t physics_frames_per_block;
    uint32_t delivery_frames_per_block;
    uint64_t total_block_count;
    uint64_t preparation_block_count;
    uint32_t audio_bus_count;
    eso_live_control_capability_mask_t live_control_capabilities;
    size_t engine_id_utf8_bytes;
    size_t scenario_id_utf8_bytes;
    /*
     * total_block_count is the exact authored horizon for FINITE_SCENARIO and
     * canonical zero for OPEN_ENDED. The execution kind is the discriminator;
     * callers must not infer it from the count.
     */
    eso_session_execution_kind_t execution_kind;
    eso_motion_mode_t motion_mode;
    uint32_t forward_gear_count;
} eso_session_descriptor_t;

typedef struct eso_session_identity_buffers {
    eso_mutable_utf8_buffer_t engine_id;
    eso_mutable_utf8_buffer_t scenario_id;
} eso_session_identity_buffers_t;

typedef struct eso_forward_gear_descriptor {
    uint32_t gear_id;
    uint32_t authored_ordinal;
    double ratio;
    size_t semantic_id_utf8_bytes;
} eso_forward_gear_descriptor_t;

typedef uint32_t eso_audio_bus_kind_t;
enum {
    ESO_AUDIO_BUS_SOURCE_ROUTE_DRY = 1,
    ESO_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER = 2,
    ESO_AUDIO_BUS_SOURCE_ROUTE_SELECTED = 3,
    ESO_AUDIO_BUS_ENGINE_RAW_MASTER = 4,
    ESO_AUDIO_BUS_ENGINE_AUDITION_MASTER = 5
};

typedef uint32_t eso_source_route_kind_t;
enum {
    ESO_SOURCE_ROUTE_UNSPECIFIED = 0,
    ESO_SOURCE_ROUTE_EXHAUST_OUTLET = 1,
    ESO_SOURCE_ROUTE_INTAKE_INLET = 2,
    ESO_SOURCE_ROUTE_MECHANICAL_ENGINE = 3,
    ESO_SOURCE_ROUTE_MECHANICAL_STARTER = 4
};

typedef uint32_t eso_audio_signal_disposition_t;
enum { ESO_AUDIO_SIGNAL_ACTIVE = 1, ESO_AUDIO_SIGNAL_DECLARED_SILENT = 2 };

typedef struct eso_audio_bus_descriptor {
    eso_audio_bus_kind_t kind;
    uint32_t channel_count;
    uint64_t sample_rate_numerator_hz;
    uint64_t sample_rate_denominator;
    uint32_t has_route_id;
    uint32_t route_id;
    eso_source_route_kind_t source_route_kind;
    eso_audio_signal_disposition_t signal_disposition;
    size_t id_utf8_bytes;
} eso_audio_bus_descriptor_t;

typedef uint32_t eso_control_kind_t;
enum {
    ESO_CONTROL_THROTTLE = 1,
    ESO_CONTROL_IGNITION_ENABLED = 2,
    ESO_CONTROL_FUEL_ENABLED = 3,
    ESO_CONTROL_LIMITER_ENABLED = 4,
    ESO_CONTROL_EXTERNAL_RESISTING_TORQUE = 5,
    ESO_CONTROL_STARTER_ENABLED = 6,
    ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED = 7,
    ESO_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE = 8,
    ESO_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE = 9,
    ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR = 10,
    ESO_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT = 11,
    ESO_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION = 12
};

/*
 * Boolean controls use enabled exactly 0 or 1 and require scalar_value to be
 * positive zero and id_value to be zero. Scalar controls require enabled and
 * id_value to be zero. The selected-forward-gear control requires enabled and
 * scalar_value to be zero; id_value is zero for neutral and otherwise carries
 * the one-based authored forward-gear ordinal. Negative zero is not canonical.
 * reserved must be zero.
 */
typedef struct eso_control_command {
    uint64_t delivery_frame;
    uint64_t sequence;
    eso_control_kind_t kind;
    uint32_t enabled;
    double scalar_value;
    uint32_t id_value;
    uint32_t reserved;
} eso_control_command_t;

typedef struct eso_control_rejection {
    eso_error_code_t code;
    size_t command_index;
} eso_control_rejection_t;

typedef uint32_t eso_availability_t;
enum { ESO_UNAVAILABLE = 0, ESO_AVAILABLE = 1 };

typedef uint32_t eso_completeness_t;
enum { ESO_INCOMPLETE = 0, ESO_COMPLETE = 1 };

typedef uint32_t eso_quantity_unavailable_reason_t;
enum {
    ESO_QUANTITY_UNAVAILABLE_NONE = 0,
    ESO_QUANTITY_UNAVAILABLE_SCENARIO_NOT_APPLICABLE = 1,
    ESO_QUANTITY_UNAVAILABLE_MODEL_NOT_ADMITTED = 2,
    ESO_QUANTITY_UNAVAILABLE_EQUIVALENT_INERTIA_MISSING = 3,
    ESO_QUANTITY_UNAVAILABLE_CYCLE_INTEGRATION_NOT_ADMITTED = 4,
    ESO_QUANTITY_UNAVAILABLE_NOT_SETTLED = 5,
    ESO_QUANTITY_UNAVAILABLE_REQUIRED_INPUT_MISSING = 6
};

typedef struct eso_quantity_value {
    double value;
    eso_availability_t availability;
    eso_completeness_t completeness;
    eso_quantity_unavailable_reason_t unavailable_reason;
} eso_quantity_value_t;

typedef struct eso_torque_value_nm {
    double value_nm;
    eso_availability_t availability;
    eso_completeness_t completeness;
    eso_quantity_unavailable_reason_t unavailable_reason;
    uint64_t included_terms;
    uint64_t omitted_terms;
} eso_torque_value_nm_t;

typedef struct eso_torque_telemetry {
    eso_torque_value_nm_t instantaneous_indicated_gas;
    eso_torque_value_nm_t pumping_partition;
    eso_torque_value_nm_t friction_pump_and_accessory;
    eso_torque_value_nm_t starter;
    eso_torque_value_nm_t instantaneous_net_shaft;
    eso_torque_value_nm_t cycle_mean_net_shaft;
    eso_torque_value_nm_t actuator;
    eso_torque_value_nm_t dyno_reaction;
    eso_quantity_value_t cycle_work_j;
    eso_quantity_value_t net_bmep_pa;
    eso_quantity_value_t instantaneous_power_w;
    eso_quantity_value_t cycle_mean_power_w;
} eso_torque_telemetry_t;

typedef struct eso_engine_telemetry {
    uint64_t engine_step_end_index;
    uint32_t validity_mask;
    uint32_t ignition_enabled;
    uint32_t fuel_enabled;
    uint32_t starter_enabled;
    uint32_t dyno_enabled;
    uint32_t limiter_enabled;
    uint32_t limiter_cut_active;
    double theta_rad;
    double theta_cycle_rad;
    double angular_speed_rad_s;
    double angular_acceleration_rad_s2;
    double engine_speed_rpm;
    double requested_throttle_01;
    double resolved_engine_throttle_01;
    double intake_plate_position_01;
    double main_flow_multiplier_01;
    double requested_external_resisting_torque_nm;
    eso_torque_telemetry_t torque;
} eso_engine_telemetry_t;

typedef uint32_t eso_held_dyno_disposition_t;
enum {
    ESO_HELD_DYNO_TRACKING = 1,
    ESO_HELD_DYNO_ABSORBING_TORQUE_LIMITED = 2,
    ESO_HELD_DYNO_DRIVING_TORQUE_LIMITED = 3
};

typedef struct eso_held_dyno_telemetry {
    double target_engine_speed_rpm;
    double maximum_absorbing_torque_nm;
    double maximum_driving_torque_nm;
    double required_actuator_torque_nm;
    double applied_actuator_torque_nm;
    eso_held_dyno_disposition_t disposition;
} eso_held_dyno_telemetry_t;

typedef uint32_t eso_clutch_disposition_t;
enum {
    ESO_CLUTCH_NEUTRAL = 1,
    ESO_CLUTCH_DISENGAGED = 2,
    ESO_CLUTCH_ENGINE_DRIVING_TORQUE_LIMITED = 3,
    ESO_CLUTCH_VEHICLE_BACKDRIVE_TORQUE_LIMITED = 4,
    ESO_CLUTCH_TRACKING = 5
};

typedef uint32_t eso_road_load_disposition_t;
enum {
    ESO_ROAD_LOAD_MOVING = 1,
    ESO_ROAD_LOAD_STOPPED_WITHIN_STEP = 2,
    ESO_ROAD_LOAD_HELD_AT_REST = 3
};

typedef struct eso_free_vehicle_telemetry {
    double vehicle_speed_m_s;
    double vehicle_distance_m;
    uint32_t has_selected_forward_gear;
    uint32_t selected_forward_gear_ordinal;
    double clutch_engagement_01;
    double service_brake_application_01;
    eso_clutch_disposition_t clutch_disposition;
    uint32_t has_final_clutch_slip;
    double clutch_torque_capacity_nm;
    double applied_average_clutch_torque_on_engine_nm;
    double final_clutch_slip_rad_s;
    eso_road_load_disposition_t road_load_disposition;
    double requested_road_load_force_n;
    double applied_average_road_load_force_n;
} eso_free_vehicle_telemetry_t;

/*
 * has_held_dyno and has_free_vehicle are canonical 0/1 discriminators. An
 * absent sidecar is returned as an all-zero POD. The selected-gear and
 * final-clutch-slip presence fields follow the same rule within FreeVehicle.
 */
typedef struct eso_session_telemetry {
    uint64_t physics_step_end;
    double mean_intake_manifold_pressure_pa_abs;
    eso_engine_telemetry_t engine;
    uint32_t has_held_dyno;
    uint32_t has_free_vehicle;
    eso_held_dyno_telemetry_t held_dyno;
    eso_free_vehicle_telemetry_t free_vehicle;
} eso_session_telemetry_t;

typedef uint32_t eso_engine_cycle_state_flag_mask_t;
enum {
    ESO_ENGINE_CYCLE_STATE_IGNITION_ENABLED = UINT32_C(1) << 0U,
    ESO_ENGINE_CYCLE_STATE_FUEL_ENABLED = UINT32_C(1) << 1U,
    ESO_ENGINE_CYCLE_STATE_STARTER_ENABLED = UINT32_C(1) << 2U,
    ESO_ENGINE_CYCLE_STATE_DYNO_ENABLED = UINT32_C(1) << 3U,
    ESO_ENGINE_CYCLE_STATE_LIMITER_ENABLED = UINT32_C(1) << 4U,
    ESO_ENGINE_CYCLE_STATE_LIMITER_CUT_ACTIVE = UINT32_C(1) << 5U
};

typedef struct eso_cycle_boundary_evidence {
    int64_t cycle_ordinal;
    uint64_t left_physics_frame;
    uint64_t right_physics_frame;
    double fraction_from_left_01;
    double theta_unwrapped_rad;
    double time_s;
    double delivery_frame;
} eso_cycle_boundary_evidence_t;

typedef struct eso_cycle_control_evidence {
    double time_weighted_mean_01;
    double minimum_01;
    double maximum_01;
    uint32_t change_count;
} eso_cycle_control_evidence_t;

typedef struct eso_cycle_net_shaft_evidence {
    double angular_work_j;
    double cycle_mean_torque_nm;
    eso_availability_t availability;
    eso_completeness_t completeness;
    eso_quantity_unavailable_reason_t unavailable_reason;
    uint64_t included_terms;
    uint64_t omitted_terms;
} eso_cycle_net_shaft_evidence_t;

/*
 * Exact completed 720-degree cycle evidence. Boundary delivery_frame values are
 * fractional by design; the integer ordinals and physics-frame brackets remain
 * exact 64-bit values across native and WASM callers.
 */
typedef struct eso_completed_cycle_evidence {
    uint64_t completed_cycle_ordinal;
    eso_cycle_boundary_evidence_t start_boundary;
    eso_cycle_boundary_evidence_t end_boundary;
    double duration_s;
    double mean_engine_speed_rpm;
    eso_cycle_control_evidence_t requested_throttle;
    eso_cycle_control_evidence_t resolved_engine_throttle;
    eso_cycle_control_evidence_t intake_plate_position;
    eso_cycle_net_shaft_evidence_t instantaneous_net_shaft;
    eso_engine_cycle_state_flag_mask_t start_state_flags;
    eso_engine_cycle_state_flag_mask_t end_state_flags;
    eso_engine_cycle_state_flag_mask_t state_transition_flags;
} eso_completed_cycle_evidence_t;

typedef struct eso_audio_copy_buffer {
    uint32_t bus_index;
    float *samples;
    size_t sample_capacity;
    size_t samples_written;
} eso_audio_copy_buffer_t;

typedef uint32_t eso_process_kind_t;
enum { ESO_PROCESS_BLOCK = 1, ESO_PROCESS_COMPLETED = 2 };

typedef uint32_t eso_block_phase_t;
enum { ESO_BLOCK_PREPARATION = 1, ESO_BLOCK_AUDIBLE = 2 };

typedef struct eso_process_info {
    eso_process_kind_t kind;
    eso_block_phase_t block_phase;
    uint64_t block_ordinal;
    uint64_t first_physics_frame;
    uint32_t physics_frame_count;
    uint64_t first_delivery_frame;
    uint32_t delivery_frame_count;
    size_t telemetry_written;
    size_t cycle_evidence_written;
    uint64_t completed_physics_frame_count;
    uint64_t completed_delivery_frame_count;
    uint64_t completed_block_count;
    uint32_t live_controls_accepted;
    uint32_t has_held_speed_operating_point;
    uint32_t has_inertial_dyno_result;
} eso_process_info_t;

uint32_t eso_api_version(void) ESO_C_API_NOEXCEPT;
eso_status_t eso_get_abi_layout(eso_abi_layout_t *out_layout) ESO_C_API_NOEXCEPT;

eso_status_t eso_context_create(uint32_t requested_api_version,
                                eso_context_t **out_context) ESO_C_API_NOEXCEPT;
eso_status_t eso_context_destroy(eso_context_t *context) ESO_C_API_NOEXCEPT;

/*
 * Error inspection never clears or replaces the recorded error. String byte
 * counts exclude the trailing NUL. A {NULL, 0} text buffer skips that field.
 */
eso_status_t eso_context_get_last_error(const eso_context_t *context,
                                        eso_error_info_t *out_error) ESO_C_API_NOEXCEPT;
eso_status_t
eso_context_copy_last_error_text(const eso_context_t *context,
                                 eso_error_text_buffers_t *buffers) ESO_C_API_NOEXCEPT;
eso_status_t
eso_context_get_diagnostic(const eso_context_t *context, size_t diagnostic_index,
                           eso_diagnostic_info_t *out_diagnostic) ESO_C_API_NOEXCEPT;
eso_status_t eso_context_copy_diagnostic_text(
    const eso_context_t *context, size_t diagnostic_index,
    eso_diagnostic_text_buffers_t *buffers) ESO_C_API_NOEXCEPT;
eso_status_t eso_context_get_related_diagnostic(
    const eso_context_t *context, size_t diagnostic_index, size_t related_index,
    eso_related_diagnostic_info_t *out_related) ESO_C_API_NOEXCEPT;
eso_status_t eso_context_copy_related_diagnostic_text(
    const eso_context_t *context, size_t diagnostic_index, size_t related_index,
    eso_diagnostic_text_buffers_t *buffers) ESO_C_API_NOEXCEPT;

eso_status_t
eso_compile_engine_json(eso_context_t *context, eso_utf8_view_t engine_json,
                        const eso_asset_payload_t *assets, size_t asset_count,
                        eso_engine_handle_t *out_engine) ESO_C_API_NOEXCEPT;
eso_status_t eso_destroy_engine(eso_context_t *context,
                                eso_engine_handle_t engine) ESO_C_API_NOEXCEPT;
eso_status_t eso_engine_copy_id(eso_context_t *context, eso_engine_handle_t engine,
                                eso_mutable_utf8_buffer_t buffer,
                                size_t *out_utf8_bytes) ESO_C_API_NOEXCEPT;
/*
 * Copies the compiled engine's canonical provenance bundle SHA-256 in digest byte
 * order. Hex encoders must encode bytes[0] first and use two digits per byte.
 */
eso_status_t
eso_engine_copy_provenance_sha256(eso_context_t *context, eso_engine_handle_t engine,
                                  eso_sha256_digest_t *out_sha256) ESO_C_API_NOEXCEPT;
/*
 * Copies the source-closure SHA-256 embedded in this renderer build. Builds whose
 * source stamp is dirty, unavailable, or malformed return ESO_STATUS_NOT_AVAILABLE.
 */
eso_status_t eso_renderer_copy_source_closure_sha256(
    eso_context_t *context, eso_sha256_digest_t *out_sha256) ESO_C_API_NOEXCEPT;

eso_status_t
eso_compile_scenario_json(eso_context_t *context, eso_engine_handle_t engine,
                          eso_utf8_view_t scenario_json,
                          eso_scenario_handle_t *out_scenario) ESO_C_API_NOEXCEPT;
eso_status_t eso_destroy_scenario(eso_context_t *context,
                                  eso_scenario_handle_t scenario) ESO_C_API_NOEXCEPT;
eso_status_t eso_scenario_copy_id(eso_context_t *context,
                                  eso_scenario_handle_t scenario,
                                  eso_mutable_utf8_buffer_t buffer,
                                  size_t *out_utf8_bytes) ESO_C_API_NOEXCEPT;

eso_status_t eso_create_session(eso_context_t *context, eso_scenario_handle_t scenario,
                                eso_session_execution_kind_t execution_kind,
                                eso_session_handle_t *out_session) ESO_C_API_NOEXCEPT;
eso_status_t eso_destroy_session(eso_context_t *context,
                                 eso_session_handle_t session) ESO_C_API_NOEXCEPT;
eso_status_t
eso_session_get_descriptor(eso_context_t *context, eso_session_handle_t session,
                           eso_session_descriptor_t *out_descriptor) ESO_C_API_NOEXCEPT;
eso_status_t
eso_session_copy_identity(eso_context_t *context, eso_session_handle_t session,
                          eso_session_identity_buffers_t *buffers) ESO_C_API_NOEXCEPT;
eso_status_t eso_session_get_audio_bus_descriptor(
    eso_context_t *context, eso_session_handle_t session, uint32_t bus_index,
    eso_audio_bus_descriptor_t *out_descriptor) ESO_C_API_NOEXCEPT;
eso_status_t
eso_session_copy_audio_bus_id(eso_context_t *context, eso_session_handle_t session,
                              uint32_t bus_index,
                              eso_mutable_utf8_buffer_t buffer) ESO_C_API_NOEXCEPT;
eso_status_t eso_session_get_forward_gear_descriptor(
    eso_context_t *context, eso_session_handle_t session, uint32_t gear_index,
    eso_forward_gear_descriptor_t *out_descriptor) ESO_C_API_NOEXCEPT;
eso_status_t eso_session_copy_forward_gear_semantic_id(
    eso_context_t *context, eso_session_handle_t session, uint32_t gear_index,
    eso_mutable_utf8_buffer_t buffer) ESO_C_API_NOEXCEPT;

eso_status_t
eso_session_enqueue_controls(eso_context_t *context, eso_session_handle_t session,
                             const eso_control_command_t *commands,
                             size_t command_count,
                             eso_control_rejection_t *out_rejection) ESO_C_API_NOEXCEPT;

/*
 * Every supplied audio buffer names one bus to copy. Omitted buses are discarded.
 * All capacities are checked before process_block advances the session. Passing
 * {NULL, 0} for telemetry or cycle evidence discards that output; otherwise the
 * corresponding capacity must hold one complete returned block.
 */
eso_status_t eso_session_process(eso_context_t *context, eso_session_handle_t session,
                                 eso_audio_copy_buffer_t *audio_buffers,
                                 size_t audio_buffer_count,
                                 eso_session_telemetry_t *telemetry,
                                 size_t telemetry_capacity,
                                 eso_completed_cycle_evidence_t *cycle_evidence,
                                 size_t cycle_evidence_capacity,
                                 eso_process_info_t *out_process) ESO_C_API_NOEXCEPT;

#if defined(__cplusplus)
} /* extern "C" */
#endif

#undef ESO_C_API_NOEXCEPT

#endif /* ENGINE_SIM_OFFLINE_C_API_H */
