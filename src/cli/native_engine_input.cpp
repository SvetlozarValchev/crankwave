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

} // namespace

std::optional<NativeInputError> append_confined_engine_asset(
    NativeEngineInput &input, const std::filesystem::path &engine_document,
    const OpenedAssetRoot &asset_root, compile::AssetKind kind,
    NativeInputSubject subject, std::string_view id, std::string_view uri,
    const NativeInputLimits &limits, std::uintmax_t &total_bytes) {
    const auto remaining =
        total_bytes <= limits.maximum_total_asset_bytes
            ? limits.maximum_total_asset_bytes - total_bytes
            : 0U;
    const auto read_limit = std::min(limits.maximum_asset_bytes, remaining);
    auto file_result =
        read_confined_asset(asset_root, engine_document, uri, subject, id,
                            read_limit);
    if (auto *error = std::get_if<NativeInputError>(&file_result)) {
        if (error->code == NativeInputErrorCode::file_too_large &&
            remaining < limits.maximum_asset_bytes) {
            error->code =
                NativeInputErrorCode::total_asset_bytes_limit_exceeded;
            error->message = "declared assets exceed the total byte limit";
        }
        return std::move(*error);
    }

    auto file = std::get<ReadFile>(std::move(file_result));
    if (file.bytes.size() > remaining) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::total_asset_bytes_limit_exceeded, subject,
            file.canonical_path, "declared assets exceed the total byte limit",
            std::string(id));
    }
    total_bytes += static_cast<std::uintmax_t>(file.bytes.size());
    input.assets.push_back({kind, std::string(id), std::move(file.bytes)});
    return std::nullopt;
}

NativeEngineInputResult load_engine_document_impl(
    const std::filesystem::path &engine_path, const NativeInputLimits &limits) {
    auto engine_file = read_exact_regular_file(
        engine_path, NativeInputSubject::engine_document, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&engine_file)) {
        return std::move(*error);
    }
    auto read_engine = std::get<ReadFile>(std::move(engine_file));

    auto parsed = authoring::parse_engine_document(
        bytes_to_string(read_engine.bytes), limits.authoring_limits);
    if (auto *diagnostics =
            std::get_if<authoring::DiagnosticReport>(&parsed)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::invalid_engine_document,
            NativeInputSubject::engine_document, read_engine.canonical_path,
            "engine document is invalid", {}, std::move(*diagnostics));
    }

    const auto engine_sha256 = contract::sha256(read_engine.bytes);
    NativeEngineInput result{
        std::get<authoring::EnginePackageDocument>(std::move(parsed)), {},
        {read_engine.canonical_path, std::move(read_engine.bytes),
         engine_sha256},
        std::nullopt};
    const auto audio_count = result.document.presentation.assets.size();
    const auto accessory_count =
        result.document.engine.accessory_configurations.size();
    if (audio_count > std::numeric_limits<std::size_t>::max() - accessory_count) {
        return input_error(NativeInputErrorKind::data_error,
                           NativeInputErrorCode::size_overflow,
                           NativeInputSubject::engine_document,
                           result.source.canonical_path,
                           "declared asset count overflows this process");
    }
    const auto asset_count = audio_count + accessory_count;
    if (asset_count > limits.maximum_asset_count) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::asset_count_limit_exceeded,
            NativeInputSubject::engine_document, result.source.canonical_path,
            "engine document declares too many assets");
    }
    result.assets.reserve(asset_count);
    return result;
}

NativeEngineInputResult load_engine_impl(
    const std::filesystem::path &engine_path,
    const std::filesystem::path &asset_root, const NativeInputLimits &limits) {
    auto document_result = load_engine_document_impl(engine_path, limits);
    if (auto *error = std::get_if<NativeInputError>(&document_result)) {
        return std::move(*error);
    }
    auto result = std::get<NativeEngineInput>(std::move(document_result));

    auto root_result = open_asset_root(asset_root);
    if (auto *error = std::get_if<NativeInputError>(&root_result)) {
        return std::move(*error);
    }
    auto root = std::get<OpenedAssetRoot>(std::move(root_result));

    std::uintmax_t total_bytes = 0;
    for (const auto &asset : result.document.presentation.assets) {
        auto error = append_confined_engine_asset(
            result, result.source.canonical_path, root, compile::AssetKind::audio,
            NativeInputSubject::audio_asset, asset.id.value, asset.uri, limits,
            total_bytes);
        if (error) {
            return std::move(*error);
        }
    }
    for (const auto &asset :
         result.document.engine.accessory_configurations) {
        auto error = append_confined_engine_asset(
            result, result.source.canonical_path, root,
            compile::AssetKind::accessory_configuration,
            NativeInputSubject::accessory_configuration_asset, asset.id.value,
            asset.uri, limits, total_bytes);
        if (error) {
            return std::move(*error);
        }
    }
    return result;
}

NativeScenarioInputResult load_scenario_impl(
    const std::filesystem::path &scenario_path,
    const NativeInputLimits &limits) {
    auto scenario_file = read_exact_regular_file(
        scenario_path, NativeInputSubject::scenario_document, {},
        limits.maximum_document_bytes);
    if (auto *error = std::get_if<NativeInputError>(&scenario_file)) {
        return std::move(*error);
    }
    auto read_scenario = std::get<ReadFile>(std::move(scenario_file));
    auto parsed = authoring::parse_scenario_document(
        bytes_to_string(read_scenario.bytes), limits.authoring_limits);
    if (auto *diagnostics =
            std::get_if<authoring::DiagnosticReport>(&parsed)) {
        return input_error(
            NativeInputErrorKind::data_error,
            NativeInputErrorCode::invalid_scenario_document,
            NativeInputSubject::scenario_document, read_scenario.canonical_path,
            "scenario document is invalid", {}, std::move(*diagnostics));
    }
    return std::get<authoring::ScenarioDocument>(std::move(parsed));
}

} // namespace engine_sim_offline::cli::detail
