#include "c_api/c_api_internal.hpp"

#include "determinism/renderer_source_stamp.hpp"

#include <cstring>
#include <variant>

extern "C" {

eso_status_t eso_renderer_copy_source_closure_sha256(
    eso_context_t *const context, eso_sha256_digest_t *const out_sha256) noexcept {
    if (context == nullptr) {
        return ESO_STATUS_INVALID_ARGUMENT;
    }
    return engine_sim_offline::c_api::boundary(*context, [&]() -> eso_status_t {
        using namespace engine_sim_offline;
        using namespace engine_sim_offline::c_api;
        if (out_sha256 == nullptr) {
            return set_error(*context, ESO_STATUS_INVALID_ARGUMENT,
                             ESO_ERROR_STAGE_ARGUMENT, ESO_ERROR_INVALID_POINTER,
                             "c-api-invalid-pointer", "out_sha256 must not be null");
        }

        auto result = determinism::renderer_source_closure();
        if (const auto *error =
                std::get_if<determinism::RendererSourceStampError>(&result)) {
            return set_error(*context, ESO_STATUS_NOT_AVAILABLE, ESO_ERROR_STAGE_ABI,
                             ESO_ERROR_RENDERER_SOURCE_STAMP_UNAVAILABLE,
                             "renderer-source-stamp-unavailable", error->message);
        }

        const auto &bytes = std::get<determinism::RendererSourceClosure>(result)
                                .source_closure_sha256.bytes;
        static_assert(sizeof(bytes) == ESO_SHA256_DIGEST_SIZE);
        std::memcpy(out_sha256->bytes, bytes.data(), bytes.size());
        clear_error(*context);
        return ESO_STATUS_OK;
    });
}

} // extern "C"
