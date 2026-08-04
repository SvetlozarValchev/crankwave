#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/package_bake.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
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

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace contract = engine_sim_offline::contract;
using engine_sim_offline::CompiledPackageBake;
using engine_sim_offline::CompiledPackageBakeAudioBus;
using engine_sim_offline::PackageBakeCompileResult;
using engine_sim_offline::PackageBakeScenarioInputView;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const double left, const double right) noexcept {
    return std::abs(left - right) <= 1.0e-12;
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string diagnostics(const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

template <class Value>
[[nodiscard]] Value require(std::variant<Value, authoring::DiagnosticReport> result,
                            const std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] const authoring::DiagnosticReport &
require_report(const PackageBakeCompileResult &result) {
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid package-bake plan was accepted"};
    }
    return *report;
}

[[nodiscard]] bool has_diagnostic(const authoring::DiagnosticReport &report,
                                  const authoring::DiagnosticCode code,
                                  const std::string_view path) {
    return std::ranges::any_of(report.diagnostics, [&](const auto &diagnostic) {
        return diagnostic.code == code && diagnostic.json_pointer == path;
    });
}

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

struct Fixture {
    compile::CompiledEngine engine;
    authoring::PackageBakeDocument package;
    std::vector<authoring::ScenarioDocument> scenarios;
};

[[nodiscard]] Fixture load_fixture(const std::filesystem::path &repository_root) {
    const auto engine_root = repository_root / "data/engines/bmw-m52tub28-cleanroom";
    const auto engine_path = engine_root / "engine.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "package fixture engine parse failed");
    auto package = require(authoring::parse_package_bake_document(
                               read_text(engine_root / "package-bake.json")),
                           "package fixture plan parse failed");

    std::vector<OwnedAsset> assets;
    assets.reserve(engine_document.presentation.assets.size() +
                   engine_document.engine.accessory_configurations.size());
    for (const auto &asset : engine_document.presentation.assets) {
        assets.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_root / asset.uri)});
    }
    for (const auto &asset : engine_document.engine.accessory_configurations) {
        assets.push_back({compile::AssetKind::accessory_configuration, asset.id.value,
                          read_bytes(engine_root / asset.uri)});
    }
    std::vector<compile::AssetPayloadView> asset_views;
    asset_views.reserve(assets.size());
    for (const auto &asset : assets) {
        asset_views.push_back({asset.kind, asset.id, asset.bytes});
    }
    auto engine = require(compile::compile_engine(engine_document, asset_views),
                          "package fixture engine compile failed");

    std::vector<authoring::ScenarioDocument> scenarios;
    scenarios.reserve(package.scenario_sources.size());
    for (const auto &source : package.scenario_sources) {
        scenarios.push_back(require(
            authoring::parse_scenario_document(read_text(engine_root / source.uri)),
            "package fixture scenario parse failed"));
    }
    return {std::move(engine), std::move(package), std::move(scenarios)};
}

[[nodiscard]] std::vector<PackageBakeScenarioInputView>
reverse_inputs(const Fixture &fixture) {
    std::vector<PackageBakeScenarioInputView> inputs;
    inputs.reserve(fixture.package.scenario_sources.size());
    for (std::size_t offset = 0; offset < fixture.package.scenario_sources.size();
         ++offset) {
        const auto index = fixture.package.scenario_sources.size() - 1U - offset;
        inputs.push_back({fixture.package.scenario_sources[index].id.value,
                          &fixture.scenarios[index]});
    }
    return inputs;
}

void test_real_bmw_plan_compiles_and_retains_authored_order(
    const std::filesystem::path &repository_root) {
    const auto fixture = load_fixture(repository_root);
    const auto inputs = reverse_inputs(fixture);
    const auto plan = require(engine_sim_offline::compile_package_bake(
                                  fixture.package, fixture.engine, inputs),
                              "valid BMW package compile failed");

    expect(plan.id() == "bmw-m52tub28-cleanroom-normal-running" &&
               plan.engine().id() == "bmw-m52tub28-cleanroom" &&
               plan.public_seed() == 12648430U,
           "compiled package lost its exact package, engine, or seed identity");
    expect(plan.audio_sample_rate() == compile::SiRate{192000U, 1U} &&
               plan.audio_buses().size() == 1U &&
               plan.audio_buses().front() == CompiledPackageBakeAudioBus{
                                                     "master-engine-audition",
                                                     "master.engine.audition",
                                                     contract::OutputBusKind::
                                                         master_engine_audition},
           "compiled package lost its isolated A/B audio contract");
    expect(plan.method_geometry() == engine_sim_offline::kPackageBakeMethodGeometry &&
               near(plan.method_geometry().cycle_signal_alignment_frames, 1228.8) &&
               plan.method_geometry().edge_guard_frames == 3840U &&
               near(plan.rpm_range().playback_minimum_rpm, 700.0) &&
               near(plan.rpm_range().playback_maximum_rpm, 6500.0) &&
               near(plan.rpm_range().padded_minimum_rpm, 625.0) &&
               near(plan.rpm_range().padded_maximum_rpm, 6575.0),
           "compiled package changed the fixed current method geometry");

    const auto sources = plan.scenario_sources();
    expect(sources.size() == 4U && sources[0].id == "coast-fall-source" &&
               sources[1].id == "part-rise-source" &&
               sources[2].id == "power-rise-source" && sources[3].id == "idle-source",
           "compiled scenario sources followed mapping order instead of authored "
           "order");
    const auto planes = plan.running_planes();
    expect(planes.size() == 3U && planes[0].id == "coast" &&
               planes[0].load_coordinate == -1.0 &&
               planes[0].direction == authoring::PackageBakeRunningDirection::falling &&
               planes[0].scenario_source_index == 0U &&
               planes[0].scenario.id() == "bmw-m52tub28-cleanroom-package-coast-fall" &&
               planes[2].id == "power" && plan.idle_scenario_source_index() == 3U &&
               plan.idle_scenario().id() == "bmw-m52tub28-cleanroom-package-idle",
           "compiled package lost ordered plane binding or idle ownership");
}

void test_source_graph_and_capture_invariants_are_diagnostic(
    const std::filesystem::path &repository_root) {
    auto fixture = load_fixture(repository_root);
    auto inputs = reverse_inputs(fixture);
    inputs.erase(std::ranges::find(inputs, std::string_view{"coast-fall-source"},
                                   &PackageBakeScenarioInputView::source_id));
    const auto missing = engine_sim_offline::compile_package_bake(
        fixture.package, fixture.engine, inputs);
    expect(has_diagnostic(require_report(missing),
                          authoring::DiagnosticCode::missing_value,
                          "/scenario_sources/0/document"),
           "missing in-memory scenario mapping lacks its authored source path");

    inputs = reverse_inputs(fixture);
    fixture.scenarios[0].public_seed += 1U;
    const auto mismatched_seed = engine_sim_offline::compile_package_bake(
        fixture.package, fixture.engine, inputs);
    expect(has_diagnostic(require_report(mismatched_seed),
                          authoring::DiagnosticCode::inconsistent_value,
                          "/scenario_sources/0/document/public_seed"),
           "source seed mismatch lacks its external-document path");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const std::filesystem::path repository_root{argv[1]};
        test_real_bmw_plan_compiles_and_retains_authored_order(repository_root);
        test_source_graph_and_capture_invariants_are_diagnostic(repository_root);
        std::cout << "package-bake compilation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "package-bake compilation tests failed: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
