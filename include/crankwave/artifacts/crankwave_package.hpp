#pragma once

#include "crankwave/artifacts/crankwave_container.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace crankwave::artifacts {

inline constexpr std::string_view kCrankwavePackageDescriptorPath = "crankwave.json";
inline constexpr std::string_view kCrankwavePackageSchema =
    "crankwave/crankwave-package";
inline constexpr std::uint32_t kCrankwavePackageSchemaVersion = 1;
inline constexpr std::string_view kCrankwaveResponsiveAudioRuntimeKind =
    "responsive-audio";
inline constexpr std::size_t kCrankwaveMaximumDescriptorByteCount = 16U * 1024U;

struct CrankwaveRuntimeDescriptor {
    std::string kind;
    std::string manifest_path;
    contract::Sha256Digest manifest_sha256;

    friend bool operator==(const CrankwaveRuntimeDescriptor &,
                           const CrankwaveRuntimeDescriptor &) = default;
};

struct CrankwavePackageDescriptor {
    std::string schema;
    std::uint32_t version = 0;
    std::string engine_id;
    CrankwaveRuntimeDescriptor runtime;

    friend bool operator==(const CrankwavePackageDescriptor &,
                           const CrankwavePackageDescriptor &) = default;
};

enum class CrankwavePackageErrorCode : std::uint8_t {
    malformed_json,
    invalid_shape,
    unknown_field,
    missing_field,
    invalid_value,
    missing_descriptor,
    missing_runtime_manifest,
    runtime_manifest_hash_mismatch,
};

struct CrankwavePackageError {
    CrankwavePackageErrorCode code = CrankwavePackageErrorCode::invalid_value;
    std::string path;
    std::string message;

    friend bool operator==(const CrankwavePackageError &,
                           const CrankwavePackageError &) = default;
};

using CrankwavePackageParseResult =
    std::variant<CrankwavePackageDescriptor, CrankwavePackageError>;
using CrankwavePackageValidationResult =
    std::variant<CrankwavePackageDescriptor, CrankwavePackageError>;

// Strictly parses the Crankwave-owned package entry point. Duplicate JSON keys are
// rejected by the bounded parser, and unknown object members fail closed here.
[[nodiscard]] CrankwavePackageParseResult
parse_crankwave_package_descriptor(std::string_view json);

// Parses the exact crankwave.json entry in the supplied package tree, requires its
// declared runtime manifest, verifies the manifest digest over its exact bytes, and
// returns the descriptor derived from those authenticated package bytes. A caller
// cannot substitute an externally parsed descriptor for the tree's entry point.
[[nodiscard]] CrankwavePackageValidationResult
validate_crankwave_package_tree(std::span<const CrankwavePackEntry> entries);

[[nodiscard]] bool is_crankwave_engine_id(std::string_view value) noexcept;

} // namespace crankwave::artifacts
