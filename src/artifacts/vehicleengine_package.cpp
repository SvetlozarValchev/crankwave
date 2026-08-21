#include "engine_sim_offline/artifacts/vehicleengine_package.hpp"

#include "engine_sim_offline/authoring/json.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace engine_sim_offline::artifacts {
namespace {

[[nodiscard]] VehicleEnginePackageError error(VehicleEnginePackageErrorCode code,
                                          std::string path, std::string message) {
    return {code, std::move(path), std::move(message)};
}

template <std::size_t Size>
[[nodiscard]] bool known_field(const std::string_view field,
                               const std::array<std::string_view, Size> &known) {
    for (const auto candidate : known) {
        if (field == candidate) {
            return true;
        }
    }
    return false;
}

template <std::size_t Size>
[[nodiscard]] std::optional<VehicleEnginePackageError>
reject_unknown_fields(const authoring::JsonValue object, const std::string_view path,
                      const std::array<std::string_view, Size> &known) {
    for (std::size_t index = 0; index < object.size(); ++index) {
        const auto member = object.member_at(index);
        if (!member || !known_field(member.key, known)) {
            return error(VehicleEnginePackageErrorCode::unknown_field,
                         std::string{path} + "/" + std::string{member.key},
                         "unknown VEHICLEENGINE package descriptor field");
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::variant<std::string_view, VehicleEnginePackageError>
required_string(const authoring::JsonValue object, const std::string_view key,
                const std::string_view path) {
    const auto value = object.find(key);
    if (!value.valid()) {
        return error(VehicleEnginePackageErrorCode::missing_field, std::string{path},
                     "required VEHICLEENGINE package descriptor field is absent");
    }
    const auto text = value.string();
    if (!text.has_value()) {
        return error(VehicleEnginePackageErrorCode::invalid_shape, std::string{path},
                     "VEHICLEENGINE package descriptor field must be a string");
    }
    return *text;
}

[[nodiscard]] int hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

[[nodiscard]] std::optional<contract::Sha256Digest>
parse_sha256(const std::string_view value) noexcept {
    if (value.size() != 64) {
        return std::nullopt;
    }
    contract::Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        const auto high = hex_value(value[index * 2]);
        const auto low = hex_value(value[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        digest.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return digest;
}

} // namespace

bool is_vehicleengine_engine_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128 ||
        !((value.front() >= 'a' && value.front() <= 'z') ||
          (value.front() >= '0' && value.front() <= '9')) ||
        !((value.back() >= 'a' && value.back() <= 'z') ||
          (value.back() >= '0' && value.back() <= '9'))) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

VehicleEnginePackageParseResult
parse_vehicleengine_package_descriptor(const std::string_view json) {
    if (json.size() > kVehicleEngineMaximumDescriptorByteCount) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "",
                     "VEHICLEENGINE package descriptor exceeds its byte limit");
    }
    auto parsed = authoring::parse_json(
        json, {.maximum_input_bytes = kVehicleEngineMaximumDescriptorByteCount,
               .maximum_depth = 4,
               .maximum_nodes = 32});
    if (const auto *failure = std::get_if<authoring::JsonParseError>(&parsed)) {
        return error(VehicleEnginePackageErrorCode::malformed_json, "",
                     "VEHICLEENGINE package descriptor JSON is malformed at byte " +
                         std::to_string(failure->location.byte_offset));
    }
    auto document = std::get<authoring::JsonDocument>(std::move(parsed));
    const auto root = document.root();
    if (root.kind() != authoring::JsonKind::object) {
        return error(VehicleEnginePackageErrorCode::invalid_shape, "",
                     "VEHICLEENGINE package descriptor root must be an object");
    }
    constexpr std::array<std::string_view, 4> root_fields{"schema", "version",
                                                          "engine_id", "runtime"};
    if (const auto unknown = reject_unknown_fields(root, "", root_fields)) {
        return *unknown;
    }

    const auto schema_result = required_string(root, "schema", "/schema");
    if (const auto *failure = std::get_if<VehicleEnginePackageError>(&schema_result)) {
        return *failure;
    }
    const auto schema = std::get<std::string_view>(schema_result);
    if (schema != kVehicleEnginePackageSchema) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "/schema",
                     "VEHICLEENGINE package schema is unsupported");
    }

