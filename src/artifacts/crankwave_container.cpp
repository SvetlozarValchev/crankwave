#include "crankwave/artifacts/crankwave_container.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>

namespace crankwave::artifacts {
namespace {

constexpr std::array<std::byte, 8> kMagic{
    std::byte{'C'}, std::byte{'R'}, std::byte{'K'}, std::byte{'W'},
    std::byte{'A'}, std::byte{'V'}, std::byte{'E'}, std::byte{'1'},
};

constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kHeaderSizeOffset = 10;
constexpr std::size_t kFlagsOffset = 12;
constexpr std::size_t kEntryCountOffset = 16;
constexpr std::size_t kEntryPrefixSizeOffset = 20;
constexpr std::size_t kIndexOffsetOffset = 24;
constexpr std::size_t kIndexSizeOffset = 32;
constexpr std::size_t kPayloadOffsetOffset = 40;
constexpr std::size_t kPayloadSizeOffset = 48;
constexpr std::size_t kContainerSizeOffset = 56;
constexpr std::size_t kIndexDigestOffset = 64;
constexpr std::size_t kPayloadDigestOffset = 96;

[[nodiscard]] CrankwaveContainerError
error(CrankwaveContainerErrorCode code, std::string message, std::string path = {}) {
    return {code, std::move(path), std::move(message)};
}

[[nodiscard]] bool checked_add(const std::uint64_t left, const std::uint64_t right,
                               std::uint64_t &result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

template <class Integer>
void write_le(std::span<std::byte> bytes, const std::size_t offset,
              const Integer value) noexcept {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(value >> static_cast<unsigned>(index * 8U));
    }
}

template <class Integer>
[[nodiscard]] Integer read_le(std::span<const std::byte> bytes,
                              const std::size_t offset) noexcept {
    Integer value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |=
            static_cast<Integer>(std::to_integer<std::uint8_t>(bytes[offset + index]))
            << static_cast<unsigned>(index * 8U);
    }
    return value;
}

void write_digest(std::span<std::byte> bytes, const std::size_t offset,
                  const contract::Sha256Digest &digest) noexcept {
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        bytes[offset + index] = static_cast<std::byte>(digest.bytes[index]);
    }
}

[[nodiscard]] contract::Sha256Digest read_digest(std::span<const std::byte> bytes,
                                                 const std::size_t offset) noexcept {
    contract::Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        digest.bytes[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return digest;
}

[[nodiscard]] bool is_lower_ascii_alnum(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
}

[[nodiscard]] bool
is_reserved_portable_segment(const std::string_view segment) noexcept {
    const auto dot = segment.find('.');
    const auto stem = segment.substr(0, dot);
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return true;
    }
    return false;
}

[[nodiscard]] std::span<const std::byte>
bounded_span(const std::span<const std::byte> bytes, const std::uint64_t offset,
             const std::uint64_t size) noexcept {
    std::uint64_t end = 0;
    if (!checked_add(offset, size, end) || end > bytes.size() ||
        offset > std::numeric_limits<std::size_t>::max() ||
        size > std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    return bytes.subspan(static_cast<std::size_t>(offset),
                         static_cast<std::size_t>(size));
}

struct OrderedPackEntry {
    std::string_view path;
    std::span<const std::byte> payload;
    contract::Sha256Digest payload_sha256;
};

} // namespace

