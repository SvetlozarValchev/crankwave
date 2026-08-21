#include "engine_sim_offline/artifacts/vehicleengine_container.hpp"
#include "engine_sim_offline/artifacts/vehicleengine_package.hpp"

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

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;

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

[[nodiscard]] std::vector<std::byte> require_pack(VehicleEnginePackResult result) {
    if (const auto *failure = std::get_if<VehicleEngineContainerError>(&result)) {
        throw std::runtime_error{"pack failed: " + failure->message};
    }
    return std::get<std::vector<std::byte>>(std::move(result));
}

[[nodiscard]] VehicleEngineContainerIndex require_index(VehicleEngineInspectResult result) {
    if (const auto *failure = std::get_if<VehicleEngineContainerError>(&result)) {
        throw std::runtime_error{"inspection failed: " + failure->message};
    }
    return std::get<VehicleEngineContainerIndex>(std::move(result));
}

void expect_error(const VehicleEnginePackResult &result,
                  const VehicleEngineContainerErrorCode code,
                  const std::string_view message) {
    const auto *failure = std::get_if<VehicleEngineContainerError>(&result);
    expect(failure != nullptr && failure->code == code, message);
}

void expect_error(const VehicleEngineInspectResult &result,
                  const VehicleEngineContainerErrorCode code,
                  const std::string_view message) {
    const auto *failure = std::get_if<VehicleEngineContainerError>(&result);
    expect(failure != nullptr && failure->code == code, message);
}

struct Fixture {
    std::vector<std::byte> runtime = bytes("{\"schema\":\"responsive-v1\"}\n");
    std::vector<std::byte> audio{std::byte{0x00}, std::byte{0x7f}, std::byte{0x80},
                                 std::byte{0xff}};
    std::vector<std::byte> empty;

