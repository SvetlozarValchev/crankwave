#include "engine_sim_offline/responsive/native_package.hpp"

#include "engine_sim_offline/artifacts/vehicleengine_container.hpp"
#include "engine_sim_offline/artifacts/vehicleengine_package.hpp"
#include "engine_sim_offline/authoring/json.hpp"
#include "engine_sim_offline/c_api.h"

#include "../numeric/target_extended_precision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace engine_sim_offline::responsive {
namespace {

using Error = NativeResponsivePackageError;
using ErrorCode = NativeResponsivePackageErrorCode;

constexpr std::size_t kMaximumIdentityTextBytes = 128U;
constexpr std::size_t kMaximumChildManifestBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumResolvedAssetCount = 8'192U;
constexpr std::size_t kMaximumDryBusCount = 256U;
constexpr std::uint64_t kRpmSerializationScale = 1'000'000U;
constexpr double kMaximumResponsiveRpm = 1'000'000.0;

#if defined(__linux__) && defined(__x86_64__)
static_assert(numeric::kTargetExtendedPrecisionIdentity ==
              kNativeResponsiveNumericRuntimeV1);
#endif

[[nodiscard]] Error error(ErrorCode code, std::string detail_code, std::string path,
                          std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] Error cancelled_error() {
    return error(ErrorCode::cancelled, "native-responsive-bake-cancelled", "",
                 "native responsive package construction was cancelled");
}

[[nodiscard]] bool valid_identity_text(const std::string_view value) noexcept {
    return value.size() <= kMaximumIdentityTextBytes &&
           contract::is_valid_semantic_id(value) && value.find('/') == value.npos;
}

[[nodiscard]] bool valid_release_identity(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaximumIdentityTextBytes) {
        return false;
    }
    std::size_t cursor = 0U;
    const auto digits = [&]() {
        const auto begin = cursor;
        while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
            ++cursor;
        }
        return cursor != begin;
    };
    for (std::size_t component = 0U; component < 3U; ++component) {
        if (!digits()) {
            return false;
        }
        if (component != 2U) {
            if (cursor == value.size() || value[cursor] != '.') {
                return false;
            }
            ++cursor;
        }
    }
    if (cursor == value.size()) {
        return true;
    }
    if (value[cursor] != '-' && value[cursor] != '+') {
        return false;
    }
    ++cursor;
    if (cursor == value.size() || !((value[cursor] >= '0' && value[cursor] <= '9') ||
                                    (value[cursor] >= 'a' && value[cursor] <= 'z') ||
                                    (value[cursor] >= 'A' && value[cursor] <= 'Z'))) {
        return false;
    }
    for (; cursor < value.size(); ++cursor) {
        const auto character = value[cursor];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') || character == '.' ||
              character == '+' || character == '-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_bus_id(const std::string_view value) noexcept {
    return value.size() <= kMaximumIdentityTextBytes &&
           contract::is_valid_semantic_id(value) && value.find('/') == value.npos;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto value : digest.bytes) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

