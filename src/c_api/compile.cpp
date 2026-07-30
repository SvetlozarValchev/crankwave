#include "c_api/c_api_internal.hpp"

#include "engine_sim_offline/authoring/parse.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::c_api {
namespace {

[[nodiscard]] std::string_view text(const eso_utf8_view_t view) noexcept {
    return {view.data, view.size};
}

[[nodiscard]] std::span<const std::byte> bytes(const eso_byte_view_t view) noexcept {
    return {reinterpret_cast<const std::byte *>(view.data), view.size};
}

[[nodiscard]] std::optional<compile::AssetKind>
asset_kind(const eso_asset_kind_t kind) noexcept {
    switch (kind) {
    case ESO_ASSET_AUDIO:
        return compile::AssetKind::audio;
    case ESO_ASSET_ACCESSORY_CONFIGURATION:
        return compile::AssetKind::accessory_configuration;
    default:
        return std::nullopt;
    }
}

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

} // namespace
} // namespace engine_sim_offline::c_api

extern "C" {

eso_status_t eso_compile_engine_json(eso_context_t *const context,
                                     const eso_utf8_view_t engine_json,
                                     const eso_asset_payload_t *const assets,
                                     const size_t asset_count,
                                     eso_engine_handle_t *const out_engine) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_engine == nullptr) {
            return invalid_pointer(*context, "out_engine must not be null");
        }
        *out_engine = ESO_INVALID_HANDLE;
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
                    *context, ESO_STATUS_INVALID_ARGUMENT, ESO_ERROR_STAGE_ARGUMENT,
                    ESO_ERROR_INVALID_ENUM, "c-api-invalid-asset-kind",
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
            return set_diagnostics(*context, ESO_STATUS_ENGINE_PARSE_FAILED,
                                   ESO_ERROR_STAGE_ENGINE_PARSE, "engine-json-invalid",
                                   "engine JSON parsing or schema validation failed",
                                   std::move(*report));
        }

        auto compiled = compile::compile_engine(
            std::get<authoring::EnginePackageDocument>(std::move(parsed)), asset_views);
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
            return set_diagnostics(
                *context, ESO_STATUS_ENGINE_COMPILE_FAILED,
                ESO_ERROR_STAGE_ENGINE_COMPILE, "engine-compilation-rejected",
                "engine compilation rejected the authored package", std::move(*report));
        }

        *out_engine = context->engines.insert(
            std::get<compile::CompiledEngine>(std::move(compiled)));
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_destroy_engine(eso_context_t *const context,
                                const eso_engine_handle_t engine) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (!context->engines.erase(engine)) {
            return invalid_handle(
                *context, "compiled-engine handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_engine_copy_id(eso_context_t *const context,
                                const eso_engine_handle_t engine,
                                const eso_mutable_utf8_buffer_t buffer,
                                size_t *const out_utf8_bytes) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
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
        if (status != ESO_STATUS_OK) {
            return set_error(*context, status, ESO_ERROR_STAGE_ARGUMENT,
                             status == ESO_STATUS_BUFFER_TOO_SMALL
                                 ? ESO_ERROR_BUFFER_CAPACITY
                                 : ESO_ERROR_INVALID_POINTER,
                             "c-api-engine-id-buffer-invalid",
                             "engine ID output buffer is invalid or too small");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t
eso_compile_scenario_json(eso_context_t *const context,
                          const eso_engine_handle_t engine,
                          const eso_utf8_view_t scenario_json,
                          eso_scenario_handle_t *const out_scenario) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_scenario == nullptr) {
            return invalid_pointer(*context, "out_scenario must not be null");
        }
        *out_scenario = ESO_INVALID_HANDLE;
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
            return set_diagnostics(*context, ESO_STATUS_SCENARIO_PARSE_FAILED,
                                   ESO_ERROR_STAGE_SCENARIO_PARSE,
                                   "scenario-json-invalid",
                                   "scenario JSON parsing or schema validation failed",
                                   std::move(*report));
        }

        auto compiled = compile::compile_scenario(
            *compiled_engine, std::get<authoring::ScenarioDocument>(std::move(parsed)));
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
            return set_diagnostics(*context, ESO_STATUS_SCENARIO_COMPILE_FAILED,
                                   ESO_ERROR_STAGE_SCENARIO_COMPILE,
                                   "scenario-compilation-rejected",
                                   "scenario compilation rejected the authored request",
                                   std::move(*report));
        }

        *out_scenario = context->scenarios.insert(
            std::get<compile::CompiledScenario>(std::move(compiled)));
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_destroy_scenario(eso_context_t *const context,
                                  const eso_scenario_handle_t scenario) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
        if (!context->scenarios.erase(scenario)) {
            return invalid_handle(
                *context, "compiled-scenario handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

eso_status_t eso_scenario_copy_id(eso_context_t *const context,
                                  const eso_scenario_handle_t scenario,
                                  const eso_mutable_utf8_buffer_t buffer,
                                  size_t *const out_utf8_bytes) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline::c_api;
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
        if (status != ESO_STATUS_OK) {
            return set_error(*context, status, ESO_ERROR_STAGE_ARGUMENT,
                             status == ESO_STATUS_BUFFER_TOO_SMALL
                                 ? ESO_ERROR_BUFFER_CAPACITY
                                 : ESO_ERROR_INVALID_POINTER,
                             "c-api-scenario-id-buffer-invalid",
                             "scenario ID output buffer is invalid or too small");
        }
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

} // extern "C"
