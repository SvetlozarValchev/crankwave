#include "crankwave/artifacts/crankwave_container.hpp"
#include "crankwave/artifacts/crankwave_package.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::artifacts;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Integer>
void write_le(std::vector<std::byte> &bytes, const std::size_t offset,
              const Integer value) {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(value >> static_cast<unsigned>(index * 8U));
    }
}

void write_digest(std::vector<std::byte> &bytes, const std::size_t offset,
                  const contract::Sha256Digest &digest) {
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        bytes[offset + index] = static_cast<std::byte>(digest.bytes[index]);
    }
}

[[nodiscard]] std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

[[nodiscard]] std::string hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto value : digest.bytes) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> require_pack(CrankwavePackResult result) {
    if (const auto *failure = std::get_if<CrankwaveContainerError>(&result)) {
        throw std::runtime_error{"pack failed: " + failure->message};
    }
    return std::get<std::vector<std::byte>>(std::move(result));
}

[[nodiscard]] CrankwaveContainerIndex require_index(CrankwaveInspectResult result) {
    if (const auto *failure = std::get_if<CrankwaveContainerError>(&result)) {
        throw std::runtime_error{"inspection failed: " + failure->message};
    }
    return std::get<CrankwaveContainerIndex>(std::move(result));
}

void expect_error(const CrankwavePackResult &result,
                  const CrankwaveContainerErrorCode code,
                  const std::string_view message) {
    const auto *failure = std::get_if<CrankwaveContainerError>(&result);
    expect(failure != nullptr && failure->code == code, message);
}

void expect_error(const CrankwaveInspectResult &result,
                  const CrankwaveContainerErrorCode code,
                  const std::string_view message) {
    const auto *failure = std::get_if<CrankwaveContainerError>(&result);
    expect(failure != nullptr && failure->code == code, message);
}

struct Fixture {
    std::vector<std::byte> runtime = bytes("{\"schema\":\"responsive-v1\"}\n");
    std::vector<std::byte> audio{std::byte{0x00}, std::byte{0x7f}, std::byte{0x80},
                                 std::byte{0xff}};
    std::vector<std::byte> empty;

    [[nodiscard]] std::array<CrankwavePackEntry, 3> entries() const {
        return {{{"runtime.json", runtime},
                 {"audio/idle.pcm", audio},
                 {"evidence/empty.bin", empty}}};
    }
};

void test_deterministic_pack_and_verified_lookup() {
    const Fixture fixture;
    const auto canonical_entries = fixture.entries();
    const std::array reordered{canonical_entries[2], canonical_entries[0],
                               canonical_entries[1]};
    const auto first = require_pack(pack_crankwave_v1(canonical_entries));
    const auto second = require_pack(pack_crankwave_v1(reordered));
    expect(first == second, "input order changed deterministic CRANKWAVE bytes");
    expect(first.size() == 371,
           "small fixture changed the frozen CRANKWAVE v1 byte count");

    const auto inspected = require_index(inspect_crankwave(first));
    expect(inspected.version == 1 && inspected.container_byte_count == first.size() &&
               inspected.entries.size() == 3,
           "inspection did not report the exact v1 carrier bounds");
    expect(inspected.entries[0].path == "audio/idle.pcm" &&
               inspected.entries[1].path == "evidence/empty.bin" &&
               inspected.entries[2].path == "runtime.json",
           "CRANKWAVE index is not canonical path-byte order");
    expect(inspected.entries[1].payload_byte_count == 0,
           "zero-byte package entry was not retained");

    const auto verified = require_index(verify_crankwave(first));
    expect(verified == inspected,
           "verified index differs from structurally inspected index");
    const auto runtime_payload = crankwave_entry_payload(first, verified.entries[2]);
    expect(std::equal(runtime_payload.begin(), runtime_payload.end(),
                      fixture.runtime.begin(), fixture.runtime.end()),
           "indexed package payload lookup returned the wrong exact bytes");

    auto impossible = verified.entries[0];
    impossible.payload_offset = first.size();
    impossible.payload_byte_count = 1;
    expect(crankwave_entry_payload(first, impossible).empty(),
           "out-of-bounds indexed lookup did not fail closed");
}

