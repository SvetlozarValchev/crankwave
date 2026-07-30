#include "c_api/c_api_internal.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace engine_sim_offline::c_api {
namespace {

std::atomic<std::uint64_t> next_context_tag{1U};

[[nodiscard]] std::optional<std::uint32_t> acquire_context_tag() noexcept {
    const auto tag = next_context_tag.fetch_add(1U, std::memory_order_relaxed);
    if (tag == 0U || tag > kHandleContextMask) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(tag);
}

[[nodiscard]] bool fits(const std::string_view text,
                        const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr) {
        return buffer.capacity == 0U;
    }
    return text.size() != std::numeric_limits<std::size_t>::max() &&
           buffer.capacity >= text.size() + 1U;
}

[[nodiscard]] bool fits(const authoring::Diagnostic &diagnostic,
                        const eso_diagnostic_text_buffers_t &buffers) noexcept {
    const std::string_view subject_kind =
        diagnostic.subject ? diagnostic.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        diagnostic.subject ? diagnostic.subject->object_id : std::string_view{};
    return fits(diagnostic.json_pointer, buffers.json_pointer) &&
           fits(subject_kind, buffers.subject_kind) &&
           fits(subject_id, buffers.subject_id) &&
           fits(diagnostic.message, buffers.message);
}

[[nodiscard]] bool fits(const authoring::RelatedDiagnosticLocation &related,
                        const eso_diagnostic_text_buffers_t &buffers) noexcept {
    const std::string_view subject_kind =
        related.subject ? related.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        related.subject ? related.subject->object_id : std::string_view{};
    return fits(related.json_pointer, buffers.json_pointer) &&
           fits(subject_kind, buffers.subject_kind) &&
           fits(subject_id, buffers.subject_id) &&
           fits(related.message, buffers.message);
}

void copy_unchecked(const std::string_view text,
                    const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr) {
        return;
    }
    if (!text.empty()) {
        std::memcpy(buffer.data, text.data(), text.size());
    }
    buffer.data[text.size()] = '\0';
}

} // namespace

bool valid(const eso_utf8_view_t view) noexcept {
    return view.data != nullptr || view.size == 0U;
}

bool valid(const eso_byte_view_t view) noexcept {
    return view.data != nullptr || view.size == 0U;
}

void clear_error(eso_context &context) noexcept {
    context.last_error.reset();
}

eso_status_t set_error(eso_context &context, const eso_status_t status,
                       const eso_error_stage_t stage, const eso_error_code_t code,
                       std::string detail_code, std::string message) {
    context.last_error = ErrorRecord{
        status, stage, code, std::move(detail_code), std::move(message), {}};
    return status;
}

eso_status_t set_diagnostics(eso_context &context, const eso_status_t status,
                             const eso_error_stage_t stage, std::string detail_code,
                             std::string message, authoring::DiagnosticReport report) {
    context.last_error = ErrorRecord{status,
                                     stage,
                                     ESO_ERROR_AUTHORING_DIAGNOSTICS,
                                     std::move(detail_code),
                                     std::move(message),
                                     std::move(report.diagnostics)};
    return status;
}

void set_unexpected_error_noexcept(eso_context &context, const bool resource) noexcept {
    try {
        context.last_error = ErrorRecord{
            resource ? ESO_STATUS_RESOURCE_EXHAUSTED : ESO_STATUS_INTERNAL_ERROR,
            ESO_ERROR_STAGE_ABI,
            resource ? ESO_ERROR_SESSION_RESOURCE_EXHAUSTED
                     : ESO_ERROR_SESSION_INTERNAL,
            resource ? "c-api-resource-exhausted" : "c-api-boundary-failed",
            resource ? "the C API boundary exhausted memory"
                     : "the C API boundary caught an unexpected exception",
            {}};
    } catch (...) {
        context.last_error.reset();
    }
}

eso_status_t copy_text(const std::string_view text,
                       const eso_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr && buffer.capacity != 0U) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (buffer.data == nullptr) {
        return ESO_STATUS_OK;
    }
    if (text.size() == std::numeric_limits<std::size_t>::max() ||
        buffer.capacity < text.size() + 1U) {
        return ESO_STATUS_BUFFER_TOO_SMALL;
    }
    copy_unchecked(text, buffer);
    return ESO_STATUS_OK;
}

