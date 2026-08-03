#include "c_api/c_api_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::c_api {
namespace {

[[nodiscard]] eso_status_t invalid_pointer(eso_context &context, std::string message) {
    return set_error(context, ESO_STATUS_INVALID_ARGUMENT, ESO_ERROR_STAGE_ARGUMENT,
                     ESO_ERROR_INVALID_POINTER, "c-api-invalid-pointer",
                     std::move(message));
}

[[nodiscard]] eso_status_t invalid_handle(eso_context &context, std::string message) {
    return set_error(context, ESO_STATUS_INVALID_HANDLE, ESO_ERROR_STAGE_HANDLE,
                     ESO_ERROR_INVALID_HANDLE, "c-api-invalid-handle",
                     std::move(message));
}

[[nodiscard]] eso_status_t buffer_error(eso_context &context, std::string message) {
    return set_error(context, ESO_STATUS_BUFFER_TOO_SMALL, ESO_ERROR_STAGE_ARGUMENT,
                     ESO_ERROR_BUFFER_CAPACITY, "c-api-buffer-too-small",
                     std::move(message));
}

[[nodiscard]] bool can_copy(const std::string_view value,
                            const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr) {
        return buffer.capacity == 0U;
    }
    return value.size() != std::numeric_limits<std::size_t>::max() &&
           buffer.capacity >= value.size() + 1U;
}

[[nodiscard]] eso_status_t set_session_error(eso_context &context,
                                             const eso_status_t status,
                                             const eso_error_stage_t stage,
                                             const EngineSessionError &error) {
    std::string message = error.message;
    if (error.simulation_failure.has_value()) {
        const auto &failure = *error.simulation_failure;
        if (!failure.detail_code.empty()) {
            message += "; simulation detail: " + failure.detail_code;
        }
        if (!failure.state_summary.empty()) {
            message += "; state: " + failure.state_summary;
        }
    }
    return set_error(context, status, stage, session_error_code(error.code),
                     error.detail_code, std::move(message));
}

[[nodiscard]] EngineControlCommand control_command(const eso_control_command_t &input) {
    EngineControlPayload payload;
    switch (input.kind) {
    case ESO_CONTROL_THROTTLE:
        payload = SetEngineThrottle{input.scalar_value};
        break;
    case ESO_CONTROL_IGNITION_ENABLED:
        payload = SetEngineIgnitionEnabled{input.enabled != 0U};
        break;
    case ESO_CONTROL_FUEL_ENABLED:
        payload = SetEngineFuelEnabled{input.enabled != 0U};
        break;
    case ESO_CONTROL_STARTER_ENABLED:
        payload = SetEngineStarterEnabled{input.enabled != 0U};
        break;
    case ESO_CONTROL_LIMITER_ENABLED:
        payload = SetEngineLimiterEnabled{input.enabled != 0U};
        break;
    case ESO_CONTROL_EXTERNAL_RESISTING_TORQUE:
        payload = SetEngineExternalResistingTorque{input.scalar_value};
        break;
    case ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED:
        payload = SetHeldDynoTargetEngineSpeed{input.scalar_value};
        break;
    case ESO_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE:
        payload = SetHeldDynoMaximumAbsorbingTorque{input.scalar_value};
        break;
    case ESO_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE:
        payload = SetHeldDynoMaximumDrivingTorque{input.scalar_value};
        break;
    case ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR:
        payload = SetVehicleSelectedForwardGear{input.id_value};
        break;
    case ESO_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT:
        payload = SetVehicleClutchEngagement{input.scalar_value};
        break;
    case ESO_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION:
        payload = SetVehicleServiceBrakeApplication{input.scalar_value};
        break;
    default:
        payload = SetEngineThrottle{input.scalar_value};
        break;
    }
    return {input.delivery_frame, input.sequence, std::move(payload)};
}

