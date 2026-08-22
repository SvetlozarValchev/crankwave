#include "crankwave/authoring/parse.hpp"
#include "crankwave/compile.hpp"
#include "crankwave/responsive/presentation_transfer.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

void expect(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"cannot open presentation transfer fixture"};
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

template <class Value, class Error>
[[nodiscard]] Value require(std::variant<Value, Error> result, const char *message) {
    if (!std::holds_alternative<Value>(result)) {
        throw std::runtime_error{message};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> result;
    for (const auto &asset : document.presentation.assets) {
        result.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back({compile::AssetKind::accessory_configuration, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return result;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

[[nodiscard]] compile::CompiledScenario
compile_fixture(const authoring::EnginePackageDocument &engine_document,
                const authoring::ScenarioDocument &scenario_document,
                const std::vector<OwnedAsset> &assets) {
    const auto asset_views = views(assets);
    auto engine = require(compile::compile_engine(engine_document, asset_views),
                          "presentation transfer engine compile failed");
    return require(compile::compile_scenario(engine, scenario_document),
                   "presentation transfer scenario compile failed");
}

[[nodiscard]] responsive::ResponsiveCompiledPresentation
compile_transfer(const compile::CompiledScenario &scenario) {
    return require(responsive::compile_responsive_presentation_transfer(scenario),
                   "responsive presentation transfer compile failed");
}

void run_test(const std::filesystem::path &repository_root) {
    const auto engine_path = repository_root / "data/engines/bmw-m52b28/engine.json";
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

    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "presentation transfer engine JSON parse failed");
    const auto scenario_document =
        require(authoring::parse_scenario_document(read_text(scenario_path)),
                "presentation transfer scenario JSON parse failed");
    auto assets = load_assets(engine_document, engine_path);

    const auto legacy_scenario =
        compile_fixture(engine_document, scenario_document, assets);
    const auto legacy = compile_transfer(legacy_scenario);
    const auto legacy_repeat = compile_transfer(legacy_scenario);
    expect(legacy == legacy_repeat,
           "legacy responsive transfer is not byte-repeatable");
    expect(legacy.engine_id == "bmw-m52b28" &&
               legacy.audition_bus_id == "master-engine-audition" &&
               legacy.audition_dry_bus_order.size() == 2U &&
               legacy.audition_dry_bus_order[0] == "exhaust.reference.0.dry" &&
               legacy.audition_dry_bus_order[1] == "exhaust.reference.1.dry" &&
               legacy.routes.size() == 2U && legacy.transfers.size() == 1U &&
               legacy.routes[0].transfer_index == 0U &&
               legacy.routes[1].transfer_index == 0U,
           "legacy responsive presentation topology changed");
    const auto &fixed = legacy.transfers.front();
    expect(fixed.shape == responsive::ResponsiveTransferShape::fixed_overlap_save &&
               fixed.fft_size == 65'536U && fixed.coefficient_count == 30'071U &&
               fixed.partition_frame_count == 0U && fixed.partition_count == 1U &&
               fixed.spectrum_bytes.size() == 65'536U * 2U * 8U &&
               digest_hex(fixed.spectrum_sha256) ==
                   "a1a12fc0224ecdf824e402562ed6b5fd31d915278693a8cfaaeea41a5cf957d2",
           "legacy smooth-39 transfer bytes changed");
    expect(legacy.captured_to_source_scale == 67'108'864.0 &&
               legacy.master_volume_linear == 1.0,
           "legacy publication calibration changed");

    auto long_definition = engine_document.presentation.assets.front();
    long_definition.id.value = "smooth-45";
    long_definition.uri = "unused-content-addressed-long-ir-locator";
    long_definition.sha256 =
        "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819";
    engine_document.presentation.assets.push_back(long_definition);
    expect(engine_document.presentation.routes.size() == 2U &&
               engine_document.presentation.routes[1].impulse_response.has_value(),
           "mixed transfer fixture lost its second configured route");
    engine_document.presentation.routes[1].impulse_response->value = "smooth-45";
    assets.push_back(
        {compile::AssetKind::audio, "smooth-45", read_bytes(long_ir_path)});

    const auto mixed =
        compile_transfer(compile_fixture(engine_document, scenario_document, assets));
    expect(mixed.routes.size() == 2U && mixed.transfers.size() == 2U,
           "mixed legacy/long presentation did not retain two transfers");
    const auto &mixed_fixed = mixed.transfers[mixed.routes[0].transfer_index];
    const auto &partitioned = mixed.transfers[mixed.routes[1].transfer_index];
    expect(mixed_fixed.shape ==
                   responsive::ResponsiveTransferShape::fixed_overlap_save &&
               digest_hex(mixed_fixed.spectrum_sha256) ==
                   "a1a12fc0224ecdf824e402562ed6b5fd31d915278693a8cfaaeea41a5cf957d2",
           "hybrid-v2 changed the legacy route's fixed spectrum");
    expect(
        partitioned.shape ==
                responsive::ResponsiveTransferShape::uniform_partitioned_overlap_save &&
            partitioned.fft_size == 8'192U &&
            partitioned.coefficient_count == 384'292U &&
            partitioned.partition_frame_count == 3'840U &&
            partitioned.partition_count == 101U &&
            partitioned.spectrum_bytes.size() == 101U * 8'192U * 2U * 8U,
        "long responsive transfer was truncated or mis-shaped");

    auto pcm24_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "PCM24 transfer engine JSON parse failed");
    pcm24_document.presentation.assets.front().id.value = "archive-test-engine";
    pcm24_document.presentation.assets.front().uri =
        "unused-content-addressed-pcm24-locator";
    pcm24_document.presentation.assets.front().sha256 =
        "7910907c1d2dbd3c4a52eb428cb89a40ab3655d1e0e62003a308bd41f42dbb74";
    for (auto &route : pcm24_document.presentation.routes) {
        expect(route.impulse_response.has_value(),
               "PCM24 transfer fixture route has no configured IR");
        route.impulse_response->value = "archive-test-engine";
    }
    auto pcm24_assets =
        load_assets(require(authoring::parse_engine_document(read_text(engine_path)),
                            "PCM24 base engine parse failed"),
                    engine_path);
    for (auto &asset : pcm24_assets) {
        if (asset.kind == compile::AssetKind::audio) {
            asset.id = "archive-test-engine";
            asset.bytes = read_bytes(pcm24_ir_path);
        }
    }
    const auto pcm24 = compile_transfer(
        compile_fixture(pcm24_document, scenario_document, pcm24_assets));
    expect(
        pcm24.transfers.size() == 1U &&
            pcm24.transfers.front().shape ==
                responsive::ResponsiveTransferShape::uniform_partitioned_overlap_save &&
            !pcm24.transfers.front().spectrum_bytes.empty(),
        "PCM24 responsive transfer did not compile completely");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected one repository root"};
        }
        run_test(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "responsive presentation transfer test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
