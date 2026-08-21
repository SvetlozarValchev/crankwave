#include "engine_sim_offline/artifacts/vehicleengine_container.hpp"
#include "engine_sim_offline/authoring/json.hpp"
#include "engine_sim_offline/c_api.h"
#include "engine_sim_offline/responsive/native_package.hpp"
#include "engine_sim_offline/responsive/native_publication.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <cstdlib>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::responsive;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    std::vector<std::byte> result(text.size());
    std::transform(text.begin(), text.end(), result.begin(), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return result;
}

[[nodiscard]] std::string text(const std::span<const std::byte> value) {
    return value.empty() ? std::string{}
                         : std::string{reinterpret_cast<const char *>(value.data()),
                                       value.size()};
}

[[nodiscard]] contract::Sha256Digest digest(const std::string_view value) {
    return contract::sha256(bytes(value));
}

[[nodiscard]] std::string hex(const contract::Sha256Digest &value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto byte : value.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

[[nodiscard]] PortableResponsivePackageMember member(std::string path,
                                                     std::string_view value) {
    return {std::move(path), bytes(value)};
}

[[nodiscard]] NativeResponsivePackageInputV2 input_fixture() {
    NativeResponsivePackageInputV2 input;
    input.identity.backend.release_identity = "1.1.0";
    input.identity.backend.c_api_version = ESO_C_API_VERSION;
    input.identity.backend.executable_sha256 = digest("native-executable");
    input.identity.backend.source_closure_sha256 = digest("source-closure");
    input.identity.backend.method_registry_sha256 = digest("method-registry");
    input.identity.engine_id = "example-engine";
    input.identity.engine_source_sha256 = digest("engine-source-json");
    input.identity.profile_id = "interactive-preview-redline-v1";
    input.identity.profile_sha256 = digest("selected-profile");
    input.identity.bake_recipe_sha256 = digest("native-bake-recipe-v2");
    input.identity.builtin_asset_catalog_sha256 = digest("builtin-assets");
    input.identity.resolved_assets = {
        {"accessory-configuration", "alternator-a", digest("accessory")},
        {"audio", "smooth-39", digest("smooth-39")},
    };

    input.runtime.engine_id = input.identity.engine_id;
    input.runtime.compiled_engine_provenance_sha256 = digest("compiled-engine");
    input.runtime.minimum_rpm = 550.123456;
    input.runtime.maximum_rpm = 6700.654321;
    input.runtime.audition_bus_id = "master-engine-audition";
    input.runtime.dry_bus_ids = {"exhaust.front.dry", "exhaust.rear.dry"};

    input.payload_members.push_back(member("held/package.json",
                                           R"JSON({
  "schema": "engine-sim-offline/responsive-audio-held-texture",
  "engine": "example-engine",
  "dry_bus_ids": ["exhaust.front.dry", "exhaust.rear.dry"],
  "route_manifests": [
    {"bus_id": "exhaust.front.dry"},
    {"bus_id": "exhaust.rear.dry"}
  ],
  "presentation": {
    "audition_bus_id": "master-engine-audition",
    "audition_dry_bus_order": ["exhaust.front.dry", "exhaust.rear.dry"],
    "routes": [
      {"dry_bus_id": "exhaust.front.dry"},
      {"dry_bus_id": "exhaust.rear.dry"}
    ]
  }
}
)JSON"));
    input.payload_members.push_back(member("directional/runtime.json",
                                           R"JSON({
  "schema": "engine-sim-offline/responsive-audio-directional-texture",
  "engine": "example-engine",
  "dry_bus_ids": ["exhaust.front.dry", "exhaust.rear.dry"],
  "route_manifests": [
    {"bus_id": "exhaust.front.dry"},
    {"bus_id": "exhaust.rear.dry"}
  ]
}
)JSON"));
    input.payload_members.push_back(
        {"audio/empty-but-declared.bin", std::vector<std::byte>{}});
    return input;
}

[[nodiscard]] NativeResponsivePackageV2
require_package(NativeResponsivePackageBuildResultV2 result) {
    if (const auto *failure = std::get_if<NativeResponsivePackageError>(&result)) {
        throw std::runtime_error{"package build failed: " + failure->detail_code +
                                 ": " + failure->message};
    }
    return std::get<NativeResponsivePackageV2>(std::move(result));
}

[[nodiscard]] const PortableResponsivePackageMember &
find_member(const NativeResponsivePackageV2 &package, const std::string_view path) {
    const auto candidate =
        std::find_if(package.members.begin(), package.members.end(),
                     [&](const auto &entry) { return entry.path == path; });
    if (candidate == package.members.end()) {
        throw std::runtime_error{"package member is absent: " + std::string{path}};
    }
    return *candidate;
}

[[nodiscard]] const NativeResponsivePackageError &
require_error(const NativeResponsivePackageBuildResultV2 &result,
              const NativeResponsivePackageErrorCode code) {
    const auto *failure = std::get_if<NativeResponsivePackageError>(&result);
    expect(failure != nullptr && failure->code == code,
           "package operation did not return the expected typed failure");
    return *failure;
}

void test_native_identity_and_package_are_deterministic() {
    auto first_input = input_fixture();
    auto reordered_input = input_fixture();
    std::reverse(reordered_input.identity.resolved_assets.begin(),
                 reordered_input.identity.resolved_assets.end());

    const auto first_identity =
        encode_native_responsive_bake_identity_v1(first_input.identity);
    const auto reordered_identity =
        encode_native_responsive_bake_identity_v1(reordered_input.identity);
    const auto &first_encoded =
        std::get<EncodedNativeResponsiveBakeIdentityV1>(first_identity);
    const auto &reordered_encoded =
        std::get<EncodedNativeResponsiveBakeIdentityV1>(reordered_identity);
    expect(first_encoded == reordered_encoded,
           "resolved asset input order changed native cache identity bytes");
    auto starter_identity_input = input_fixture();
    starter_identity_input.identity.shared_recorded_starter =
        SharedRecordedStarterIdentityV1{digest("shared-starter-tree"), 3U};
    const auto starter_identity =
        encode_native_responsive_bake_identity_v1(starter_identity_input.identity);
    expect(std::get<EncodedNativeResponsiveBakeIdentityV1>(starter_identity).sha256 !=
               first_encoded.sha256,
           "shared recorded starter identity does not invalidate native cache");
    auto prerelease_identity_input = input_fixture();
    prerelease_identity_input.identity.backend.release_identity = "1.1.0-rc.1+native";
    expect(std::holds_alternative<EncodedNativeResponsiveBakeIdentityV1>(
               encode_native_responsive_bake_identity_v1(
                   prerelease_identity_input.identity)),
           "installed semantic prerelease identity grammar was rejected");
    const auto identity_text = text(first_encoded.bytes);
    expect(identity_text.find("native-cpp") != identity_text.npos &&
               identity_text.find("native-responsive-bake-cache-identity.v1") !=
                   identity_text.npos &&
               identity_text.find("wasm") == identity_text.npos &&
               identity_text.find("node") == identity_text.npos &&
               identity_text.find("v8") == identity_text.npos,
           "native cache identity is missing native separation or carries JS/WASM "
           "identity");
    expect(std::holds_alternative<authoring::JsonDocument>(
               authoring::parse_json(identity_text)),
           "canonical native cache identity bytes are not valid JSON");

    const auto first =
        require_package(build_native_responsive_package_v2(std::move(first_input)));
    const auto second =
        require_package(build_native_responsive_package_v2(std::move(reordered_input)));
    expect(first.cache_identity == second.cache_identity &&
               first.members == second.members &&
               first.vehicleengine_v1 == second.vehicleengine_v1 &&
               first.vehicleengine_sha256 == second.vehicleengine_sha256,
           "identical native package inputs did not produce identical package bytes");

    const auto runtime_text = text(find_member(first, "runtime.json").bytes);
    auto runtime_parse = authoring::parse_json(runtime_text);
    expect(std::holds_alternative<authoring::JsonDocument>(runtime_parse),
           "generated root runtime is not JSON");
    auto runtime_document = std::get<authoring::JsonDocument>(std::move(runtime_parse));
    const auto runtime = runtime_document.root();
    expect(runtime.kind() == authoring::JsonKind::object && runtime.size() == 9U,
           "root runtime changed its accepted required key set");
    expect(runtime.find("dry_bus_ids").valid() == false &&
               runtime.find("audio").find("bus_id").string() ==
                   std::optional<std::string_view>{"master-engine-audition"},
           "root runtime leaked validation-only dry buses or lost audio.bus_id");
    expect(runtime.find("held_package_path").string() ==
                   std::optional<std::string_view>{"held/package.json"} &&
               runtime.find("directional_package_path").string() ==
                   std::optional<std::string_view>{"directional/runtime.json"},
           "root runtime changed stable child manifest paths");
    expect(runtime.find("domain").find("minimum_rpm").number() ==
                   std::optional<double>{550.123456} &&
               runtime.find("domain").find("maximum_rpm").number() ==
                   std::optional<double>{6700.654321},
           "root runtime did not preserve the selected six-decimal RPM domain");

    const auto report_text = text(find_member(first, "bake-report.json").bytes);
    expect(report_text.find(std::string{kNativeResponsiveBakeReportSchemaV2}) !=
                   report_text.npos &&
               report_text.find("\"kind\": \"native-cpp\"") != report_text.npos &&
               report_text.find("wasm_sha256") == report_text.npos &&
               report_text.find("loader_sha256") == report_text.npos &&
               report_text.find("execution_runtime") == report_text.npos,
           "native v2 report is missing or retains obsolete JS/WASM identity");
    auto report_parse = authoring::parse_json(report_text);
    expect(std::holds_alternative<authoring::JsonDocument>(report_parse),
           "native v2 bake report is not JSON");
    auto report_document = std::get<authoring::JsonDocument>(std::move(report_parse));
    const auto report = report_document.root();
    const auto expected_runtime_sha256 =
        hex(contract::sha256(find_member(first, "runtime.json").bytes));
    const auto expected_descriptor_sha256 =
        hex(contract::sha256(find_member(first, "vehicleengine.json").bytes));
    expect(report.size() == 10U &&
               report.find("runtime_manifest_sha256").string() ==
                   std::optional<std::string_view>{expected_runtime_sha256} &&
               report.find("vehicleengine_descriptor_sha256").string() ==
                   std::optional<std::string_view>{expected_descriptor_sha256},
           "native v2 report does not bind exact generated manifest bytes");

    const auto verified = artifacts::verify_vehicleengine(first.vehicleengine_v1);
    expect(std::holds_alternative<artifacts::VehicleEngineContainerIndex>(verified),
           "built carrier did not pass existing VEHICLEENGINE v1 verification");
    const auto &index = std::get<artifacts::VehicleEngineContainerIndex>(verified);
    expect(index.version == artifacts::kVehicleEngineContainerVersionV1 &&
               index.entries.size() == first.members.size(),
           "built carrier changed the VEHICLEENGINE v1 contract or tree closure");
}

void test_wasm_identity_builds_the_same_portable_carrier_contract() {
    auto input = input_fixture();
    input.identity.backend.kind = std::string{kWasmResponsiveBackendKindV1};
    input.identity.backend.target = std::string{kWasmResponsiveTargetV1};
    input.identity.backend.numeric_runtime =
        std::string{kWasmResponsiveNumericRuntimeV1};
    input.identity.backend.executable_sha256 = digest("wasm-module");
    input.identity.bake_recipe_sha256 = digest("wasm-bake-recipe-v1");

    const auto encoded = encode_native_responsive_bake_identity_v1(input.identity);
    const auto *identity = std::get_if<EncodedNativeResponsiveBakeIdentityV1>(&encoded);
    expect(identity != nullptr &&
               text(identity->bytes).find("wasm-cpp") != std::string::npos,
           "WASM responsive identity was rejected or lost its backend kind");

    const auto package =
        require_package(build_native_responsive_package_v2(std::move(input)));
    expect(!package.vehicleengine_v1.empty() &&
               text(find_member(package, "bake-report.json").bytes)
                       .find("wasm32-ieee754-binary128-strict-v1") != std::string::npos,
           "WASM responsive package did not retain its numeric backend identity");
}

void test_fail_closed_topology_identity_and_cancellation() {
    auto mismatch = input_fixture();
    auto &directional = mismatch.payload_members[1].bytes;
    auto directional_text = text(directional);
    const auto second_bus = directional_text.find("exhaust.rear.dry");
    expect(second_bus != directional_text.npos, "test fixture route is absent");
    directional_text.replace(second_bus, std::string_view{"exhaust.rear.dry"}.size(),
                             "exhaust.wrong.dry");
    directional = bytes(directional_text);
    const auto topology_result =
        build_native_responsive_package_v2(std::move(mismatch));
    static_cast<void>(require_error(
        topology_result, NativeResponsivePackageErrorCode::topology_mismatch));

    auto wasm_identity = input_fixture();
    wasm_identity.identity.backend.target = "wasm32";
    const auto identity_result =
        encode_native_responsive_bake_identity_v1(wasm_identity.identity);
    const auto *identity_failure =
        std::get_if<NativeResponsivePackageError>(&identity_result);
    expect(identity_failure != nullptr &&
               identity_failure->code ==
                   NativeResponsivePackageErrorCode::invalid_identity,
           "WASM backend identity was admitted into native cache namespace");

    std::stop_source stopped;
    stopped.request_stop();
    const auto cancelled =
        build_native_responsive_package_v2(input_fixture(), stopped.get_token());
    static_cast<void>(
        require_error(cancelled, NativeResponsivePackageErrorCode::cancelled));
}

void test_optional_runtime_paths_and_native_provenance_round_trip() {
    auto input = input_fixture();
    input.runtime.motoring_package_path = "motoring/runtime.json";
    input.runtime.lifecycle_package_path = "lifecycle/runtime.json";
    input.runtime.shared_recorded_starter_package_path =
        "shared-recorded-starter/runtime.json";
    input.identity.shared_recorded_starter =
        SharedRecordedStarterIdentityV1{digest("shared-starter-tree"), 3U};
    const auto evidence_bytes = bytes("{\"verdict\":\"exact\"}\n");
    input.runtime.renderer_compatibility = ResponsiveRendererCompatibilityV1{
        digest("admitted-source-closure"),
        "evidence/renderer-compatibility-parity-report-v1.json", evidence_bytes.size(),
        contract::sha256(evidence_bytes)};
    input.payload_members.push_back(member("motoring/runtime.json", "{}\n"));
    input.payload_members.push_back(member("lifecycle/runtime.json", "{}\n"));
    input.payload_members.push_back(
        member("shared-recorded-starter/runtime.json", "{}\n"));
    input.payload_members.push_back(
        {"evidence/renderer-compatibility-parity-report-v1.json", evidence_bytes});

    const auto package =
        require_package(build_native_responsive_package_v2(std::move(input)));
    auto parsed =
        authoring::parse_json(text(find_member(package, "runtime.json").bytes));
    expect(std::holds_alternative<authoring::JsonDocument>(parsed),
           "runtime with optional package paths is not JSON");
    auto document = std::get<authoring::JsonDocument>(std::move(parsed));
    const auto runtime = document.root();
    expect(
        runtime.size() == 13U &&
            runtime.find("motoring_package_path").string() ==
                std::optional<std::string_view>{"motoring/runtime.json"} &&
            runtime.find("lifecycle_package_path").string() ==
                std::optional<std::string_view>{"lifecycle/runtime.json"} &&
            runtime.find("shared_recorded_starter_package_path").string() ==
                std::optional<std::string_view>{"shared-recorded-starter/runtime.json"},
        "optional stable runtime paths did not round-trip exactly");
    const auto compatibility = runtime.find("runtime_renderer_compatibility");
    const auto expected_capture_closure = hex(digest("source-closure"));
    expect(compatibility.find("capture_source_closure_sha256").string() ==
                   std::optional<std::string_view>{expected_capture_closure} &&
               compatibility.find("evidence_byte_count").number() ==
                   std::optional<double>{static_cast<double>(evidence_bytes.size())},
           "renderer compatibility did not bind native provenance and evidence");
    const auto report = text(find_member(package, "bake-report.json").bytes);
    expect(report.find("\"shared_recorded_starter\": {") != report.npos &&
               report.find("\"entry_count\": 3") != report.npos,
           "native v2 report did not preserve shared starter identity");
}

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() /
                               "eso-native-responsive-package-XXXXXX")
                                  .string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const auto *created = ::mkdtemp(writable.data());
        if (created == nullptr) {
            throw std::runtime_error{"could not create focused test directory"};
        }
        path_ = created;
    }
    ~TemporaryDirectory() {
        std::filesystem::remove_all(path_);
    }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error{"could not open published file"};
    }
    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error{"could not size published file"};
    }
    input.seekg(0);
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char *>(result.data()),
               static_cast<std::streamsize>(result.size()));
    if (!input && !result.empty()) {
        throw std::runtime_error{"could not read complete published file"};
    }
    return result;
}

