#include "native_input_files_support.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace engine_sim_offline::cli::detail {
namespace {

std::string bytes_to_string(const std::vector<std::byte> &bytes) {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

NativeSourceDocument retain_source(ReadFile file) {
    auto digest = contract::sha256(file.bytes);
    return {std::move(file.canonical_path), std::move(file.bytes), digest};
}

} // namespace

NativePackageBakeInputResult load_package_bake_impl(
    const std::filesystem::path &package_bake_path,
    const std::filesystem::path &asset_root,
    const NativeInputLimits &limits) {
    auto package_file = read_exact_regular_file(
        package_bake_path, NativeInputSubject::package_bake_document, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&package_file)) {
        return std::move(*error);
    }
    auto read_package = std::get<ReadFile>(std::move(package_file));

    auto parsed = authoring::parse_package_bake_document(
        bytes_to_string(read_package.bytes), limits.authoring_limits);
    if (auto *diagnostics =
            std::get_if<authoring::DiagnosticReport>(&parsed)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::invalid_package_bake_document,
            NativeInputSubject::package_bake_document,
            read_package.canonical_path, "package bake document is invalid", {},
            std::move(*diagnostics));
    }

    auto root_result = open_asset_root(asset_root);
    if (auto *error = std::get_if<NativeInputError>(&root_result)) {
        return std::move(*error);
    }
    auto root = std::get<OpenedAssetRoot>(std::move(root_result));

    NativePackageBakeInput result;
    result.document =
        std::get<authoring::PackageBakeDocument>(std::move(parsed));
    result.source = retain_source(std::move(read_package));

    if (result.document.scenario_sources.size() > limits.maximum_asset_count) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::asset_count_limit_exceeded,
            NativeInputSubject::package_bake_document,
            result.source.canonical_path,
            "package bake document declares too many scenario sources");
    }
    result.scenarios.reserve(result.document.scenario_sources.size());

    std::uintmax_t total_bytes = 0;
    for (const auto &declared : result.document.scenario_sources) {
        const auto remaining =
            total_bytes <= limits.maximum_total_asset_bytes
                ? limits.maximum_total_asset_bytes - total_bytes
                : 0U;
        const auto read_limit =
            std::min(limits.maximum_document_bytes, remaining);
        auto scenario_file = read_confined_asset(
            root, result.source.canonical_path, declared.uri,
            NativeInputSubject::package_scenario_document,
            declared.id.value, read_limit);
        if (auto *error = std::get_if<NativeInputError>(&scenario_file)) {
            if (error->code == NativeInputErrorCode::file_too_large &&
                remaining < limits.maximum_document_bytes) {
                error->code =
                    NativeInputErrorCode::total_asset_bytes_limit_exceeded;
                error->message =
                    "package scenario sources exceed the total byte limit";
            }
            return std::move(*error);
        }

        auto read_scenario = std::get<ReadFile>(std::move(scenario_file));
        if (read_scenario.bytes.size() > remaining) {
            return input_error(
                NativeInputErrorKind::data_error,
                NativeInputErrorCode::total_asset_bytes_limit_exceeded,
                NativeInputSubject::package_scenario_document,
                read_scenario.canonical_path,
                "package scenario sources exceed the total byte limit",
                declared.id.value);
        }
        total_bytes +=
            static_cast<std::uintmax_t>(read_scenario.bytes.size());

        auto scenario = authoring::parse_scenario_document(
            bytes_to_string(read_scenario.bytes), limits.authoring_limits);
        if (auto *diagnostics =
                std::get_if<authoring::DiagnosticReport>(&scenario)) {
            return input_error(
                NativeInputErrorKind::data_error,
                NativeInputErrorCode::invalid_scenario_document,
                NativeInputSubject::package_scenario_document,
                read_scenario.canonical_path,
                "package scenario document is invalid", declared.id.value,
                std::move(*diagnostics));
        }

        result.scenarios.push_back(
            {declared.id.value,
             std::get<authoring::ScenarioDocument>(std::move(scenario)),
             retain_source(std::move(read_scenario))});
    }
    return result;
}

} // namespace engine_sim_offline::cli::detail