void append_json_string(std::string &output, const std::string_view value) {
    constexpr std::string_view digits = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output.append("\\\"");
            break;
        case '\\':
            output.append("\\\\");
            break;
        case '\b':
            output.append("\\b");
            break;
        case '\f':
            output.append("\\f");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\r':
            output.append("\\r");
            break;
        case '\t':
            output.append("\\t");
            break;
        default:
            if (character < 0x20U) {
                output.append("\\u00");
                output.push_back(digits[character >> 4U]);
                output.push_back(digits[character & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void append_indent(std::string &output, const std::size_t count) {
    output.append(count, ' ');
}

void append_key(std::string &output, const std::size_t indent,
                const std::string_view key) {
    append_indent(output, indent);
    append_json_string(output, key);
    output.append(": ");
}

void append_string_field(std::string &output, const std::size_t indent,
                         const std::string_view key, const std::string_view value,
                         const bool comma = true) {
    append_key(output, indent, key);
    append_json_string(output, value);
    output.append(comma ? ",\n" : "\n");
}

void append_integer_field(std::string &output, const std::size_t indent,
                          const std::string_view key, const std::uint64_t value,
                          const bool comma = true) {
    append_key(output, indent, key);
    output.append(std::to_string(value));
    output.append(comma ? ",\n" : "\n");
}

void append_bool_field(std::string &output, const std::size_t indent,
                       const std::string_view key, const bool value,
                       const bool comma = true) {
    append_key(output, indent, key);
    output.append(value ? "true" : "false");
    output.append(comma ? ",\n" : "\n");
}

[[nodiscard]] bool canonical_rpm(const double value) noexcept {
    return std::isfinite(value) && value > 0.0 && value <= kMaximumResponsiveRpm &&
           std::round(value * static_cast<double>(kRpmSerializationScale)) /
                   static_cast<double>(kRpmSerializationScale) ==
               value;
}

void append_rpm_field(std::string &output, const std::size_t indent,
                      const std::string_view key, const double value,
                      const bool comma = true) {
    const auto scaled = static_cast<std::uint64_t>(
        std::llround(value * static_cast<double>(kRpmSerializationScale)));
    const auto whole = scaled / kRpmSerializationScale;
    auto fraction = scaled % kRpmSerializationScale;
    append_key(output, indent, key);
    output.append(std::to_string(whole));
    if (fraction != 0U) {
        std::array<char, 6U> digits{};
        for (std::size_t index = digits.size(); index > 0U; --index) {
            digits[index - 1U] = static_cast<char>('0' + fraction % 10U);
            fraction /= 10U;
        }
        auto length = digits.size();
        while (length > 0U && digits[length - 1U] == '0') {
            --length;
        }
        output.push_back('.');
        output.append(digits.data(), length);
    }
    output.append(comma ? ",\n" : "\n");
}

[[nodiscard]] std::vector<std::byte> bytes_from_string(std::string value) {
    std::vector<std::byte> result(value.size());
    std::transform(value.begin(), value.end(), result.begin(), [](const char byte) {
        return static_cast<std::byte>(static_cast<unsigned char>(byte));
    });
    return result;
}

[[nodiscard]] std::string_view
string_view_of(const std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::string
encode_cache_identity(const NativeResponsiveBakeIdentityInputV1 &input,
                      const std::vector<ResolvedResponsiveAssetIdentity> &assets) {
    std::string output;
    output.reserve(1024U + assets.size() * 192U);
    output.append("{\"schema\":");
    append_json_string(output, kNativeResponsiveCacheIdentitySchemaV1);
    output.append(",\"backend\":{\"kind\":");
    append_json_string(output, kNativeResponsiveBackendKindV1);
    output.append(",\"release_identity\":");
    append_json_string(output, input.backend.release_identity);
    output.append(",\"c_api_version\":");
    output.append(std::to_string(input.backend.c_api_version));
    output.append(",\"target\":");
    append_json_string(output, input.backend.target);
    output.append(",\"numeric_runtime\":");
    append_json_string(output, input.backend.numeric_runtime);
    output.append(",\"executable_sha256\":");
    append_json_string(output, digest_hex(input.backend.executable_sha256));
    output.append(",\"source_closure_sha256\":");
    append_json_string(output, digest_hex(input.backend.source_closure_sha256));
    output.append(",\"method_registry_sha256\":");
    append_json_string(output, digest_hex(input.backend.method_registry_sha256));
    output.append("},\"engine\":{\"id\":");
    append_json_string(output, input.engine_id);
    output.append(",\"source_sha256\":");
    append_json_string(output, digest_hex(input.engine_source_sha256));
    output.append("},\"profile\":{\"id\":");
    append_json_string(output, input.profile_id);
    output.append(",\"sha256\":");
    append_json_string(output, digest_hex(input.profile_sha256));
    output.append("},\"bake_recipe_sha256\":");
    append_json_string(output, digest_hex(input.bake_recipe_sha256));
    output.append(",\"builtin_asset_catalog_sha256\":");
    append_json_string(output, digest_hex(input.builtin_asset_catalog_sha256));
    output.append(",\"resolved_assets\":[");
    for (std::size_t index = 0; index < assets.size(); ++index) {
        if (index != 0U) {
            output.push_back(',');
        }
        output.append("{\"kind\":");
        append_json_string(output, assets[index].kind);
        output.append(",\"id\":");
        append_json_string(output, assets[index].id);
        output.append(",\"sha256\":");
        append_json_string(output, digest_hex(assets[index].sha256));
        output.push_back('}');
    }
    output.append("],\"shared_recorded_starter\":");
    if (input.shared_recorded_starter.has_value()) {
        output.append("{\"aggregate_sha256\":");
        append_json_string(output,
                           digest_hex(input.shared_recorded_starter->aggregate_sha256));
        output.append(",\"entry_count\":");
        output.append(std::to_string(input.shared_recorded_starter->entry_count));
        output.push_back('}');
    } else {
        output.append("null");
    }
    output.append("}\n");
    return output;
}

[[nodiscard]] std::optional<Error>
validate_identity(const NativeResponsiveBakeIdentityInputV1 &input,
                  std::vector<ResolvedResponsiveAssetIdentity> &ordered_assets,
                  const std::stop_token stop_token) {
#if !defined(__linux__) || !defined(__x86_64__)
    static_cast<void>(input);
    static_cast<void>(ordered_assets);
    static_cast<void>(stop_token);
    return error(ErrorCode::unsupported_platform,
                 "native-responsive-backend-platform-unsupported", "/backend",
                 "the native responsive backend is admitted only on Linux x86-64");
#else
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (!numeric::target_extended_precision_format_is_admitted()) {
        return error(
            ErrorCode::unsupported_platform,
            "native-responsive-numeric-runtime-unavailable", "/backend",
            "compiled long-double format does not match the native backend identity");
    }
    if (!valid_release_identity(input.backend.release_identity)) {
        return error(
            ErrorCode::invalid_identity, "native-responsive-release-identity-invalid",
            "/backend/release_identity",
            "release identity must match the installed semantic release grammar");
    }
    if (input.backend.c_api_version != ESO_C_API_VERSION) {
        return error(
            ErrorCode::invalid_identity, "native-responsive-c-api-version-mismatch",
            "/backend/c_api_version",
            "native backend identity must use the current exact C API version");
    }
    if (input.backend.target != kNativeResponsiveTargetV1 ||
        input.backend.numeric_runtime != kNativeResponsiveNumericRuntimeV1) {
        return error(
            ErrorCode::invalid_identity, "native-responsive-numeric-backend-mismatch",
            "/backend",
            "native backend target or numeric runtime identity is unsupported");
    }
    if (input.backend.executable_sha256.is_zero() ||
        input.backend.source_closure_sha256.is_zero() ||
        input.backend.method_registry_sha256.is_zero()) {
        return error(ErrorCode::invalid_identity,
                     "native-responsive-backend-digest-missing", "/backend",
                     "every native backend digest must be nonzero");
    }
    if (!artifacts::is_vehicleengine_engine_id(input.engine_id)) {
        return error(ErrorCode::invalid_identity, "native-responsive-engine-id-invalid",
                     "/engine/id",
                     "engine ID is not a portable VEHICLEENGINE engine identity");
    }
    if (!valid_identity_text(input.profile_id)) {
        return error(ErrorCode::invalid_identity,
                     "native-responsive-profile-id-invalid", "/profile/id",
                     "profile ID must be a bounded portable semantic ID");
    }
    if (input.engine_source_sha256.is_zero() || input.profile_sha256.is_zero() ||
        input.bake_recipe_sha256.is_zero() ||
        input.builtin_asset_catalog_sha256.is_zero()) {
        return error(ErrorCode::invalid_identity,
                     "native-responsive-input-digest-missing", "/",
                     "engine, profile, recipe, and catalog digests must be nonzero");
    }
    if (input.shared_recorded_starter.has_value() &&
        (input.shared_recorded_starter->aggregate_sha256.is_zero() ||
         input.shared_recorded_starter->entry_count == 0U)) {
        return error(ErrorCode::invalid_identity,
                     "native-responsive-starter-identity-invalid",
                     "/shared_recorded_starter",
                     "shared starter aggregate digest and entry count must be nonzero");
    }
    if (input.resolved_assets.size() > kMaximumResolvedAssetCount) {
        return error(ErrorCode::resource_limit,
                     "native-responsive-asset-identity-limit-exceeded",
                     "/resolved_assets",
                     "resolved asset identity count exceeds its fixed native bound");
    }
    ordered_assets = input.resolved_assets;
    for (std::size_t index = 0; index < ordered_assets.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &asset = ordered_assets[index];
        if (!valid_identity_text(asset.kind) || !valid_identity_text(asset.id) ||
            asset.sha256.is_zero()) {
            return error(ErrorCode::invalid_identity,
                         "native-responsive-asset-identity-invalid",
                         "/resolved_assets/" + std::to_string(index),
                         "resolved asset identity is incomplete or nonportable");
        }
    }
    std::sort(ordered_assets.begin(), ordered_assets.end(),
              [](const auto &left, const auto &right) {
                  if (left.kind != right.kind) {
                      return left.kind < right.kind;
                  }
                  if (left.id != right.id) {
                      return left.id < right.id;
                  }
                  return left.sha256.bytes < right.sha256.bytes;
              });
    for (std::size_t index = 1; index < ordered_assets.size(); ++index) {
        if (ordered_assets[index - 1].kind == ordered_assets[index].kind &&
            ordered_assets[index - 1].id == ordered_assets[index].id) {
            return error(ErrorCode::invalid_identity,
                         "native-responsive-asset-identity-duplicate",
                         "/resolved_assets",
                         "resolved asset kind and ID pairs must be unique");
        }
    }
    return std::nullopt;
#endif
}

[[nodiscard]] const PortableResponsivePackageMember *
find_member(const std::vector<PortableResponsivePackageMember> &members,
            const std::string_view path) noexcept {
    const auto candidate = std::lower_bound(
        members.begin(), members.end(), path,
        [](const PortableResponsivePackageMember &member,
           const std::string_view requested) { return member.path < requested; });
    return candidate != members.end() && candidate->path == path ? &*candidate
                                                                 : nullptr;
}

[[nodiscard]] std::optional<Error>
require_string_array(const authoring::JsonValue value,
                     const std::span<const std::string> expected,
                     const std::string_view path) {
    if (value.kind() != authoring::JsonKind::array || value.size() != expected.size()) {
        return error(ErrorCode::topology_mismatch,
                     "native-responsive-child-route-count-mismatch", std::string{path},
                     "child route array does not match the declared ordered dry buses");
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto text = value.at(index).string();
        if (!text.has_value() || *text != expected[index]) {
            return error(
                ErrorCode::topology_mismatch,
                "native-responsive-child-route-order-mismatch",
                std::string{path} + "/" + std::to_string(index),
                "child route array does not preserve the declared dry-bus order");
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error> require_object_id_array(
    const authoring::JsonValue value, const std::string_view member_name,
    const std::span<const std::string> expected, const std::string_view path) {
    if (value.kind() != authoring::JsonKind::array || value.size() != expected.size()) {
        return error(
            ErrorCode::topology_mismatch,
            "native-responsive-child-route-count-mismatch", std::string{path},
            "child route manifest count does not match the declared dry buses");
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto object = value.at(index);
        const auto text = object.find(member_name).string();
        if (object.kind() != authoring::JsonKind::object || !text.has_value() ||
            *text != expected[index]) {
            return error(
                ErrorCode::topology_mismatch,
                "native-responsive-child-route-order-mismatch",
                std::string{path} + "/" + std::to_string(index) + "/" +
                    std::string{member_name},
                "child route manifests do not preserve the declared dry-bus order");
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
validate_child_manifest(const PortableResponsivePackageMember &member,
                        const std::string_view schema,
                        const ResponsiveRuntimeInputV1 &runtime, const bool held,
                        const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (member.bytes.empty() || member.bytes.size() > kMaximumChildManifestBytes) {
        return error(ErrorCode::malformed_child_manifest,
                     "native-responsive-child-manifest-size-invalid", member.path,
                     "child manifest is empty or exceeds its bounded JSON size");
    }
    auto parsed =
        authoring::parse_json(string_view_of(member.bytes),
                              {.maximum_input_bytes = kMaximumChildManifestBytes,
                               .maximum_depth = 64U,
                               .maximum_nodes = 524'288U});
    if (const auto *failure = std::get_if<authoring::JsonParseError>(&parsed)) {
        return error(ErrorCode::malformed_child_manifest,
                     "native-responsive-child-manifest-json-malformed", member.path,
                     "child manifest JSON is malformed at byte " +
                         std::to_string(failure->location.byte_offset));
    }
    auto document = std::get<authoring::JsonDocument>(std::move(parsed));
    const auto root = document.root();
    const auto actual_schema = root.find("schema").string();
    const auto actual_engine = root.find("engine").string();
    if (root.kind() != authoring::JsonKind::object || !actual_schema.has_value() ||
        *actual_schema != schema || !actual_engine.has_value() ||
        *actual_engine != runtime.engine_id) {
        return error(ErrorCode::malformed_child_manifest,
                     "native-responsive-child-manifest-identity-mismatch", member.path,
                     "child manifest schema or engine identity is inconsistent");
    }
    if (const auto failure =
            require_string_array(root.find("dry_bus_ids"), runtime.dry_bus_ids,
                                 member.path + "/dry_bus_ids")) {
        return *failure;
    }
    if (const auto failure = require_object_id_array(
            root.find("route_manifests"), "bus_id", runtime.dry_bus_ids,
            member.path + "/route_manifests")) {
        return *failure;
    }
    if (!held) {
        return std::nullopt;
    }
    const auto presentation = root.find("presentation");
    const auto audition = presentation.find("audition_bus_id").string();
    if (presentation.kind() != authoring::JsonKind::object || !audition.has_value() ||
        *audition != runtime.audition_bus_id) {
        return error(ErrorCode::topology_mismatch,
                     "native-responsive-audition-bus-mismatch",
                     member.path + "/presentation/audition_bus_id",
                     "held presentation audition bus does not match root audio.bus_id");
    }
    if (const auto failure = require_string_array(
            presentation.find("audition_dry_bus_order"), runtime.dry_bus_ids,
            member.path + "/presentation/audition_dry_bus_order")) {
        return *failure;
    }
    return require_object_id_array(presentation.find("routes"), "dry_bus_id",
                                   runtime.dry_bus_ids,
                                   member.path + "/presentation/routes");
}

[[nodiscard]] std::optional<Error>
validate_runtime_input(const ResponsiveRuntimeInputV1 &runtime,
                       const NativeResponsiveBakeIdentityInputV1 &identity,
                       const std::vector<PortableResponsivePackageMember> &members,
                       const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (runtime.engine_id != identity.engine_id ||
        !artifacts::is_vehicleengine_engine_id(runtime.engine_id) ||
        runtime.compiled_engine_provenance_sha256.is_zero()) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-runtime-engine-identity-invalid", "/runtime",
                     "runtime engine identity or compiled provenance is invalid");
    }
    if (runtime.physics_rate_hz != kResponsiveRuntimePhysicsRateHzV1 ||
        !canonical_rpm(runtime.minimum_rpm) || !canonical_rpm(runtime.maximum_rpm) ||
        runtime.maximum_rpm <= runtime.minimum_rpm || runtime.canonical_offline_bake) {
        return error(
            ErrorCode::invalid_argument, "native-responsive-runtime-domain-invalid",
            "/runtime",
            "runtime rates/domain are invalid or claim canonical offline fidelity");
    }
    if (!valid_bus_id(runtime.audition_bus_id) || runtime.dry_bus_ids.empty() ||
        runtime.dry_bus_ids.size() > kMaximumDryBusCount) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-runtime-bus-invalid", "/runtime/audio",
                     "audition and dry bus identities must be nonempty portable IDs");
    }
    std::set<std::string_view> buses;
    for (std::size_t index = 0; index < runtime.dry_bus_ids.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (!valid_bus_id(runtime.dry_bus_ids[index]) ||
            !buses.insert(runtime.dry_bus_ids[index]).second) {
            return error(ErrorCode::invalid_argument,
                         "native-responsive-runtime-dry-bus-invalid",
                         "/runtime/dry_bus_ids/" + std::to_string(index),
                         "dry bus identities must be unique bounded portable IDs");
        }
    }
    if (!valid_identity_text(runtime.representation)) {
        return error(ErrorCode::invalid_argument,
                     "native-responsive-runtime-representation-invalid",
                     "/runtime/provenance/representation",
                     "runtime representation must be a bounded portable semantic ID");
    }

    std::vector<std::pair<std::string_view, std::string_view>> declared_paths{
        {"/runtime/held_package_path", runtime.held_package_path},
        {"/runtime/directional_package_path", runtime.directional_package_path},
    };
    const auto add_optional_path = [&](const std::string_view label,
                                       const std::optional<std::string> &path) {
        if (path.has_value()) {
            declared_paths.emplace_back(label, *path);
        }
    };
    add_optional_path("/runtime/motoring_package_path", runtime.motoring_package_path);
    add_optional_path("/runtime/lifecycle_package_path",
                      runtime.lifecycle_package_path);
    add_optional_path("/runtime/shared_recorded_starter_package_path",
                      runtime.shared_recorded_starter_package_path);
    std::set<std::string_view> unique_paths;
    for (const auto &[label, path] : declared_paths) {
        if (!artifacts::is_portable_vehicleengine_path(path) ||
            path == artifacts::kVehicleEnginePackageDescriptorPath ||
            path == kResponsiveRuntimePathV1 ||
            path == kNativeResponsiveBakeReportPathV2 ||
            !unique_paths.insert(path).second) {
            return error(
                ErrorCode::invalid_argument, "native-responsive-runtime-path-invalid",
                std::string{label},
                "runtime package paths must be distinct portable payload paths");
        }
        if (find_member(members, path) == nullptr) {
            return error(ErrorCode::missing_member,
                         "native-responsive-runtime-member-missing", std::string{path},
                         "runtime declares a package member that is absent");
        }
    }

    if (runtime.shared_recorded_starter_package_path.has_value() !=
        identity.shared_recorded_starter.has_value()) {
        return error(
            ErrorCode::invalid_argument, "native-responsive-starter-identity-mismatch",
            "/shared_recorded_starter",
            "shared starter path and report identity must be supplied together");
    }

    if (runtime.renderer_compatibility.has_value()) {
        const auto &compatibility = *runtime.renderer_compatibility;
        if (compatibility.admitted_source_closure_sha256.is_zero() ||
            compatibility.admitted_source_closure_sha256 ==
                identity.backend.source_closure_sha256 ||
            compatibility.evidence_byte_count == 0U ||
            compatibility.evidence_sha256.is_zero() ||
            !artifacts::is_portable_vehicleengine_path(compatibility.evidence_path) ||
            !unique_paths.insert(compatibility.evidence_path).second) {
            return error(
                ErrorCode::invalid_identity,
                "native-responsive-renderer-compatibility-invalid",
                "/runtime/runtime_renderer_compatibility",
                "renderer compatibility identity is incomplete or self-referential");
        }
        const auto *evidence = find_member(members, compatibility.evidence_path);
        if (evidence == nullptr ||
            evidence->bytes.size() != compatibility.evidence_byte_count ||
            contract::sha256(evidence->bytes) != compatibility.evidence_sha256) {
            return error(
                ErrorCode::invalid_member,
                "native-responsive-renderer-evidence-mismatch",
                compatibility.evidence_path,
                "renderer compatibility evidence bytes do not match their descriptor");
        }
    }

    const auto *held = find_member(members, runtime.held_package_path);
    const auto *directional = find_member(members, runtime.directional_package_path);
    if (held == nullptr || directional == nullptr) {
        return error(ErrorCode::missing_member,
                     "native-responsive-core-manifest-missing", "/runtime",
                     "held and directional manifests are both required");
    }
    if (const auto failure = validate_child_manifest(
            *held, "engine-sim-offline/responsive-audio-held-texture", runtime, true,
            stop_token)) {
        return *failure;
    }
    return validate_child_manifest(
        *directional, "engine-sim-offline/responsive-audio-directional-texture",
        runtime, false, stop_token);
}

[[nodiscard]] std::string
encode_runtime(const ResponsiveRuntimeInputV1 &runtime,
               const NativeResponsiveBackendIdentityV1 &backend) {
    std::string output;
    output.reserve(2048U);
    output.append("{\n");
    append_string_field(output, 2U, "schema", kResponsiveRuntimeSchemaV1);
    append_string_field(output, 2U, "id", runtime.engine_id + "-responsive-audio");
    append_string_field(output, 2U, "engine", runtime.engine_id);
    append_key(output, 2U, "fidelity");
    output.append("{\n");
    append_string_field(output, 4U, "purpose",
                        "interactive-source-b-versus-source-a-audition-preview");
    append_integer_field(output, 4U, "physics_rate_hz", runtime.physics_rate_hz);
    append_bool_field(output, 4U, "canonical_offline_bake",
                      runtime.canonical_offline_bake, false);
    output.append("  },\n");
    append_key(output, 2U, "audio");
    output.append("{\n");
    append_integer_field(output, 4U, "sample_rate_hz",
                         kResponsiveRuntimeSampleRateHzV1);
    append_string_field(output, 4U, "encoding", "float32le");
    append_string_field(output, 4U, "channel_layout", "mono");
    append_string_field(output, 4U, "bus_id", runtime.audition_bus_id, false);
    output.append("  },\n");
    append_string_field(output, 2U, "held_package_path", runtime.held_package_path);
    append_string_field(output, 2U, "directional_package_path",
                        runtime.directional_package_path);
    if (runtime.motoring_package_path.has_value()) {
        append_string_field(output, 2U, "motoring_package_path",
                            *runtime.motoring_package_path);
    }
    append_key(output, 2U, "domain");
    output.append("{\n");
    append_rpm_field(output, 4U, "minimum_rpm", runtime.minimum_rpm);
    append_rpm_field(output, 4U, "maximum_rpm", runtime.maximum_rpm, false);
    output.append("  },\n");
    append_key(output, 2U, "provenance");
    output.append("{\n");
    append_key(output, 4U, "engine");
    output.append("{\n");
    append_string_field(output, 6U, "id", runtime.engine_id);
    append_string_field(output, 6U, "sha256",
                        digest_hex(runtime.compiled_engine_provenance_sha256), false);
    output.append("    },\n");
    append_key(output, 4U, "renderer_build");
    output.append("{\n");
    append_string_field(output, 6U, "id", "engine-sim-offline-renderer-build");
    append_string_field(output, 6U, "sha256", digest_hex(backend.source_closure_sha256),
                        false);
    output.append("    },\n");
    append_string_field(output, 4U, "representation", runtime.representation, false);
    output.append("  }");

    if (runtime.renderer_compatibility.has_value()) {
        const auto &compatibility = *runtime.renderer_compatibility;
        output.append(",\n");
        append_key(output, 2U, "runtime_renderer_compatibility");
        output.append("{\n");
        append_string_field(output, 4U, "capture_source_closure_sha256",
                            digest_hex(backend.source_closure_sha256));
        append_string_field(output, 4U, "admitted_source_closure_sha256",
                            digest_hex(compatibility.admitted_source_closure_sha256));
        append_string_field(output, 4U, "evidence_path", compatibility.evidence_path);
        append_integer_field(output, 4U, "evidence_byte_count",
                             compatibility.evidence_byte_count);
        append_string_field(output, 4U, "evidence_sha256",
                            digest_hex(compatibility.evidence_sha256), false);
        output.append("  }");
    }
    if (runtime.lifecycle_package_path.has_value()) {
        output.append(",\n");
        append_string_field(output, 2U, "lifecycle_package_path",
                            *runtime.lifecycle_package_path, false);
        // append_string_field emitted a newline. Remove it so the root separator is
        // governed uniformly by the next optional field or final close.
        output.pop_back();
    }
    if (runtime.shared_recorded_starter_package_path.has_value()) {
        output.append(",\n");
        append_string_field(output, 2U, "shared_recorded_starter_package_path",
                            *runtime.shared_recorded_starter_package_path, false);
        output.pop_back();
    }
    output.append("\n}\n");
    return output;
}

[[nodiscard]] std::string
encode_descriptor(const std::string_view engine_id,
                  const contract::Sha256Digest &runtime_sha256) {
    std::string output;
    output.reserve(512U);
    output.append("{\n");
    append_string_field(output, 2U, "schema", artifacts::kVehicleEnginePackageSchema);
    append_integer_field(output, 2U, "version",
                         artifacts::kVehicleEnginePackageSchemaVersion);
    append_string_field(output, 2U, "engine_id", engine_id);
    append_key(output, 2U, "runtime");
    output.append("{\n");
    append_string_field(output, 4U, "kind",
                        artifacts::kVehicleEngineResponsiveAudioRuntimeKind);
    append_string_field(output, 4U, "manifest_path", kResponsiveRuntimePathV1);
    append_string_field(output, 4U, "manifest_sha256", digest_hex(runtime_sha256),
                        false);
    output.append("  }\n}\n");
    return output;
}

[[nodiscard]] std::string
encode_report(const NativeResponsivePackageInputV2 &input,
              const EncodedNativeResponsiveBakeIdentityV1 &cache_identity,
              const std::vector<ResolvedResponsiveAssetIdentity> &assets,
              const contract::Sha256Digest &runtime_sha256,
              const contract::Sha256Digest &descriptor_sha256) {
    const auto &identity = input.identity;
    const auto &backend = identity.backend;
    std::string output;
    output.reserve(2048U + assets.size() * 192U);
    output.append("{\n");
    append_string_field(output, 2U, "schema", kNativeResponsiveBakeReportSchemaV2);
    append_string_field(output, 2U, "release_identity", backend.release_identity);
    append_key(output, 2U, "engine");
    output.append("{\n");
    append_string_field(output, 4U, "id", identity.engine_id);
    append_string_field(output, 4U, "sha256", digest_hex(identity.engine_source_sha256),
                        false);
    output.append("  },\n");
    append_key(output, 2U, "profile");
    output.append("{\n");
    append_string_field(output, 4U, "id", identity.profile_id);
    append_string_field(output, 4U, "sha256", digest_hex(identity.profile_sha256),
                        false);
    output.append("  },\n");
    append_key(output, 2U, "implementation");
    output.append("{\n");
    append_string_field(output, 4U, "bake_recipe_sha256",
                        digest_hex(identity.bake_recipe_sha256));
    append_string_field(output, 4U, "builtin_asset_catalog_sha256",
                        digest_hex(identity.builtin_asset_catalog_sha256));
    append_string_field(output, 4U, "cache_identity_sha256",
                        digest_hex(cache_identity.sha256));
    append_key(output, 4U, "backend");
    output.append("{\n");
    append_string_field(output, 6U, "kind", kNativeResponsiveBackendKindV1);
    append_integer_field(output, 6U, "c_api_version", backend.c_api_version);
    append_string_field(output, 6U, "target", backend.target);
    append_string_field(output, 6U, "numeric_runtime", backend.numeric_runtime);
    append_string_field(output, 6U, "executable_sha256",
                        digest_hex(backend.executable_sha256));
    append_string_field(output, 6U, "source_closure_sha256",
                        digest_hex(backend.source_closure_sha256));
    append_string_field(output, 6U, "method_registry_sha256",
                        digest_hex(backend.method_registry_sha256), false);
    output.append("    }\n  },\n");
    append_key(output, 2U, "resolved_assets");
    output.append("[\n");
    for (std::size_t index = 0; index < assets.size(); ++index) {
        output.append("    {\n");
        append_string_field(output, 6U, "kind", assets[index].kind);
        append_string_field(output, 6U, "id", assets[index].id);
        append_string_field(output, 6U, "sha256", digest_hex(assets[index].sha256),
                            false);
        output.append(index + 1U == assets.size() ? "    }\n" : "    },\n");
    }
    output.append("  ],\n");
    append_key(output, 2U, "shared_recorded_starter");
    if (identity.shared_recorded_starter.has_value()) {
        output.append("{\n");
        append_string_field(
            output, 4U, "aggregate_sha256",
            digest_hex(identity.shared_recorded_starter->aggregate_sha256));
        append_integer_field(output, 4U, "entry_count",
                             identity.shared_recorded_starter->entry_count, false);
        output.append("  },\n");
    } else {
        output.append("null,\n");
    }
    append_string_field(output, 2U, "runtime_manifest_sha256",
                        digest_hex(runtime_sha256));
    append_string_field(output, 2U, "vehicleengine_descriptor_sha256",
                        digest_hex(descriptor_sha256));
    append_bool_field(output, 2U, "completed", true, false);
    output.append("}\n");
    return output;
}

[[nodiscard]] Error pack_error(const artifacts::VehicleEngineContainerError &failure) {
    return error(ErrorCode::pack_failure, "native-responsive-vehicleengine-pack-failed",
                 failure.path, failure.message);
}

} // namespace

EncodedNativeResponsiveBakeIdentityResultV1 encode_native_responsive_bake_identity_v1(
    const NativeResponsiveBakeIdentityInputV1 &input,
    const std::stop_token stop_token) {
    std::vector<ResolvedResponsiveAssetIdentity> assets;
    if (const auto failure = validate_identity(input, assets, stop_token)) {
        return *failure;
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    auto bytes = bytes_from_string(encode_cache_identity(input, assets));
    return EncodedNativeResponsiveBakeIdentityV1{bytes, contract::sha256(bytes)};
}

NativeResponsivePackageBuildResultV2
build_native_responsive_package_v2(NativeResponsivePackageInputV2 input,
                                   const std::stop_token stop_token) {
    auto cache_result =
        encode_native_responsive_bake_identity_v1(input.identity, stop_token);
    if (const auto *failure =
            std::get_if<NativeResponsivePackageError>(&cache_result)) {
        return *failure;
    }
    auto cache_identity =
        std::get<EncodedNativeResponsiveBakeIdentityV1>(std::move(cache_result));

    if (input.payload_members.size() > artifacts::kVehicleEngineMaximumEntryCountV1 - 3U) {
        return error(
            ErrorCode::resource_limit, "native-responsive-package-entry-limit-exceeded",
            "", "responsive payload leaves no room for generated package manifests");
    }
    std::uint64_t payload_bytes = 0U;
    for (std::size_t index = 0; index < input.payload_members.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &member = input.payload_members[index];
        if (!artifacts::is_portable_vehicleengine_path(member.path) ||
            member.path == artifacts::kVehicleEnginePackageDescriptorPath ||
            member.path == kResponsiveRuntimePathV1 ||
            member.path == kNativeResponsiveBakeReportPathV2) {
            return error(
                ErrorCode::invalid_member,
                "native-responsive-package-member-path-invalid", member.path,
                "payload path is nonportable or reserved for a generated manifest");
        }
        if (member.bytes.size() > artifacts::kVehicleEngineMaximumEntryByteCountV1 ||
            member.bytes.size() >
                kNativeResponsiveMaximumPackagePayloadBytes - payload_bytes) {
            return error(
                ErrorCode::resource_limit,
                "native-responsive-package-byte-limit-exceeded", member.path,
                "responsive package payload exceeds its native admission bound");
        }
        payload_bytes += member.bytes.size();
    }
    std::sort(
        input.payload_members.begin(), input.payload_members.end(),
        [](const auto &left, const auto &right) { return left.path < right.path; });
    for (std::size_t index = 1; index < input.payload_members.size(); ++index) {
        if (input.payload_members[index - 1].path ==
            input.payload_members[index].path) {
            return error(ErrorCode::duplicate_member,
                         "native-responsive-package-member-duplicate",
                         input.payload_members[index].path,
                         "responsive package payload contains a duplicate path");
        }
    }

    if (const auto failure = validate_runtime_input(
            input.runtime, input.identity, input.payload_members, stop_token)) {
        return *failure;
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }

    std::vector<ResolvedResponsiveAssetIdentity> ordered_assets;
    if (const auto failure =
            validate_identity(input.identity, ordered_assets, stop_token)) {
        return *failure;
    }
    auto runtime_bytes =
        bytes_from_string(encode_runtime(input.runtime, input.identity.backend));
    const auto runtime_sha256 = contract::sha256(runtime_bytes);
    auto descriptor_bytes =
        bytes_from_string(encode_descriptor(input.identity.engine_id, runtime_sha256));
    const auto descriptor_sha256 = contract::sha256(descriptor_bytes);
    auto report_bytes = bytes_from_string(encode_report(
        input, cache_identity, ordered_assets, runtime_sha256, descriptor_sha256));

    for (const auto generated_size :
         {runtime_bytes.size(), descriptor_bytes.size(), report_bytes.size()}) {
        if (generated_size >
            kNativeResponsiveMaximumPackagePayloadBytes - payload_bytes) {
            return error(
                ErrorCode::resource_limit,
                "native-responsive-package-byte-limit-exceeded", "",
                "generated manifests exceed the complete native package byte bound");
        }
        payload_bytes += generated_size;
    }

    input.payload_members.push_back(
        {std::string{kResponsiveRuntimePathV1}, std::move(runtime_bytes)});
    input.payload_members.push_back(
        {std::string{artifacts::kVehicleEnginePackageDescriptorPath},
         std::move(descriptor_bytes)});
    input.payload_members.push_back(
        {std::string{kNativeResponsiveBakeReportPathV2}, std::move(report_bytes)});
    std::sort(
        input.payload_members.begin(), input.payload_members.end(),
        [](const auto &left, const auto &right) { return left.path < right.path; });

    std::vector<artifacts::VehicleEnginePackEntry> entries;
    entries.reserve(input.payload_members.size());
    for (const auto &member : input.payload_members) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        entries.push_back({member.path, member.bytes});
    }
    auto package_validation = artifacts::validate_vehicleengine_package_tree(entries);
    if (const auto *failure =
            std::get_if<artifacts::VehicleEnginePackageError>(&package_validation)) {
        return error(ErrorCode::pack_failure,
                     "native-responsive-package-tree-validation-failed", failure->path,
                     failure->message);
    }
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    auto packed = artifacts::pack_vehicleengine_v1(entries);
    if (const auto *failure =
            std::get_if<artifacts::VehicleEngineContainerError>(&packed)) {
        return pack_error(*failure);
    }
    auto carrier = std::get<std::vector<std::byte>>(std::move(packed));
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    const auto carrier_sha256 = contract::sha256(carrier);
    return NativeResponsivePackageV2{std::move(cache_identity),
                                     std::move(input.payload_members),
                                     std::move(carrier), carrier_sha256};
}

} // namespace engine_sim_offline::responsive
