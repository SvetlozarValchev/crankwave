#include "native_input_files_support.hpp"

#include "engine_sim_offline_installed_layout.hpp"

#include "engine_sim_offline/authoring/json.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace engine_sim_offline::cli {
namespace {

constexpr std::string_view kCatalogSchema =
    "engine-sim-offline/builtin-asset-catalog.v1";
constexpr std::size_t kMaximumCatalogEntries = 16384U;
constexpr std::size_t kMaximumExecutablePathBytes = 1024U * 1024U;

struct CatalogEntry {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::string sha256;

    friend bool operator==(const CatalogEntry &, const CatalogEntry &) = default;
};

struct ParsedCatalog {
    std::filesystem::path canonical_path;
    contract::Sha256Digest sha256;
    std::vector<CatalogEntry> entries;
};

struct AuthoringCatalogSelection {
    std::string id;
    std::string sha256;
};

struct ParsedAuthoringCatalog {
    IrAuthoringCatalogDocument document;
    std::vector<AuthoringCatalogSelection> selections;
};

using ParsedAuthoringCatalogResult =
    std::variant<ParsedAuthoringCatalog, NativeInputError>;

[[nodiscard]] bool catalog_contains(const ParsedCatalog &catalog,
                                    compile::AssetKind kind, std::string_view id,
                                    std::string_view sha256);

using ParsedCatalogResult = std::variant<ParsedCatalog, NativeInputError>;

[[nodiscard]] std::string bytes_to_string(const std::vector<std::byte> &bytes) {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool valid_stable_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    const auto first_is_valid = [](const char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    const auto rest_is_valid = [&](const char byte) {
        return first_is_valid(byte) || byte == '.' || byte == '_' || byte == '-';
    };
    return first_is_valid(value.front()) &&
           std::all_of(value.begin() + 1, value.end(), rest_is_valid);
}

[[nodiscard]] bool valid_sha256(std::string_view value) noexcept {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](const char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
           });
}

