#include "determinism/renderer_source_stamp.hpp"

#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::determinism;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string(message)};
    }
}

detail::EmbeddedRendererSourceStamp clean_stamp() {
    return {
        "clean",
        "0123456789abcdef0123456789abcdef01234567",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "available",
        "GNU",
        "13.3.0",
        "x86_64-linux-gnu",
    };
}

void test_clean_stamp_is_admitted() {
    const auto result = detail::decode_renderer_source_stamp(clean_stamp());
    const auto *stamp = std::get_if<RendererSourceStamp>(&result);
    expect(stamp != nullptr, "complete clean source stamp was rejected");
    expect(stamp->source_state == RendererSourceState::clean,
           "clean source stamp retained the wrong state");
    expect(stamp->full_git_head == "0123456789abcdef0123456789abcdef01234567",
           "clean source stamp changed the Git revision");
    expect(!stamp->source_closure_sha256.is_zero(),
           "clean source stamp lost its closure digest");
    expect(stamp->compiler_id == "GNU" && stamp->compiler_version == "13.3.0" &&
               stamp->target_triple == "x86_64-linux-gnu",
           "clean source stamp changed its toolchain facts");

    auto sha256_git = clean_stamp();
    sha256_git.full_git_head =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    expect(std::holds_alternative<RendererSourceStamp>(
               detail::decode_renderer_source_stamp(sha256_git)),
           "full SHA-256 Git object ID was rejected");
}

void test_dirty_stamp_is_rejected() {
    auto embedded = clean_stamp();
    embedded.source_state = "dirty";
    const auto result = detail::decode_renderer_source_stamp(embedded);
    const auto *error = std::get_if<RendererSourceStampError>(&result);
    expect(error != nullptr, "dirty source produced an admissible stamp");
    expect(error->code == RendererSourceStampErrorCode::dirty_source &&
               error->source_state == RendererSourceState::dirty,
           "dirty source returned the wrong typed error");
}

void test_unavailable_stamp_is_rejected() {
    detail::EmbeddedRendererSourceStamp embedded{
        "unavailable", "", "", "available", "GNU", "13.3.0",
        "x86_64-linux-gnu"};
    const auto result = detail::decode_renderer_source_stamp(embedded);
    const auto *error = std::get_if<RendererSourceStampError>(&result);
    expect(error != nullptr, "unavailable source produced an admissible stamp");
    expect(error->code == RendererSourceStampErrorCode::unavailable_source &&
               error->source_state == RendererSourceState::unavailable,
           "unavailable source returned the wrong typed error");
}

void test_clean_closure_does_not_require_native_toolchain() {
    auto embedded = clean_stamp();
    embedded.toolchain_state = "unavailable";
    embedded.compiler_id = "Clang";
    embedded.compiler_version = "24.0.0";
    embedded.target_triple = "";

    const auto closure_result = detail::decode_renderer_source_closure(embedded);
    const auto *closure = std::get_if<RendererSourceClosure>(&closure_result);
    expect(closure != nullptr && !closure->source_closure_sha256.is_zero(),
           "clean source closure depended on native toolchain identity");

    const auto stamp_result = detail::decode_renderer_source_stamp(embedded);
    const auto *error = std::get_if<RendererSourceStampError>(&stamp_result);
    expect(error != nullptr &&
               error->code == RendererSourceStampErrorCode::unavailable_toolchain,
           "full renderer stamp admitted an unavailable toolchain");
}

void test_malformed_clean_stamp_is_rejected() {
    auto embedded = clean_stamp();
    embedded.source_closure_sha256 = "not-a-sha256";
    const auto result = detail::decode_renderer_source_stamp(embedded);
    const auto *error = std::get_if<RendererSourceStampError>(&result);
    expect(error != nullptr, "malformed clean source produced an admissible stamp");
    expect(error->code == RendererSourceStampErrorCode::malformed_embedded_stamp,
           "malformed clean source returned the wrong typed error");

    embedded = clean_stamp();
    embedded.source_closure_sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const auto zero_result = detail::decode_renderer_source_stamp(embedded);
    const auto *zero_error = std::get_if<RendererSourceStampError>(&zero_result);
    expect(zero_error != nullptr,
           "zero source-closure digest produced an admissible stamp");
    expect(zero_error->code == RendererSourceStampErrorCode::malformed_embedded_stamp,
           "zero source-closure digest returned the wrong typed error");

    embedded = clean_stamp();
    embedded.full_git_head = "0000000000000000000000000000000000000000";
    const auto zero_head_result = detail::decode_renderer_source_stamp(embedded);
    const auto *zero_head_error =
        std::get_if<RendererSourceStampError>(&zero_head_result);
    expect(zero_head_error != nullptr,
           "null Git object ID produced an admissible stamp");
    expect(zero_head_error->code ==
               RendererSourceStampErrorCode::malformed_embedded_stamp,
           "null Git object ID returned the wrong typed error");
}

void test_embedded_build_stamp_fails_closed_or_is_complete() {
    const auto closure_result = renderer_source_closure();
    if (const auto *closure = std::get_if<RendererSourceClosure>(&closure_result)) {
        expect(closure->source_state == RendererSourceState::clean &&
                   !closure->full_git_head.empty() &&
                   !closure->source_closure_sha256.is_zero(),
               "embedded source closure was incomplete");
    }

    const auto result = renderer_source_stamp();
    if (const auto *stamp = std::get_if<RendererSourceStamp>(&result)) {
        expect(stamp->source_state == RendererSourceState::clean,
               "embedded admissible stamp was not clean");
        expect(!stamp->full_git_head.empty() &&
                   !stamp->source_closure_sha256.is_zero() &&
                   !stamp->compiler_id.empty() && !stamp->compiler_version.empty() &&
                   !stamp->target_triple.empty(),
               "embedded admissible stamp was incomplete");
        return;
    }

    const auto &error = std::get<RendererSourceStampError>(result);
    expect(error.code == RendererSourceStampErrorCode::dirty_source ||
               error.code == RendererSourceStampErrorCode::unavailable_source ||
               error.code == RendererSourceStampErrorCode::unavailable_toolchain,
           "embedded build stamp failed for a non-source-state reason");
}

} // namespace

int main() {
    test_clean_stamp_is_admitted();
    test_dirty_stamp_is_rejected();
    test_unavailable_stamp_is_rejected();
    test_clean_closure_does_not_require_native_toolchain();
    test_malformed_clean_stamp_is_rejected();
    test_embedded_build_stamp_fails_closed_or_is_complete();
}
