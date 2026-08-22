#pragma once

#include "crankwave/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::artifacts {

// CRANKWAVE v1 is a deterministic, uncompressed carrier for an already validated
// responsive package tree. It deliberately does not interpret package JSON or grant
// product, licence, signing, or runtime compatibility authority.
inline constexpr std::uint16_t kCrankwaveContainerVersionV1 = 1;
inline constexpr std::uint64_t kCrankwaveHeaderByteCountV1 = 128;
inline constexpr std::uint32_t kCrankwaveIndexEntryPrefixByteCountV1 = 56;
inline constexpr std::uint32_t kCrankwaveMaximumEntryCountV1 = 8'192;
inline constexpr std::uint32_t kCrankwaveMaximumPathByteCountV1 = 512;
inline constexpr std::uint32_t kCrankwaveMaximumPathSegmentByteCountV1 = 127;
inline constexpr std::uint64_t kCrankwaveMaximumEntryByteCountV1 = UINT64_C(1) << 30U;
inline constexpr std::uint64_t kCrankwaveMaximumContainerByteCountV1 = UINT64_C(1)
                                                                       << 32U;

struct CrankwavePackEntry {
    std::string path;
    std::span<const std::byte> payload;
};

struct CrankwaveIndexedEntry {
    std::string path;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const CrankwaveIndexedEntry &,
                           const CrankwaveIndexedEntry &) = default;
};

struct CrankwaveContainerIndex {
    std::uint16_t version = 0;
    std::uint64_t container_byte_count = 0;
    std::uint64_t index_byte_count = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_byte_count = 0;
    contract::Sha256Digest index_sha256;
    contract::Sha256Digest payload_sha256;
    std::vector<CrankwaveIndexedEntry> entries;

    friend bool operator==(const CrankwaveContainerIndex &,
                           const CrankwaveContainerIndex &) = default;
};

enum class CrankwaveContainerErrorCode : std::uint8_t {
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

struct CrankwaveContainerError {
    CrankwaveContainerErrorCode code = CrankwaveContainerErrorCode::invalid_argument;
    std::string path;
    std::string message;

    friend bool operator==(const CrankwaveContainerError &,
                           const CrankwaveContainerError &) = default;
};

using CrankwavePackResult =
    std::variant<std::vector<std::byte>, CrankwaveContainerError>;
using CrankwaveInspectResult =
    std::variant<CrankwaveContainerIndex, CrankwaveContainerError>;

// Entries may be supplied in any order. The output index and payload are always
// ordered by portable path bytes, so the same path/byte tree produces identical
// container bytes. At least one entry is required.
[[nodiscard]] CrankwavePackResult
pack_crankwave_v1(std::span<const CrankwavePackEntry> entries);

// Parses the complete carrier, validates all bounds and canonical layout, and
// authenticates the index. It intentionally does not hash payload bytes.
[[nodiscard]] CrankwaveInspectResult
inspect_crankwave(std::span<const std::byte> container);

// Performs inspect_crankwave plus aggregate and per-entry payload verification.
[[nodiscard]] CrankwaveInspectResult
verify_crankwave(std::span<const std::byte> container);

[[nodiscard]] bool is_portable_crankwave_path(std::string_view path) noexcept;

// Returns an entry's exact bytes only when its already-inspected bounds still fit
// the supplied complete container.
[[nodiscard]] std::span<const std::byte>
crankwave_entry_payload(std::span<const std::byte> container,
                        const CrankwaveIndexedEntry &entry) noexcept;

} // namespace crankwave::artifacts