[[nodiscard]] bool
has_exact_members(const authoring::JsonValue value,
                  const std::span<const std::string_view> expected) noexcept {
    if (value.kind() != authoring::JsonKind::object ||
        value.size() != expected.size()) {
        return false;
    }
    for (const auto member_name : expected) {
        if (!value.find(member_name).valid()) {
            return false;
        }
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto member = value.member_at(index);
        if (!member ||
            std::find(expected.begin(), expected.end(), member.key) == expected.end()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] NativeInputError
catalog_error(const NativeInputErrorCode code, std::filesystem::path path,
              std::string message,
              const NativeInputErrorKind kind = NativeInputErrorKind::unavailable) {
    return detail::input_error(kind, code, NativeInputSubject::builtin_asset_catalog,
                               std::move(path), std::move(message));
}

[[nodiscard]] NativeInputError authoring_catalog_error(
    const NativeInputErrorCode code, std::filesystem::path path, std::string message,
    const NativeInputErrorKind kind = NativeInputErrorKind::unavailable) {
    return detail::input_error(kind, code, NativeInputSubject::ir_authoring_catalog,
                               std::move(path), std::move(message));
}

[[nodiscard]] ParsedCatalogResult
parse_catalog(detail::ReadFile catalog_file,
              const authoring::JsonParseLimits &parse_limits) {
    auto parsed =
        authoring::parse_json(bytes_to_string(catalog_file.bytes), parse_limits);
    if (const auto *error = std::get_if<authoring::JsonParseError>(&parsed)) {
        return catalog_error(NativeInputErrorCode::invalid_builtin_asset_catalog,
                             std::move(catalog_file.canonical_path),
                             "built-in asset catalog JSON is invalid at byte " +
                                 std::to_string(error->location.byte_offset) + ": " +
                                 std::string{error->message()});
    }

    auto document = std::get<authoring::JsonDocument>(std::move(parsed));
    const auto root = document.root();
    constexpr std::array root_members{std::string_view{"schema"},
                                      std::string_view{"assets"}};
    if (!has_exact_members(root, root_members)) {
        return catalog_error(
            NativeInputErrorCode::invalid_builtin_asset_catalog,
            std::move(catalog_file.canonical_path),
            "built-in asset catalog root must contain exactly 'schema' and "
            "'assets'");
    }
    const auto schema = root.find("schema").string();
    if (!schema || *schema != kCatalogSchema) {
        return catalog_error(NativeInputErrorCode::invalid_builtin_asset_catalog,
                             std::move(catalog_file.canonical_path),
                             "built-in asset catalog has an unsupported schema");
    }
    const auto assets = root.find("assets");
    if (assets.kind() != authoring::JsonKind::array ||
        assets.size() > kMaximumCatalogEntries) {
        return catalog_error(NativeInputErrorCode::invalid_builtin_asset_catalog,
                             std::move(catalog_file.canonical_path),
                             "built-in asset catalog 'assets' must be a bounded array");
    }

    const auto catalog_sha256 = contract::sha256(catalog_file.bytes);
    ParsedCatalog result{std::move(catalog_file.canonical_path), catalog_sha256, {}};
    result.entries.reserve(assets.size());
    constexpr std::array entry_members{std::string_view{"kind"}, std::string_view{"id"},
                                       std::string_view{"sha256"}};
    for (std::size_t index = 0; index < assets.size(); ++index) {
        const auto value = assets.at(index);
        if (!has_exact_members(value, entry_members)) {
            return catalog_error(
                NativeInputErrorCode::invalid_builtin_asset_catalog,
                result.canonical_path,
                "every built-in asset catalog entry must contain exactly "
                "'kind', 'id', and 'sha256'");
        }
        const auto kind = value.find("kind").string();
        const auto id = value.find("id").string();
        const auto sha256 = value.find("sha256").string();
        if (!kind || !id || !sha256 || !valid_stable_id(*id) ||
            !valid_sha256(*sha256)) {
            return catalog_error(NativeInputErrorCode::invalid_builtin_asset_catalog,
                                 result.canonical_path,
                                 "built-in asset catalog entry values are invalid");
        }

        compile::AssetKind parsed_kind = compile::AssetKind::audio;
        if (*kind == "audio") {
            parsed_kind = compile::AssetKind::audio;
        } else if (*kind == "accessory-configuration") {
            parsed_kind = compile::AssetKind::accessory_configuration;
        } else {
            return catalog_error(
                NativeInputErrorCode::invalid_builtin_asset_catalog,
                result.canonical_path,
                "built-in asset catalog entry has an unsupported kind");
        }

        CatalogEntry entry{parsed_kind, std::string{*id}, std::string{*sha256}};
        if (std::find(result.entries.begin(), result.entries.end(), entry) !=
            result.entries.end()) {
            return catalog_error(
                NativeInputErrorCode::invalid_builtin_asset_catalog,
                result.canonical_path,
                "built-in asset catalog contains a duplicate exact entry");
        }
        result.entries.push_back(std::move(entry));
    }
    return result;
}

[[nodiscard]] ParsedAuthoringCatalogResult
parse_ir_authoring_catalog(detail::ReadFile catalog_file,
                           const std::string_view expected_release_identity,
                           const authoring::JsonParseLimits &parse_limits) {
    auto json = bytes_to_string(catalog_file.bytes);
    auto parsed = authoring::parse_json(json, parse_limits);
    if (const auto *error = std::get_if<authoring::JsonParseError>(&parsed)) {
        return authoring_catalog_error(
            NativeInputErrorCode::invalid_ir_authoring_catalog,
            std::move(catalog_file.canonical_path),
            "IR authoring catalog JSON is invalid at byte " +
                std::to_string(error->location.byte_offset) + ": " +
                std::string{error->message()});
    }

    auto parsed_json = std::get<authoring::JsonDocument>(std::move(parsed));
    const auto root = parsed_json.root();
    const auto schema = root.find("schema").string();
    const auto release_identity = root.find("release_identity").string();
    const auto entries = root.find("entries");
    if (root.kind() != authoring::JsonKind::object || !schema ||
        *schema != "engine-sim-offline/ir-authoring-catalog.v1" || !release_identity ||
        *release_identity != expected_release_identity ||
        entries.kind() != authoring::JsonKind::array || entries.size() == 0U ||
        entries.size() > kMaximumCatalogEntries) {
        return authoring_catalog_error(
            NativeInputErrorCode::invalid_ir_authoring_catalog,
            std::move(catalog_file.canonical_path),
            "IR authoring catalog schema, release identity, or bounded entries "
            "array is invalid");
    }

    ParsedAuthoringCatalog result;
    result.document.canonical_path = std::move(catalog_file.canonical_path);
    result.document.sha256 = contract::sha256(catalog_file.bytes);
    result.document.json = std::move(json);
    result.document.release_identity = std::string{*release_identity};
    result.document.entry_count = entries.size();
    result.selections.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto entry = entries.at(index);
        const auto id = entry.find("id").string();
        const auto sha256 = entry.find("sha256").string();
        if (entry.kind() != authoring::JsonKind::object || !id || !sha256 ||
            !valid_stable_id(*id) || !valid_sha256(*sha256)) {
            return authoring_catalog_error(
                NativeInputErrorCode::invalid_ir_authoring_catalog,
                result.document.canonical_path,
                "IR authoring catalog entry id or SHA-256 is invalid");
        }
        if (std::any_of(result.selections.begin(), result.selections.end(),
                        [&](const AuthoringCatalogSelection &selection) {
                            return selection.id == *id;
                        })) {
            return authoring_catalog_error(
                NativeInputErrorCode::invalid_ir_authoring_catalog,
                result.document.canonical_path,
                "IR authoring catalog contains a duplicate stable ID");
        }
        result.selections.push_back({std::string{*id}, std::string{*sha256}});
    }
    return result;
}

[[nodiscard]] IrAuthoringCatalogResult
load_ir_authoring_catalog_impl(const std::filesystem::path &authoring_catalog_path,
                               const std::string_view expected_release_identity,
                               const NativeInputLimits &limits) {
    auto authoring_file = detail::read_exact_regular_file(
        authoring_catalog_path, NativeInputSubject::ir_authoring_catalog, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&authoring_file)) {
        if (error->code == NativeInputErrorCode::path_not_found) {
            error->kind = NativeInputErrorKind::unavailable;
            error->code = NativeInputErrorCode::ir_authoring_catalog_not_found;
            error->message = "IR authoring catalog is absent from the installed "
                             "asset bundle";
        }
        return std::move(*error);
    }
    auto authoring_catalog = parse_ir_authoring_catalog(
        std::get<detail::ReadFile>(std::move(authoring_file)),
        expected_release_identity, limits.authoring_limits.json);
    if (auto *error = std::get_if<NativeInputError>(&authoring_catalog)) {
        return std::move(*error);
    }
    auto parsed_authoring =
        std::get<ParsedAuthoringCatalog>(std::move(authoring_catalog));

    const auto runtime_catalog_path =
        parsed_authoring.document.canonical_path.parent_path() / "catalog.v1.json";
    auto runtime_file = detail::read_exact_regular_file(
        runtime_catalog_path, NativeInputSubject::builtin_asset_catalog, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&runtime_file)) {
        return std::move(*error);
    }
    auto runtime_catalog =
        parse_catalog(std::get<detail::ReadFile>(std::move(runtime_file)),
                      limits.authoring_limits.json);
    if (auto *error = std::get_if<NativeInputError>(&runtime_catalog)) {
        return std::move(*error);
    }
    const auto &runtime = std::get<ParsedCatalog>(runtime_catalog);
    for (const auto &selection : parsed_authoring.selections) {
        if (!catalog_contains(runtime, compile::AssetKind::audio, selection.id,
                              selection.sha256)) {
            return authoring_catalog_error(
                NativeInputErrorCode::invalid_ir_authoring_catalog,
                parsed_authoring.document.canonical_path,
                "IR authoring catalog exposes a selection outside the technical "
                "built-in asset catalog: " +
                    selection.id);
        }
    }
    return std::move(parsed_authoring.document);
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = digits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] NativeInputError missing_digest_error(const NativeEngineInput &input,
                                                    const NativeInputSubject subject,
                                                    std::string_view id) {
    return detail::input_error(
        NativeInputErrorKind::data_error,
        NativeInputErrorCode::builtin_asset_digest_required, subject,
        input.source.canonical_path,
        "built-in asset resolution requires every declared asset to carry an "
        "exact SHA-256",
        std::string{id});
}

