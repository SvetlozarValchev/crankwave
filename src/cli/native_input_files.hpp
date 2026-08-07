#pragma once

#include "engine_sim_offline/authoring/diagnostic.hpp"
#include "engine_sim_offline/authoring/atlas_bake_document.hpp"
#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::cli {

struct NativeInputLimits {
    std::uintmax_t maximum_document_bytes = 8U * 1024U * 1024U;
    std::size_t maximum_asset_count = 4096U;
    std::uintmax_t maximum_asset_bytes = 256U * 1024U * 1024U;
    std::uintmax_t maximum_total_asset_bytes = 1024U * 1024U * 1024U;
    authoring::AuthoringParseLimits authoring_limits;

    friend bool operator==(const NativeInputLimits &,
                           const NativeInputLimits &) = default;
};

enum class NativeInputErrorKind : std::uint8_t {
    data_error,
    no_input,
    unavailable,
    software,
};

enum class NativeInputSubject : std::uint8_t {
    engine_document,
    atlas_bake_document,
    scenario_document,
    builtin_asset_catalog,
    asset_root,
    audio_asset,
    accessory_configuration_asset,
};

enum class NativeInputErrorCode : std::uint8_t {
    empty_path,
    path_not_found,
    symbolic_link_not_allowed,
    not_regular_file,
    asset_root_not_directory,
    invalid_local_asset_uri,
    asset_outside_root,
    file_size_unavailable,
    size_overflow,
    file_too_large,
    asset_count_limit_exceeded,
    total_asset_bytes_limit_exceeded,
    file_open_failed,
    file_read_failed,
    file_changed_during_read,
    invalid_engine_document,
    invalid_atlas_bake_document,
    invalid_scenario_document,
    builtin_asset_catalog_not_found,
    invalid_builtin_asset_catalog,
    builtin_asset_digest_required,
    builtin_asset_not_cataloged,
    builtin_asset_payload_unavailable,
    builtin_asset_payload_hash_mismatch,
    memory_allocation_failed,
    filesystem_failure,
    platform_unavailable,
};

struct NativeInputError {
    NativeInputErrorKind kind = NativeInputErrorKind::software;
    NativeInputErrorCode code = NativeInputErrorCode::filesystem_failure;
    NativeInputSubject subject = NativeInputSubject::engine_document;
    std::filesystem::path path;
    std::string asset_id;
    std::string message;
    std::optional<authoring::DiagnosticReport> diagnostics;

    friend bool operator==(const NativeInputError &,
                           const NativeInputError &) = default;
};

[[nodiscard]] std::string_view
native_input_error_code_label(NativeInputErrorCode code) noexcept;

struct OwnedAssetPayload {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;

    friend bool operator==(const OwnedAssetPayload &,
                           const OwnedAssetPayload &) = default;
};

struct NativeSourceDocument {
    std::filesystem::path canonical_path;
    std::vector<std::byte> bytes;
    contract::Sha256Digest sha256;

    friend bool operator==(const NativeSourceDocument &,
                           const NativeSourceDocument &) = default;
};

struct NativeEngineInput {
    authoring::EnginePackageDocument document;
    std::vector<OwnedAssetPayload> assets;
    NativeSourceDocument source;

    // The returned spans borrow this object's strings and byte vectors. Regenerate
    // them after moving or mutating NativeEngineInput.
    [[nodiscard]] std::vector<compile::AssetPayloadView> asset_views() const;
};

struct NativeAtlasScenarioInput {
    std::string source_id;
    authoring::ScenarioDocument document;
    NativeSourceDocument source;
};

struct NativeAtlasBakeInput {
    authoring::AtlasBakeDocument document;
    std::vector<NativeAtlasScenarioInput> scenarios;
    NativeSourceDocument source;
};

using NativeEngineInputResult =
    std::variant<NativeEngineInput, NativeInputError>;
using NativeScenarioInputResult =
    std::variant<authoring::ScenarioDocument, NativeInputError>;
using NativeAtlasBakeInputResult =
    std::variant<NativeAtlasBakeInput, NativeInputError>;
using BuiltinAssetCatalogLocationResult =
    std::variant<std::filesystem::path, NativeInputError>;

[[nodiscard]] NativeEngineInputResult load_native_engine_input(
    const std::filesystem::path &engine_path,
    const std::filesystem::path &asset_root,
    NativeInputLimits limits = {});

// Resolves every declared engine asset by exact kind, stable ID, and authored
// SHA-256 in a tracked content-addressed catalog. Authored URI values are not
// consulted by this production-oriented path.
[[nodiscard]] NativeEngineInputResult
load_native_engine_input_from_builtin_catalog(
    const std::filesystem::path &engine_path,
    const std::filesystem::path &catalog_path,
    NativeInputLimits limits = {});

// Discovers the catalog beside the current executable in a build tree or under
// the configured install datadir in a relocatable installed prefix, then
// resolves the engine through it.
[[nodiscard]] NativeEngineInputResult load_native_engine_input_with_builtin_assets(
    const std::filesystem::path &engine_path,
    NativeInputLimits limits = {});

// Deterministic discovery seam used by packaging and tests. The executable path
// need not exist, but must be absolute and name a file within its containing bin
// directory.
[[nodiscard]] BuiltinAssetCatalogLocationResult
discover_builtin_asset_catalog(
    const std::filesystem::path &executable_path);

[[nodiscard]] NativeScenarioInputResult load_native_scenario_input(
    const std::filesystem::path &scenario_path,
    NativeInputLimits limits = {});

// Loads the atlas document and every relative scenario source beneath the atlas
// document directory in exact authored order.
[[nodiscard]] NativeAtlasBakeInputResult load_native_atlas_bake_input(
    const std::filesystem::path &atlas_bake_path,
    NativeInputLimits limits = {});

enum class NativeOutputErrorKind : std::uint8_t {
    cant_create,
    temp_fail,
};

enum class NativeOutputErrorCode : std::uint8_t {
    empty_path,
    invalid_final_component,
    parent_not_found,
    parent_not_directory,
    destination_exists,
    parent_canonicalization_failed,
    destination_status_failed,
    filesystem_failure,
};

struct NativeOutputError {
    NativeOutputErrorKind kind = NativeOutputErrorKind::cant_create;
    NativeOutputErrorCode code = NativeOutputErrorCode::filesystem_failure;
    std::filesystem::path path;
    std::string message;

    friend bool operator==(const NativeOutputError &,
                           const NativeOutputError &) = default;
};

[[nodiscard]] std::string_view
native_output_error_code_label(NativeOutputErrorCode code) noexcept;

struct NativeOutputDirectory {
    std::filesystem::path publication_root;
    std::string publication_name;

    friend bool operator==(const NativeOutputDirectory &,
                           const NativeOutputDirectory &) = default;
};

using NativeOutputDirectoryResult =
    std::variant<NativeOutputDirectory, NativeOutputError>;

// Splits an exact final output directory into the existing canonical parent and a
// new conservative portable leaf suitable for DirectoryRenderSink.
[[nodiscard]] NativeOutputDirectoryResult preflight_native_output_directory(
    const std::filesystem::path &output_directory);

} // namespace engine_sim_offline::cli
