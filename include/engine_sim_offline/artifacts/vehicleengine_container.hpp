#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::artifacts {

// VEHICLEENGINE v1 is a deterministic, uncompressed carrier for an already validated
// responsive package tree. It deliberately does not interpret package JSON or grant
// product, licence, signing, or runtime compatibility authority.
inline constexpr std::uint16_t kVehicleEngineContainerVersionV1 = 1;
inline constexpr std::uint64_t kVehicleEngineHeaderByteCountV1 = 128;
inline constexpr std::uint32_t kVehicleEngineIndexEntryPrefixByteCountV1 = 56;
inline constexpr std::uint32_t kVehicleEngineMaximumEntryCountV1 = 8'192;
inline constexpr std::uint32_t kVehicleEngineMaximumPathByteCountV1 = 512;
inline constexpr std::uint32_t kVehicleEngineMaximumPathSegmentByteCountV1 = 127;
inline constexpr std::uint64_t kVehicleEngineMaximumEntryByteCountV1 = UINT64_C(1) << 30U;
inline constexpr std::uint64_t kVehicleEngineMaximumContainerByteCountV1 = UINT64_C(1)
                                                                       << 32U;

struct VehicleEnginePackEntry {
    std::string path;
    std::span<const std::byte> payload;
};

struct VehicleEngineIndexedEntry {
    std::string path;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const VehicleEngineIndexedEntry &,
                           const VehicleEngineIndexedEntry &) = default;
};

struct VehicleEngineContainerIndex {
    std::uint16_t version = 0;
    std::uint64_t container_byte_count = 0;
    std::uint64_t index_byte_count = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest index_sha256;
    contract::Sha256Digest payload_sha256;
    std::vector<VehicleEngineIndexedEntry> entries;

    friend bool operator==(const VehicleEngineContainerIndex &,
                           const VehicleEngineContainerIndex &) = default;
};

enum class VehicleEngineContainerErrorCode : std::uint8_t {
    invalid_argument,
    invalid_path,
    duplicate_path,
    resource_limit,
    malformed_header,
    unsupported_version,
    malformed_index,
    noncanonical_index,
    index_hash_mismatch,
    payload_hash_mismatch,
    entry_hash_mismatch,
};

struct VehicleEngineContainerError {
    VehicleEngineContainerErrorCode code = VehicleEngineContainerErrorCode::invalid_argument;
    std::string path;
    std::string message;

    friend bool operator==(const VehicleEngineContainerError &,
                           const VehicleEngineContainerError &) = default;
};

using VehicleEnginePackResult =
    std::variant<std::vector<std::byte>, VehicleEngineContainerError>;
using VehicleEngineInspectResult =
    std::variant<VehicleEngineContainerIndex, VehicleEngineContainerError>;

// Entries may be supplied in any order. The output index and payload are always
// ordered by portable path bytes, so the same path/byte tree produces identical
// container bytes. At least one entry is required.
[[nodiscard]] VehicleEnginePackResult
pack_vehicleengine_v1(std::span<const VehicleEnginePackEntry> entries);

// Parses the complete carrier, validates all bounds and canonical layout, and
// authenticates the index. It intentionally does not hash payload bytes.
[[nodiscard]] VehicleEngineInspectResult
inspect_vehicleengine(std::span<const std::byte> container);

// Performs inspect_vehicleengine plus aggregate and per-entry payload verification.
[[nodiscard]] VehicleEngineInspectResult
verify_vehicleengine(std::span<const std::byte> container);

[[nodiscard]] bool is_portable_vehicleengine_path(std::string_view path) noexcept;

// Returns an entry's exact bytes only when its already-inspected bounds still fit
// the supplied complete container.
[[nodiscard]] std::span<const std::byte>
vehicleengine_entry_payload(std::span<const std::byte> container,
                        const VehicleEngineIndexedEntry &entry) noexcept;

} // namespace engine_sim_offline::artifacts
