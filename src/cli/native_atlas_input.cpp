#include "native_input_files_support.hpp"

#include <new>
#include <string>
#include <utility>

namespace engine_sim_offline::cli {
namespace {

[[nodiscard]] std::string bytes_to_string(
    const std::vector<std::byte> &bytes) {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] NativeAtlasBakeInputResult load_impl(
    const std::filesystem::path &atlas_bake_path,
    const NativeInputLimits &limits) {
    auto atlas_file = detail::read_exact_regular_file(
        atlas_bake_path, NativeInputSubject::atlas_bake_document, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&atlas_file)) {
        return std::move(*error);
    }
    auto read_atlas = std::get<detail::ReadFile>(std::move(atlas_file));
    auto parsed = authoring::parse_atlas_bake_document(
        bytes_to_string(read_atlas.bytes), limits.authoring_limits);
    if (auto *diagnostics =
            std::get_if<authoring::DiagnosticReport>(&parsed)) {
        return detail::input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::invalid_atlas_bake_document,
            NativeInputSubject::atlas_bake_document,
            read_atlas.canonical_path, "atlas-bake document is invalid", {},
            std::move(*diagnostics));
    }

    auto document =
        std::get<authoring::AtlasBakeDocument>(std::move(parsed));
    auto root_result =
        detail::open_asset_root(read_atlas.canonical_path.parent_path());
    if (auto *error = std::get_if<NativeInputError>(&root_result)) {
        return std::move(*error);
    }
    auto root = std::get<detail::OpenedAssetRoot>(std::move(root_result));

    NativeAtlasBakeInput result;
    result.document = std::move(document);
    result.source = {read_atlas.canonical_path, std::move(read_atlas.bytes), {}};
    result.source.sha256 = contract::sha256(result.source.bytes);
    result.scenarios.reserve(result.document.scenario_sources.size());
    for (const auto &authored : result.document.scenario_sources) {
        auto scenario_file = detail::read_confined_asset(
            root, result.source.canonical_path, authored.uri,
            NativeInputSubject::scenario_document, authored.id.value,
            limits.maximum_document_bytes);
        if (auto *error = std::get_if<NativeInputError>(&scenario_file)) {
            return std::move(*error);
        }
        auto read_scenario =
            std::get<detail::ReadFile>(std::move(scenario_file));
        auto scenario_parsed = authoring::parse_scenario_document(
            bytes_to_string(read_scenario.bytes), limits.authoring_limits);
        if (auto *diagnostics =
                std::get_if<authoring::DiagnosticReport>(&scenario_parsed)) {
            return detail::input_error(
                NativeInputErrorKind::data_error,
                NativeInputErrorCode::invalid_scenario_document,
                NativeInputSubject::scenario_document,
                read_scenario.canonical_path,
                "atlas source scenario document is invalid", authored.id.value,
                std::move(*diagnostics));
        }
        const auto digest = contract::sha256(read_scenario.bytes);
        result.scenarios.push_back(
            {authored.id.value,
             std::get<authoring::ScenarioDocument>(
                 std::move(scenario_parsed)),
             {read_scenario.canonical_path, std::move(read_scenario.bytes),
              digest}});
    }
    return result;
}

} // namespace

NativeAtlasBakeInputResult load_native_atlas_bake_input(
    const std::filesystem::path &atlas_bake_path, NativeInputLimits limits) {
    try {
        return load_impl(atlas_bake_path, limits);
    } catch (const std::bad_alloc &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::memory_allocation_failed,
            NativeInputSubject::atlas_bake_document, atlas_bake_path,
            "native atlas-bake input allocation failed");
    } catch (const std::filesystem::filesystem_error &) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::atlas_bake_document, atlas_bake_path,
            "native atlas-bake input filesystem operation failed");
    } catch (...) {
        return detail::input_error(
            NativeInputErrorKind::software,
            NativeInputErrorCode::filesystem_failure,
            NativeInputSubject::atlas_bake_document, atlas_bake_path,
            "native atlas-bake input loading failed unexpectedly");
    }
}

} // namespace engine_sim_offline::cli