bool is_portable_crankwave_path(const std::string_view path) noexcept {
    if (path.empty() || path.size() > kCrankwaveMaximumPathByteCountV1 ||
        path.front() == '/' || path.back() == '/') {
        return false;
    }

    std::size_t segment_start = 0;
    while (segment_start < path.size()) {
        const auto slash = path.find('/', segment_start);
        const auto segment_end = slash == std::string_view::npos ? path.size() : slash;
        const auto segment = path.substr(segment_start, segment_end - segment_start);
        if (segment.empty() ||
            segment.size() > kCrankwaveMaximumPathSegmentByteCountV1 ||
            !is_lower_ascii_alnum(segment.front()) ||
            !is_lower_ascii_alnum(segment.back()) ||
            is_reserved_portable_segment(segment)) {
            return false;
        }
        for (const char value : segment) {
            if (!is_lower_ascii_alnum(value) && value != '.' && value != '_' &&
                value != '-') {
                return false;
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        segment_start = slash + 1U;
    }
    return true;
}

CrankwavePackResult
pack_crankwave_v1(const std::span<const CrankwavePackEntry> entries) {
    if (entries.empty()) {
        return error(CrankwaveContainerErrorCode::invalid_argument,
                     "a CRANKWAVE container requires at least one entry");
    }
    if (entries.size() > kCrankwaveMaximumEntryCountV1) {
        return error(CrankwaveContainerErrorCode::resource_limit,
                     "CRANKWAVE entry count exceeds the v1 limit");
    }

    std::vector<OrderedPackEntry> ordered;
    ordered.reserve(entries.size());
    for (const auto &entry : entries) {
        if (!is_portable_crankwave_path(entry.path)) {
            return error(CrankwaveContainerErrorCode::invalid_path,
                         "entry path is not a canonical portable relative path",
                         entry.path);
        }
        if (entry.payload.size() > kCrankwaveMaximumEntryByteCountV1) {
            return error(CrankwaveContainerErrorCode::resource_limit,
                         "entry payload exceeds the v1 byte limit", entry.path);
        }
        ordered.push_back({entry.path, entry.payload, {}});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
        return left.path < right.path;
    });
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        if (ordered[index - 1].path == ordered[index].path) {
            return error(CrankwaveContainerErrorCode::duplicate_path,
                         "duplicate entry path", std::string{ordered[index].path});
        }
    }

    std::uint64_t index_byte_count = 0;
    std::uint64_t payload_byte_count = 0;
    for (const auto &entry : ordered) {
        std::uint64_t next_index_size = 0;
        if (!checked_add(index_byte_count,
                         kCrankwaveIndexEntryPrefixByteCountV1 + entry.path.size(),
                         next_index_size) ||
            !checked_add(payload_byte_count, entry.payload.size(),
                         payload_byte_count)) {
            return error(CrankwaveContainerErrorCode::resource_limit,
                         "CRANKWAVE container size overflow");
        }
        index_byte_count = next_index_size;
    }

    std::uint64_t payload_offset = 0;
    std::uint64_t container_byte_count = 0;
    if (!checked_add(kCrankwaveHeaderByteCountV1, index_byte_count, payload_offset) ||
        !checked_add(payload_offset, payload_byte_count, container_byte_count) ||
        container_byte_count > kCrankwaveMaximumContainerByteCountV1 ||
        container_byte_count > std::numeric_limits<std::size_t>::max()) {
        return error(CrankwaveContainerErrorCode::resource_limit,
                     "CRANKWAVE container exceeds the v1 byte limit");
    }

    // Payload bytes are deliberately untouched until every path, duplicate, entry,
    // aggregate, and container bound has been admitted. Invalid trees therefore
    // cannot amplify a guaranteed structural rejection into unbounded hashing work.
    for (auto &entry : ordered) {
        entry.payload_sha256 = contract::sha256(entry.payload);
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(container_byte_count));
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    const auto mutable_bytes = std::span<std::byte>{bytes};
    write_le<std::uint16_t>(mutable_bytes, kVersionOffset,
                            kCrankwaveContainerVersionV1);
    write_le<std::uint16_t>(mutable_bytes, kHeaderSizeOffset,
                            static_cast<std::uint16_t>(kCrankwaveHeaderByteCountV1));
    write_le<std::uint32_t>(mutable_bytes, kFlagsOffset, 0);
    write_le<std::uint32_t>(mutable_bytes, kEntryCountOffset,
                            static_cast<std::uint32_t>(ordered.size()));
    write_le<std::uint32_t>(mutable_bytes, kEntryPrefixSizeOffset,
                            kCrankwaveIndexEntryPrefixByteCountV1);
    write_le<std::uint64_t>(mutable_bytes, kIndexOffsetOffset,
                            kCrankwaveHeaderByteCountV1);
    write_le<std::uint64_t>(mutable_bytes, kIndexSizeOffset, index_byte_count);
    write_le<std::uint64_t>(mutable_bytes, kPayloadOffsetOffset, payload_offset);
    write_le<std::uint64_t>(mutable_bytes, kPayloadSizeOffset, payload_byte_count);
    write_le<std::uint64_t>(mutable_bytes, kContainerSizeOffset, container_byte_count);

    std::uint64_t index_cursor = kCrankwaveHeaderByteCountV1;
    std::uint64_t payload_cursor = payload_offset;
    for (const auto &entry : ordered) {
        const auto index = static_cast<std::size_t>(index_cursor);
        write_le<std::uint16_t>(mutable_bytes, index,
                                static_cast<std::uint16_t>(entry.path.size()));
        write_le<std::uint16_t>(mutable_bytes, index + 2U, 0);
        write_le<std::uint32_t>(mutable_bytes, index + 4U, 0);
        write_le<std::uint64_t>(mutable_bytes, index + 8U, payload_cursor);
        write_le<std::uint64_t>(mutable_bytes, index + 16U, entry.payload.size());
        write_digest(mutable_bytes, index + 24U, entry.payload_sha256);
        std::memcpy(bytes.data() + index + kCrankwaveIndexEntryPrefixByteCountV1,
                    entry.path.data(), entry.path.size());
        if (!entry.payload.empty()) {
            std::memcpy(bytes.data() + static_cast<std::size_t>(payload_cursor),
                        entry.payload.data(), entry.payload.size());
        }
        index_cursor += kCrankwaveIndexEntryPrefixByteCountV1 + entry.path.size();
        payload_cursor += entry.payload.size();
    }

    const auto immutable_bytes = std::span<const std::byte>{bytes};
    const auto index_digest = contract::sha256(
        immutable_bytes.subspan(static_cast<std::size_t>(kCrankwaveHeaderByteCountV1),
                                static_cast<std::size_t>(index_byte_count)));
    const auto payload_digest = contract::sha256(
        immutable_bytes.subspan(static_cast<std::size_t>(payload_offset),
                                static_cast<std::size_t>(payload_byte_count)));
    write_digest(mutable_bytes, kIndexDigestOffset, index_digest);
    write_digest(mutable_bytes, kPayloadDigestOffset, payload_digest);
    return bytes;
}