    const auto version_value = root.find("version");
    if (!version_value.valid()) {
        return error(VehicleEnginePackageErrorCode::missing_field, "/version",
                     "required VEHICLEENGINE package descriptor field is absent");
    }
    const auto version = version_value.number();
    if (!version.has_value() || !std::isfinite(*version) ||
        *version != static_cast<double>(kVehicleEnginePackageSchemaVersion)) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "/version",
                     "VEHICLEENGINE package descriptor version must be 1");
    }

    const auto engine_result = required_string(root, "engine_id", "/engine_id");
    if (const auto *failure = std::get_if<VehicleEnginePackageError>(&engine_result)) {
        return *failure;
    }
    const auto engine_id = std::get<std::string_view>(engine_result);
    if (!is_vehicleengine_engine_id(engine_id)) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "/engine_id",
                     "VEHICLEENGINE engine_id is not a portable identifier");
    }

    const auto runtime = root.find("runtime");
    if (!runtime.valid()) {
        return error(VehicleEnginePackageErrorCode::missing_field, "/runtime",
                     "required VEHICLEENGINE package descriptor field is absent");
    }
    if (runtime.kind() != authoring::JsonKind::object) {
        return error(VehicleEnginePackageErrorCode::invalid_shape, "/runtime",
                     "VEHICLEENGINE runtime descriptor must be an object");
    }
    constexpr std::array<std::string_view, 3> runtime_fields{"kind", "manifest_path",
                                                             "manifest_sha256"};
    if (const auto unknown =
            reject_unknown_fields(runtime, "/runtime", runtime_fields)) {
        return *unknown;
    }

    const auto kind_result = required_string(runtime, "kind", "/runtime/kind");
    if (const auto *failure = std::get_if<VehicleEnginePackageError>(&kind_result)) {
        return *failure;
    }
    const auto kind = std::get<std::string_view>(kind_result);
    if (kind != kVehicleEngineResponsiveAudioRuntimeKind) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "/runtime/kind",
                     "VEHICLEENGINE runtime kind is unsupported");
    }

    const auto manifest_path_result =
        required_string(runtime, "manifest_path", "/runtime/manifest_path");
    if (const auto *failure =
            std::get_if<VehicleEnginePackageError>(&manifest_path_result)) {
        return *failure;
    }
    const auto manifest_path = std::get<std::string_view>(manifest_path_result);
    if (!is_portable_vehicleengine_path(manifest_path) ||
        manifest_path == kVehicleEnginePackageDescriptorPath) {
        return error(VehicleEnginePackageErrorCode::invalid_value, "/runtime/manifest_path",
                     "VEHICLEENGINE runtime manifest path is invalid");
    }

    const auto manifest_sha_result =
        required_string(runtime, "manifest_sha256", "/runtime/manifest_sha256");
    if (const auto *failure =
            std::get_if<VehicleEnginePackageError>(&manifest_sha_result)) {
        return *failure;
    }
    const auto manifest_sha =
        parse_sha256(std::get<std::string_view>(manifest_sha_result));
    if (!manifest_sha.has_value()) {
        return error(
            VehicleEnginePackageErrorCode::invalid_value, "/runtime/manifest_sha256",
            "VEHICLEENGINE runtime manifest SHA-256 must be 64 lowercase hex digits");
    }

    return VehicleEnginePackageDescriptor{
        std::string{schema},
        kVehicleEnginePackageSchemaVersion,
        std::string{engine_id},
        {std::string{kind}, std::string{manifest_path}, *manifest_sha},
    };
}

VehicleEnginePackageValidationResult
validate_vehicleengine_package_tree(const std::span<const VehicleEnginePackEntry> entries) {
    const VehicleEnginePackEntry *descriptor_entry = nullptr;
    for (const auto &entry : entries) {
        if (entry.path == kVehicleEnginePackageDescriptorPath) {
            if (descriptor_entry != nullptr) {
                return error(VehicleEnginePackageErrorCode::invalid_value,
                             std::string{kVehicleEnginePackageDescriptorPath},
                             "package tree contains duplicate vehicleengine.json entries");
            }
            descriptor_entry = &entry;
        }
    }
    if (descriptor_entry == nullptr) {
        return error(VehicleEnginePackageErrorCode::missing_descriptor,
                     std::string{kVehicleEnginePackageDescriptorPath},
                     "package tree does not contain vehicleengine.json");
    }

    const auto descriptor_bytes = descriptor_entry->payload;
    const auto descriptor_json =
        descriptor_bytes.empty()
            ? std::string_view{}
            : std::string_view{reinterpret_cast<const char *>(descriptor_bytes.data()),
                               descriptor_bytes.size()};
    auto parsed = parse_vehicleengine_package_descriptor(descriptor_json);
    if (const auto *failure = std::get_if<VehicleEnginePackageError>(&parsed)) {
        return *failure;
    }
    auto descriptor = std::get<VehicleEnginePackageDescriptor>(std::move(parsed));

    const VehicleEnginePackEntry *manifest_entry = nullptr;
    for (const auto &entry : entries) {
        if (entry.path != descriptor.runtime.manifest_path) {
            continue;
        }
        if (manifest_entry != nullptr) {
            return error(VehicleEnginePackageErrorCode::invalid_value,
                         descriptor.runtime.manifest_path,
                         "package tree contains duplicate runtime manifests");
        }
        manifest_entry = &entry;
    }
    if (manifest_entry == nullptr) {
        return error(VehicleEnginePackageErrorCode::missing_runtime_manifest,
                     descriptor.runtime.manifest_path,
                     "package tree does not contain the declared runtime manifest");
    }
    if (contract::sha256(manifest_entry->payload) !=
        descriptor.runtime.manifest_sha256) {
        return error(VehicleEnginePackageErrorCode::runtime_manifest_hash_mismatch,
                     descriptor.runtime.manifest_path,
                     "runtime manifest bytes do not match vehicleengine.json");
    }
    return descriptor;
}

} // namespace engine_sim_offline::artifacts
