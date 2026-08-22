#include "c_api/c_api_internal.hpp"

#include "crankwave/authoring/parse.hpp"

#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::c_api {
namespace {

[[nodiscard]] std::string_view text(const crankwave_utf8_view_t view) noexcept {
    return {view.data, view.size};
}

[[nodiscard]] std::span<const std::byte> bytes(const crankwave_byte_view_t view) noexcept {
    return {reinterpret_cast<const std::byte *>(view.data), view.size};
}

[[nodiscard]] std::optional<compile::AssetKind>
asset_kind(const crankwave_asset_kind_t kind) noexcept {
    switch (kind) {
    case CRANKWAVE_ASSET_AUDIO:
        return compile::AssetKind::audio;
    case CRANKWAVE_ASSET_ACCESSORY_CONFIGURATION:
        return compile::AssetKind::accessory_configuration;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] crankwave_status_t invalid_pointer(crankwave_context &context, std::string message) {
    return set_error(context, CRANKWAVE_STATUS_INVALID_ARGUMENT, CRANKWAVE_ERROR_STAGE_ARGUMENT,
                     CRANKWAVE_ERROR_INVALID_POINTER, "c-api-invalid-pointer",
                     std::move(message));
}

[[nodiscard]] crankwave_status_t invalid_handle(crankwave_context &context, std::string message) {
    return set_error(context, CRANKWAVE_STATUS_INVALID_HANDLE, CRANKWAVE_ERROR_STAGE_HANDLE,
                     CRANKWAVE_ERROR_INVALID_HANDLE, "c-api-invalid-handle",
                     std::move(message));
}

} // namespace
} // namespace crankwave::c_api