    [[nodiscard]] std::array<VehicleEnginePackEntry, 3> entries() const {
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
    const auto first = require_pack(pack_vehicleengine_v1(canonical_entries));
    const auto second = require_pack(pack_vehicleengine_v1(reordered));
    expect(first == second, "input order changed deterministic VEHICLEENGINE bytes");
    expect(first.size() == 371,
           "small fixture changed the frozen VEHICLEENGINE v1 byte count");

    const auto inspected = require_index(inspect_vehicleengine(first));
    expect(inspected.version == 1 && inspected.container_byte_count == first.size() &&
               inspected.entries.size() == 3,
           "inspection did not report the exact v1 carrier bounds");
    expect(inspected.entries[0].path == "audio/idle.pcm" &&
               inspected.entries[1].path == "evidence/empty.bin" &&
               inspected.entries[2].path == "runtime.json",
           "VEHICLEENGINE index is not canonical path-byte order");
    expect(inspected.entries[1].payload_byte_count == 0,
           "zero-byte package entry was not retained");

    const auto verified = require_index(verify_vehicleengine(first));
    expect(verified == inspected,
           "verified index differs from structurally inspected index");
    const auto runtime_payload = vehicleengine_entry_payload(first, verified.entries[2]);
    expect(std::equal(runtime_payload.begin(), runtime_payload.end(),
                      fixture.runtime.begin(), fixture.runtime.end()),
           "indexed package payload lookup returned the wrong exact bytes");

    auto impossible = verified.entries[0];
    impossible.payload_offset = first.size();
    impossible.payload_byte_count = 1;
    expect(vehicleengine_entry_payload(first, impossible).empty(),
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
        const std::array entries{VehicleEnginePackEntry{path, payload}};
        expect_error(pack_vehicleengine_v1(entries),
                     VehicleEngineContainerErrorCode::invalid_path,
                     "nonportable or traversal path was admitted");
    }
    const std::vector<std::string> valid_paths{"runtime.json", "audio/idle.pcm",
                                               "a/b-c_d.1", "9/0"};
    for (const auto &path : valid_paths) {
        expect(is_portable_vehicleengine_path(path),
               "canonical portable package path was rejected");
    }

    const std::array duplicate{
        VehicleEnginePackEntry{"runtime.json", payload},
        VehicleEnginePackEntry{"runtime.json", payload},
    };
    expect_error(pack_vehicleengine_v1(duplicate),
                 VehicleEngineContainerErrorCode::duplicate_path,
                 "duplicate path was admitted");
    expect_error(pack_vehicleengine_v1(std::span<const VehicleEnginePackEntry>{}),
                 VehicleEngineContainerErrorCode::invalid_argument,
                 "empty package tree was admitted");

    std::vector<VehicleEnginePackEntry> too_many_entries(
        kVehicleEngineMaximumEntryCountV1 + 1U,
        VehicleEnginePackEntry{"../invalid-before-hashing", payload});
    expect_error(pack_vehicleengine_v1(too_many_entries),
                 VehicleEngineContainerErrorCode::resource_limit,
                 "entry-count admission did not precede per-entry processing");
}

void test_header_index_and_extent_corruption_rejection() {
    const Fixture fixture;
    const auto entries = fixture.entries();
    const auto valid = require_pack(pack_vehicleengine_v1(entries));

    auto truncated = valid;
    truncated.resize(100);
    expect_error(inspect_vehicleengine(truncated),
                 VehicleEngineContainerErrorCode::malformed_header,
                 "truncated header was admitted");

    auto wrong_magic = valid;
    wrong_magic[0] ^= std::byte{0x01};
    expect_error(inspect_vehicleengine(wrong_magic),
                 VehicleEngineContainerErrorCode::malformed_header,
                 "wrong magic was admitted");

    auto unknown_version = valid;
    write_le<std::uint16_t>(unknown_version, 8, 2);
    expect_error(inspect_vehicleengine(unknown_version),
                 VehicleEngineContainerErrorCode::unsupported_version,
                 "unknown container version was admitted");

    auto unknown_flags = valid;
    write_le<std::uint32_t>(unknown_flags, 12, 1);
    expect_error(inspect_vehicleengine(unknown_flags),
                 VehicleEngineContainerErrorCode::malformed_header,
                 "unknown header flags were admitted");

    auto trailing = valid;
    trailing.push_back(std::byte{0});
    expect_error(inspect_vehicleengine(trailing),
                 VehicleEngineContainerErrorCode::malformed_header,
                 "undeclared trailing bytes were admitted");

    auto index_corruption = valid;
    index_corruption[kVehicleEngineHeaderByteCountV1 +
                     kVehicleEngineIndexEntryPrefixByteCountV1] ^= std::byte{0x01};
    expect_error(inspect_vehicleengine(index_corruption),
                 VehicleEngineContainerErrorCode::index_hash_mismatch,
                 "corrupt index was admitted without matching its digest");

    auto too_many_entries = valid;
    write_le<std::uint32_t>(too_many_entries, 16, kVehicleEngineMaximumEntryCountV1 + 1U);
    expect_error(inspect_vehicleengine(too_many_entries),
                 VehicleEngineContainerErrorCode::resource_limit,
                 "unbounded declared entry count was admitted");
}

void test_authenticated_malicious_index_rejection() {
    const auto payload = bytes("payload");
    const std::array entries{
        VehicleEnginePackEntry{"aa", payload},
        VehicleEnginePackEntry{"bb", payload},
    };
    const auto valid = require_pack(pack_vehicleengine_v1(entries));
    constexpr std::size_t first_path =
        kVehicleEngineHeaderByteCountV1 + kVehicleEngineIndexEntryPrefixByteCountV1;
    constexpr std::size_t first_record = kVehicleEngineIndexEntryPrefixByteCountV1 + 2;
    constexpr std::size_t second_path = kVehicleEngineHeaderByteCountV1 + first_record +
                                        kVehicleEngineIndexEntryPrefixByteCountV1;
    constexpr std::size_t index_size = first_record * 2;

    auto traversal = valid;
    traversal[first_path] = std::byte{'.'};
    traversal[first_path + 1] = std::byte{'.'};
    write_digest(traversal, 64,
                 contract::sha256(std::span<const std::byte>{traversal}.subspan(
                     kVehicleEngineHeaderByteCountV1, index_size)));
    expect_error(inspect_vehicleengine(traversal),
                 VehicleEngineContainerErrorCode::invalid_path,
                 "digest-authenticated traversal path was admitted");

    auto unsorted = valid;
    unsorted[first_path] = std::byte{'z'};
    unsorted[first_path + 1] = std::byte{'z'};
    unsorted[second_path] = std::byte{'a'};
    unsorted[second_path + 1] = std::byte{'a'};
    write_digest(unsorted, 64,
                 contract::sha256(std::span<const std::byte>{unsorted}.subspan(
                     kVehicleEngineHeaderByteCountV1, index_size)));
    expect_error(inspect_vehicleengine(unsorted),
                 VehicleEngineContainerErrorCode::noncanonical_index,
                 "digest-authenticated unsorted index was admitted");

    auto overlap = valid;
    write_le<std::uint64_t>(overlap, kVehicleEngineHeaderByteCountV1 + first_record + 8,
                            valid.size() - payload.size() * 2);
    write_digest(overlap, 64,
                 contract::sha256(std::span<const std::byte>{overlap}.subspan(
                     kVehicleEngineHeaderByteCountV1, index_size)));
    expect_error(inspect_vehicleengine(overlap),
                 VehicleEngineContainerErrorCode::malformed_index,
                 "overlapping payload extents were admitted");
}

void test_payload_and_entry_corruption_rejection() {
    const Fixture fixture;
    const auto entries = fixture.entries();
    const auto valid = require_pack(pack_vehicleengine_v1(entries));
    const auto index = require_index(inspect_vehicleengine(valid));

    auto corrupt_payload = valid;
    corrupt_payload[static_cast<std::size_t>(index.entries[0].payload_offset)] ^=
        std::byte{0x80};
    expect(!std::holds_alternative<VehicleEngineContainerError>(
               inspect_vehicleengine(corrupt_payload)),
           "inspection unexpectedly read or authenticated payload contents");
    expect_error(verify_vehicleengine(corrupt_payload),
                 VehicleEngineContainerErrorCode::payload_hash_mismatch,
                 "aggregate payload corruption was admitted");

    const auto payload_span = std::span<const std::byte>{corrupt_payload}.subspan(
        static_cast<std::size_t>(index.payload_offset),
        static_cast<std::size_t>(index.payload_byte_count));
    write_digest(corrupt_payload, 96, contract::sha256(payload_span));
    const auto failure = verify_vehicleengine(corrupt_payload);
    expect_error(failure, VehicleEngineContainerErrorCode::entry_hash_mismatch,
                 "per-entry payload corruption was admitted after aggregate rehash");
    const auto *entry_failure = std::get_if<VehicleEngineContainerError>(&failure);
    expect(entry_failure != nullptr && entry_failure->path == index.entries[0].path,
           "entry hash failure did not identify the corrupt package path");
}

[[nodiscard]] std::string descriptor_json(const std::span<const std::byte> manifest) {
    return "{\"schema\":\"engine-sim-offline/vehicleengine-package\","
           "\"version\":1,\"engine_id\":\"test-engine-v1\",\"runtime\":{"
           "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
           "\"manifest_sha256\":\"" +
           hex(contract::sha256(manifest)) + "\"}}";
}

void test_strict_package_descriptor_and_tree_binding() {
    const auto manifest = bytes("{\"schema\":\"responsive-audio\"}\n");
    const auto json = descriptor_json(manifest);
    auto parsed = parse_vehicleengine_package_descriptor(json);
    const auto *descriptor = std::get_if<VehicleEnginePackageDescriptor>(&parsed);
    expect(descriptor != nullptr && descriptor->version == 1 &&
               descriptor->engine_id == "test-engine-v1" &&
               descriptor->runtime.kind == "responsive-audio" &&
               descriptor->runtime.manifest_path == "runtime.json" &&
               descriptor->runtime.manifest_sha256 == contract::sha256(manifest),
           "valid strict VEHICLEENGINE package descriptor was not retained exactly");

    const auto descriptor_bytes = bytes(json);
    const std::array tree{
        VehicleEnginePackEntry{"vehicleengine.json", descriptor_bytes},
        VehicleEnginePackEntry{"runtime.json", manifest},
    };
    const auto validated = validate_vehicleengine_package_tree(tree);
    const auto *validated_descriptor =
        std::get_if<VehicleEnginePackageDescriptor>(&validated);
    expect(validated_descriptor != nullptr && *validated_descriptor == *descriptor,
           "valid package tree did not return its exact bound descriptor");

    const std::array missing_manifest{
        VehicleEnginePackEntry{"vehicleengine.json", descriptor_bytes}};
    const auto missing = validate_vehicleengine_package_tree(missing_manifest);
    const auto *missing_error = std::get_if<VehicleEnginePackageError>(&missing);
    expect(missing_error != nullptr &&
               missing_error->code ==
                   VehicleEnginePackageErrorCode::missing_runtime_manifest,
           "missing declared runtime manifest was admitted");

    const auto changed_manifest = bytes("changed");
    const std::array mismatched{
        VehicleEnginePackEntry{"vehicleengine.json", descriptor_bytes},
        VehicleEnginePackEntry{"runtime.json", changed_manifest},
    };
    const auto mismatch = validate_vehicleengine_package_tree(mismatched);
    const auto *mismatch_error = std::get_if<VehicleEnginePackageError>(&mismatch);
    expect(mismatch_error != nullptr &&
               mismatch_error->code ==
                   VehicleEnginePackageErrorCode::runtime_manifest_hash_mismatch,
           "runtime manifest digest mismatch was admitted");

    // Regression: the former API accepted a separately supplied descriptor and
    // could therefore validate this runtime against `descriptor` while ignoring
    // the contradictory vehicleengine.json bytes in the tree. The validator must parse
    // and bind the actual package entry instead.
    const auto forged_descriptor_bytes = bytes(descriptor_json(changed_manifest));
    const std::array forged_tree{
        VehicleEnginePackEntry{"vehicleengine.json", forged_descriptor_bytes},
        VehicleEnginePackEntry{"runtime.json", manifest},
    };
    const auto forged = validate_vehicleengine_package_tree(forged_tree);
    const auto *forged_error = std::get_if<VehicleEnginePackageError>(&forged);
    expect(forged_error != nullptr &&
               forged_error->code ==
                   VehicleEnginePackageErrorCode::runtime_manifest_hash_mismatch,
           "an external descriptor substituted for the tree's vehicleengine.json");
}

void test_package_descriptor_rejections() {
    const auto manifest = bytes("runtime");
    const auto valid = descriptor_json(manifest);
    const std::vector<std::pair<std::string, VehicleEnginePackageErrorCode>> invalid{
        {"{", VehicleEnginePackageErrorCode::malformed_json},
        {"[]", VehicleEnginePackageErrorCode::invalid_shape},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"},\"extra\":true}",
         VehicleEnginePackageErrorCode::unknown_field},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"runtime\":{\"kind\":\"responsive-audio\","
         "\"manifest_path\":\"runtime.json\",\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::missing_field},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\",\"extra\":true}}",
         VehicleEnginePackageErrorCode::unknown_field},
        {"{\"schema\":\"wrong\",\"version\":1,\"engine_id\":\"engine\","
         "\"runtime\":{\"kind\":\"responsive-audio\","
         "\"manifest_path\":\"runtime.json\",\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":2,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"Engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"pcm-loops\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"../runtime.json\","
         "\"manifest_sha256\":\"" +
             hex(contract::sha256(manifest)) + "\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
        {"{\"schema\":\"engine-sim-offline/vehicleengine-package\","
         "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
         "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
         "\"manifest_sha256\":"
         "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}}",
         VehicleEnginePackageErrorCode::invalid_value},
    };
    for (const auto &[json, expected_code] : invalid) {
        const auto result = parse_vehicleengine_package_descriptor(json);
        const auto *failure = std::get_if<VehicleEnginePackageError>(&result);
        expect(failure != nullptr && failure->code == expected_code,
               "invalid VEHICLEENGINE package descriptor was admitted or misclassified");
    }

    std::string oversized(kVehicleEngineMaximumDescriptorByteCount + 1U, ' ');
    const auto oversized_result = parse_vehicleengine_package_descriptor(oversized);
    const auto *oversized_error = std::get_if<VehicleEnginePackageError>(&oversized_result);
    expect(oversized_error != nullptr &&
               oversized_error->code == VehicleEnginePackageErrorCode::invalid_value,
           "oversized VEHICLEENGINE package descriptor was admitted");

    const auto duplicate_key =
        "{\"schema\":\"engine-sim-offline/vehicleengine-package\","
        "\"schema\":\"engine-sim-offline/vehicleengine-package\","
        "\"version\":1,\"engine_id\":\"engine\",\"runtime\":{"
        "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
        "\"manifest_sha256\":\"" +
        hex(contract::sha256(manifest)) + "\"}}";
    const auto duplicate_result = parse_vehicleengine_package_descriptor(duplicate_key);
    const auto *duplicate_error = std::get_if<VehicleEnginePackageError>(&duplicate_result);
    expect(duplicate_error != nullptr &&
               duplicate_error->code == VehicleEnginePackageErrorCode::malformed_json,
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
        std::cerr << "VEHICLEENGINE container test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
