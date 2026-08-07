#include "compile/compiled_scenario_view.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"cannot open IR selection fixture"};
    }
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

template <class Value, class Error>
Value require(std::variant<Value, Error> result, const char *message) {
    if (!std::holds_alternative<Value>(result)) {
        throw std::runtime_error{message};
    }
    return std::get<Value>(std::move(result));
}

std::vector<OwnedAsset> load_declared_assets(
    const authoring::EnginePackageDocument &document,
    const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> assets;
    for (const auto &asset : document.presentation.assets) {
        assets.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        assets.push_back({compile::AssetKind::accessory_configuration,
                          asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return assets;
}

std::vector<compile::AssetPayloadView>
views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

const contract::PresentationCalibration &compile_presentation(
    const authoring::EnginePackageDocument &engine_document,
    const authoring::ScenarioDocument &scenario_document,
    const std::vector<OwnedAsset> &assets,
    compile::CompiledScenario &retained_scenario) {
    const auto asset_views = views(assets);
    auto engine = require(compile::compile_engine(engine_document, asset_views),
                          "IR selection engine compilation failed");
    retained_scenario = require(
        compile::compile_scenario(engine, scenario_document),
        "IR selection scenario compilation failed");
    return compile::detail::CompiledScenarioViewAccess::inputs(retained_scenario)
        .engine.presentation;
}

void run_test(const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_path =
        repository_root /
        "data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json";
    const auto long_ir_path =
        repository_root /
        "assets/builtin/ir-library/payloads/"
        "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819.wav";
    const auto pcm24_ir_path =
        repository_root /
        "assets/builtin/ir-library/payloads/"
        "7910907c1d2dbd3c4a52eb428cb89a40ab3655d1e0e62003a308bd41f42dbb74.wav";

    auto engine_document = require(
        authoring::parse_engine_document(read_text(engine_path)),
        "IR selection engine JSON parse failed");
    const auto scenario_document = require(
        authoring::parse_scenario_document(read_text(scenario_path)),
        "IR selection scenario JSON parse failed");
    auto original_assets = load_declared_assets(engine_document, engine_path);

    compile::CompiledScenario legacy_scenario = require(
        compile::compile_scenario(
            require(compile::compile_engine(engine_document,
                                            views(original_assets)),
                    "legacy engine compile failed"),
            scenario_document),
        "legacy scenario compile failed");
    const auto &legacy_presentation =
        compile::detail::CompiledScenarioViewAccess::inputs(legacy_scenario)
            .engine.presentation;
    const auto &legacy_authority =
        presentation::implemented_presentation_method_identities();
    expect(legacy_presentation.methods.impulse_response_conversion.value ==
                   legacy_authority.impulse_response_conversion &&
               legacy_presentation.methods.convolution.value ==
                   legacy_authority.convolution,
           "legacy-only engine no longer retains the exact v1 transfer authority");

    auto pcm24_document = engine_document;
    pcm24_document.presentation.assets.front().id.value = "archive-test-engine";
    pcm24_document.presentation.assets.front().uri =
        "unused-content-addressed-pcm24-test-locator";
    pcm24_document.presentation.assets.front().sha256 =
        "7910907c1d2dbd3c4a52eb428cb89a40ab3655d1e0e62003a308bd41f42dbb74";
    for (auto &route : pcm24_document.presentation.routes) {
        expect(route.impulse_response.has_value(),
               "PCM24 selection fixture route has no configured IR");
        route.impulse_response->value = "archive-test-engine";
    }
    auto pcm24_assets = original_assets;
    bool replaced_pcm24 = false;
    for (auto &asset : pcm24_assets) {
        if (asset.kind == compile::AssetKind::audio) {
            asset.id = "archive-test-engine";
            asset.bytes = read_bytes(pcm24_ir_path);
            replaced_pcm24 = true;
        }
    }
    expect(replaced_pcm24, "PCM24 selection fixture lost its audio payload");
    compile::CompiledScenario pcm24_scenario = legacy_scenario;
    const auto &pcm24_presentation = compile_presentation(
        pcm24_document, scenario_document, pcm24_assets, pcm24_scenario);
    const auto &extended_authority =
        presentation::extended_presentation_method_identities();
    expect(pcm24_presentation.methods.impulse_response_conversion.value ==
                   extended_authority.impulse_response_conversion &&
               pcm24_presentation.methods.convolution.value ==
                   extended_authority.convolution &&
               pcm24_presentation.assets.size() == 1U &&
               pcm24_presentation.assets.front().media.value.encoding ==
                   contract::AudioSampleEncoding::pcm_s24le,
           "engine JSON PCM24 selection did not publish hybrid-v2 media and "
           "transfer authority");

    auto long_definition = engine_document.presentation.assets.front();
    long_definition.id.value = "smooth-45";
    long_definition.uri = "unused-content-addressed-test-locator";
    long_definition.sha256 =
        "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819";
    engine_document.presentation.assets.push_back(long_definition);
    expect(engine_document.presentation.routes.size() == 2U &&
               engine_document.presentation.routes[1]
                   .impulse_response.has_value(),
           "mixed-route fixture no longer has two configured exhaust routes");
    engine_document.presentation.routes[1].impulse_response->value = "smooth-45";
    original_assets.push_back({compile::AssetKind::audio, "smooth-45",
                               read_bytes(long_ir_path)});

    compile::CompiledScenario mixed_scenario = legacy_scenario;
    const auto &mixed_presentation = compile_presentation(
        engine_document, scenario_document, original_assets, mixed_scenario);
    expect(mixed_presentation.methods.impulse_response_conversion.value ==
                   extended_authority.impulse_response_conversion &&
               mixed_presentation.methods.convolution.value ==
                   extended_authority.convolution &&
               presentation::exactly_matches_implemented_presentation_methods(
                   mixed_presentation.methods),
           "mixed legacy/long routes did not publish one coherent hybrid-v2 "
           "transfer authority");
    expect(mixed_presentation.assets.size() == 2U &&
               mixed_presentation.routes.size() == 2U &&
               mixed_presentation.routes[0].impulse_response_asset_id !=
                   mixed_presentation.routes[1].impulse_response_asset_id,
           "mixed-route compilation lost its per-route selected IR bindings");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected one repository root"};
        }
        run_test(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "IR method selection test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
