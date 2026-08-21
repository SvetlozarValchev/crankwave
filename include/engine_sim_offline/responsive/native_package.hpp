#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

inline constexpr std::string_view kNativeResponsiveCacheIdentitySchemaV1 =
    "engine-sim-offline/native-responsive-bake-cache-identity.v1";
inline constexpr std::string_view kNativeResponsiveBakeReportSchemaV2 =
    "engine-sim-offline/responsive-audio-bake-report-v2";
inline constexpr std::string_view kResponsiveRuntimeSchemaV1 =
    "engine-sim-offline/responsive-audio-preview";
inline constexpr std::string_view kNativeResponsiveBackendKindV1 = "native-cpp";
inline constexpr std::string_view kNativeResponsiveTargetV1 = "linux-x86_64";
inline constexpr std::string_view kNativeResponsiveNumericRuntimeV1 =
    "linux-x86-64-sysv-x87-extended-strict-v1";
inline constexpr std::string_view kResponsiveRuntimePathV1 = "runtime.json";
inline constexpr std::string_view kNativeResponsiveBakeReportPathV2 =
    "bake-report.json";
inline constexpr std::uint32_t kResponsiveRuntimePhysicsRateHzV1 = 10'000U;
inline constexpr std::uint32_t kResponsiveRuntimeSampleRateHzV1 = 192'000U;
inline constexpr std::uint64_t kNativeResponsiveMaximumPackagePayloadBytes =
    UINT64_C(512) * 1024U * 1024U;

enum class NativeResponsivePackageErrorCode : std::uint8_t {
    invalid_argument,
    invalid_identity,
    invalid_member,
    duplicate_member,
    missing_member,
    malformed_child_manifest,
    topology_mismatch,
    resource_limit,
    cancelled,
    pack_failure,
    publication_failure,
    output_conflict,
    unsupported_platform,
};

struct NativeResponsivePackageError {
    NativeResponsivePackageErrorCode code =
        NativeResponsivePackageErrorCode::invalid_argument;
    std::string detail_code;
    std::string path;
    std::string message;

    friend bool operator==(const NativeResponsivePackageError &,
                           const NativeResponsivePackageError &) = default;
};

struct ResolvedResponsiveAssetIdentity {
    std::string kind;
    std::string id;
    contract::Sha256Digest sha256;

    friend bool operator==(const ResolvedResponsiveAssetIdentity &,
                           const ResolvedResponsiveAssetIdentity &) = default;
};

struct SharedRecordedStarterIdentityV1 {
    contract::Sha256Digest aggregate_sha256;
    std::uint32_t entry_count = 0;

    friend bool operator==(const SharedRecordedStarterIdentityV1 &,
                           const SharedRecordedStarterIdentityV1 &) = default;
};

// Every field is part of the native cache boundary. Native and WASM/Node products
// intentionally cannot share cache entries, even if their package payloads happen
// to be byte-identical.
struct NativeResponsiveBackendIdentityV1 {
    std::string release_identity;
    std::uint32_t c_api_version = 0;
    std::string target = std::string{kNativeResponsiveTargetV1};
    std::string numeric_runtime = std::string{kNativeResponsiveNumericRuntimeV1};
    contract::Sha256Digest executable_sha256;
    contract::Sha256Digest source_closure_sha256;
    contract::Sha256Digest method_registry_sha256;

    friend bool operator==(const NativeResponsiveBackendIdentityV1 &,
                           const NativeResponsiveBackendIdentityV1 &) = default;
};

struct NativeResponsiveBakeIdentityInputV1 {
    NativeResponsiveBackendIdentityV1 backend;
    std::string engine_id;
    contract::Sha256Digest engine_source_sha256;
    std::string profile_id;
    contract::Sha256Digest profile_sha256;
    contract::Sha256Digest bake_recipe_sha256;
    contract::Sha256Digest builtin_asset_catalog_sha256;
    std::vector<ResolvedResponsiveAssetIdentity> resolved_assets;
    std::optional<SharedRecordedStarterIdentityV1> shared_recorded_starter;