[[nodiscard]] NativeInputError not_cataloged_error(const ParsedCatalog &catalog,
                                                   const NativeInputSubject subject,
                                                   std::string_view id) {
    return detail::input_error(
        NativeInputErrorKind::unavailable,
        NativeInputErrorCode::builtin_asset_not_cataloged, subject,
        catalog.canonical_path,
        "declared engine asset is outside the built-in catalog coverage",
        std::string{id});
}

[[nodiscard]] bool catalog_contains(const ParsedCatalog &catalog,
                                    const compile::AssetKind kind,
                                    const std::string_view id,
                                    const std::string_view sha256) {
    return std::any_of(
        catalog.entries.begin(), catalog.entries.end(), [&](const CatalogEntry &entry) {
            return entry.kind == kind && entry.id == id && entry.sha256 == sha256;
        });
}

[[nodiscard]] std::optional<NativeInputError>
append_catalog_asset(NativeEngineInput &input, const ParsedCatalog &catalog,
                     const detail::OpenedAssetRoot &asset_root,
                     const compile::AssetKind kind, const NativeInputSubject subject,
                     const std::string_view id,
                     const std::optional<std::string> &declared_sha256,
                     const NativeInputLimits &limits, std::uintmax_t &total_bytes) {
    if (!declared_sha256) {
        return missing_digest_error(input, subject, id);
    }
    if (!catalog_contains(catalog, kind, id, *declared_sha256)) {
        return not_cataloged_error(catalog, subject, id);
    }

    const auto payload_uri = "payloads/" + *declared_sha256;
    auto append_error = detail::append_confined_engine_asset(
        input, catalog.canonical_path, asset_root, kind, subject, id, payload_uri,
        limits, total_bytes);
    if (append_error) {
        switch (append_error->code) {
        case NativeInputErrorCode::path_not_found:
        case NativeInputErrorCode::symbolic_link_not_allowed:
        case NativeInputErrorCode::not_regular_file:
        case NativeInputErrorCode::file_open_failed:
        case NativeInputErrorCode::file_read_failed:
            append_error->kind = NativeInputErrorKind::unavailable;
            append_error->code =
                NativeInputErrorCode::builtin_asset_payload_unavailable;
            append_error->message = "built-in asset catalog payload is unavailable";
            break;
        default:
            break;
        }
        return append_error;
    }

    const auto &payload = input.assets.back();
    if (digest_hex(contract::sha256(payload.bytes)) != *declared_sha256) {
        return detail::input_error(
            NativeInputErrorKind::unavailable,
            NativeInputErrorCode::builtin_asset_payload_hash_mismatch, subject,
            asset_root.canonical_path / payload_uri,
            "built-in asset catalog payload does not match its content "
            "address",
            std::string{id});
    }
    return std::nullopt;
}

