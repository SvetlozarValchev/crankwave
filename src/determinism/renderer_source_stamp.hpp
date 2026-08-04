#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::determinism {

enum class RendererSourceState {
    clean,
    dirty,
    unavailable,
};

struct RendererSourceStamp {
    RendererSourceState source_state = RendererSourceState::unavailable;
    std::string full_git_head;
    contract::Sha256Digest source_closure_sha256;
    std::string compiler_id;
    std::string compiler_version;
    std::string target_triple;

    friend bool operator==(const RendererSourceStamp &,
                           const RendererSourceStamp &) = default;
};

// Source identity is useful independently of the native publication toolchain.
// In particular, the WASM renderer can prove that it was built from the same clean
// source closure without claiming to be the canonical native Release toolchain.
struct RendererSourceClosure {
    RendererSourceState source_state = RendererSourceState::unavailable;
    std::string full_git_head;
    contract::Sha256Digest source_closure_sha256;

    friend bool operator==(const RendererSourceClosure &,
                           const RendererSourceClosure &) = default;
};

enum class RendererSourceStampErrorCode {
    dirty_source,
    unavailable_source,
    unavailable_toolchain,
    malformed_embedded_stamp,
};

struct RendererSourceStampError {
    RendererSourceStampErrorCode code =
        RendererSourceStampErrorCode::malformed_embedded_stamp;
    RendererSourceState source_state = RendererSourceState::unavailable;
    std::string message;

    friend bool operator==(const RendererSourceStampError &,
                           const RendererSourceStampError &) = default;
};

using RendererSourceStampResult =
    std::variant<RendererSourceStamp, RendererSourceStampError>;
using RendererSourceClosureResult =
    std::variant<RendererSourceClosure, RendererSourceStampError>;

// Returns only a clean, complete source-closure identity. Unlike the full stamp,
// this does not require the canonical native compiler/target identity.
[[nodiscard]] RendererSourceClosureResult renderer_source_closure();

// Returns only a clean, complete build-owned stamp. Dirty or unavailable source is
// represented by a typed error and can never be mistaken for admissible evidence.
[[nodiscard]] RendererSourceStampResult renderer_source_stamp();

namespace detail {

// Private decoding seam used by the generated build record and focused tests. These
// strings are never accepted from the CLI or the public rendering API.
struct EmbeddedRendererSourceStamp {
    std::string_view source_state;
    std::string_view full_git_head;
    std::string_view source_closure_sha256;
    std::string_view toolchain_state;
    std::string_view compiler_id;
    std::string_view compiler_version;
    std::string_view target_triple;
};

[[nodiscard]] RendererSourceClosureResult
decode_renderer_source_closure(const EmbeddedRendererSourceStamp &embedded);

[[nodiscard]] RendererSourceStampResult
decode_renderer_source_stamp(const EmbeddedRendererSourceStamp &embedded);

} // namespace detail
} // namespace engine_sim_offline::determinism