    friend bool operator==(const NativeResponsiveBakeIdentityInputV1 &,
                           const NativeResponsiveBakeIdentityInputV1 &) = default;
};

struct EncodedNativeResponsiveBakeIdentityV1 {
    std::vector<std::byte> bytes;
    contract::Sha256Digest sha256;

    friend bool operator==(const EncodedNativeResponsiveBakeIdentityV1 &,
                           const EncodedNativeResponsiveBakeIdentityV1 &) = default;
};

struct ResponsiveRendererCompatibilityV1 {
    contract::Sha256Digest admitted_source_closure_sha256;
    std::string evidence_path;
    std::uint64_t evidence_byte_count = 0;
    contract::Sha256Digest evidence_sha256;

    friend bool operator==(const ResponsiveRendererCompatibilityV1 &,
                           const ResponsiveRendererCompatibilityV1 &) = default;
};

// dry_bus_ids is validation-only package metadata. It is deliberately not emitted
// as a root runtime.json member: the accepted root schema binds the audition bus in
// audio.bus_id, while held and directional child manifests own dry-route topology.
struct ResponsiveRuntimeInputV1 {
    std::string engine_id;
    contract::Sha256Digest compiled_engine_provenance_sha256;
    std::uint32_t physics_rate_hz = kResponsiveRuntimePhysicsRateHzV1;
    double minimum_rpm = 0.0;
    double maximum_rpm = 0.0;
    bool canonical_offline_bake = false;
    std::string audition_bus_id;
    std::vector<std::string> dry_bus_ids;
    std::string held_package_path = "held/package.json";
    std::string directional_package_path = "directional/runtime.json";
    std::optional<std::string> motoring_package_path;
    std::optional<std::string> lifecycle_package_path;
    std::optional<std::string> shared_recorded_starter_package_path;
    std::string representation =
        "exact-held-non-repeating-texture-plus-n-route-directional-transients-"
        "through-authored-presentation";
    std::optional<ResponsiveRendererCompatibilityV1> renderer_compatibility;

    friend bool operator==(const ResponsiveRuntimeInputV1 &,
                           const ResponsiveRuntimeInputV1 &) = default;
};

struct PortableResponsivePackageMember {
    std::string path;
    std::vector<std::byte> bytes;

    friend bool operator==(const PortableResponsivePackageMember &,
                           const PortableResponsivePackageMember &) = default;
};

struct NativeResponsivePackageInputV2 {
    NativeResponsiveBakeIdentityInputV1 identity;
    ResponsiveRuntimeInputV1 runtime;
    std::vector<PortableResponsivePackageMember> payload_members;
};

// Owns both canonical sorted package members and the deterministic VEHICLEENGINE v1
// carrier. All spans passed into the existing packer expire before this value is
// returned.
struct NativeResponsivePackageV2 {
    EncodedNativeResponsiveBakeIdentityV1 cache_identity;
    std::vector<PortableResponsivePackageMember> members;
    std::vector<std::byte> vehicleengine_v1;
    contract::Sha256Digest vehicleengine_sha256;
};

using EncodedNativeResponsiveBakeIdentityResultV1 =
    std::variant<EncodedNativeResponsiveBakeIdentityV1, NativeResponsivePackageError>;
using NativeResponsivePackageBuildResultV2 =
    std::variant<NativeResponsivePackageV2, NativeResponsivePackageError>;

[[nodiscard]] EncodedNativeResponsiveBakeIdentityResultV1
encode_native_responsive_bake_identity_v1(
    const NativeResponsiveBakeIdentityInputV1 &input, std::stop_token stop_token = {});

[[nodiscard]] NativeResponsivePackageBuildResultV2
build_native_responsive_package_v2(NativeResponsivePackageInputV2 input,
                                   std::stop_token stop_token = {});

} // namespace engine_sim_offline::responsive
