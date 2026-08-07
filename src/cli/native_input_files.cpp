#include "native_input_files.hpp"

#include "native_input_files_support.hpp"

#include <new>

namespace engine_sim_offline::cli {

std::string_view
native_input_error_code_label(NativeInputErrorCode code) noexcept {
    switch (code) {
    case NativeInputErrorCode::empty_path:
        return "empty-path";
    case NativeInputErrorCode::path_not_found:
        return "path-not-found";
    case NativeInputErrorCode::symbolic_link_not_allowed:
        return "symbolic-link-not-allowed";
    case NativeInputErrorCode::not_regular_file:
        return "not-regular-file";
    case NativeInputErrorCode::asset_root_not_directory:
        return "asset-root-not-directory";
    case NativeInputErrorCode::invalid_local_asset_uri:
        return "invalid-local-asset-uri";
    case NativeInputErrorCode::asset_outside_root:
        return "asset-outside-root";
    case NativeInputErrorCode::file_size_unavailable:
        return "file-size-unavailable";
    case NativeInputErrorCode::size_overflow:
        return "size-overflow";
    case NativeInputErrorCode::file_too_large:
        return "file-too-large";
    case NativeInputErrorCode::asset_count_limit_exceeded:
        return "asset-count-limit-exceeded";
    case NativeInputErrorCode::total_asset_bytes_limit_exceeded:
        return "total-asset-bytes-limit-exceeded";
    case NativeInputErrorCode::file_open_failed:
        return "file-open-failed";
    case NativeInputErrorCode::file_read_failed:
        return "file-read-failed";
    case NativeInputErrorCode::file_changed_during_read:
        return "file-changed-during-read";
    case NativeInputErrorCode::invalid_engine_document:
        return "invalid-engine-document";
    case NativeInputErrorCode::invalid_atlas_bake_document:
        return "invalid-atlas-bake-document";
    case NativeInputErrorCode::invalid_scenario_document:
        return "invalid-scenario-document";
    case NativeInputErrorCode::builtin_asset_catalog_not_found:
        return "builtin-asset-catalog-not-found";
    case NativeInputErrorCode::invalid_builtin_asset_catalog:
        return "invalid-builtin-asset-catalog";
    case NativeInputErrorCode::builtin_asset_digest_required:
        return "builtin-asset-digest-required";
    case NativeInputErrorCode::builtin_asset_not_cataloged:
        return "builtin-asset-not-cataloged";
    case NativeInputErrorCode::builtin_asset_payload_unavailable:
        return "builtin-asset-payload-unavailable";
    case NativeInputErrorCode::builtin_asset_payload_hash_mismatch:
        return "builtin-asset-payload-hash-mismatch";
    case NativeInputErrorCode::memory_allocation_failed:
        return "memory-allocation-failed";
    case NativeInputErrorCode::filesystem_failure:
        return "filesystem-failure";
    case NativeInputErrorCode::platform_unavailable:
        return "platform-unavailable";
    }
    return "unknown-input-error";
}

std::string_view
native_output_error_code_label(NativeOutputErrorCode code) noexcept {
    switch (code) {
    case NativeOutputErrorCode::empty_path:
        return "empty-path";
    case NativeOutputErrorCode::invalid_final_component:
        return "invalid-final-component";
    case NativeOutputErrorCode::parent_not_found:
        return "parent-not-found";
    case NativeOutputErrorCode::parent_not_directory:
        return "parent-not-directory";
    case NativeOutputErrorCode::destination_exists:
        return "destination-exists";
    case NativeOutputErrorCode::parent_canonicalization_failed:
        return "parent-canonicalization-failed";
    case NativeOutputErrorCode::destination_status_failed:
        return "destination-status-failed";
    case NativeOutputErrorCode::filesystem_failure:
        return "filesystem-failure";
    }
    return "unknown-output-error";
}

std::vector<compile::AssetPayloadView> NativeEngineInput::asset_views() const {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

NativeEngineInputResult load_native_engine_input(
    const std::filesystem::path &engine_path,
    const std::filesystem::path &asset_root, NativeInputLimits limits) {
    try {
        return detail::load_engine_impl(engine_path, asset_root, limits);
    } catch (const std::bad_alloc &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::memory_allocation_failed,
            NativeInputSubject::engine_document, engine_path,
            "native engine input allocation failed");
    } catch (const std::filesystem::filesystem_error &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::engine_document, engine_path,
            "native engine input filesystem operation failed");
    } catch (...) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::engine_document, engine_path,
            "native engine input loading failed unexpectedly");
    }
}

NativeScenarioInputResult load_native_scenario_input(
    const std::filesystem::path &scenario_path, NativeInputLimits limits) {
    try {
        return detail::load_scenario_impl(scenario_path, limits);
    } catch (const std::bad_alloc &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::memory_allocation_failed,
            NativeInputSubject::scenario_document, scenario_path,
            "native scenario input allocation failed");
    } catch (const std::filesystem::filesystem_error &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::scenario_document, scenario_path,
            "native scenario input filesystem operation failed");
    } catch (...) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::scenario_document, scenario_path,
            "native scenario input loading failed unexpectedly");
    }
}

} // namespace engine_sim_offline::cli