extern "C" {

crankwave_status_t crankwave_compile_engine_json(crankwave_context_t *const context,
                                     const crankwave_utf8_view_t engine_json,
                                     const crankwave_asset_payload_t *const assets,
                                     const size_t asset_count,
                                     crankwave_engine_handle_t *const out_engine) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave;
        using namespace crankwave::c_api;
        if (out_engine == nullptr) {
            return invalid_pointer(*context, "out_engine must not be null");
        }
        *out_engine = CRANKWAVE_INVALID_HANDLE;
        if (!valid(engine_json)) {
            return invalid_pointer(
                *context, "engine JSON data must not be null when its size is nonzero");
        }
        if (assets == nullptr && asset_count != 0U) {
            return invalid_pointer(
                *context, "asset array must not be null when asset_count is nonzero");
        }

        std::vector<compile::AssetPayloadView> asset_views;
        asset_views.reserve(asset_count);
        for (std::size_t index = 0; index < asset_count; ++index) {
            const auto kind = asset_kind(assets[index].kind);
            if (!kind.has_value()) {
                return set_error(
                    *context, CRANKWAVE_STATUS_INVALID_ARGUMENT, CRANKWAVE_ERROR_STAGE_ARGUMENT,
                    CRANKWAVE_ERROR_INVALID_ENUM, "c-api-invalid-asset-kind",
                    "asset " + std::to_string(index) + " has an unknown asset kind");
            }
            if (!valid(assets[index].asset_id) || !valid(assets[index].bytes)) {
                return invalid_pointer(*context,
                                       "asset " + std::to_string(index) +
                                           " has a null pointer with a nonzero extent");
            }
            asset_views.push_back(
                {*kind, text(assets[index].asset_id), bytes(assets[index].bytes)});
        }

        auto parsed = authoring::parse_engine_document(text(engine_json));
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&parsed)) {
            return set_diagnostics(*context, CRANKWAVE_STATUS_ENGINE_PARSE_FAILED,
                                   CRANKWAVE_ERROR_STAGE_ENGINE_PARSE, "engine-json-invalid",
                                   "engine JSON parsing or schema validation failed",
                                   std::move(*report));
        }

        auto compiled = compile::compile_engine(
            std::get<authoring::EnginePackageDocument>(std::move(parsed)), asset_views);
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
            return set_diagnostics(
                *context, CRANKWAVE_STATUS_ENGINE_COMPILE_FAILED,
                CRANKWAVE_ERROR_STAGE_ENGINE_COMPILE, "engine-compilation-rejected",
                "engine compilation rejected the authored package", std::move(*report));
        }

        *out_engine = context->engines.insert(
            std::get<compile::CompiledEngine>(std::move(compiled)));
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_destroy_engine(crankwave_context_t *const context,
                                const crankwave_engine_handle_t engine) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (!context->engines.erase(engine)) {
            return invalid_handle(
                *context, "compiled-engine handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_engine_copy_id(crankwave_context_t *const context,
                                const crankwave_engine_handle_t engine,
                                const crankwave_mutable_utf8_buffer_t buffer,
                                size_t *const out_utf8_bytes) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (out_utf8_bytes == nullptr) {
            return invalid_pointer(*context, "out_utf8_bytes must not be null");
        }
        const auto *compiled = context->engines.get(engine);
        if (compiled == nullptr) {
            return invalid_handle(
                *context, "compiled-engine handle is stale, invalid, or wrong-kind");
        }
        *out_utf8_bytes = compiled->id().size();
        const auto status = copy_text(compiled->id(), buffer);
        if (status != CRANKWAVE_STATUS_OK) {
            return set_error(*context, status, CRANKWAVE_ERROR_STAGE_ARGUMENT,
                             status == CRANKWAVE_STATUS_BUFFER_TOO_SMALL
                                 ? CRANKWAVE_ERROR_BUFFER_CAPACITY
                                 : CRANKWAVE_ERROR_INVALID_POINTER,
                             "c-api-engine-id-buffer-invalid",
                             "engine ID output buffer is invalid or too small");
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t
crankwave_engine_copy_provenance_sha256(crankwave_context_t *const context,
                                  const crankwave_engine_handle_t engine,
                                  crankwave_sha256_digest_t *const out_sha256) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (out_sha256 == nullptr) {
            return invalid_pointer(*context, "out_sha256 must not be null");
        }
        const auto *compiled = context->engines.get(engine);
        if (compiled == nullptr) {
            return invalid_handle(
                *context, "compiled-engine handle is stale, invalid, or wrong-kind");
        }
        const auto &bytes = compiled->provenance().bundle.sha256.bytes;
        static_assert(sizeof(bytes) == CRANKWAVE_SHA256_DIGEST_SIZE);
        std::memcpy(out_sha256->bytes, bytes.data(), bytes.size());
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t
crankwave_compile_scenario_json(crankwave_context_t *const context,
                          const crankwave_engine_handle_t engine,
                          const crankwave_utf8_view_t scenario_json,
                          crankwave_scenario_handle_t *const out_scenario) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave;
        using namespace crankwave::c_api;
        if (out_scenario == nullptr) {
            return invalid_pointer(*context, "out_scenario must not be null");
        }
        *out_scenario = CRANKWAVE_INVALID_HANDLE;
        if (!valid(scenario_json)) {
            return invalid_pointer(
                *context,
                "scenario JSON data must not be null when its size is nonzero");
        }
        const auto *compiled_engine = context->engines.get(engine);
        if (compiled_engine == nullptr) {
            return invalid_handle(
                *context, "compiled-engine handle is stale, invalid, or wrong-kind");
        }

        auto parsed = authoring::parse_scenario_document(text(scenario_json));
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&parsed)) {
            return set_diagnostics(*context, CRANKWAVE_STATUS_SCENARIO_PARSE_FAILED,
                                   CRANKWAVE_ERROR_STAGE_SCENARIO_PARSE,
                                   "scenario-json-invalid",
                                   "scenario JSON parsing or schema validation failed",
                                   std::move(*report));
        }

        auto compiled = compile::compile_scenario(
            *compiled_engine, std::get<authoring::ScenarioDocument>(std::move(parsed)));
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
            return set_diagnostics(*context, CRANKWAVE_STATUS_SCENARIO_COMPILE_FAILED,
                                   CRANKWAVE_ERROR_STAGE_SCENARIO_COMPILE,
                                   "scenario-compilation-rejected",
                                   "scenario compilation rejected the authored request",
                                   std::move(*report));
        }

        *out_scenario = context->scenarios.insert(
            std::get<compile::CompiledScenario>(std::move(compiled)));
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_destroy_scenario(crankwave_context_t *const context,
                                  const crankwave_scenario_handle_t scenario) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (!context->scenarios.erase(scenario)) {
            return invalid_handle(
                *context, "compiled-scenario handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_scenario_copy_id(crankwave_context_t *const context,
                                  const crankwave_scenario_handle_t scenario,
                                  const crankwave_mutable_utf8_buffer_t buffer,
                                  size_t *const out_utf8_bytes) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (out_utf8_bytes == nullptr) {
            return invalid_pointer(*context, "out_utf8_bytes must not be null");
        }
        const auto *compiled = context->scenarios.get(scenario);
        if (compiled == nullptr) {
            return invalid_handle(
                *context, "compiled-scenario handle is stale, invalid, or wrong-kind");
        }
        *out_utf8_bytes = compiled->id().size();
        const auto status = copy_text(compiled->id(), buffer);
        if (status != CRANKWAVE_STATUS_OK) {
            return set_error(*context, status, CRANKWAVE_ERROR_STAGE_ARGUMENT,
                             status == CRANKWAVE_STATUS_BUFFER_TOO_SMALL
                                 ? CRANKWAVE_ERROR_BUFFER_CAPACITY
                                 : CRANKWAVE_ERROR_INVALID_POINTER,
                             "c-api-scenario-id-buffer-invalid",
                             "scenario ID output buffer is invalid or too small");
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

} // extern "C"