eso_diagnostic_severity_t
diagnostic_severity(const authoring::DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case authoring::DiagnosticSeverity::error:
        return ESO_DIAGNOSTIC_ERROR;
    case authoring::DiagnosticSeverity::warning:
        return ESO_DIAGNOSTIC_WARNING;
    }
    return ESO_DIAGNOSTIC_ERROR;
}

eso_diagnostic_code_t diagnostic_code(const authoring::DiagnosticCode code) noexcept {
    using Code = authoring::DiagnosticCode;
    switch (code) {
    case Code::malformed_document:
        return ESO_DIAGNOSTIC_MALFORMED_DOCUMENT;
    case Code::unsupported_schema:
        return ESO_DIAGNOSTIC_UNSUPPORTED_SCHEMA;
    case Code::missing_value:
        return ESO_DIAGNOSTIC_MISSING_VALUE;
    case Code::unknown_field:
        return ESO_DIAGNOSTIC_UNKNOWN_FIELD;
    case Code::invalid_type:
        return ESO_DIAGNOSTIC_INVALID_TYPE;
    case Code::invalid_unit:
        return ESO_DIAGNOSTIC_INVALID_UNIT;
    case Code::invalid_value:
        return ESO_DIAGNOSTIC_INVALID_VALUE;
    case Code::out_of_range:
        return ESO_DIAGNOSTIC_OUT_OF_RANGE;
    case Code::duplicate_id:
        return ESO_DIAGNOSTIC_DUPLICATE_ID;
    case Code::dangling_reference:
        return ESO_DIAGNOSTIC_DANGLING_REFERENCE;
    case Code::forbidden_cycle:
        return ESO_DIAGNOSTIC_FORBIDDEN_CYCLE;
    case Code::disconnected_object:
        return ESO_DIAGNOSTIC_DISCONNECTED_OBJECT;
    case Code::inconsistent_value:
        return ESO_DIAGNOSTIC_INCONSISTENT_VALUE;
    case Code::unsupported_capability:
        return ESO_DIAGNOSTIC_UNSUPPORTED_CAPABILITY;
    case Code::missing_asset:
        return ESO_DIAGNOSTIC_MISSING_ASSET;
    case Code::asset_hash_mismatch:
        return ESO_DIAGNOSTIC_ASSET_HASH_MISMATCH;
    case Code::resource_limit:
        return ESO_DIAGNOSTIC_RESOURCE_LIMIT;
    case Code::internal_failure:
        return ESO_DIAGNOSTIC_INTERNAL_FAILURE;
    }
    return ESO_DIAGNOSTIC_INTERNAL_FAILURE;
}

eso_error_code_t session_error_code(const EngineSessionErrorCode code) noexcept {
    switch (code) {
    case EngineSessionErrorCode::invalid_compiled_scenario:
        return ESO_ERROR_SESSION_INVALID_COMPILED_SCENARIO;
    case EngineSessionErrorCode::unsupported_configuration:
        return ESO_ERROR_SESSION_UNSUPPORTED_CONFIGURATION;
    case EngineSessionErrorCode::resource_exhausted:
        return ESO_ERROR_SESSION_RESOURCE_EXHAUSTED;
    case EngineSessionErrorCode::processing_failed:
        return ESO_ERROR_SESSION_PROCESSING_FAILED;
    case EngineSessionErrorCode::consumer_state_invalid:
        return ESO_ERROR_SESSION_CONSUMER_STATE_INVALID;
    case EngineSessionErrorCode::internal_error:
        return ESO_ERROR_SESSION_INTERNAL;
    }
    return ESO_ERROR_SESSION_INTERNAL;
}

eso_error_code_t control_error_code(const EngineControlRejectionCode code) noexcept {
    switch (code) {
    case EngineControlRejectionCode::capacity_exceeded:
        return ESO_ERROR_CONTROL_CAPACITY_EXCEEDED;
    case EngineControlRejectionCode::late_command:
        return ESO_ERROR_CONTROL_LATE_COMMAND;
    case EngineControlRejectionCode::invalid_payload:
        return ESO_ERROR_CONTROL_INVALID_PAYLOAD;
    case EngineControlRejectionCode::unordered_delivery_frame:
        return ESO_ERROR_CONTROL_UNORDERED_DELIVERY_FRAME;
    case EngineControlRejectionCode::duplicate_sequence:
        return ESO_ERROR_CONTROL_DUPLICATE_SEQUENCE;
    case EngineControlRejectionCode::unordered_sequence:
        return ESO_ERROR_CONTROL_UNORDERED_SEQUENCE;
    case EngineControlRejectionCode::unsupported_for_operating_mode:
        return ESO_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE;
    case EngineControlRejectionCode::unavailable_during_preparation:
        return ESO_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION;
    case EngineControlRejectionCode::outside_session_horizon:
        return ESO_ERROR_CONTROL_OUTSIDE_SESSION_HORIZON;
    case EngineControlRejectionCode::session_terminal:
        return ESO_ERROR_CONTROL_SESSION_TERMINAL;
    case EngineControlRejectionCode::internal_clock_error:
        return ESO_ERROR_CONTROL_INTERNAL_CLOCK;
    }
    return ESO_ERROR_CONTROL_INTERNAL_CLOCK;
}

