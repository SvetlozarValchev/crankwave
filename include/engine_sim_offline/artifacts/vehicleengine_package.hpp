#pragma once

#include "engine_sim_offline/artifacts/vehicleengine_container.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kVehicleEnginePackageDescriptorPath = "vehicleengine.json";
inline constexpr std::string_view kVehicleEnginePackageSchema =
    "engine-sim-offline/vehicleengine-package";
inline constexpr std::uint32_t kVehicleEnginePackageSchemaVersion = 1;
inline constexpr std::string_view kVehicleEngineResponsiveAudioRuntimeKind =
    "responsive-audio";
inline constexpr std::size_t kVehicleEngineMaximumDescriptorByteCount = 16U * 1024U;

struct VehicleEngineRuntimeDescriptor {
    std::string kind;
    std::string manifest_path;
    contract::Sha256Digest manifest_sha256;

    friend bool operator==(const VehicleEngineRuntimeDescriptor &,
                           const VehicleEngineRuntimeDescriptor &) = default;
};

struct VehicleEnginePackageDescriptor {
    std::string schema;
    std::uint32_t version = 0;
    std::string engine_id;
    VehicleEngineRuntimeDescriptor runtime;

    friend bool operator==(const VehicleEnginePackageDescriptor &,
                           const VehicleEnginePackageDescriptor &) = default;
};

enum class VehicleEnginePackageErrorCode : std::uint8_t {
    malformed_json,
    invalid_shape,
    unknown_field,
    missing_field,
    invalid_value,
    missing_descriptor,
    missing_runtime_manifest,
    runtime_manifest_hash_mismatch,
};

struct VehicleEnginePackageError {
    VehicleEnginePackageErrorCode code = VehicleEnginePackageErrorCode::invalid_value;
    std::string path;
    std::string message;

    friend bool operator==(const VehicleEnginePackageError &,
                           const VehicleEnginePackageError &) = default;
};

using VehicleEnginePackageParseResult =
    std::variant<VehicleEnginePackageDescriptor, VehicleEnginePackageError>;
using VehicleEnginePackageValidationResult =
    std::variant<VehicleEnginePackageDescriptor, VehicleEnginePackageError>;

// Strictly parses the ESO-owned package entry point. Duplicate JSON keys are
// rejected by the bounded parser, and unknown object members fail closed here.
[[nodiscard]] VehicleEnginePackageParseResult
parse_vehicleengine_package_descriptor(std::string_view json);

// Parses the exact vehicleengine.json entry in the supplied package tree, requires its
// declared runtime manifest, verifies the manifest digest over its exact bytes, and
// returns the descriptor derived from those authenticated package bytes. A caller
// cannot substitute an externally parsed descriptor for the tree's entry point.
[[nodiscard]] VehicleEnginePackageValidationResult
validate_vehicleengine_package_tree(std::span<const VehicleEnginePackEntry> entries);

[[nodiscard]] bool is_vehicleengine_engine_id(std::string_view value) noexcept;

} // namespace engine_sim_offline::artifacts
