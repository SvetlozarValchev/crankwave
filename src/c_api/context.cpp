#include "c_api/c_api_internal.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace crankwave::c_api {
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
                        const crankwave_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr) {
        return buffer.capacity == 0U;
    }
    return text.size() != std::numeric_limits<std::size_t>::max() &&
           buffer.capacity >= text.size() + 1U;
}

[[nodiscard]] bool fits(const authoring::Diagnostic &diagnostic,
                        const crankwave_diagnostic_text_buffers_t &buffers) noexcept {
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
                        const crankwave_diagnostic_text_buffers_t &buffers) noexcept {
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
                    const crankwave_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr) {
        return;
    }
    if (!text.empty()) {
        std::memcpy(buffer.data, text.data(), text.size());
    }
    buffer.data[text.size()] = '\0';
}

} // namespace

bool valid(const crankwave_utf8_view_t view) noexcept {
    return view.data != nullptr || view.size == 0U;
}

bool valid(const crankwave_byte_view_t view) noexcept {
    return view.data != nullptr || view.size == 0U;
}

void clear_error(crankwave_context &context) noexcept {
    context.last_error.reset();
}

crankwave_status_t set_error(crankwave_context &context, const crankwave_status_t status,
                       const crankwave_error_stage_t stage, const crankwave_error_code_t code,
                       std::string detail_code, std::string message) {
    context.last_error = ErrorRecord{
        status, stage, code, std::move(detail_code), std::move(message), {}};
    return status;
}

crankwave_status_t set_diagnostics(crankwave_context &context, const crankwave_status_t status,
                             const crankwave_error_stage_t stage, std::string detail_code,
                             std::string message, authoring::DiagnosticReport report) {
    context.last_error = ErrorRecord{status,
                                     stage,
                                     CRANKWAVE_ERROR_AUTHORING_DIAGNOSTICS,
                                     std::move(detail_code),
                                     std::move(message),
                                     std::move(report.diagnostics)};
    return status;
}

void set_unexpected_error_noexcept(crankwave_context &context, const bool resource) noexcept {
    try {
        context.last_error = ErrorRecord{
            resource ? CRANKWAVE_STATUS_RESOURCE_EXHAUSTED : CRANKWAVE_STATUS_INTERNAL_ERROR,
            CRANKWAVE_ERROR_STAGE_ABI,
            resource ? CRANKWAVE_ERROR_SESSION_RESOURCE_EXHAUSTED
                     : CRANKWAVE_ERROR_SESSION_INTERNAL,
            resource ? "c-api-resource-exhausted" : "c-api-boundary-failed",
            resource ? "the C API boundary exhausted memory"
                     : "the C API boundary caught an unexpected exception",
            {}};
    } catch (...) {
        context.last_error.reset();
    }
}

crankwave_status_t copy_text(const std::string_view text,
                       const crankwave_mutable_utf8_buffer_t buffer) noexcept {
    if (buffer.data == nullptr && buffer.capacity != 0U) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (buffer.data == nullptr) {
        return CRANKWAVE_STATUS_OK;
    }
    if (text.size() == std::numeric_limits<std::size_t>::max() ||
        buffer.capacity < text.size() + 1U) {
        return CRANKWAVE_STATUS_BUFFER_TOO_SMALL;
    }
    copy_unchecked(text, buffer);
    return CRANKWAVE_STATUS_OK;
}

crankwave_diagnostic_severity_t
diagnostic_severity(const authoring::DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case authoring::DiagnosticSeverity::error:
        return CRANKWAVE_DIAGNOSTIC_ERROR;
    case authoring::DiagnosticSeverity::warning:
        return CRANKWAVE_DIAGNOSTIC_WARNING;
    }
    return CRANKWAVE_DIAGNOSTIC_ERROR;
}