eso_audio_bus_kind_t audio_bus_kind(const EngineAudioBusKind kind) noexcept {
    switch (kind) {
    case EngineAudioBusKind::exhaust_route_dry:
        return ESO_AUDIO_BUS_EXHAUST_ROUTE_DRY;
    case EngineAudioBusKind::exhaust_route_configured_ir:
        return ESO_AUDIO_BUS_EXHAUST_ROUTE_CONFIGURED_IR;
    case EngineAudioBusKind::exhaust_route_selected:
        return ESO_AUDIO_BUS_EXHAUST_ROUTE_SELECTED;
    case EngineAudioBusKind::engine_raw_master:
        return ESO_AUDIO_BUS_ENGINE_RAW_MASTER;
    case EngineAudioBusKind::engine_audition_master:
        return ESO_AUDIO_BUS_ENGINE_AUDITION_MASTER;
    }
    return ESO_AUDIO_BUS_EXHAUST_ROUTE_SELECTED;
}

} // namespace engine_sim_offline::c_api

extern "C" {

uint32_t eso_api_version(void) noexcept {
    return ESO_C_API_VERSION;
}

eso_status_t eso_get_abi_layout(eso_abi_layout_t *const out_layout) noexcept {
    if (out_layout == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    *out_layout = {
        ESO_C_API_VERSION,
        static_cast<std::uint32_t>(sizeof(void *)),
        static_cast<std::uint32_t>(sizeof(std::size_t)),
        static_cast<std::uint32_t>(sizeof(float)),
        static_cast<std::uint32_t>(sizeof(double)),
        std::endian::native == std::endian::little ? 1U : 0U,
        static_cast<std::uint32_t>(sizeof(eso_control_command_t)),
        static_cast<std::uint32_t>(sizeof(eso_session_descriptor_t)),
        static_cast<std::uint32_t>(sizeof(eso_audio_bus_descriptor_t)),
        static_cast<std::uint32_t>(sizeof(eso_engine_telemetry_t)),
    };
    return ESO_STATUS_OK;
}

eso_status_t eso_context_create(const uint32_t requested_api_version,
                                eso_context_t **const out_context) noexcept {
    if (out_context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    *out_context = nullptr;
    if (requested_api_version != ESO_C_API_VERSION) {
        return ESO_STATUS_ABI_VERSION_MISMATCH;
    }
    try {
        const auto tag = engine_sim_offline::c_api::acquire_context_tag();
        if (!tag.has_value()) {
            return ESO_STATUS_RESOURCE_EXHAUSTED;
        }
        *out_context = new eso_context{*tag};
        return ESO_STATUS_OK;
    } catch (const std::bad_alloc &) {
        return ESO_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return ESO_STATUS_INTERNAL_ERROR;
    }
}

eso_status_t eso_context_destroy(eso_context_t *const context) noexcept {
    delete context;
    return ESO_STATUS_OK;
}

eso_status_t eso_context_get_last_error(const eso_context_t *const context,
                                        eso_error_info_t *const out_error) noexcept {
    if (context == nullptr || out_error == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &error = *context->last_error;
    *out_error = {
        error.status,         error.stage,
        error.code,           error.detail_code.size(),
        error.message.size(), error.diagnostics.size(),
    };
    return ESO_STATUS_OK;
}

eso_status_t
eso_context_copy_last_error_text(const eso_context_t *const context,
                                 eso_error_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &error = *context->last_error;
    const auto detail_fits =
        (buffers->detail_code.data == nullptr && buffers->detail_code.capacity == 0U) ||
        (buffers->detail_code.data != nullptr &&
         buffers->detail_code.capacity > error.detail_code.size());
    const auto message_fits =
        (buffers->message.data == nullptr && buffers->message.capacity == 0U) ||
        (buffers->message.data != nullptr &&
         buffers->message.capacity > error.message.size());
    if ((buffers->detail_code.data == nullptr && buffers->detail_code.capacity != 0U) ||
        (buffers->message.data == nullptr && buffers->message.capacity != 0U)) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!detail_fits || !message_fits) {
        return ESO_STATUS_BUFFER_TOO_SMALL;
    }
    (void)engine_sim_offline::c_api::copy_text(error.detail_code, buffers->detail_code);
    (void)engine_sim_offline::c_api::copy_text(error.message, buffers->message);
    return ESO_STATUS_OK;
}

eso_status_t
eso_context_get_diagnostic(const eso_context_t *const context,
                           const size_t diagnostic_index,
                           eso_diagnostic_info_t *const out_diagnostic) noexcept {
    if (context == nullptr || out_diagnostic == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    const auto subject_kind_size =
        diagnostic.subject ? diagnostic.subject->object_kind.size() : 0U;
    const auto subject_id_size =
        diagnostic.subject ? diagnostic.subject->object_id.size() : 0U;
    *out_diagnostic = {
        engine_sim_offline::c_api::diagnostic_severity(diagnostic.severity),
        engine_sim_offline::c_api::diagnostic_code(diagnostic.code),
        diagnostic.subject.has_value() ? 1U : 0U,
        diagnostic.source_position.has_value() ? 1U : 0U,
        diagnostic.source_position ? diagnostic.source_position->byte_offset : 0U,
        diagnostic.source_position ? diagnostic.source_position->line : 0U,
        diagnostic.source_position ? diagnostic.source_position->column : 0U,
        diagnostic.json_pointer.size(),
        subject_kind_size,
        subject_id_size,
        diagnostic.message.size(),
        diagnostic.related.size(),
    };
    return ESO_STATUS_OK;
}

eso_status_t eso_context_copy_diagnostic_text(
    const eso_context_t *const context, const size_t diagnostic_index,
    eso_diagnostic_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (!engine_sim_offline::c_api::fits(diagnostic, *buffers)) {
        return ESO_STATUS_BUFFER_TOO_SMALL;
    }
    const std::string_view subject_kind =
        diagnostic.subject ? diagnostic.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        diagnostic.subject ? diagnostic.subject->object_id : std::string_view{};
    engine_sim_offline::c_api::copy_unchecked(diagnostic.json_pointer,
                                              buffers->json_pointer);
    engine_sim_offline::c_api::copy_unchecked(subject_kind, buffers->subject_kind);
    engine_sim_offline::c_api::copy_unchecked(subject_id, buffers->subject_id);
    engine_sim_offline::c_api::copy_unchecked(diagnostic.message, buffers->message);
    return ESO_STATUS_OK;
}

eso_status_t eso_context_get_related_diagnostic(
    const eso_context_t *const context, const size_t diagnostic_index,
    const size_t related_index,
    eso_related_diagnostic_info_t *const out_related) noexcept {
    if (context == nullptr || out_related == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (related_index >= diagnostic.related.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &related = diagnostic.related[related_index];
    *out_related = {
        related.subject.has_value() ? 1U : 0U,
        related.json_pointer.size(),
        related.subject ? related.subject->object_kind.size() : 0U,
        related.subject ? related.subject->object_id.size() : 0U,
        related.message.size(),
    };
    return ESO_STATUS_OK;
}

eso_status_t eso_context_copy_related_diagnostic_text(
    const eso_context_t *const context, const size_t diagnostic_index,
    const size_t related_index, eso_diagnostic_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (related_index >= diagnostic.related.size()) {
        return ESO_STATUS_NOT_AVAILABLE;
    }
    const auto &related = diagnostic.related[related_index];
    if (!engine_sim_offline::c_api::fits(related, *buffers)) {
        return ESO_STATUS_BUFFER_TOO_SMALL;
    }
    const std::string_view subject_kind =
        related.subject ? related.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        related.subject ? related.subject->object_id : std::string_view{};
    engine_sim_offline::c_api::copy_unchecked(related.json_pointer,
                                              buffers->json_pointer);
    engine_sim_offline::c_api::copy_unchecked(subject_kind, buffers->subject_kind);
    engine_sim_offline::c_api::copy_unchecked(subject_id, buffers->subject_id);
    engine_sim_offline::c_api::copy_unchecked(related.message, buffers->message);
    return ESO_STATUS_OK;
}

} // extern "C"