[[nodiscard]] NativeEngineInputResult
load_from_catalog_impl(const std::filesystem::path &engine_path,
                       const std::filesystem::path &catalog_path,
                       const NativeInputLimits &limits) {
    auto document_result = detail::load_engine_document_impl(engine_path, limits);
    if (auto *error = std::get_if<NativeInputError>(&document_result)) {
        return std::move(*error);
    }
    auto input = std::get<NativeEngineInput>(std::move(document_result));

    auto catalog_file = detail::read_exact_regular_file(
        catalog_path, NativeInputSubject::builtin_asset_catalog, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&catalog_file)) {
        return std::move(*error);
    }
    auto parsed_catalog =
        parse_catalog(std::get<detail::ReadFile>(std::move(catalog_file)),
                      limits.authoring_limits.json);
    if (auto *error = std::get_if<NativeInputError>(&parsed_catalog)) {
        return std::move(*error);
    }
    auto catalog = std::get<ParsedCatalog>(std::move(parsed_catalog));
    input.builtin_asset_catalog_sha256 = catalog.sha256;

    auto root_result = detail::open_asset_root(catalog.canonical_path.parent_path());
    if (auto *error = std::get_if<NativeInputError>(&root_result)) {
        error->subject = NativeInputSubject::builtin_asset_catalog;
        return std::move(*error);
    }
    auto root = std::get<detail::OpenedAssetRoot>(std::move(root_result));

    std::uintmax_t total_bytes = 0;
    for (const auto &asset : input.document.presentation.assets) {
        auto error =
            append_catalog_asset(input, catalog, root, compile::AssetKind::audio,
                                 NativeInputSubject::audio_asset, asset.id.value,
                                 asset.sha256, limits, total_bytes);
        if (error) {
            return std::move(*error);
        }
    }
    for (const auto &asset : input.document.engine.accessory_configurations) {
        auto error = append_catalog_asset(
            input, catalog, root, compile::AssetKind::accessory_configuration,
            NativeInputSubject::accessory_configuration_asset, asset.id.value,
            asset.sha256, limits, total_bytes);
        if (error) {
            return std::move(*error);
        }
    }
    return input;
}