[[nodiscard]] bool well_formed(const eso_control_command_t &input) noexcept {
    if (input.reserved != 0U) {
        return false;
    }
    switch (input.kind) {
    case ESO_CONTROL_THROTTLE:
        return input.enabled == 0U && std::isfinite(input.scalar_value) &&
               !std::signbit(input.scalar_value) && input.scalar_value >= 0.0 &&
               input.scalar_value <= 1.0 && input.id_value == 0U;
    case ESO_CONTROL_IGNITION_ENABLED:
    case ESO_CONTROL_FUEL_ENABLED:
    case ESO_CONTROL_STARTER_ENABLED:
    case ESO_CONTROL_LIMITER_ENABLED:
        return input.enabled <= 1U && input.scalar_value == 0.0 &&
               !std::signbit(input.scalar_value) && input.id_value == 0U;
    case ESO_CONTROL_EXTERNAL_RESISTING_TORQUE:
    case ESO_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE:
    case ESO_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE:
        return input.enabled == 0U && std::isfinite(input.scalar_value) &&
               !std::signbit(input.scalar_value) && input.scalar_value >= 0.0 &&
               input.id_value == 0U;
    case ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED:
        return input.enabled == 0U && std::isfinite(input.scalar_value) &&
               !std::signbit(input.scalar_value) && input.scalar_value > 0.0 &&
               input.id_value == 0U;
    case ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR:
        return input.enabled == 0U && input.scalar_value == 0.0 &&
               !std::signbit(input.scalar_value);
    case ESO_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT:
    case ESO_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION:
        return input.enabled == 0U && std::isfinite(input.scalar_value) &&
               !std::signbit(input.scalar_value) && input.scalar_value >= 0.0 &&
               input.scalar_value <= 1.0 && input.id_value == 0U;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<EngineSessionExecutionKind>
session_execution_kind(const eso_session_execution_kind_t input) noexcept {
    switch (input) {
    case ESO_SESSION_EXECUTION_FINITE_SCENARIO:
        return EngineSessionExecutionKind::finite_scenario;
    case ESO_SESSION_EXECUTION_OPEN_ENDED:
        return EngineSessionExecutionKind::open_ended;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] eso_session_execution_kind_t
session_execution_kind(const EngineSessionExecutionKind input) noexcept {
    switch (input) {
    case EngineSessionExecutionKind::finite_scenario:
        return ESO_SESSION_EXECUTION_FINITE_SCENARIO;
    case EngineSessionExecutionKind::open_ended:
        return ESO_SESSION_EXECUTION_OPEN_ENDED;
    }
    return 0U;
}

[[nodiscard]] eso_motion_mode_t motion_mode(const EngineMotionMode input) noexcept {
    switch (input) {
    case EngineMotionMode::held_speed:
        return ESO_MOTION_HELD_SPEED;
    case EngineMotionMode::prescribed_kinematic_sweep:
        return ESO_MOTION_PRESCRIBED_KINEMATIC_SWEEP;
    case EngineMotionMode::held_dyno:
        return ESO_MOTION_HELD_DYNO;
    case EngineMotionMode::load_target_held_capture:
        return ESO_MOTION_LOAD_TARGET_HELD_CAPTURE;
    case EngineMotionMode::inertial_dyno:
        return ESO_MOTION_INERTIAL_DYNO;
    case EngineMotionMode::free_engine:
        return ESO_MOTION_FREE_ENGINE;
    case EngineMotionMode::free_vehicle:
        return ESO_MOTION_FREE_VEHICLE;
    }
    return 0U;
}

[[nodiscard]] eso_status_t reject_control(eso_context &context,
                                          eso_control_rejection_t &output,
                                          const std::size_t command_index,
                                          const eso_error_code_t code,
                                          std::string message) {
    output = {code, command_index};
    return set_error(context, ESO_STATUS_CONTROL_REJECTED, ESO_ERROR_STAGE_CONTROL,
                     code, "control-command-rejected", std::move(message));
}

} // namespace
} // namespace engine_sim_offline::c_api

extern "C" {

eso_status_t eso_create_session(eso_context_t *const context,
                                const eso_scenario_handle_t scenario,
                                const eso_session_execution_kind_t execution_kind,
                                eso_session_handle_t *const out_session) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_session == nullptr) {
            return invalid_pointer(*context, "out_session must not be null");
        }
        *out_session = ESO_INVALID_HANDLE;
        const auto requested_execution = session_execution_kind(execution_kind);
        if (!requested_execution.has_value()) {
            return set_error(
                *context, ESO_STATUS_INVALID_ARGUMENT, ESO_ERROR_STAGE_ARGUMENT,
                ESO_ERROR_INVALID_ENUM, "c-api-session-execution-kind-invalid",
                "execution_kind must name finite-scenario or open-ended execution");
        }
        const auto *compiled = context->scenarios.get(scenario);
        if (compiled == nullptr) {
            return invalid_handle(
                *context, "compiled-scenario handle is stale, invalid, or wrong-kind");
        }

        auto result = create_engine_session(*compiled, *requested_execution);
        if (const auto *error = std::get_if<EngineSessionError>(&result)) {
            return set_session_error(*context, ESO_STATUS_SESSION_CREATE_FAILED,
                                     ESO_ERROR_STAGE_SESSION_CREATE, *error);
        }
        *out_session = context->sessions.insert(
            SessionEntry{std::get<EngineSession>(std::move(result))});
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_destroy_session(eso_context_t *const context,
                                 const eso_session_handle_t session) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (!context->sessions.erase(session)) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t
eso_session_get_descriptor(eso_context_t *const context,
                           const eso_session_handle_t session,
                           eso_session_descriptor_t *const out_descriptor) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (out_descriptor == nullptr) {
            return invalid_pointer(*context, "out_descriptor must not be null");
        }
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto descriptor = entry->session.descriptor();
        *out_descriptor = {
            descriptor.capacities.maximum_delivery_frames_per_process_call,
            descriptor.capacities.control_command_queue_capacity,
            descriptor.capacities.maximum_telemetry_frames_per_process_call,
            descriptor.physics_rate.numerator,
            descriptor.physics_rate.denominator,
            descriptor.delivery_rate.numerator,
            descriptor.delivery_rate.denominator,
            descriptor.physics_frames_per_block,
            descriptor.delivery_frames_per_block,
            descriptor.total_block_count,
            descriptor.preparation_block_count,
            static_cast<std::uint32_t>(descriptor.audio_buses.size()),
            descriptor.live_control_capabilities,
            descriptor.engine_id.size(),
            descriptor.scenario_id.size(),
            session_execution_kind(descriptor.execution_kind),
            motion_mode(descriptor.motion_mode),
            static_cast<std::uint32_t>(descriptor.forward_gears.size()),
        };
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t
eso_session_copy_identity(eso_context_t *const context,
                          const eso_session_handle_t session,
                          eso_session_identity_buffers_t *const buffers) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (buffers == nullptr) {
            return invalid_pointer(*context, "identity buffers must not be null");
        }
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto descriptor = entry->session.descriptor();
        if ((buffers->engine_id.data == nullptr && buffers->engine_id.capacity != 0U) ||
            (buffers->scenario_id.data == nullptr &&
             buffers->scenario_id.capacity != 0U)) {
            return invalid_pointer(
                *context, "identity buffer has a null pointer and nonzero capacity");
        }
        if (!can_copy(descriptor.engine_id, buffers->engine_id) ||
            !can_copy(descriptor.scenario_id, buffers->scenario_id)) {
            return buffer_error(*context, "one or more identity buffers are too small");
        }
        (void)copy_text(descriptor.engine_id, buffers->engine_id);
        (void)copy_text(descriptor.scenario_id, buffers->scenario_id);
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_get_audio_bus_descriptor(
    eso_context_t *const context, const eso_session_handle_t session,
    const uint32_t bus_index,
    eso_audio_bus_descriptor_t *const out_descriptor) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (out_descriptor == nullptr) {
            return invalid_pointer(*context, "out_descriptor must not be null");
        }
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto buses = entry->session.descriptor().audio_buses;
        if (bus_index >= buses.size()) {
            return set_error(*context, ESO_STATUS_INVALID_ARGUMENT,
                             ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_COUNT,
                             "c-api-audio-bus-index-invalid",
                             "audio bus index is outside the session descriptor");
        }
        const auto &bus = buses[bus_index];
        *out_descriptor = {
            audio_bus_kind(bus.kind),
            bus.channel_count,
            bus.sample_rate.numerator,
            bus.sample_rate.denominator,
            bus.route_id.has_value() ? 1U : 0U,
            bus.route_id ? bus.route_id->value : 0U,
            source_route_kind(bus.source_route_kind),
            audio_signal_disposition(bus.signal_disposition),
            bus.id.size(),
        };
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_copy_audio_bus_id(
    eso_context_t *const context, const eso_session_handle_t session,
    const uint32_t bus_index, const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto buses = entry->session.descriptor().audio_buses;
        if (bus_index >= buses.size()) {
            return set_error(*context, ESO_STATUS_INVALID_ARGUMENT,
                             ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_COUNT,
                             "c-api-audio-bus-index-invalid",
                             "audio bus index is outside the session descriptor");
        }
        const auto status = copy_text(buses[bus_index].id, buffer);
        if (status != ESO_STATUS_OK) {
            return set_error(*context, status, ESO_ERROR_STAGE_ARGUMENT,
                             status == ESO_STATUS_BUFFER_TOO_SMALL
                                 ? ESO_ERROR_BUFFER_CAPACITY
                                 : ESO_ERROR_INVALID_POINTER,
                             "c-api-audio-bus-id-buffer-invalid",
                             "audio bus ID output buffer is invalid or too small");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_get_forward_gear_descriptor(
    eso_context_t *const context, const eso_session_handle_t session,
    const uint32_t gear_index,
    eso_forward_gear_descriptor_t *const out_descriptor) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (out_descriptor == nullptr) {
            return invalid_pointer(*context, "out_descriptor must not be null");
        }
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto gears = entry->session.descriptor().forward_gears;
        if (gear_index >= gears.size()) {
            return set_error(*context, ESO_STATUS_INVALID_ARGUMENT,
                             ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_COUNT,
                             "c-api-forward-gear-index-invalid",
                             "forward gear index is outside the session descriptor");
        }
        const auto &gear = gears[gear_index];
        *out_descriptor = {
            gear.id.value,
            gear.authored_ordinal,
            gear.ratio,
            gear.semantic_id.size(),
        };
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_copy_forward_gear_semantic_id(
    eso_context_t *const context, const eso_session_handle_t session,
    const uint32_t gear_index, const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        const auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        const auto gears = entry->session.descriptor().forward_gears;
        if (gear_index >= gears.size()) {
            return set_error(*context, ESO_STATUS_INVALID_ARGUMENT,
                             ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_COUNT,
                             "c-api-forward-gear-index-invalid",
                             "forward gear index is outside the session descriptor");
        }
        const auto status = copy_text(gears[gear_index].semantic_id, buffer);
        if (status != ESO_STATUS_OK) {
            return set_error(*context, status, ESO_ERROR_STAGE_ARGUMENT,
                             status == ESO_STATUS_BUFFER_TOO_SMALL
                                 ? ESO_ERROR_BUFFER_CAPACITY
                                 : ESO_ERROR_INVALID_POINTER,
                             "c-api-forward-gear-id-buffer-invalid",
                             "forward gear semantic-ID output buffer is invalid or "
                             "too small");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_enqueue_controls(
    eso_context_t *const context, const eso_session_handle_t session,
    const eso_control_command_t *const commands, const size_t command_count,
    eso_control_rejection_t *const out_rejection) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_rejection == nullptr) {
            return invalid_pointer(*context, "out_rejection must not be null");
        }
        *out_rejection = {ESO_ERROR_NONE, 0U};
        if (commands == nullptr && command_count != 0U) {
            return invalid_pointer(
                *context,
                "control array must not be null when command_count is nonzero");
        }
        auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }

        if (command_count > entry->control_scratch.size()) {
            return reject_control(
                *context, *out_rejection, 0U, ESO_ERROR_CONTROL_CAPACITY_EXCEEDED,
                "control batch exceeds the session's complete bounded queue");
        }
        for (std::size_t index = 0; index < command_count; ++index) {
            if (!well_formed(commands[index])) {
                return reject_control(
                    *context, *out_rejection, index, ESO_ERROR_CONTROL_INVALID_PAYLOAD,
                    "control command has an unknown type, a nonzero reserved "
                    "field, or a noncanonical typed payload");
            }
        }
        for (std::size_t index = 0; index < command_count; ++index) {
            entry->control_scratch[index] = control_command(commands[index]);
        }

        const auto rejection =
            entry->session.enqueue_controls(std::span<const EngineControlCommand>{
                entry->control_scratch.data(), command_count});
        if (rejection.has_value()) {
            const auto code = control_error_code(rejection->code);
            return reject_control(*context, *out_rejection, rejection->command_index,
                                  code, rejection->message);
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_session_process(eso_context_t *const context,
                                 const eso_session_handle_t session,
                                 eso_audio_copy_buffer_t *const audio_buffers,
                                 const size_t audio_buffer_count,
                                 eso_session_telemetry_t *const telemetry,
                                 const size_t telemetry_capacity,
                                 eso_process_info_t *const out_process) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_process == nullptr) {
            return invalid_pointer(*context, "out_process must not be null");
        }
        *out_process = {};
        if (audio_buffers == nullptr && audio_buffer_count != 0U) {
            return invalid_pointer(
                *context,
                "audio buffer array must not be null when its count is nonzero");
        }
        if (telemetry == nullptr && telemetry_capacity != 0U) {
            return invalid_pointer(
                *context,
                "telemetry buffer must not be null when its capacity is nonzero");
        }
        auto *entry = context->sessions.get(session);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "engine-session handle is stale, invalid, or wrong-kind");
        }
        for (std::size_t index = 0; index < audio_buffer_count; ++index) {
            audio_buffers[index].samples_written = 0U;
        }

        const auto descriptor = entry->session.descriptor();
        const bool expects_block =
            !entry->terminal &&
            (descriptor.execution_kind == EngineSessionExecutionKind::open_ended ||
             entry->emitted_block_count < descriptor.total_block_count);
        if (expects_block) {
            for (std::size_t index = 0; index < audio_buffer_count; ++index) {
                const auto &buffer = audio_buffers[index];
                if (buffer.bus_index >= descriptor.audio_buses.size()) {
                    return set_error(
                        *context, ESO_STATUS_INVALID_ARGUMENT, ESO_ERROR_STAGE_ARGUMENT,
                        ESO_ERROR_INVALID_COUNT, "c-api-audio-bus-index-invalid",
                        "audio copy buffer names a bus outside the session descriptor");
                }
                for (std::size_t prior = 0; prior < index; ++prior) {
                    if (audio_buffers[prior].bus_index == buffer.bus_index) {
                        return set_error(
                            *context, ESO_STATUS_INVALID_ARGUMENT,
                            ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_COUNT,
                            "c-api-audio-bus-duplicated",
                            "one process call may copy each audio bus at most once");
                    }
                }
                const auto &bus = descriptor.audio_buses[buffer.bus_index];
                const auto required =
                    static_cast<std::size_t>(descriptor.delivery_frames_per_block) *
                    bus.channel_count;
                if (buffer.samples == nullptr && buffer.sample_capacity != 0U) {
                    return invalid_pointer(
                        *context,
                        "audio sample buffer has a null pointer and nonzero capacity");
                }
                if (buffer.samples == nullptr || buffer.sample_capacity < required) {
                    return buffer_error(
                        *context,
                        "audio sample buffer cannot hold one complete method block");
                }
            }
            if (telemetry != nullptr &&
                telemetry_capacity <
                    descriptor.capacities.maximum_telemetry_frames_per_process_call) {
                return buffer_error(
                    *context,
                    "telemetry buffer cannot hold one complete returned block");
            }
        }

        auto result = entry->session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&result)) {
            entry->terminal = true;
            return set_session_error(*context, ESO_STATUS_PROCESS_FAILED,
                                     ESO_ERROR_STAGE_PROCESS, *error);
        }
        if (const auto *completed = std::get_if<EngineSessionCompleted>(&result)) {
            entry->terminal = true;
            *out_process = {
                ESO_PROCESS_COMPLETED,
                0U,
                0U,
                0U,
                0U,
                0U,
                0U,
                0U,
                completed->physics_frame_count,
                completed->delivery_frame_count,
                completed->block_count,
                completed->live_controls_accepted ? 1U : 0U,
                completed->held_speed_operating_point.has_value() ? 1U : 0U,
                completed->inertial_dyno.has_value() ? 1U : 0U,
            };
            clear_error(*context);
            return ESO_STATUS_OK;
        }

        const auto &block = std::get<EngineSessionBlockView>(result);
        if (block.block_ordinal() != entry->emitted_block_count ||
            block.audio_buses().size() != descriptor.audio_buses.size() ||
            block.delivery_frame_count() != descriptor.delivery_frames_per_block ||
            block.physics_frame_count() != descriptor.physics_frames_per_block) {
            entry->terminal = true;
            return set_error(*context, ESO_STATUS_PROCESS_FAILED,
                             ESO_ERROR_STAGE_PROCESS, ESO_ERROR_SESSION_INTERNAL,
                             "c-api-session-block-invalid",
                             "session block disagrees with its immutable descriptor");
        }

        for (std::size_t index = 0; index < audio_buffer_count; ++index) {
            auto &destination = audio_buffers[index];
            const auto source = block.audio_buses()[destination.bus_index].samples;
            const auto &bus = descriptor.audio_buses[destination.bus_index];
            const auto expected =
                static_cast<std::size_t>(block.delivery_frame_count()) *
                bus.channel_count;
            if (source.size() != expected) {
                entry->terminal = true;
                return set_error(
                    *context, ESO_STATUS_PROCESS_FAILED, ESO_ERROR_STAGE_PROCESS,
                    ESO_ERROR_SESSION_INTERNAL, "c-api-session-bus-extent-invalid",
                    "session audio bus disagrees with its declared block extent");
            }
            std::copy(source.begin(), source.end(), destination.samples);
            destination.samples_written = source.size();
        }

        std::size_t telemetry_written = 0U;
        if (telemetry != nullptr) {
            if (block.telemetry().size() > telemetry_capacity) {
                entry->terminal = true;
                return set_error(
                    *context, ESO_STATUS_PROCESS_FAILED, ESO_ERROR_STAGE_PROCESS,
                    ESO_ERROR_SESSION_INTERNAL,
                    "c-api-session-telemetry-extent-invalid",
                    "session telemetry exceeded its compiled caller capacity");
            }
            for (const auto &frame : block.telemetry()) {
                telemetry[telemetry_written] = session_telemetry(frame);
                ++telemetry_written;
            }
        }

        *out_process = {
            ESO_PROCESS_BLOCK,
            block.phase() == EngineSessionBlockPhase::preparation
                ? ESO_BLOCK_PREPARATION
                : ESO_BLOCK_AUDIBLE,
            block.block_ordinal(),
            block.first_physics_frame(),
            block.physics_frame_count(),
            block.first_delivery_frame(),
            block.delivery_frame_count(),
            telemetry_written,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
        };
        ++entry->emitted_block_count;
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

} // extern "C"
