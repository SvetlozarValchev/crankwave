#include "c_api/c_api_internal.hpp"

#include "determinism/renderer_source_stamp.hpp"

#include <cstring>
#include <variant>

extern "C" {

crankwave_status_t crankwave_renderer_copy_source_closure_sha256(
    crankwave_context_t *const context, crankwave_sha256_digest_t *const out_sha256) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave;
        using namespace crankwave::c_api;
        if (out_sha256 == nullptr) {
            return set_error(*context, CRANKWAVE_STATUS_INVALID_ARGUMENT,
                             CRANKWAVE_ERROR_STAGE_ARGUMENT, CRANKWAVE_ERROR_INVALID_POINTER,
                             "c-api-invalid-pointer", "out_sha256 must not be null");
        }

        auto result = determinism::renderer_source_closure();
        if (const auto *error =
                std::get_if<determinism::RendererSourceStampError>(&result)) {
            return set_error(*context, CRANKWAVE_STATUS_NOT_AVAILABLE, CRANKWAVE_ERROR_STAGE_ABI,
                             CRANKWAVE_ERROR_RENDERER_SOURCE_STAMP_UNAVAILABLE,
                             "renderer-source-stamp-unavailable", error->message);
        }

        const auto &bytes = std::get<determinism::RendererSourceClosure>(result)
                                .source_closure_sha256.bytes;
        static_assert(sizeof(bytes) == CRANKWAVE_SHA256_DIGEST_SIZE);
        std::memcpy(out_sha256->bytes, bytes.data(), bytes.size());
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

} // extern "C"