[[nodiscard]] BuiltinAssetCatalogLocationResult
discover_impl(const std::filesystem::path &executable_path) {
    if (executable_path.empty() || !executable_path.is_absolute() ||
        executable_path.filename().empty()) {
        return catalog_error(
            NativeInputErrorCode::empty_path, executable_path,
            "executable path for built-in asset discovery must be an absolute "
            "file path",
            NativeInputErrorKind::software);
    }

    const auto executable_directory = executable_path.parent_path();
    const std::array candidates{
        (executable_directory / "engine-sim-offline-assets" / "catalog.v1.json")
            .lexically_normal(),
        (executable_directory /
         std::filesystem::path{detail::kInstalledAssetDirectoryRelativeToExecutable} /
         "catalog.v1.json")
            .lexically_normal(),
    };
    for (const auto &candidate : candidates) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (!error && status.type() != std::filesystem::file_type::not_found) {
            return candidate;
        }
        if (error && !detail::not_found(error)) {
            return catalog_error(NativeInputErrorCode::filesystem_failure, candidate,
                                 "built-in asset catalog discovery could not inspect a "
                                 "candidate path",
                                 NativeInputErrorKind::software);
        }
    }
    return catalog_error(
        NativeInputErrorCode::builtin_asset_catalog_not_found, executable_directory,
        "built-in asset catalog was not found beside the executable or in "
        "the configured installed data directory");
}

[[nodiscard]] BuiltinAssetCatalogLocationResult current_executable_path() {
#if !defined(__linux__)
    return catalog_error(
        NativeInputErrorCode::platform_unavailable, "/proc/self/exe",
        "current executable discovery is unavailable on this platform");
#else
    std::vector<char> buffer(256U);
    for (;;) {
        const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return catalog_error(
                NativeInputErrorCode::platform_unavailable, "/proc/self/exe",
                "current executable path is unavailable for built-in asset "
                "discovery");
        }
        const auto size = static_cast<std::size_t>(length);
        if (size < buffer.size()) {
            return std::filesystem::path{std::string{buffer.data(), size}}
                .lexically_normal();
        }
        if (buffer.size() == kMaximumExecutablePathBytes) {
            return catalog_error(
                NativeInputErrorCode::size_overflow, "/proc/self/exe",
                "current executable path exceeds the discovery byte limit",
                NativeInputErrorKind::software);
        }
        buffer.resize(std::min(buffer.size() * 2U, kMaximumExecutablePathBytes));
    }
#endif
}

[[nodiscard]] NativeInputError
unexpected_catalog_failure(const std::filesystem::path &path, std::string message) {
    return catalog_error(NativeInputErrorCode::filesystem_failure, path,
                         std::move(message), NativeInputErrorKind::software);
}

} // namespace

BuiltinAssetCatalogLocationResult
discover_builtin_asset_catalog(const std::filesystem::path &executable_path) {
    try {
        return discover_impl(executable_path);
    } catch (const std::bad_alloc &) {
        return catalog_error(NativeInputErrorCode::memory_allocation_failed,
                             executable_path,
                             "built-in asset catalog discovery allocation failed",
                             NativeInputErrorKind::software);
    } catch (...) {
        return unexpected_catalog_failure(
            executable_path, "built-in asset catalog discovery failed unexpectedly");
    }
}

NativeEngineInputResult
load_native_engine_input_from_builtin_catalog(const std::filesystem::path &engine_path,
                                              const std::filesystem::path &catalog_path,
                                              NativeInputLimits limits) {
    try {
        return load_from_catalog_impl(engine_path, catalog_path, limits);
    } catch (const std::bad_alloc &) {
        return detail::input_error(NativeInputErrorKind::software,
                                   NativeInputErrorCode::memory_allocation_failed,
                                   NativeInputSubject::engine_document, engine_path,
                                   "built-in engine input allocation failed");
    } catch (const std::filesystem::filesystem_error &) {
        return unexpected_catalog_failure(
            catalog_path, "built-in engine input filesystem operation failed");
    } catch (...) {
        return unexpected_catalog_failure(
            catalog_path, "built-in engine input loading failed unexpectedly");
    }
}

