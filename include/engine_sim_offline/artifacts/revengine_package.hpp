#pragma once

#include "engine_sim_offline/artifacts/revengine_container.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kRevenginePackageDescriptorPath = "revengine.json";
inline constexpr std::string_view kRevenginePackageSchema =
    "engine-sim-offline/revengine-package";
inline constexpr std::uint32_t kRevenginePackageSchemaVersion = 1;
inline constexpr std::string_view kRevengineResponsiveAudioRuntimeKind =
    "responsive-audio";
inline constexpr std::size_t kRevengineMaximumDescriptorByteCount = 16U * 1024U;

struct RevengineRuntimeDescriptor {
    std::string kind;
    std::string manifest_path;
    contract::Sha256Digest manifest_sha256;

    friend bool operator==(const RevengineRuntimeDescriptor &,
                           const RevengineRuntimeDescriptor &) = default;
};

struct RevenginePackageDescriptor {
    std::string schema;
    std::uint32_t version = 0;
    std::string engine_id;
    RevengineRuntimeDescriptor runtime;

    friend bool operator==(const RevenginePackageDescriptor &,
                           const RevenginePackageDescriptor &) = default;
};

enum class RevenginePackageErrorCode : std::uint8_t {
    malformed_json,
    invalid_shape,
    unknown_field,
    missing_field,
    invalid_value,
    missing_descriptor,
    missing_runtime_manifest,
    runtime_manifest_hash_mismatch,
};

struct RevenginePackageError {
    RevenginePackageErrorCode code = RevenginePackageErrorCode::invalid_value;
    std::string path;
    std::string message;

    friend bool operator==(const RevenginePackageError &,
                           const RevenginePackageError &) = default;
};

using RevenginePackageParseResult =
    std::variant<RevenginePackageDescriptor, RevenginePackageError>;
using RevenginePackageValidationResult =
    std::variant<RevenginePackageDescriptor, RevenginePackageError>;

// Strictly parses the ESO-owned package entry point. Duplicate JSON keys are
// rejected by the bounded parser, and unknown object members fail closed here.
[[nodiscard]] RevenginePackageParseResult
parse_revengine_package_descriptor(std::string_view json);

// Parses the exact revengine.json entry in the supplied package tree, requires its
// declared runtime manifest, verifies the manifest digest over its exact bytes, and
// returns the descriptor derived from those authenticated package bytes. A caller
// cannot substitute an externally parsed descriptor for the tree's entry point.
[[nodiscard]] RevenginePackageValidationResult
validate_revengine_package_tree(std::span<const RevenginePackEntry> entries);

[[nodiscard]] bool is_revengine_engine_id(std::string_view value) noexcept;

} // namespace engine_sim_offline::artifacts