void test_portable_path_and_duplicate_admission() {
    const auto payload = bytes("x");
    const std::vector<std::string> invalid_paths{
        "",           "/absolute.bin", "../escape.bin", "safe/../escape.bin",
        "safe\\x",    "safe//x",       "safe/x.",       "safe/.hidden",
        "safe/X.bin", "safe/x bin",    "con",           "aux.txt",
        "com1.bin",   "safe/",         "./safe.bin",
    };
    for (const auto &path : invalid_paths) {
        const std::array entries{CrankwavePackEntry{path, payload}};
        expect_error(pack_crankwave_v1(entries),
                     CrankwaveContainerErrorCode::invalid_path,
                     "nonportable or traversal path was admitted");
    }
    const std::vector<std::string> valid_paths{"runtime.json", "audio/idle.pcm",
                                               "a/b-c_d.1", "9/0"};
    for (const auto &path : valid_paths) {
        expect(is_portable_crankwave_path(path),
               "canonical portable package path was rejected");
    }

    const std::array duplicate{
        CrankwavePackEntry{"runtime.json", payload},
        CrankwavePackEntry{"runtime.json", payload},
    };
    expect_error(pack_crankwave_v1(duplicate),
                 CrankwaveContainerErrorCode::duplicate_path,
                 "duplicate path was admitted");
    expect_error(pack_crankwave_v1(std::span<const CrankwavePackEntry>{}),
                 CrankwaveContainerErrorCode::invalid_argument,
                 "empty package tree was admitted");

    std::vector<CrankwavePackEntry> too_many_entries(
        kCrankwaveMaximumEntryCountV1 + 1U,
        CrankwavePackEntry{"../invalid-before-hashing", payload});
    expect_error(pack_crankwave_v1(too_many_entries),
                 CrankwaveContainerErrorCode::resource_limit,
                 "entry-count admission did not precede per-entry processing");
}

void test_header_index_and_extent_corruption_rejection() {
    const Fixture fixture;
    const auto entries = fixture.entries();
    const auto valid = require_pack(pack_crankwave_v1(entries));

    auto truncated = valid;
    truncated.resize(100);
    expect_error(inspect_crankwave(truncated),
                 CrankwaveContainerErrorCode::malformed_header,
                 "truncated header was admitted");

    auto wrong_magic = valid;
    wrong_magic[0] ^= std::byte{0x01};
    expect_error(inspect_crankwave(wrong_magic),
                 CrankwaveContainerErrorCode::malformed_header,
                 "wrong magic was admitted");

    auto unknown_version = valid;
    write_le<std::uint16_t>(unknown_version, 8, 2);
    expect_error(inspect_crankwave(unknown_version),
                 CrankwaveContainerErrorCode::unsupported_version,
                 "unknown container version was admitted");

    auto unknown_flags = valid;
    write_le<std::uint32_t>(unknown_flags, 12, 1);
    expect_error(inspect_crankwave(unknown_flags),
                 CrankwaveContainerErrorCode::malformed_header,
                 "unknown header flags were admitted");

    auto trailing = valid;
    trailing.push_back(std::byte{0});
    expect_error(inspect_crankwave(trailing),
                 CrankwaveContainerErrorCode::malformed_header,
                 "undeclared trailing bytes were admitted");

    auto index_corruption = valid;
    index_corruption[kCrankwaveHeaderByteCountV1 +
                     kCrankwaveIndexEntryPrefixByteCountV1] ^= std::byte{0x01};
    expect_error(inspect_crankwave(index_corruption),
                 CrankwaveContainerErrorCode::index_hash_mismatch,
                 "corrupt index was admitted without matching its digest");

    auto too_many_entries = valid;
    write_le<std::uint32_t>(too_many_entries, 16, kCrankwaveMaximumEntryCountV1 + 1U);
    expect_error(inspect_crankwave(too_many_entries),
                 CrankwaveContainerErrorCode::resource_limit,
                 "unbounded declared entry count was admitted");
}

void test_authenticated_malicious_index_rejection() {
    const auto payload = bytes("payload");
    const std::array entries{
        CrankwavePackEntry{"aa", payload},
        CrankwavePackEntry{"bb", payload},
    };
    const auto valid = require_pack(pack_crankwave_v1(entries));
    constexpr std::size_t first_path =
        kCrankwaveHeaderByteCountV1 + kCrankwaveIndexEntryPrefixByteCountV1;
    constexpr std::size_t first_record = kCrankwaveIndexEntryPrefixByteCountV1 + 2;
    constexpr std::size_t second_path = kCrankwaveHeaderByteCountV1 + first_record +
                                        kCrankwaveIndexEntryPrefixByteCountV1;
    constexpr std::size_t index_size = first_record * 2;

    auto traversal = valid;
    traversal[first_path] = std::byte{'.'};
    traversal[first_path + 1] = std::byte{'.'};
    write_digest(traversal, 64,
                 contract::sha256(std::span<const std::byte>{traversal}.subspan(
                     kCrankwaveHeaderByteCountV1, index_size)));
    expect_error(inspect_crankwave(traversal),
                 CrankwaveContainerErrorCode::invalid_path,
                 "digest-authenticated traversal path was admitted");

    auto unsorted = valid;
    unsorted[first_path] = std::byte{'z'};
    unsorted[first_path + 1] = std::byte{'z'};
    unsorted[second_path] = std::byte{'a'};
    unsorted[second_path + 1] = std::byte{'a'};
    write_digest(unsorted, 64,
                 contract::sha256(std::span<const std::byte>{unsorted}.subspan(
                     kCrankwaveHeaderByteCountV1, index_size)));
    expect_error(inspect_crankwave(unsorted),
                 CrankwaveContainerErrorCode::noncanonical_index,
                 "digest-authenticated unsorted index was admitted");

    auto overlap = valid;
    write_le<std::uint64_t>(overlap, kCrankwaveHeaderByteCountV1 + first_record + 8,
                            valid.size() - payload.size() * 2);
    write_digest(overlap, 64,
                 contract::sha256(std::span<const std::byte>{overlap}.subspan(
                     kCrankwaveHeaderByteCountV1, index_size)));
    expect_error(inspect_crankwave(overlap),
                 CrankwaveContainerErrorCode::malformed_index,
                 "overlapping payload extents were admitted");
}