crankwave_diagnostic_code_t diagnostic_code(const authoring::DiagnosticCode code) noexcept {
    using Code = authoring::DiagnosticCode;
    switch (code) {
    case Code::malformed_document:
        return CRANKWAVE_DIAGNOSTIC_MALFORMED_DOCUMENT;
    case Code::unsupported_schema:
        return CRANKWAVE_DIAGNOSTIC_UNSUPPORTED_SCHEMA;
    case Code::missing_value:
        return CRANKWAVE_DIAGNOSTIC_MISSING_VALUE;
    case Code::unknown_field:
        return CRANKWAVE_DIAGNOSTIC_UNKNOWN_FIELD;
    case Code::invalid_type:
        return CRANKWAVE_DIAGNOSTIC_INVALID_TYPE;
    case Code::invalid_unit:
        return CRANKWAVE_DIAGNOSTIC_INVALID_UNIT;
    case Code::invalid_value:
        return CRANKWAVE_DIAGNOSTIC_INVALID_VALUE;
    case Code::out_of_range:
        return CRANKWAVE_DIAGNOSTIC_OUT_OF_RANGE;
    case Code::duplicate_id:
        return CRANKWAVE_DIAGNOSTIC_DUPLICATE_ID;
    case Code::dangling_reference:
        return CRANKWAVE_DIAGNOSTIC_DANGLING_REFERENCE;
    case Code::forbidden_cycle:
        return CRANKWAVE_DIAGNOSTIC_FORBIDDEN_CYCLE;
    case Code::disconnected_object:
        return CRANKWAVE_DIAGNOSTIC_DISCONNECTED_OBJECT;
    case Code::inconsistent_value:
        return CRANKWAVE_DIAGNOSTIC_INCONSISTENT_VALUE;
    case Code::unsupported_capability:
        return CRANKWAVE_DIAGNOSTIC_UNSUPPORTED_CAPABILITY;
    case Code::missing_asset:
        return CRANKWAVE_DIAGNOSTIC_MISSING_ASSET;
    case Code::asset_hash_mismatch:
        return CRANKWAVE_DIAGNOSTIC_ASSET_HASH_MISMATCH;
    case Code::resource_limit:
        return CRANKWAVE_DIAGNOSTIC_RESOURCE_LIMIT;
    case Code::internal_failure:
        return CRANKWAVE_DIAGNOSTIC_INTERNAL_FAILURE;
    }
    return CRANKWAVE_DIAGNOSTIC_INTERNAL_FAILURE;
}

crankwave_error_code_t session_error_code(const EngineSessionErrorCode code) noexcept {
    switch (code) {
    case EngineSessionErrorCode::invalid_compiled_scenario:
        return CRANKWAVE_ERROR_SESSION_INVALID_COMPILED_SCENARIO;
    case EngineSessionErrorCode::unsupported_configuration:
        return CRANKWAVE_ERROR_SESSION_UNSUPPORTED_CONFIGURATION;
    case EngineSessionErrorCode::resource_exhausted:
        return CRANKWAVE_ERROR_SESSION_RESOURCE_EXHAUSTED;
    case EngineSessionErrorCode::processing_failed:
        return CRANKWAVE_ERROR_SESSION_PROCESSING_FAILED;
    case EngineSessionErrorCode::consumer_state_invalid:
        return CRANKWAVE_ERROR_SESSION_CONSUMER_STATE_INVALID;
    case EngineSessionErrorCode::internal_error:
        return CRANKWAVE_ERROR_SESSION_INTERNAL;
    }
    return CRANKWAVE_ERROR_SESSION_INTERNAL;
}

crankwave_error_code_t control_error_code(const EngineControlRejectionCode code) noexcept {
    switch (code) {
    case EngineControlRejectionCode::capacity_exceeded:
        return CRANKWAVE_ERROR_CONTROL_CAPACITY_EXCEEDED;
    case EngineControlRejectionCode::late_command:
        return CRANKWAVE_ERROR_CONTROL_LATE_COMMAND;
    case EngineControlRejectionCode::invalid_payload:
        return CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD;
    case EngineControlRejectionCode::unordered_delivery_frame:
        return CRANKWAVE_ERROR_CONTROL_UNORDERED_DELIVERY_FRAME;
    case EngineControlRejectionCode::duplicate_sequence:
        return CRANKWAVE_ERROR_CONTROL_DUPLICATE_SEQUENCE;
    case EngineControlRejectionCode::unordered_sequence:
        return CRANKWAVE_ERROR_CONTROL_UNORDERED_SEQUENCE;
    case EngineControlRejectionCode::unsupported_for_operating_mode:
        return CRANKWAVE_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE;
    case EngineControlRejectionCode::unavailable_during_preparation:
        return CRANKWAVE_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION;
    case EngineControlRejectionCode::outside_session_horizon:
        return CRANKWAVE_ERROR_CONTROL_OUTSIDE_SESSION_HORIZON;
    case EngineControlRejectionCode::session_terminal:
        return CRANKWAVE_ERROR_CONTROL_SESSION_TERMINAL;
    case EngineControlRejectionCode::internal_clock_error:
        return CRANKWAVE_ERROR_CONTROL_INTERNAL_CLOCK;
    }
    return CRANKWAVE_ERROR_CONTROL_INTERNAL_CLOCK;
}

