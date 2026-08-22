#pragma once

#include "crankwave/responsive/directional_cook.hpp"
#include "crankwave/responsive/held_texture.hpp"
#include "crankwave/responsive/lifecycle.hpp"
#include "crankwave/responsive/native_package.hpp"
#include "crankwave/responsive/presentation_transfer.hpp"
#include "crankwave/responsive/profile.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

namespace crankwave::responsive {

inline constexpr std::string_view kResponsiveHeldPackagePathV1 = "held/package.json";
inline constexpr std::string_view kResponsiveDirectionalPackagePathV1 =
    "directional/runtime.json";
inline constexpr std::string_view kResponsiveLifecyclePackagePathV1 =
    "lifecycle/runtime.json";
inline constexpr std::string_view kResponsiveSharedRecordedStarterPackagePathV1 =
    "shared-recorded-starter/runtime.json";

struct ResponsivePackageProvenanceV1 {
    std::string engine_id;
    contract::Sha256Digest compiled_engine_sha256;
    std::string renderer_build_id = "crankwave-renderer-build";
    contract::Sha256Digest renderer_source_sha256;

    friend bool operator==(const ResponsivePackageProvenanceV1 &,
                           const ResponsivePackageProvenanceV1 &) = default;
};

enum class ResponsiveOptionalChildRole : std::uint8_t {
    motoring,
    lifecycle,
    shared_recorded_starter,
};

// This is a typed attachment boundary, not an arbitrary package-member escape
// hatch. The encoder checks the role's exact schema, root path, engine and
// provenance bindings, dry-route topology where applicable, and every member
// path before it admits the child into the package transaction.
struct ResponsiveOptionalChildPackageV1 {
    ResponsiveOptionalChildRole role = ResponsiveOptionalChildRole::lifecycle;
    std::string runtime_path;
    std::string engine_id;
    contract::Sha256Digest compiled_engine_sha256;
    std::string renderer_build_id = "crankwave-renderer-build";
    contract::Sha256Digest renderer_source_sha256;
    std::vector<std::string> dry_bus_ids;
    std::vector<PortableResponsivePackageMember> members;
};

// The pointed-to cooked values only need to outlive one encoder/coordinator call.
// Keeping this as a view prevents another complete copy of the large held and
// directional Float32 payloads before they become portable package members.
struct ResponsivePackageChildrenViewV1 {
    const ResponsiveBakeProfile *profile = nullptr;
    const HeldCookedGrid *held = nullptr;
    const DirectionalCookedModel *directional = nullptr;
    const ResponsiveCompiledPresentation *presentation = nullptr;
    ResponsivePackageProvenanceV1 provenance;
    bool canonical_offline_bake = false;
    std::vector<ResponsiveOptionalChildPackageV1> optional_children;
};

struct EncodedResponsivePackageChildrenV1 {
    ResponsiveRuntimeInputV1 runtime;
    ResponsivePackageProvenanceV1 provenance;
    std::vector<PortableResponsivePackageMember> members;
    contract::Sha256Digest held_manifest_sha256;
};

using ResponsivePackageChildrenResultV1 =
    std::variant<EncodedResponsivePackageChildrenV1, NativeResponsivePackageError>;

using ResponsiveOptionalChildEncodeResultV1 =
    std::variant<ResponsiveOptionalChildPackageV1, NativeResponsivePackageError>;

// Encodes the frozen lifecycle cooked aggregate into the existing playback package
// schema. The exact held manifest digest is attached through the lifecycle startup
// admission evidence and runtime presentation before either document is hashed.
[[nodiscard]] ResponsiveOptionalChildEncodeResultV1
encode_responsive_lifecycle_child_v1(const LifecycleCookedPackage &lifecycle,
                                     const ResponsivePackageProvenanceV1 &provenance,
                                     const contract::Sha256Digest &held_manifest_sha256,
                                     std::stop_token stop_token = {});

// The installed distribution owns locating these bytes. This pure boundary admits
// only the existing CC0 two-member starter package, validates the complete
// manifest/PCM/rights contract, and computes the same canonical tree identity used
// by the responsive bake cache.
struct ResponsiveSharedRecordedStarterBytesV1 {
    std::span<const std::byte> runtime_json;
    std::span<const std::byte> audio_payload;
};

struct EncodedResponsiveSharedRecordedStarterV1 {
    ResponsiveOptionalChildPackageV1 child;
    SharedRecordedStarterIdentityV1 identity;
};

using ResponsiveSharedRecordedStarterEncodeResultV1 =
    std::variant<EncodedResponsiveSharedRecordedStarterV1,
                 NativeResponsivePackageError>;

[[nodiscard]] ResponsiveSharedRecordedStarterEncodeResultV1
encode_responsive_shared_recorded_starter_v1(
    const ResponsiveSharedRecordedStarterBytesV1 &input,
    const ResponsivePackageProvenanceV1 &package_provenance,
    std::stop_token stop_token = {});

[[nodiscard]] ResponsivePackageChildrenResultV1
encode_responsive_package_children_v1(const ResponsivePackageChildrenViewV1 &input,
                                      std::stop_token stop_token = {});

// Supports the lifecycle dependency without re-encoding the large held and
// directional payloads: encode the core with no optional children, construct the
// lifecycle child against held_manifest_sha256, then attach it here.
[[nodiscard]] ResponsivePackageChildrenResultV1 attach_responsive_optional_children_v1(
    EncodedResponsivePackageChildrenV1 core,
    std::vector<ResponsiveOptionalChildPackageV1> optional_children,
    std::stop_token stop_token = {});

struct NativeResponsiveCookedPackageInputV2 {
    NativeResponsiveBakeIdentityInputV1 identity;
    ResponsivePackageChildrenViewV1 children;
    std::optional<ResponsiveRendererCompatibilityV1> renderer_compatibility;
};

struct NativeResponsiveCookedPackageV2 {
    NativeResponsivePackageV2 package;
    contract::Sha256Digest held_manifest_sha256;
};

using NativeResponsiveCookedPackageBuildResultV2 =
    std::variant<NativeResponsiveCookedPackageV2, NativeResponsivePackageError>;

struct NativeResponsiveEncodedPackageInputV2 {
    NativeResponsiveBakeIdentityInputV1 identity;
    EncodedResponsivePackageChildrenV1 children;
    std::optional<ResponsiveRendererCompatibilityV1> renderer_compatibility;
};

[[nodiscard]] NativeResponsiveCookedPackageBuildResultV2
build_native_responsive_package_from_encoded_v2(
    NativeResponsiveEncodedPackageInputV2 input, std::stop_token stop_token = {});

[[nodiscard]] NativeResponsiveCookedPackageBuildResultV2
build_native_responsive_package_from_cooked_v2(
    NativeResponsiveCookedPackageInputV2 input, std::stop_token stop_token = {});

} // namespace crankwave::responsive