void test_atomic_tree_and_carrier_publication() {
    const auto package =
        require_package(build_native_responsive_package_v2(input_fixture()));
    TemporaryDirectory temporary;
    const auto directory_result = publish_native_responsive_package_atomic(
        package, temporary.path(), "example-package");
    if (const auto *failure =
            std::get_if<NativeResponsivePackageError>(&directory_result)) {
        throw std::runtime_error{"atomic package publication failed: " +
                                 failure->detail_code + ": " + failure->message};
    }
    expect(
        std::holds_alternative<NativeResponsiveDirectoryPublication>(directory_result),
        "atomic package directory publication failed");
    for (const auto &entry : package.members) {
        expect(read_file(temporary.path() / "example-package" / entry.path) ==
                   entry.bytes,
               "published package member differs from built bytes");
    }
    const auto directory_conflict = publish_native_responsive_package_atomic(
        package, temporary.path(), "example-package");
    const auto *directory_failure =
        std::get_if<NativeResponsivePackageError>(&directory_conflict);
    expect(directory_failure != nullptr &&
               directory_failure->code ==
                   NativeResponsivePackageErrorCode::output_conflict,
           "directory publisher replaced an existing destination");

    const auto carrier_path = temporary.path() / "example-engine.vehicleengine";
    const auto carrier_result =
        publish_native_vehicleengine_atomic(package, carrier_path);
    expect(std::holds_alternative<NativeResponsiveCarrierPublication>(carrier_result) &&
               read_file(carrier_path) == package.vehicleengine_v1,
           "atomic carrier publication did not preserve exact bytes");
    const auto carrier_conflict =
        publish_native_vehicleengine_atomic(package, carrier_path);
    const auto *carrier_failure =
        std::get_if<NativeResponsivePackageError>(&carrier_conflict);
    expect(carrier_failure != nullptr &&
               carrier_failure->code ==
                   NativeResponsivePackageErrorCode::output_conflict,
           "carrier publisher replaced an existing destination");

    auto tampered = package;
    tampered.vehicleengine_v1.back() ^= std::byte{0x01};
    const auto tampered_result = publish_native_vehicleengine_atomic(
        tampered, temporary.path() / "tampered.vehicleengine");
    const auto *tampered_failure =
        std::get_if<NativeResponsivePackageError>(&tampered_result);
    expect(tampered_failure != nullptr &&
               !std::filesystem::exists(temporary.path() / "tampered.vehicleengine"),
           "publication admitted a tampered carrier or left an incomplete output");

    auto tampered_tree = package;
    const auto mutable_member =
        std::find_if(tampered_tree.members.begin(), tampered_tree.members.end(),
                     [](const auto &entry) { return !entry.bytes.empty(); });
    expect(mutable_member != tampered_tree.members.end(),
           "test package has no mutable package member");
    mutable_member->bytes.back() ^= std::byte{0x01};
    const auto tampered_tree_result = publish_native_responsive_package_atomic(
        tampered_tree, temporary.path(), "tampered-package");
    expect(std::holds_alternative<NativeResponsivePackageError>(tampered_tree_result) &&
               !std::filesystem::exists(temporary.path() / "tampered-package"),
           "publication admitted a tree/carrier mismatch or left a partial tree");

    std::stop_source stopped;
    stopped.request_stop();
    const auto cancelled = publish_native_vehicleengine_atomic(
        package, temporary.path() / "cancelled.vehicleengine", stopped.get_token());
    const auto *cancelled_failure =
        std::get_if<NativeResponsivePackageError>(&cancelled);
    expect(cancelled_failure != nullptr &&
               cancelled_failure->code == NativeResponsivePackageErrorCode::cancelled &&
               !std::filesystem::exists(temporary.path() / "cancelled.vehicleengine"),
           "cancelled carrier publication left an incomplete output");

    for (const auto &entry : std::filesystem::directory_iterator(temporary.path())) {
        expect(!entry.path().filename().string().starts_with(".engine-sim-offline-"),
               "publication left a private staging entry behind");
    }
}

} // namespace

int main() {
    try {
        test_native_identity_and_package_are_deterministic();
        test_wasm_identity_builds_the_same_portable_carrier_contract();
        test_fail_closed_topology_identity_and_cancellation();
        test_optional_runtime_paths_and_native_provenance_round_trip();
        test_atomic_tree_and_carrier_publication();
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
    std::cout << "native responsive package tests passed\n";
    return 0;
}
