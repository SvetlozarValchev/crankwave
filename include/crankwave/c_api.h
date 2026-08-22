#ifndef CRANKWAVE_C_API_H
#define CRANKWAVE_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define CRANKWAVE_C_API_NOEXCEPT noexcept
extern "C" {
#else
#define CRANKWAVE_C_API_NOEXCEPT
#endif

/*
 * This is the only crankwave C ABI. It is a greenfield, exact-version
 * contract rather than a compatibility family.
 */
#define CRANKWAVE_C_API_VERSION UINT32_C(10)
#define CRANKWAVE_INVALID_HANDLE UINT64_C(0)
#define CRANKWAVE_SHA256_DIGEST_SIZE UINT32_C(32)

typedef struct crankwave_context crankwave_context_t;
typedef uint64_t crankwave_engine_handle_t;
typedef uint64_t crankwave_scenario_handle_t;
typedef uint64_t crankwave_session_handle_t;
typedef uint64_t crankwave_package_handle_t;

typedef uint32_t crankwave_status_t;
enum {
    CRANKWAVE_STATUS_OK = 0,
    CRANKWAVE_STATUS_ABI_VERSION_MISMATCH = 1,
    CRANKWAVE_STATUS_INVALID_ARGUMENT = 2,
    CRANKWAVE_STATUS_INVALID_HANDLE = 3,
    CRANKWAVE_STATUS_NOT_AVAILABLE = 4,
    CRANKWAVE_STATUS_BUFFER_TOO_SMALL = 5,
    CRANKWAVE_STATUS_RESOURCE_EXHAUSTED = 6,
    CRANKWAVE_STATUS_ENGINE_PARSE_FAILED = 7,
    CRANKWAVE_STATUS_ENGINE_COMPILE_FAILED = 8,
    CRANKWAVE_STATUS_SCENARIO_PARSE_FAILED = 9,
    CRANKWAVE_STATUS_SCENARIO_COMPILE_FAILED = 10,
    CRANKWAVE_STATUS_SESSION_CREATE_FAILED = 11,
    CRANKWAVE_STATUS_CONTROL_REJECTED = 12,
    CRANKWAVE_STATUS_PROCESS_FAILED = 13,
    CRANKWAVE_STATUS_INTERNAL_ERROR = 14,
    CRANKWAVE_STATUS_BAKE_FAILED = 15
};

typedef uint32_t crankwave_error_stage_t;
enum {
    CRANKWAVE_ERROR_STAGE_NONE = 0,
    CRANKWAVE_ERROR_STAGE_ARGUMENT = 1,
    CRANKWAVE_ERROR_STAGE_HANDLE = 2,
    CRANKWAVE_ERROR_STAGE_ENGINE_PARSE = 3,
    CRANKWAVE_ERROR_STAGE_ENGINE_COMPILE = 4,
    CRANKWAVE_ERROR_STAGE_SCENARIO_PARSE = 5,
    CRANKWAVE_ERROR_STAGE_SCENARIO_COMPILE = 6,
    CRANKWAVE_ERROR_STAGE_SESSION_CREATE = 7,
    CRANKWAVE_ERROR_STAGE_CONTROL = 8,
    CRANKWAVE_ERROR_STAGE_PROCESS = 9,
    CRANKWAVE_ERROR_STAGE_ABI = 10,
    CRANKWAVE_ERROR_STAGE_BAKE = 11
};

/*
 * Domain codes are stable values carried by crankwave_error_info_t. Authoring
 * diagnostics have their own code in crankwave_diagnostic_info_t.
 */
typedef uint32_t crankwave_error_code_t;
enum {
    CRANKWAVE_ERROR_NONE = 0,
    CRANKWAVE_ERROR_INVALID_POINTER = 1,
    CRANKWAVE_ERROR_INVALID_COUNT = 2,
    CRANKWAVE_ERROR_INVALID_ENUM = 3,
    CRANKWAVE_ERROR_INVALID_HANDLE = 4,
    CRANKWAVE_ERROR_BUFFER_CAPACITY = 5,
    CRANKWAVE_ERROR_AUTHORING_DIAGNOSTICS = 6,
    CRANKWAVE_ERROR_RENDERER_SOURCE_STAMP_UNAVAILABLE = 7,
    CRANKWAVE_ERROR_SESSION_INVALID_COMPILED_SCENARIO = 100,
    CRANKWAVE_ERROR_SESSION_UNSUPPORTED_CONFIGURATION = 101,
    CRANKWAVE_ERROR_SESSION_RESOURCE_EXHAUSTED = 102,
    CRANKWAVE_ERROR_SESSION_PROCESSING_FAILED = 103,
    CRANKWAVE_ERROR_SESSION_CONSUMER_STATE_INVALID = 104,
    CRANKWAVE_ERROR_SESSION_INTERNAL = 105,
    CRANKWAVE_ERROR_CONTROL_CAPACITY_EXCEEDED = 200,
    CRANKWAVE_ERROR_CONTROL_LATE_COMMAND = 201,
    CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD = 202,
    CRANKWAVE_ERROR_CONTROL_UNORDERED_DELIVERY_FRAME = 203,
    CRANKWAVE_ERROR_CONTROL_DUPLICATE_SEQUENCE = 204,
    CRANKWAVE_ERROR_CONTROL_UNORDERED_SEQUENCE = 205,
    CRANKWAVE_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE = 206,
    CRANKWAVE_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION = 207,
    CRANKWAVE_ERROR_CONTROL_OUTSIDE_SESSION_HORIZON = 208,
    CRANKWAVE_ERROR_CONTROL_SESSION_TERMINAL = 209,
    CRANKWAVE_ERROR_CONTROL_INTERNAL_CLOCK = 210,
    CRANKWAVE_ERROR_BAKE_PROFILE = 300,
    CRANKWAVE_ERROR_BAKE_SCENARIO = 301,
    CRANKWAVE_ERROR_BAKE_CAPTURE = 302,
    CRANKWAVE_ERROR_BAKE_COOK = 303,
    CRANKWAVE_ERROR_BAKE_PACKAGE = 304,
    CRANKWAVE_ERROR_BAKE_IDENTITY = 305
};

typedef struct crankwave_utf8_view {
    const char *data;
    size_t size;
} crankwave_utf8_view_t;

typedef struct crankwave_byte_view {
    const uint8_t *data;
    size_t size;
} crankwave_byte_view_t;

typedef struct crankwave_mutable_utf8_buffer {
    char *data;
    size_t capacity;
} crankwave_mutable_utf8_buffer_t;

typedef struct crankwave_sha256_digest {
    uint8_t bytes[CRANKWAVE_SHA256_DIGEST_SIZE];
} crankwave_sha256_digest_t;

typedef struct crankwave_bake_inputs {
    crankwave_utf8_view_t engine_json;
    const struct crankwave_asset_payload *assets;
    size_t asset_count;
    crankwave_byte_view_t shared_starter_runtime_json;
    crankwave_byte_view_t shared_starter_audio;
    crankwave_utf8_view_t release_identity;
    crankwave_sha256_digest_t wasm_module_sha256;
    crankwave_sha256_digest_t asset_catalog_sha256;
} crankwave_bake_inputs_t;

typedef struct crankwave_package_descriptor {
    uint64_t container_byte_count;
    uint64_t entry_count;
    uint64_t held_cell_count;
    uint64_t directional_capture_count;
    uint64_t lifecycle_capture_count;
    size_t engine_id_utf8_bytes;
    size_t profile_id_utf8_bytes;
    crankwave_sha256_digest_t container_sha256;
    crankwave_sha256_digest_t cache_identity_sha256;
} crankwave_package_descriptor_t;

typedef struct crankwave_package_identity_buffers {
    crankwave_mutable_utf8_buffer_t engine_id;
    crankwave_mutable_utf8_buffer_t profile_id;
} crankwave_package_identity_buffers_t;

typedef uint32_t crankwave_asset_kind_t;
enum { CRANKWAVE_ASSET_AUDIO = 1, CRANKWAVE_ASSET_ACCESSORY_CONFIGURATION = 2 };

typedef struct crankwave_asset_payload {
    crankwave_asset_kind_t kind;
    crankwave_utf8_view_t asset_id;
    crankwave_byte_view_t bytes;
} crankwave_asset_payload_t;

typedef uint32_t crankwave_diagnostic_severity_t;
enum { CRANKWAVE_DIAGNOSTIC_ERROR = 1, CRANKWAVE_DIAGNOSTIC_WARNING = 2 };

typedef uint32_t crankwave_diagnostic_code_t;
enum {
    CRANKWAVE_DIAGNOSTIC_MALFORMED_DOCUMENT = 1,
    CRANKWAVE_DIAGNOSTIC_UNSUPPORTED_SCHEMA = 2,
    CRANKWAVE_DIAGNOSTIC_MISSING_VALUE = 3,
    CRANKWAVE_DIAGNOSTIC_UNKNOWN_FIELD = 4,
    CRANKWAVE_DIAGNOSTIC_INVALID_TYPE = 5,
    CRANKWAVE_DIAGNOSTIC_INVALID_UNIT = 6,
    CRANKWAVE_DIAGNOSTIC_INVALID_VALUE = 7,
    CRANKWAVE_DIAGNOSTIC_OUT_OF_RANGE = 8,
    CRANKWAVE_DIAGNOSTIC_DUPLICATE_ID = 9,
    CRANKWAVE_DIAGNOSTIC_DANGLING_REFERENCE = 10,
    CRANKWAVE_DIAGNOSTIC_FORBIDDEN_CYCLE = 11,
    CRANKWAVE_DIAGNOSTIC_DISCONNECTED_OBJECT = 12,
    CRANKWAVE_DIAGNOSTIC_INCONSISTENT_VALUE = 13,
    CRANKWAVE_DIAGNOSTIC_UNSUPPORTED_CAPABILITY = 14,
    CRANKWAVE_DIAGNOSTIC_MISSING_ASSET = 15,
    CRANKWAVE_DIAGNOSTIC_ASSET_HASH_MISMATCH = 16,
    CRANKWAVE_DIAGNOSTIC_RESOURCE_LIMIT = 17,
    CRANKWAVE_DIAGNOSTIC_INTERNAL_FAILURE = 18
};

typedef struct crankwave_error_info {
    crankwave_status_t status;
    crankwave_error_stage_t stage;
    crankwave_error_code_t code;
    size_t detail_code_utf8_bytes;
    size_t message_utf8_bytes;
    size_t diagnostic_count;
} crankwave_error_info_t;

typedef struct crankwave_error_text_buffers {
    crankwave_mutable_utf8_buffer_t detail_code;
    crankwave_mutable_utf8_buffer_t message;
} crankwave_error_text_buffers_t;

typedef struct crankwave_diagnostic_info {
    crankwave_diagnostic_severity_t severity;
    crankwave_diagnostic_code_t code;
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
} crankwave_diagnostic_info_t;

typedef struct crankwave_diagnostic_text_buffers {
    crankwave_mutable_utf8_buffer_t json_pointer;
    crankwave_mutable_utf8_buffer_t subject_kind;
    crankwave_mutable_utf8_buffer_t subject_id;
    crankwave_mutable_utf8_buffer_t message;
} crankwave_diagnostic_text_buffers_t;

typedef struct crankwave_related_diagnostic_info {
    uint32_t has_subject;
    size_t json_pointer_utf8_bytes;
    size_t subject_kind_utf8_bytes;
    size_t subject_id_utf8_bytes;
    size_t message_utf8_bytes;
} crankwave_related_diagnostic_info_t;

typedef struct crankwave_abi_layout {
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
} crankwave_abi_layout_t;

typedef uint32_t crankwave_live_control_capability_mask_t;
enum {
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_THROTTLE = UINT32_C(1) << 0U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED = UINT32_C(1) << 1U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED = UINT32_C(1) << 2U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED = UINT32_C(1) << 3U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE = UINT32_C(1) << 4U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED = UINT32_C(1) << 5U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_TARGET_ENGINE_SPEED = UINT32_C(1) << 6U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE = UINT32_C(1) << 7U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_DRIVING_TORQUE = UINT32_C(1) << 8U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_SELECTED_FORWARD_GEAR = UINT32_C(1) << 9U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_CLUTCH_ENGAGEMENT = UINT32_C(1) << 10U,
    CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_SERVICE_BRAKE_APPLICATION = UINT32_C(1) << 11U
};

typedef uint32_t crankwave_session_execution_kind_t;
enum {
    CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO = 1,
    CRANKWAVE_SESSION_EXECUTION_OPEN_ENDED = 2
};

typedef uint32_t crankwave_motion_mode_t;
enum {
    CRANKWAVE_MOTION_HELD_SPEED = 1,
    CRANKWAVE_MOTION_PRESCRIBED_KINEMATIC_SWEEP = 2,
    CRANKWAVE_MOTION_HELD_DYNO = 3,
    CRANKWAVE_MOTION_LOAD_TARGET_HELD_CAPTURE = 4,
    CRANKWAVE_MOTION_INERTIAL_DYNO = 5,
    CRANKWAVE_MOTION_FREE_ENGINE = 6,
    CRANKWAVE_MOTION_FREE_VEHICLE = 7
};

typedef struct crankwave_session_descriptor {
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
    crankwave_live_control_capability_mask_t live_control_capabilities;
    size_t engine_id_utf8_bytes;
    size_t scenario_id_utf8_bytes;
    /*
     * total_block_count is the exact authored horizon for FINITE_SCENARIO and
     * canonical zero for OPEN_ENDED. The execution kind is the discriminator;
     * callers must not infer it from the count.
     */
    crankwave_session_execution_kind_t execution_kind;
    crankwave_motion_mode_t motion_mode;
    uint32_t forward_gear_count;
} crankwave_session_descriptor_t;

typedef struct crankwave_session_identity_buffers {
    crankwave_mutable_utf8_buffer_t engine_id;
    crankwave_mutable_utf8_buffer_t scenario_id;
} crankwave_session_identity_buffers_t;

typedef struct crankwave_forward_gear_descriptor {
    uint32_t gear_id;
    uint32_t authored_ordinal;
    double ratio;
    size_t semantic_id_utf8_bytes;
} crankwave_forward_gear_descriptor_t;

typedef uint32_t crankwave_audio_bus_kind_t;
enum {
    CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_DRY = 1,
    CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER = 2,
    CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_SELECTED = 3,
    CRANKWAVE_AUDIO_BUS_ENGINE_RAW_MASTER = 4,
    CRANKWAVE_AUDIO_BUS_ENGINE_AUDITION_MASTER = 5
};

typedef uint32_t crankwave_source_route_kind_t;
enum {
    CRANKWAVE_SOURCE_ROUTE_UNSPECIFIED = 0,
    CRANKWAVE_SOURCE_ROUTE_EXHAUST_OUTLET = 1,
    CRANKWAVE_SOURCE_ROUTE_INTAKE_INLET = 2,
    CRANKWAVE_SOURCE_ROUTE_MECHANICAL_ENGINE = 3,
    CRANKWAVE_SOURCE_ROUTE_MECHANICAL_STARTER = 4
};

typedef uint32_t crankwave_audio_signal_disposition_t;
enum { CRANKWAVE_AUDIO_SIGNAL_ACTIVE = 1, CRANKWAVE_AUDIO_SIGNAL_DECLARED_SILENT = 2 };

typedef struct crankwave_audio_bus_descriptor {
    crankwave_audio_bus_kind_t kind;
    uint32_t channel_count;
    uint64_t sample_rate_numerator_hz;
    uint64_t sample_rate_denominator;
    uint32_t has_route_id;
    uint32_t route_id;
    crankwave_source_route_kind_t source_route_kind;
    crankwave_audio_signal_disposition_t signal_disposition;
    size_t id_utf8_bytes;
} crankwave_audio_bus_descriptor_t;

typedef uint32_t crankwave_control_kind_t;
enum {
    CRANKWAVE_CONTROL_THROTTLE = 1,
    CRANKWAVE_CONTROL_IGNITION_ENABLED = 2,
    CRANKWAVE_CONTROL_FUEL_ENABLED = 3,
    CRANKWAVE_CONTROL_LIMITER_ENABLED = 4,
    CRANKWAVE_CONTROL_EXTERNAL_RESISTING_TORQUE = 5,
    CRANKWAVE_CONTROL_STARTER_ENABLED = 6,
    CRANKWAVE_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED = 7,
    CRANKWAVE_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE = 8,
    CRANKWAVE_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE = 9,
    CRANKWAVE_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR = 10,
    CRANKWAVE_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT = 11,
    CRANKWAVE_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION = 12
};

/*
 * Boolean controls use enabled exactly 0 or 1 and require scalar_value to be
 * positive zero and id_value to be zero. Scalar controls require enabled and
 * id_value to be zero. The selected-forward-gear control requires enabled and
 * scalar_value to be zero; id_value is zero for neutral and otherwise carries
 * the one-based authored forward-gear ordinal. Negative zero is not canonical.
 * reserved must be zero.
 */
typedef struct crankwave_control_command {
    uint64_t delivery_frame;
    uint64_t sequence;
    crankwave_control_kind_t kind;
    uint32_t enabled;
    double scalar_value;
    uint32_t id_value;
    uint32_t reserved;
} crankwave_control_command_t;

typedef struct crankwave_control_rejection {
    crankwave_error_code_t code;
    size_t command_index;
} crankwave_control_rejection_t;

typedef uint32_t crankwave_availability_t;
enum { CRANKWAVE_UNAVAILABLE = 0, CRANKWAVE_AVAILABLE = 1 };

typedef uint32_t crankwave_completeness_t;
enum { CRANKWAVE_INCOMPLETE = 0, CRANKWAVE_COMPLETE = 1 };

typedef uint32_t crankwave_quantity_unavailable_reason_t;
enum {
    CRANKWAVE_QUANTITY_UNAVAILABLE_NONE = 0,
    CRANKWAVE_QUANTITY_UNAVAILABLE_SCENARIO_NOT_APPLICABLE = 1,
    CRANKWAVE_QUANTITY_UNAVAILABLE_MODEL_NOT_ADMITTED = 2,
    CRANKWAVE_QUANTITY_UNAVAILABLE_EQUIVALENT_INERTIA_MISSING = 3,
    CRANKWAVE_QUANTITY_UNAVAILABLE_CYCLE_INTEGRATION_NOT_ADMITTED = 4,
    CRANKWAVE_QUANTITY_UNAVAILABLE_NOT_SETTLED = 5,
    CRANKWAVE_QUANTITY_UNAVAILABLE_REQUIRED_INPUT_MISSING = 6
};

typedef struct crankwave_quantity_value {
    double value;
    crankwave_availability_t availability;
    crankwave_completeness_t completeness;
    crankwave_quantity_unavailable_reason_t unavailable_reason;
} crankwave_quantity_value_t;

typedef struct crankwave_torque_value_nm {
    double value_nm;
    crankwave_availability_t availability;
    crankwave_completeness_t completeness;
    crankwave_quantity_unavailable_reason_t unavailable_reason;
    uint64_t included_terms;
    uint64_t omitted_terms;
} crankwave_torque_value_nm_t;

typedef struct crankwave_torque_telemetry {
    crankwave_torque_value_nm_t instantaneous_indicated_gas;
    crankwave_torque_value_nm_t pumping_partition;
    crankwave_torque_value_nm_t friction_pump_and_accessory;
    crankwave_torque_value_nm_t starter;
    crankwave_torque_value_nm_t instantaneous_net_shaft;
    crankwave_torque_value_nm_t cycle_mean_net_shaft;
    crankwave_torque_value_nm_t actuator;
    crankwave_torque_value_nm_t dyno_reaction;
    crankwave_quantity_value_t cycle_work_j;
    crankwave_quantity_value_t net_bmep_pa;
    crankwave_quantity_value_t instantaneous_power_w;
    crankwave_quantity_value_t cycle_mean_power_w;
} crankwave_torque_telemetry_t;

typedef struct crankwave_engine_telemetry {
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
    crankwave_torque_telemetry_t torque;
} crankwave_engine_telemetry_t;

typedef uint32_t crankwave_held_dyno_disposition_t;
enum {
    CRANKWAVE_HELD_DYNO_TRACKING = 1,
    CRANKWAVE_HELD_DYNO_ABSORBING_TORQUE_LIMITED = 2,
    CRANKWAVE_HELD_DYNO_DRIVING_TORQUE_LIMITED = 3
};

typedef struct crankwave_held_dyno_telemetry {
    double target_engine_speed_rpm;
    double maximum_absorbing_torque_nm;
    double maximum_driving_torque_nm;
    double required_actuator_torque_nm;
    double applied_actuator_torque_nm;
    crankwave_held_dyno_disposition_t disposition;
} crankwave_held_dyno_telemetry_t;

typedef uint32_t crankwave_clutch_disposition_t;
enum {
    CRANKWAVE_CLUTCH_NEUTRAL = 1,
    CRANKWAVE_CLUTCH_DISENGAGED = 2,
    CRANKWAVE_CLUTCH_ENGINE_DRIVING_TORQUE_LIMITED = 3,
    CRANKWAVE_CLUTCH_VEHICLE_BACKDRIVE_TORQUE_LIMITED = 4,
    CRANKWAVE_CLUTCH_TRACKING = 5
};

typedef uint32_t crankwave_road_load_disposition_t;
enum {
    CRANKWAVE_ROAD_LOAD_MOVING = 1,
    CRANKWAVE_ROAD_LOAD_STOPPED_WITHIN_STEP = 2,
    CRANKWAVE_ROAD_LOAD_HELD_AT_REST = 3
};

typedef struct crankwave_free_vehicle_telemetry {
    double vehicle_speed_m_s;
    double vehicle_distance_m;
    uint32_t has_selected_forward_gear;
    uint32_t selected_forward_gear_ordinal;
    double clutch_engagement_01;
    double service_brake_application_01;
    crankwave_clutch_disposition_t clutch_disposition;
    uint32_t has_final_clutch_slip;
    double clutch_torque_capacity_nm;
    double applied_average_clutch_torque_on_engine_nm;
    double final_clutch_slip_rad_s;
    crankwave_road_load_disposition_t road_load_disposition;
    double requested_road_load_force_n;
    double applied_average_road_load_force_n;
} crankwave_free_vehicle_telemetry_t;

/*
 * has_held_dyno and has_free_vehicle are canonical 0/1 discriminators. An
 * absent sidecar is returned as an all-zero POD. The selected-gear and
 * final-clutch-slip presence fields follow the same rule within FreeVehicle.
 */
typedef struct crankwave_session_telemetry {
    uint64_t physics_step_end;
    double mean_intake_manifold_pressure_pa_abs;
    crankwave_engine_telemetry_t engine;
    uint32_t has_held_dyno;
    uint32_t has_free_vehicle;
    crankwave_held_dyno_telemetry_t held_dyno;
    crankwave_free_vehicle_telemetry_t free_vehicle;
} crankwave_session_telemetry_t;

typedef uint32_t crankwave_engine_cycle_state_flag_mask_t;
enum {
    CRANKWAVE_ENGINE_CYCLE_STATE_IGNITION_ENABLED = UINT32_C(1) << 0U,
    CRANKWAVE_ENGINE_CYCLE_STATE_FUEL_ENABLED = UINT32_C(1) << 1U,
    CRANKWAVE_ENGINE_CYCLE_STATE_STARTER_ENABLED = UINT32_C(1) << 2U,
    CRANKWAVE_ENGINE_CYCLE_STATE_DYNO_ENABLED = UINT32_C(1) << 3U,
    CRANKWAVE_ENGINE_CYCLE_STATE_LIMITER_ENABLED = UINT32_C(1) << 4U,
    CRANKWAVE_ENGINE_CYCLE_STATE_LIMITER_CUT_ACTIVE = UINT32_C(1) << 5U
};

typedef struct crankwave_cycle_boundary_evidence {
    int64_t cycle_ordinal;
    uint64_t left_physics_frame;
    uint64_t right_physics_frame;
    double fraction_from_left_01;
    double theta_unwrapped_rad;
    double time_s;
    double delivery_frame;
} crankwave_cycle_boundary_evidence_t;

typedef struct crankwave_cycle_control_evidence {
    double time_weighted_mean_01;
    double minimum_01;
    double maximum_01;
    uint32_t change_count;
} crankwave_cycle_control_evidence_t;

typedef struct crankwave_cycle_net_shaft_evidence {
    double angular_work_j;
    double cycle_mean_torque_nm;
    crankwave_availability_t availability;
    crankwave_completeness_t completeness;
    crankwave_quantity_unavailable_reason_t unavailable_reason;
    uint64_t included_terms;
    uint64_t omitted_terms;
} crankwave_cycle_net_shaft_evidence_t;

/*
 * Exact completed 720-degree cycle evidence. Boundary delivery_frame values are
 * fractional by design; the integer ordinals and physics-frame brackets remain
 * exact 64-bit values across native and WASM callers.
 */
typedef struct crankwave_completed_cycle_evidence {
    uint64_t completed_cycle_ordinal;
    crankwave_cycle_boundary_evidence_t start_boundary;
    crankwave_cycle_boundary_evidence_t end_boundary;
    double duration_s;
    double mean_engine_speed_rpm;
    crankwave_cycle_control_evidence_t requested_throttle;
    crankwave_cycle_control_evidence_t resolved_engine_throttle;
    crankwave_cycle_control_evidence_t intake_plate_position;
    crankwave_cycle_net_shaft_evidence_t instantaneous_net_shaft;
    crankwave_engine_cycle_state_flag_mask_t start_state_flags;
    crankwave_engine_cycle_state_flag_mask_t end_state_flags;
    crankwave_engine_cycle_state_flag_mask_t state_transition_flags;
} crankwave_completed_cycle_evidence_t;

typedef struct crankwave_audio_copy_buffer {
    uint32_t bus_index;
    float *samples;
    size_t sample_capacity;
    size_t samples_written;
} crankwave_audio_copy_buffer_t;

typedef uint32_t crankwave_process_kind_t;
enum { CRANKWAVE_PROCESS_BLOCK = 1, CRANKWAVE_PROCESS_COMPLETED = 2 };

typedef uint32_t crankwave_block_phase_t;
enum { CRANKWAVE_BLOCK_PREPARATION = 1, CRANKWAVE_BLOCK_AUDIBLE = 2 };

typedef struct crankwave_process_info {
    crankwave_process_kind_t kind;
    crankwave_block_phase_t block_phase;
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
} crankwave_process_info_t;

uint32_t crankwave_api_version(void) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_get_abi_layout(crankwave_abi_layout_t *out_layout) CRANKWAVE_C_API_NOEXCEPT;

crankwave_status_t crankwave_context_create(uint32_t requested_api_version,
                                crankwave_context_t **out_context) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_context_destroy(crankwave_context_t *context) CRANKWAVE_C_API_NOEXCEPT;

/*
 * Error inspection never clears or replaces the recorded error. String byte
 * counts exclude the trailing NUL. A {NULL, 0} text buffer skips that field.
 */
crankwave_status_t crankwave_context_get_last_error(const crankwave_context_t *context,
                                        crankwave_error_info_t *out_error) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_context_copy_last_error_text(const crankwave_context_t *context,
                                 crankwave_error_text_buffers_t *buffers) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_context_get_diagnostic(const crankwave_context_t *context, size_t diagnostic_index,
                           crankwave_diagnostic_info_t *out_diagnostic) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_context_copy_diagnostic_text(
    const crankwave_context_t *context, size_t diagnostic_index,
    crankwave_diagnostic_text_buffers_t *buffers) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_context_get_related_diagnostic(
    const crankwave_context_t *context, size_t diagnostic_index, size_t related_index,
    crankwave_related_diagnostic_info_t *out_related) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_context_copy_related_diagnostic_text(
    const crankwave_context_t *context, size_t diagnostic_index, size_t related_index,
    crankwave_diagnostic_text_buffers_t *buffers) CRANKWAVE_C_API_NOEXCEPT;

crankwave_status_t
crankwave_compile_engine_json(crankwave_context_t *context, crankwave_utf8_view_t engine_json,
                        const crankwave_asset_payload_t *assets, size_t asset_count,
                        crankwave_engine_handle_t *out_engine) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_destroy_engine(crankwave_context_t *context,
                                crankwave_engine_handle_t engine) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_engine_copy_id(crankwave_context_t *context, crankwave_engine_handle_t engine,
                                crankwave_mutable_utf8_buffer_t buffer,
                                size_t *out_utf8_bytes) CRANKWAVE_C_API_NOEXCEPT;
/*
 * Copies the compiled engine's canonical provenance bundle SHA-256 in digest byte
 * order. Hex encoders must encode bytes[0] first and use two digits per byte.
 */
crankwave_status_t
crankwave_engine_copy_provenance_sha256(crankwave_context_t *context, crankwave_engine_handle_t engine,
                                  crankwave_sha256_digest_t *out_sha256) CRANKWAVE_C_API_NOEXCEPT;
/*
 * Copies the source-closure SHA-256 embedded in this renderer build. This proves
 * clean source identity independently of compiler/target identity, so WASM builds
 * can expose it without claiming the canonical native publication toolchain. Builds
 * whose source closure is dirty, unavailable, or malformed return
 * CRANKWAVE_STATUS_NOT_AVAILABLE.
 */
crankwave_status_t crankwave_renderer_copy_source_closure_sha256(
    crankwave_context_t *context, crankwave_sha256_digest_t *out_sha256) CRANKWAVE_C_API_NOEXCEPT;

/*
 * Runs the complete responsive bake synchronously and retains the verified
 * CRANKWAVE carrier in context-owned memory. Browser callers should invoke
 * this from a dedicated worker. No filesystem or platform service is consulted.
 */
crankwave_status_t crankwave_bake_package(
    crankwave_context_t *context, const crankwave_bake_inputs_t *inputs,
    crankwave_package_handle_t *out_crankwave) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_destroy_package(crankwave_context_t *context,
                          crankwave_package_handle_t crankwave) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_package_get_descriptor(
    crankwave_context_t *context, crankwave_package_handle_t crankwave,
    crankwave_package_descriptor_t *out_descriptor) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_package_copy_identity(
    crankwave_context_t *context, crankwave_package_handle_t crankwave,
    crankwave_package_identity_buffers_t *buffers) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_package_copy_bytes(crankwave_context_t *context,
                                          crankwave_package_handle_t crankwave,
                                          uint8_t *bytes, size_t capacity,
                                          size_t *out_byte_count) CRANKWAVE_C_API_NOEXCEPT;

crankwave_status_t
crankwave_compile_scenario_json(crankwave_context_t *context, crankwave_engine_handle_t engine,
                          crankwave_utf8_view_t scenario_json,
                          crankwave_scenario_handle_t *out_scenario) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_destroy_scenario(crankwave_context_t *context,
                                  crankwave_scenario_handle_t scenario) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_scenario_copy_id(crankwave_context_t *context,
                                  crankwave_scenario_handle_t scenario,
                                  crankwave_mutable_utf8_buffer_t buffer,
                                  size_t *out_utf8_bytes) CRANKWAVE_C_API_NOEXCEPT;

crankwave_status_t crankwave_create_session(crankwave_context_t *context, crankwave_scenario_handle_t scenario,
                                crankwave_session_execution_kind_t execution_kind,
                                crankwave_session_handle_t *out_session) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_destroy_session(crankwave_context_t *context,
                                 crankwave_session_handle_t session) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_session_get_descriptor(crankwave_context_t *context, crankwave_session_handle_t session,
                           crankwave_session_descriptor_t *out_descriptor) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_session_copy_identity(crankwave_context_t *context, crankwave_session_handle_t session,
                          crankwave_session_identity_buffers_t *buffers) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_session_get_audio_bus_descriptor(
    crankwave_context_t *context, crankwave_session_handle_t session, uint32_t bus_index,
    crankwave_audio_bus_descriptor_t *out_descriptor) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t
crankwave_session_copy_audio_bus_id(crankwave_context_t *context, crankwave_session_handle_t session,
                              uint32_t bus_index,
                              crankwave_mutable_utf8_buffer_t buffer) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_session_get_forward_gear_descriptor(
    crankwave_context_t *context, crankwave_session_handle_t session, uint32_t gear_index,
    crankwave_forward_gear_descriptor_t *out_descriptor) CRANKWAVE_C_API_NOEXCEPT;
crankwave_status_t crankwave_session_copy_forward_gear_semantic_id(
    crankwave_context_t *context, crankwave_session_handle_t session, uint32_t gear_index,
    crankwave_mutable_utf8_buffer_t buffer) CRANKWAVE_C_API_NOEXCEPT;

crankwave_status_t
crankwave_session_enqueue_controls(crankwave_context_t *context, crankwave_session_handle_t session,
                             const crankwave_control_command_t *commands,
                             size_t command_count,
                             crankwave_control_rejection_t *out_rejection) CRANKWAVE_C_API_NOEXCEPT;

/*
 * Every supplied audio buffer names one bus to copy. Omitted buses are discarded.
 * All capacities are checked before process_block advances the session. Passing
 * {NULL, 0} for telemetry or cycle evidence discards that output; otherwise the
 * corresponding capacity must hold one complete returned block.
 */
crankwave_status_t crankwave_session_process(crankwave_context_t *context, crankwave_session_handle_t session,
                                 crankwave_audio_copy_buffer_t *audio_buffers,
                                 size_t audio_buffer_count,
                                 crankwave_session_telemetry_t *telemetry,
                                 size_t telemetry_capacity,
                                 crankwave_completed_cycle_evidence_t *cycle_evidence,
                                 size_t cycle_evidence_capacity,
                                 crankwave_process_info_t *out_process) CRANKWAVE_C_API_NOEXCEPT;

#if defined(__cplusplus)
} /* extern "C" */
#endif

#undef CRANKWAVE_C_API_NOEXCEPT

#endif /* CRANKWAVE_C_API_H */