crankwave_audio_bus_kind_t audio_bus_kind(const EngineAudioBusKind kind) noexcept {
    switch (kind) {
    case EngineAudioBusKind::source_route_dry:
        return CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_DRY;
    case EngineAudioBusKind::source_route_configured_transfer:
        return CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER;
    case EngineAudioBusKind::source_route_selected:
        return CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_SELECTED;
    case EngineAudioBusKind::engine_raw_master:
        return CRANKWAVE_AUDIO_BUS_ENGINE_RAW_MASTER;
    case EngineAudioBusKind::engine_audition_master:
        return CRANKWAVE_AUDIO_BUS_ENGINE_AUDITION_MASTER;
    }
    return CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_SELECTED;
}

crankwave_source_route_kind_t
source_route_kind(const contract::SourceRouteKind kind) noexcept {
    switch (kind) {
    case contract::SourceRouteKind::unspecified:
        return CRANKWAVE_SOURCE_ROUTE_UNSPECIFIED;
    case contract::SourceRouteKind::exhaust_outlet:
        return CRANKWAVE_SOURCE_ROUTE_EXHAUST_OUTLET;
    case contract::SourceRouteKind::intake_inlet:
        return CRANKWAVE_SOURCE_ROUTE_INTAKE_INLET;
    case contract::SourceRouteKind::mechanical_engine:
        return CRANKWAVE_SOURCE_ROUTE_MECHANICAL_ENGINE;
    case contract::SourceRouteKind::mechanical_starter:
        return CRANKWAVE_SOURCE_ROUTE_MECHANICAL_STARTER;
    }
    return CRANKWAVE_SOURCE_ROUTE_UNSPECIFIED;
}

crankwave_audio_signal_disposition_t
audio_signal_disposition(const EngineAudioSignalDisposition disposition) noexcept {
    switch (disposition) {
    case EngineAudioSignalDisposition::active:
        return CRANKWAVE_AUDIO_SIGNAL_ACTIVE;
    case EngineAudioSignalDisposition::declared_silent:
        return CRANKWAVE_AUDIO_SIGNAL_DECLARED_SILENT;
    }
    return CRANKWAVE_AUDIO_SIGNAL_DECLARED_SILENT;
}

} // namespace crankwave::c_api

