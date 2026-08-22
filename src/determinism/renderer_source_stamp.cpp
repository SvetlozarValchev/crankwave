#include "determinism/renderer_source_stamp.hpp"

#include <crankwave_generated/renderer_source_stamp_generated.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace crankwave::determinism {
namespace {

[[nodiscard]] std::optional<RendererSourceState>
source_state(std::string_view value) noexcept {
    if (value == "clean") {
        return RendererSourceState::clean;
    }
    if (value == "dirty") {
        return RendererSourceState::dirty;
    }
    if (value == "unavailable") {
        return RendererSourceState::unavailable;
    }
    return std::nullopt;
}

[[nodiscard]] bool is_lowercase_hex(std::string_view value) noexcept {
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<contract::Sha256Digest>
sha256_digest(std::string_view value) noexcept {
    if (value.size() != 64 || !is_lowercase_hex(value)) {
        return std::nullopt;
    }

    const auto nibble = [](char character) noexcept -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        return static_cast<std::uint8_t>(10 + character - 'a');
    };

    contract::Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(
            (nibble(value[index * 2]) << 4U) | nibble(value[index * 2 + 1]));
    }
    return digest;
}

[[nodiscard]] RendererSourceStampError error(RendererSourceStampErrorCode code,
                                             RendererSourceState state,
                                             std::string message) {
    return {code, state, std::move(message)};
}

} // namespace

RendererSourceStampResult
detail::decode_renderer_source_stamp(const EmbeddedRendererSourceStamp &embedded) {
    auto closure_result = detail::decode_renderer_source_closure(embedded);
    if (const auto *closure_error =
            std::get_if<RendererSourceStampError>(&closure_result)) {
        return *closure_error;
    }
    const auto &closure = std::get<RendererSourceClosure>(closure_result);

    if (embedded.toolchain_state == "unavailable") {
        return error(RendererSourceStampErrorCode::unavailable_toolchain,
                     closure.source_state,
                     "renderer toolchain identity was unavailable");
    }
    if (embedded.toolchain_state != "available") {
        return error(RendererSourceStampErrorCode::malformed_embedded_stamp,
                     closure.source_state,
                     "embedded renderer toolchain state is invalid");
    }
    if (embedded.compiler_id.empty() || embedded.compiler_version.empty() ||
        embedded.target_triple.empty()) {
        return error(RendererSourceStampErrorCode::malformed_embedded_stamp,
                     closure.source_state,
                     "available renderer toolchain stamp is incomplete");
    }

    return RendererSourceStamp{
        closure.source_state,
        closure.full_git_head,
        closure.source_closure_sha256,
        std::string(embedded.compiler_id),
        std::string(embedded.compiler_version),
        std::string(embedded.target_triple),
    };
}

RendererSourceClosureResult detail::decode_renderer_source_closure(
    const EmbeddedRendererSourceStamp &embedded) {
    const auto state = source_state(embedded.source_state);
    if (!state.has_value()) {
        return error(RendererSourceStampErrorCode::malformed_embedded_stamp,
                     RendererSourceState::unavailable,
                     "embedded renderer source state is invalid");
    }
    if (*state == RendererSourceState::dirty) {
        return error(RendererSourceStampErrorCode::dirty_source, *state,
                     "renderer source closure was dirty when the stamp was built");
    }
    if (*state == RendererSourceState::unavailable) {
        return error(RendererSourceStampErrorCode::unavailable_source, *state,
                     "renderer source identity was unavailable");
    }

    const bool valid_revision =
        (embedded.full_git_head.size() == 40 || embedded.full_git_head.size() == 64) &&
        is_lowercase_hex(embedded.full_git_head) &&
        embedded.full_git_head.find_first_not_of('0') != std::string_view::npos;
    const auto closure_digest = sha256_digest(embedded.source_closure_sha256);
    if (!valid_revision || !closure_digest.has_value() || closure_digest->is_zero()) {
        return error(RendererSourceStampErrorCode::malformed_embedded_stamp, *state,
                     "clean renderer source closure is incomplete or malformed");
    }

    return RendererSourceClosure{
        *state,
        std::string(embedded.full_git_head),
        *closure_digest,
    };
}

RendererSourceClosureResult renderer_source_closure() {
    return detail::decode_renderer_source_closure({
        generated::kRendererSourceState,
        generated::kRendererFullGitHead,
        generated::kRendererSourceClosureSha256,
        generated::kRendererToolchainState,
        generated::kRendererCompilerId,
        generated::kRendererCompilerVersion,
        generated::kRendererTargetTriple,
    });
}

RendererSourceStampResult renderer_source_stamp() {
    return detail::decode_renderer_source_stamp({
        generated::kRendererSourceState,
        generated::kRendererFullGitHead,
        generated::kRendererSourceClosureSha256,
        generated::kRendererToolchainState,
        generated::kRendererCompilerId,
        generated::kRendererCompilerVersion,
        generated::kRendererTargetTriple,
    });
}

} // namespace crankwave::determinism