void test_payload_and_entry_corruption_rejection() {
    const Fixture fixture;
    const auto entries = fixture.entries();
    const auto valid = require_pack(pack_crankwave_v1(entries));
    const auto index = require_index(inspect_crankwave(valid));

    auto corrupt_payload = valid;
    corrupt_payload[static_cast<std::size_t>(index.entries[0].payload_offset)] ^=
        std::byte{0x80};
    expect(!std::holds_alternative<CrankwaveContainerError>(
               inspect_crankwave(corrupt_payload)),
           "inspection unexpectedly read or authenticated payload contents");
    expect_error(verify_crankwave(corrupt_payload),
                 CrankwaveContainerErrorCode::payload_hash_mismatch,
                 "aggregate payload corruption was admitted");

    const auto payload_span = std::span<const std::byte>{corrupt_payload}.subspan(
        static_cast<std::size_t>(index.payload_offset),
        static_cast<std::size_t>(index.payload_byte_count));
    write_digest(corrupt_payload, 96, contract::sha256(payload_span));
    const auto failure = verify_crankwave(corrupt_payload);
    expect_error(failure, CrankwaveContainerErrorCode::entry_hash_mismatch,
                 "per-entry payload corruption was admitted after aggregate rehash");
    const auto *entry_failure = std::get_if<CrankwaveContainerError>(&failure);
    expect(entry_failure != nullptr && entry_failure->path == index.entries[0].path,
           "entry hash failure did not identify the corrupt package path");
}

[[nodiscard]] std::string descriptor_json(const std::span<const std::byte> manifest) {
    return "{\"schema\":\"crankwave/crankwave-package\","
           "\"version\":1,\"engine_id\":\"test-engine-v1\",\"runtime\":{"
           "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
           "\"manifest_sha256\":\"" +
           hex(contract::sha256(manifest)) + "\"}}";
}

void test_strict_package_descriptor_and_tree_binding() {
    const auto manifest = bytes("{\"schema\":\"responsive-audio\"}\n");
    const auto json = descriptor_json(manifest);
    auto parsed = parse_crankwave_package_descriptor(json);
    const auto *descriptor = std::get_if<CrankwavePackageDescriptor>(&parsed);
    expect(descriptor != nullptr && descriptor->version == 1 &&
               descriptor->engine_id == "test-engine-v1" &&
               descriptor->runtime.kind == "responsive-audio" &&
               descriptor->runtime.manifest_path == "runtime.json" &&
               descriptor->runtime.manifest_sha256 == contract::sha256(manifest),
           "valid strict CRANKWAVE package descriptor was not retained exactly");

    const auto descriptor_bytes = bytes(json);
    const std::array tree{
        CrankwavePackEntry{"crankwave.json", descriptor_bytes},
        CrankwavePackEntry{"runtime.json", manifest},
    };
    const auto validated = validate_crankwave_package_tree(tree);
    const auto *validated_descriptor =
        std::get_if<CrankwavePackageDescriptor>(&validated);
    expect(validated_descriptor != nullptr && *validated_descriptor == *descriptor,
           "valid package tree did not return its exact bound descriptor");

    const std::array missing_manifest{
        CrankwavePackEntry{"crankwave.json", descriptor_bytes}};
    const auto missing = validate_crankwave_package_tree(missing_manifest);
    const auto *missing_error = std::get_if<CrankwavePackageError>(&missing);
    expect(missing_error != nullptr &&
               missing_error->code ==
                   CrankwavePackageErrorCode::missing_runtime_manifest,
           "missing declared runtime manifest was admitted");

    const auto changed_manifest = bytes("changed");
    const std::array mismatched{
        CrankwavePackEntry{"crankwave.json", descriptor_bytes},
        CrankwavePackEntry{"runtime.json", changed_manifest},
    };
    const auto mismatch = validate_crankwave_package_tree(mismatched);
    const auto *mismatch_error = std::get_if<CrankwavePackageError>(&mismatch);
    expect(mismatch_error != nullptr &&
               mismatch_error->code ==
                   CrankwavePackageErrorCode::runtime_manifest_hash_mismatch,
           "runtime manifest digest mismatch was admitted");

    // Regression: the former API accepted a separately supplied descriptor and
    // could therefore validate this runtime against `descriptor` while ignoring
    // the contradictory crankwave.json bytes in the tree. The validator must parse
    // and bind the actual package entry instead.
    const auto forged_descriptor_bytes = bytes(descriptor_json(changed_manifest));
    const std::array forged_tree{
        CrankwavePackEntry{"crankwave.json", forged_descriptor_bytes},
        CrankwavePackEntry{"runtime.json", manifest},
    };
    const auto forged = validate_crankwave_package_tree(forged_tree);
    const auto *forged_error = std::get_if<CrankwavePackageError>(&forged);
    expect(forged_error != nullptr &&
               forged_error->code ==
                   CrankwavePackageErrorCode::runtime_manifest_hash_mismatch,
           "an external descriptor substituted for the tree's crankwave.json");
}