NativeEngineInputResult
load_native_engine_input_with_builtin_assets(const std::filesystem::path &engine_path,
                                             NativeInputLimits limits) {
    try {
        auto executable = current_executable_path();
        if (auto *error = std::get_if<NativeInputError>(&executable)) {
            return std::move(*error);
        }
        auto catalog =
            discover_impl(std::get<std::filesystem::path>(std::move(executable)));
        if (auto *error = std::get_if<NativeInputError>(&catalog)) {
            return std::move(*error);
        }
        return load_from_catalog_impl(
            engine_path, std::get<std::filesystem::path>(std::move(catalog)), limits);
    } catch (const std::bad_alloc &) {
        return detail::input_error(NativeInputErrorKind::software,
                                   NativeInputErrorCode::memory_allocation_failed,
                                   NativeInputSubject::engine_document, engine_path,
                                   "built-in engine input allocation failed");
    } catch (...) {
        return unexpected_catalog_failure(
            engine_path, "built-in engine input loading failed unexpectedly");
    }
}

IrAuthoringCatalogResult
load_ir_authoring_catalog(const std::filesystem::path &authoring_catalog_path,
                          const std::string_view expected_release_identity,
                          NativeInputLimits limits) {
    try {
        return load_ir_authoring_catalog_impl(authoring_catalog_path,
                                              expected_release_identity, limits);
    } catch (const std::bad_alloc &) {
        return authoring_catalog_error(
            NativeInputErrorCode::memory_allocation_failed, authoring_catalog_path,
            "IR authoring catalog allocation failed", NativeInputErrorKind::software);
    } catch (...) {
        return authoring_catalog_error(
            NativeInputErrorCode::filesystem_failure, authoring_catalog_path,
            "IR authoring catalog loading failed unexpectedly",
            NativeInputErrorKind::software);
    }
}

IrAuthoringCatalogResult load_ir_authoring_catalog_with_builtin_assets(
    const std::string_view expected_release_identity, NativeInputLimits limits) {
    try {
        auto executable = current_executable_path();
        if (auto *error = std::get_if<NativeInputError>(&executable)) {
            return std::move(*error);
        }
        auto runtime_catalog =
            discover_impl(std::get<std::filesystem::path>(std::move(executable)));
        if (auto *error = std::get_if<NativeInputError>(&runtime_catalog)) {
            return std::move(*error);
        }
        auto authoring_catalog_path =
            std::get<std::filesystem::path>(std::move(runtime_catalog));
        authoring_catalog_path.replace_filename("ir-authoring-catalog.v1.json");
        return load_ir_authoring_catalog_impl(authoring_catalog_path,
                                              expected_release_identity, limits);
    } catch (const std::bad_alloc &) {
        return authoring_catalog_error(NativeInputErrorCode::memory_allocation_failed,
                                       {}, "IR authoring catalog allocation failed",
                                       NativeInputErrorKind::software);
    } catch (...) {
        return authoring_catalog_error(
            NativeInputErrorCode::filesystem_failure, {},
            "IR authoring catalog discovery failed unexpectedly",
            NativeInputErrorKind::software);
    }
}

BuiltinAssetCatalogIdentityResult
load_builtin_asset_catalog_identity_with_builtin_assets(NativeInputLimits limits) {
    try {
        auto executable = current_executable_path();
        if (auto *error = std::get_if<NativeInputError>(&executable)) {
            return std::move(*error);
        }
        auto catalog_path =
            discover_impl(std::get<std::filesystem::path>(std::move(executable)));
        if (auto *error = std::get_if<NativeInputError>(&catalog_path)) {
            return std::move(*error);
        }
        auto catalog_file = detail::read_exact_regular_file(
            std::get<std::filesystem::path>(catalog_path),
            NativeInputSubject::builtin_asset_catalog, {},
            limits.maximum_document_bytes);
        if (auto *error = std::get_if<NativeInputError>(&catalog_file)) {
            return std::move(*error);
        }
        auto parsed_catalog = parse_catalog(
            std::get<detail::ReadFile>(std::move(catalog_file)),
            limits.authoring_limits.json);
        if (auto *error = std::get_if<NativeInputError>(&parsed_catalog)) {
            return std::move(*error);
        }
        auto parsed = std::get<ParsedCatalog>(std::move(parsed_catalog));
        return BuiltinAssetCatalogIdentity{std::move(parsed.canonical_path),
                                           parsed.sha256};
    } catch (const std::bad_alloc &) {
        return catalog_error(NativeInputErrorCode::memory_allocation_failed, {},
                             "built-in catalog identity allocation failed",
                             NativeInputErrorKind::software);
    } catch (...) {
        return unexpected_catalog_failure(
            {}, "built-in catalog identity loading failed unexpectedly");
    }
}

} // namespace engine_sim_offline::cli