CrankwaveInspectResult inspect_crankwave(const std::span<const std::byte> container) {
    if (container.size() < kCrankwaveHeaderByteCountV1) {
        return error(CrankwaveContainerErrorCode::malformed_header,
                     "CRANKWAVE header is truncated");
    }
    if (container.size() > kCrankwaveMaximumContainerByteCountV1) {
        return error(CrankwaveContainerErrorCode::resource_limit,
                     "CRANKWAVE container exceeds the v1 byte limit");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), container.begin())) {
        return error(CrankwaveContainerErrorCode::malformed_header,
                     "CRANKWAVE magic is invalid");
    }

    const auto version = read_le<std::uint16_t>(container, kVersionOffset);
    if (version != kCrankwaveContainerVersionV1) {
        return error(CrankwaveContainerErrorCode::unsupported_version,
                     "CRANKWAVE container version is unsupported");
    }
    const auto header_size = read_le<std::uint16_t>(container, kHeaderSizeOffset);
    const auto flags = read_le<std::uint32_t>(container, kFlagsOffset);
    const auto entry_count = read_le<std::uint32_t>(container, kEntryCountOffset);
    const auto entry_prefix_size =
        read_le<std::uint32_t>(container, kEntryPrefixSizeOffset);
    const auto index_offset = read_le<std::uint64_t>(container, kIndexOffsetOffset);
    const auto index_size = read_le<std::uint64_t>(container, kIndexSizeOffset);
    const auto payload_offset = read_le<std::uint64_t>(container, kPayloadOffsetOffset);
    const auto payload_size = read_le<std::uint64_t>(container, kPayloadSizeOffset);
    const auto container_size = read_le<std::uint64_t>(container, kContainerSizeOffset);
    const auto expected_index_digest = read_digest(container, kIndexDigestOffset);
    const auto expected_payload_digest = read_digest(container, kPayloadDigestOffset);

    if (header_size != kCrankwaveHeaderByteCountV1 || flags != 0 ||
        entry_prefix_size != kCrankwaveIndexEntryPrefixByteCountV1 ||
        index_offset != kCrankwaveHeaderByteCountV1) {
        return error(CrankwaveContainerErrorCode::malformed_header,
                     "CRANKWAVE v1 fixed header fields are noncanonical");
    }
    if (entry_count == 0 || entry_count > kCrankwaveMaximumEntryCountV1) {
        return error(CrankwaveContainerErrorCode::resource_limit,
                     "CRANKWAVE entry count is outside the v1 bounds");
    }
    std::uint64_t expected_payload_offset = 0;
    std::uint64_t expected_container_size = 0;
    if (!checked_add(index_offset, index_size, expected_payload_offset) ||
        !checked_add(payload_offset, payload_size, expected_container_size) ||
        payload_offset != expected_payload_offset ||
        container_size != expected_container_size ||
        container_size != container.size()) {
        return error(CrankwaveContainerErrorCode::malformed_header,
                     "CRANKWAVE declared ranges do not cover the exact container");
    }
    const auto minimum_index_size = static_cast<std::uint64_t>(entry_count) *
                                    (kCrankwaveIndexEntryPrefixByteCountV1 + 1U);
    if (index_size < minimum_index_size ||
        index_size > static_cast<std::uint64_t>(entry_count) *
                         (kCrankwaveIndexEntryPrefixByteCountV1 +
                          kCrankwaveMaximumPathByteCountV1)) {
        return error(CrankwaveContainerErrorCode::resource_limit,
                     "CRANKWAVE index size is inconsistent with its entry count");
    }

    const auto index_bytes = bounded_span(container, index_offset, index_size);
    if (index_bytes.size() != index_size ||
        contract::sha256(index_bytes) != expected_index_digest) {
        return error(CrankwaveContainerErrorCode::index_hash_mismatch,
                     "CRANKWAVE index SHA-256 does not match its header");
    }

    CrankwaveContainerIndex result;
    result.version = version;
    result.container_byte_count = container_size;
    result.index_byte_count = index_size;
    result.payload_offset = payload_offset;
    result.payload_byte_count = payload_size;
    result.index_sha256 = expected_index_digest;
    result.payload_sha256 = expected_payload_digest;
    result.entries.reserve(entry_count);

    std::uint64_t cursor = index_offset;
    std::uint64_t expected_entry_offset = payload_offset;
    std::string previous_path;
    for (std::uint32_t ordinal = 0; ordinal < entry_count; ++ordinal) {
        std::uint64_t prefix_end = 0;
        if (!checked_add(cursor, kCrankwaveIndexEntryPrefixByteCountV1, prefix_end) ||
            prefix_end > payload_offset) {
            return error(CrankwaveContainerErrorCode::malformed_index,
                         "CRANKWAVE index entry prefix is truncated");
        }
        const auto prefix = static_cast<std::size_t>(cursor);
        const auto path_size = read_le<std::uint16_t>(container, prefix);
        const auto entry_flags = read_le<std::uint16_t>(container, prefix + 2U);
        const auto reserved = read_le<std::uint32_t>(container, prefix + 4U);
        const auto entry_offset = read_le<std::uint64_t>(container, prefix + 8U);
        const auto entry_size = read_le<std::uint64_t>(container, prefix + 16U);
        const auto entry_digest = read_digest(container, prefix + 24U);
        if (path_size == 0 || path_size > kCrankwaveMaximumPathByteCountV1 ||
            entry_flags != 0 || reserved != 0) {
            return error(CrankwaveContainerErrorCode::malformed_index,
                         "CRANKWAVE index entry fields are invalid");
        }
        std::uint64_t entry_end = 0;
        std::uint64_t path_end = 0;
        if (!checked_add(prefix_end, path_size, path_end) ||
            path_end > payload_offset ||
            !checked_add(entry_offset, entry_size, entry_end) ||
            entry_offset != expected_entry_offset || entry_end > container_size) {
            return error(CrankwaveContainerErrorCode::malformed_index,
                         "CRANKWAVE index entry ranges are not contiguous");
        }
        if (entry_size > kCrankwaveMaximumEntryByteCountV1) {
            return error(CrankwaveContainerErrorCode::resource_limit,
                         "CRANKWAVE entry payload exceeds the v1 byte limit");
        }

        std::string path;
        path.reserve(path_size);
        for (std::size_t index = 0; index < path_size; ++index) {
            path.push_back(static_cast<char>(std::to_integer<unsigned char>(
                container[static_cast<std::size_t>(prefix_end) + index])));
        }
        if (!is_portable_crankwave_path(path)) {
            return error(CrankwaveContainerErrorCode::invalid_path,
                         "indexed path is not a canonical portable relative path",
                         path);
        }
        if (!previous_path.empty() && path <= previous_path) {
            return error(
                path == previous_path ? CrankwaveContainerErrorCode::duplicate_path
                                      : CrankwaveContainerErrorCode::noncanonical_index,
                path == previous_path ? "CRANKWAVE index contains a duplicate path"
                                      : "CRANKWAVE index paths are not strictly sorted",
                path);
        }
        previous_path = path;
        result.entries.push_back(
            {std::move(path), entry_offset, entry_size, entry_digest});
        cursor = path_end;
        expected_entry_offset = entry_end;
    }
    if (cursor != payload_offset || expected_entry_offset != container_size) {
        return error(CrankwaveContainerErrorCode::noncanonical_index,
                     "CRANKWAVE index or payload contains undeclared bytes");
    }
    return result;
}