void test_package_descriptor_rejections() {
    const auto manifest = bytes("runtime");
    const auto valid = descriptor_json(manifest);
    const std::vector<std::pair<std::string, CrankwavePackageErrorCode>> invalid{
        {"{", CrankwavePackageErrorCode::malformed_json},
        {"[]", CrankwavePackageErrorCode::invalid_shape},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"},\"extra\":true}",
         CrankwavePackageErrorCode::unknown_field},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"runtime\":{\"kind\":\"responsive-audio\","
         "\"manifest_path\":\"runtime.json\",\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::missing_field},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\",\"extra\":true}}",
         CrankwavePackageErrorCode::unknown_field},
        {"{\"schema\":\"wrong\",\"version\":1,\"engine_id\":\"engine\","
         "\"runtime\":{\"kind\":\"responsive-audio\","
         "\"manifest_path\":\"runtime.json\",\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::invalid_value},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":2,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::invalid_value},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"Engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::invalid_value},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"pcm-loops\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::invalid_value},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"../runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         CrankwavePackageErrorCode::invalid_value},
        {"{\"schema\":\"crankwave/crankwave-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":"
         "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}}",
         CrankwavePackageErrorCode::invalid_value},
    };
    for (const auto &[json, expected_code] : invalid) {
        const auto result = parse_crankwave_package_descriptor(json);
        const auto *failure = std::get_if<CrankwavePackageError>(&result);
        expect(failure != nullptr && failure->code == expected_code,
               "invalid CRANKWAVE package descriptor was admitted or misclassified");
    }

    std::string oversized(kCrankwaveMaximumDescriptorByteCount + 1U, ' ');
    const auto oversized_result = parse_crankwave_package_descriptor(oversized);
    const auto *oversized_error = std::get_if<CrankwavePackageError>(&oversized_result);
    expect(oversized_error != nullptr &&
               oversized_error->code == CrankwavePackageErrorCode::invalid_value,
           "oversized CRANKWAVE package descriptor was admitted");

    const auto duplicate_key =
        "{\"schema\":\"crankwave/crankwave-package\","
        "\"schema\":\"crankwave/crankwave-package\","
        "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
        "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
        "\"manifest_sha256\":\"" +
        hex(contract::sha256(manifest)) + "\"}}";
    const auto duplicate_result = parse_crankwave_package_descriptor(duplicate_key);
    const auto *duplicate_error = std::get_if<CrankwavePackageError>(&duplicate_result);
    expect(duplicate_error != nullptr &&
               duplicate_error->code == CrankwavePackageErrorCode::malformed_json,
           "duplicate descriptor JSON key was admitted");
    expect(!valid.empty(), "valid descriptor fixture unexpectedly empty");
}

} // namespace

int main() {
    try {
        test_deterministic_pack_and_verified_lookup();
        test_portable_path_and_duplicate_admission();
        test_header_index_and_extent_corruption_rejection();
        test_authenticated_malicious_index_rejection();
        test_payload_and_entry_corruption_rejection();
        test_strict_package_descriptor_and_tree_binding();
        test_package_descriptor_rejections();
    } catch (const std::exception &error) {
        std::cerr << "CRANKWAVE container test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