extern "C" {

uint32_t crankwave_api_version(void) noexcept {
    return CRANKWAVE_C_API_VERSION;
}

crankwave_status_t crankwave_get_abi_layout(crankwave_abi_layout_t *const out_layout) noexcept {
    if (out_layout == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    *out_layout = {
        CRANKWAVE_C_API_VERSION,
        static_cast<std::uint32_t>(sizeof(void *)),
        static_cast<std::uint32_t>(sizeof(std::size_t)),
        static_cast<std::uint32_t>(sizeof(float)),
        static_cast<std::uint32_t>(sizeof(double)),
        std::endian::native == std::endian::little ? 1U : 0U,
        static_cast<std::uint32_t>(sizeof(crankwave_control_command_t)),
        static_cast<std::uint32_t>(sizeof(crankwave_session_descriptor_t)),
        static_cast<std::uint32_t>(sizeof(crankwave_forward_gear_descriptor_t)),
        static_cast<std::uint32_t>(sizeof(crankwave_audio_bus_descriptor_t)),
        static_cast<std::uint32_t>(sizeof(crankwave_session_telemetry_t)),
        static_cast<std::uint32_t>(sizeof(crankwave_completed_cycle_evidence_t)),
    };
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t crankwave_context_create(const uint32_t requested_api_version,
                                crankwave_context_t **const out_context) noexcept {
    if (out_context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    *out_context = nullptr;
    if (requested_api_version != CRANKWAVE_C_API_VERSION) {
        return CRANKWAVE_STATUS_ABI_VERSION_MISMATCH;
    }
    try {
        const auto tag = crankwave::c_api::acquire_context_tag();
        if (!tag.has_value()) {
            return CRANKWAVE_STATUS_RESOURCE_EXHAUSTED;
        }
        *out_context = new crankwave_context{*tag};
        return CRANKWAVE_STATUS_OK;
    } catch (const std::bad_alloc &) {
        return CRANKWAVE_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CRANKWAVE_STATUS_INTERNAL_ERROR;
    }
}

crankwave_status_t crankwave_context_destroy(crankwave_context_t *const context) noexcept {
    delete context;
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t crankwave_context_get_last_error(const crankwave_context_t *const context,
                                        crankwave_error_info_t *const out_error) noexcept {
    if (context == nullptr || out_error == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &error = *context->last_error;
    *out_error = {
        error.status,         error.stage,
        error.code,           error.detail_code.size(),
        error.message.size(), error.diagnostics.size(),
    };
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t
crankwave_context_copy_last_error_text(const crankwave_context_t *const context,
                                 crankwave_error_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
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
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!detail_fits || !message_fits) {
        return CRANKWAVE_STATUS_BUFFER_TOO_SMALL;
    }
    (void)crankwave::c_api::copy_text(error.detail_code, buffers->detail_code);
    (void)crankwave::c_api::copy_text(error.message, buffers->message);
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t
crankwave_context_get_diagnostic(const crankwave_context_t *const context,
                           const size_t diagnostic_index,
                           crankwave_diagnostic_info_t *const out_diagnostic) noexcept {
    if (context == nullptr || out_diagnostic == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    const auto subject_kind_size =
        diagnostic.subject ? diagnostic.subject->object_kind.size() : 0U;
    const auto subject_id_size =
        diagnostic.subject ? diagnostic.subject->object_id.size() : 0U;
    *out_diagnostic = {
        crankwave::c_api::diagnostic_severity(diagnostic.severity),
        crankwave::c_api::diagnostic_code(diagnostic.code),
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
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t crankwave_context_copy_diagnostic_text(
    const crankwave_context_t *const context, const size_t diagnostic_index,
    crankwave_diagnostic_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (!crankwave::c_api::fits(diagnostic, *buffers)) {
        return CRANKWAVE_STATUS_BUFFER_TOO_SMALL;
    }
    const std::string_view subject_kind =
        diagnostic.subject ? diagnostic.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        diagnostic.subject ? diagnostic.subject->object_id : std::string_view{};
    crankwave::c_api::copy_unchecked(diagnostic.json_pointer,
                                              buffers->json_pointer);
    crankwave::c_api::copy_unchecked(subject_kind, buffers->subject_kind);
    crankwave::c_api::copy_unchecked(subject_id, buffers->subject_id);
    crankwave::c_api::copy_unchecked(diagnostic.message, buffers->message);
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t crankwave_context_get_related_diagnostic(
    const crankwave_context_t *const context, const size_t diagnostic_index,
    const size_t related_index,
    crankwave_related_diagnostic_info_t *const out_related) noexcept {
    if (context == nullptr || out_related == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (related_index >= diagnostic.related.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &related = diagnostic.related[related_index];
    *out_related = {
        related.subject.has_value() ? 1U : 0U,
        related.json_pointer.size(),
        related.subject ? related.subject->object_kind.size() : 0U,
        related.subject ? related.subject->object_id.size() : 0U,
        related.message.size(),
    };
    return CRANKWAVE_STATUS_OK;
}

crankwave_status_t crankwave_context_copy_related_diagnostic_text(
    const crankwave_context_t *const context, const size_t diagnostic_index,
    const size_t related_index, crankwave_diagnostic_text_buffers_t *const buffers) noexcept {
    if (context == nullptr || buffers == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    if (!context->last_error.has_value() ||
        diagnostic_index >= context->last_error->diagnostics.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &diagnostic = context->last_error->diagnostics[diagnostic_index];
    if (related_index >= diagnostic.related.size()) {
        return CRANKWAVE_STATUS_NOT_AVAILABLE;
    }
    const auto &related = diagnostic.related[related_index];
    if (!crankwave::c_api::fits(related, *buffers)) {
        return CRANKWAVE_STATUS_BUFFER_TOO_SMALL;
    }
    const std::string_view subject_kind =
        related.subject ? related.subject->object_kind : std::string_view{};
    const std::string_view subject_id =
        related.subject ? related.subject->object_id : std::string_view{};
    crankwave::c_api::copy_unchecked(related.json_pointer,
                                              buffers->json_pointer);
    crankwave::c_api::copy_unchecked(subject_kind, buffers->subject_kind);
    crankwave::c_api::copy_unchecked(subject_id, buffers->subject_id);
    crankwave::c_api::copy_unchecked(related.message, buffers->message);
    return CRANKWAVE_STATUS_OK;
}

} // extern "C"
