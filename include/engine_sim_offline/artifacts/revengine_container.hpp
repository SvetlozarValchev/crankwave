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

// REVENGINE v1 is a deterministic, uncompressed carrier for an already validated
// responsive package tree. It deliberately does not interpret package JSON or grant
// product, licence, signing, or runtime compatibility authority.
inline constexpr std::uint16_t kRevengineContainerVersionV1 = 1;
inline constexpr std::uint64_t kRevengineHeaderByteCountV1 = 128;
inline constexpr std::uint32_t kRevengineIndexEntryPrefixByteCountV1 = 56;
inline constexpr std::uint32_t kRevengineMaximumEntryCountV1 = 8'192;
inline constexpr std::uint32_t kRevengineMaximumPathByteCountV1 = 512;
inline constexpr std::uint32_t kRevengineMaximumPathSegmentByteCountV1 = 127;
inline constexpr std::uint64_t kRevengineMaximumEntryByteCountV1 = UINT64_C(1) << 30U;
inline constexpr std::uint64_t kRevengineMaximumContainerByteCountV1 = UINT64_C(1)
                                                                       << 32U;

struct RevenginePackEntry {
    std::string path;
    std::span<const std::byte> payload;
};

struct RevengineIndexedEntry {
    std::string path;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const RevengineIndexedEntry &,
                           const RevengineIndexedEntry &) = default;
};

struct RevengineContainerIndex {
    std::uint16_t version = 0;
    std::uint64_t container_byte_count = 0;
    std::uint64_t index_byte_count = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest index_sha256;
    contract::Sha256Digest payload_sha256;
    std::vector<RevengineIndexedEntry> entries;

    friend bool operator==(const RevengineContainerIndex &,
                           const RevengineContainerIndex &) = default;
};

enum class RevengineContainerErrorCode : std::uint8_t {
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

struct RevengineContainerError {
    RevengineContainerErrorCode code = RevengineContainerErrorCode::invalid_argument;
    std::string path;
    std::string message;

    friend bool operator==(const RevengineContainerError &,
                           const RevengineContainerError &) = default;
};

using RevenginePackResult =
    std::variant<std::vector<std::byte>, RevengineContainerError>;
using RevengineInspectResult =
    std::variant<RevengineContainerIndex, RevengineContainerError>;

// Entries may be supplied in any order. The output index and payload are always
// ordered by portable path bytes, so the same path/byte tree produces identical
// container bytes. At least one entry is required.
[[nodiscard]] RevenginePackResult
pack_revengine_v1(std::span<const RevenginePackEntry> entries);

// Parses the complete carrier, validates all bounds and canonical layout, and
// authenticates the index. It intentionally does not hash payload bytes.
[[nodiscard]] RevengineInspectResult
inspect_revengine(std::span<const std::byte> container);

// Performs inspect_revengine plus aggregate and per-entry payload verification.
[[nodiscard]] RevengineInspectResult
verify_revengine(std::span<const std::byte> container);

[[nodiscard]] bool is_portable_revengine_path(std::string_view path) noexcept;

// Returns an entry's exact bytes only when its already-inspected bounds still fit
// the supplied complete container.
[[nodiscard]] std::span<const std::byte>
revengine_entry_payload(std::span<const std::byte> container,
                        const RevengineIndexedEntry &entry) noexcept;

} // namespace engine_sim_offline::artifacts