CrankwaveInspectResult verify_crankwave(const std::span<const std::byte> container) {
    auto inspected = inspect_crankwave(container);
    if (const auto *failure = std::get_if<CrankwaveContainerError>(&inspected)) {
        return *failure;
    }
    auto result = std::get<CrankwaveContainerIndex>(std::move(inspected));
    const auto payload =
        bounded_span(container, result.payload_offset, result.payload_byte_count);
    if (payload.size() != result.payload_byte_count ||
        contract::sha256(payload) != result.payload_sha256) {
        return error(CrankwaveContainerErrorCode::payload_hash_mismatch,
                     "CRANKWAVE aggregate payload SHA-256 does not match its header");
    }
    for (const auto &entry : result.entries) {
        const auto bytes = crankwave_entry_payload(container, entry);
        if (bytes.size() != entry.payload_byte_count ||
            contract::sha256(bytes) != entry.payload_sha256) {
            return error(CrankwaveContainerErrorCode::entry_hash_mismatch,
                         "CRANKWAVE entry SHA-256 does not match its index",
                         entry.path);
        }
    }
    return result;
}

std::span<const std::byte>
crankwave_entry_payload(const std::span<const std::byte> container,
                        const CrankwaveIndexedEntry &entry) noexcept {
    return bounded_span(container, entry.payload_offset, entry.payload_byte_count);
}

} // namespace crankwave::artifacts
