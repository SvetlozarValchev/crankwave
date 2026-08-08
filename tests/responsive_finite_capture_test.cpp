#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/responsive/finite_capture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
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

struct CompiledEngineFixture {
    authoring::EnginePackageDocument document;
    compile::CompiledEngine engine;
};

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string
diagnostics(const authoring::DiagnosticReport &report) {
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
[[nodiscard]] Value
require_authoring(std::variant<Value, authoring::DiagnosticReport> result,
                  const std::string_view context) {
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " +
                                 diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> assets;
    assets.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    for (const auto &asset : document.presentation.assets) {
        assets.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        assets.push_back(
            {compile::AssetKind::accessory_configuration, asset.id.value,
             read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return assets;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> views;
    views.reserve(assets.size());
    for (const auto &asset : assets) {
        views.push_back({asset.kind, asset.id, asset.bytes});
    }
    return views;
}

[[nodiscard]] CompiledEngineFixture
compile_engine_fixture(const std::filesystem::path &repository_root) {
    const auto path =
        repository_root / "data/engines/bmw-m52tub28-cleanroom/engine.json";
    auto document = require_authoring(
        authoring::parse_engine_document(read_text(path)), "engine parse failed");
    const auto assets = load_assets(document, path);
    const auto views = asset_views(assets);
    auto engine = require_authoring(compile::compile_engine(document, views),
                                    "engine compile failed");
    return {std::move(document), std::move(engine)};
}

template <class Mutator>
[[nodiscard]] compile::CompiledScenario
compile_scenario_fixture(const std::filesystem::path &repository_root,
                         const CompiledEngineFixture &fixture,
                         const std::filesystem::path &relative_path,
                         Mutator mutator) {
    auto document = require_authoring(
        authoring::parse_scenario_document(
            read_text(repository_root / relative_path)),
        "scenario parse failed");
    mutator(document);
    const auto references =
        authoring::validate_scenario_references(document, fixture.document);
    if (!references.ok()) {
        throw std::runtime_error{"mutated scenario reference validation failed"};
    }
    return require_authoring(
        compile::compile_scenario(fixture.engine, document),
        "scenario compile failed");
}

[[nodiscard]] responsive::FiniteResponsiveCapture
require_capture(responsive::FiniteResponsiveCaptureResult result,
                const std::string_view context) {
    if (const auto *failure =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&result)) {
        throw std::runtime_error{std::string{context} + ": " +
                                 failure->detail_code + ": " +
                                 failure->message};
    }
    return std::get<responsive::FiniteResponsiveCapture>(std::move(result));
}

void shorten_held(authoring::ScenarioDocument &document) {
    document.total_duration.value = document.audible_start.value + 0.2;
    document.audible_duration.value = 0.2;
}

void shorten_directional(authoring::ScenarioDocument &document) {
    auto *mode = std::get_if<authoring::HeldDynoMode>(&document.mode);
    expect(mode != nullptr && mode->target_engine_speed.points.size() == 5U &&
               mode->throttle_01.points.size() == 3U,
           "directional fixture shape changed");
    mode->target_engine_speed.points[2].time.value = 3.2;
    mode->target_engine_speed.points[3].time.value = 3.3;
    mode->target_engine_speed.points[4].time.value = 3.6;
    mode->throttle_01.points[1].time.value = 3.2;
    mode->throttle_01.points[2].time.value = 3.3;
    document.total_duration.value = 3.6;
    document.audible_duration.value = 0.6;
}

void shorten_lifecycle(authoring::ScenarioDocument &document) {
    document.total_duration.value = 1.6;
    document.audible_duration.value = 1.6;
}

void shorten_stopped_lifecycle(authoring::ScenarioDocument &document) {
    document.total_duration.value = 0.1;
    document.audible_duration.value = 0.1;
}

void verify_exact_block_and_cycle_accounting(
    const responsive::FiniteResponsiveCapture &capture) {
    expect(capture.blocks.size() == capture.total_block_count,
           "capture lost native blocks");
    std::uint64_t cycle_ordinal = 0U;
    for (std::size_t index = 0U; index < capture.blocks.size(); ++index) {
        const auto &block = capture.blocks[index];
        expect(block.block_ordinal == index,
               "capture reordered native blocks");
        expect(block.endpoint.delivery_frame ==
                   block.first_delivery_frame + block.delivery_frame_count,
               "reduced endpoint lost the exact delivery horizon");
        expect(block.telemetry.physics_step_end ==
                   block.first_physics_frame + block.physics_frame_count,
               "full endpoint lost the exact physics horizon");
        for (const auto &cycle : block.completed_cycles) {
            expect(cycle.completed_cycle_ordinal == cycle_ordinal,
                   "cycle evidence was duplicated, dropped, or reordered");
            ++cycle_ordinal;
        }
    }
    expect(!responsive::validate_finite_responsive_capture(capture).has_value(),
           "published capture failed its standalone validator");
}

void test_held_multi_bus_capture(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "held-idle-region-700rpm.json",
        shorten_held);
    constexpr std::array<std::string_view, 2> buses{
        "master.engine.audition", "master.engine.raw"};
    const auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "held multi-bus capture failed");

    expect(capture.motion_mode == EngineMotionMode::held_speed,
           "held capture lost its motion mode");
    expect(capture.selected_bus_ids.size() == 2U &&
               capture.selected_bus_ids[0] == buses[0] &&
               capture.selected_bus_ids[1] == buses[1] &&
               capture.buses[0].descriptor.id == buses[0] &&
               capture.buses[1].descriptor.id == buses[1],
           "capture did not preserve requested bus order");
    expect(capture.engine_provenance == fixture.engine.provenance() &&
               capture.scenario_provenance == scenario.provenance(),
           "capture did not preserve compiler provenance");
    expect(capture.preparation_block_count > 0U &&
               capture.blocks.front().phase ==
                   EngineSessionBlockPhase::preparation &&
               capture.blocks.back().phase == EngineSessionBlockPhase::audible,
           "capture did not retain preparation and audible evidence");
    verify_exact_block_and_cycle_accounting(capture);
}

void test_directional_capture(const std::filesystem::path &repository_root,
                              const CompiledEngineFixture &fixture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "canonical-loaded-rise-part-load-coast-1500-4500rpm.json",
        shorten_directional);
    constexpr std::array<std::string_view, 1> buses{"master.engine.raw"};
    const auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "directional capture failed");

    expect(capture.motion_mode == EngineMotionMode::held_dyno,
           "directional capture lost held-dyno mode");
    bool saw_rise = false;
    bool saw_fall = false;
    bool every_sidecar_present = true;
    std::optional<double> previous;
    for (const auto &block : capture.blocks) {
        if (block.phase != EngineSessionBlockPhase::audible) {
            continue;
        }
        every_sidecar_present &= block.telemetry.held_dyno.has_value();
        if (previous.has_value()) {
            saw_rise |= block.endpoint.engine_speed_rpm > *previous + 1.0;
            saw_fall |= block.endpoint.engine_speed_rpm < *previous - 1.0;
        }
        previous = block.endpoint.engine_speed_rpm;
    }
    expect(every_sidecar_present,
           "directional endpoints lost held-dyno sidecar evidence");
    expect(saw_rise, "directional endpoints did not preserve an RPM rise");
    expect(saw_fall, "directional endpoints did not preserve an RPM fall");
    verify_exact_block_and_cycle_accounting(capture);
}

responsive::FiniteResponsiveCapture test_lifecycle_capture(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture) {
    const auto stopped_scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "interactive-lifecycle-0rpm.json",
        shorten_stopped_lifecycle);
    constexpr std::array<std::string_view, 1> buses{
        "master.engine.audition"};
    const auto stopped = require_capture(
        responsive::capture_finite_responsive_session(stopped_scenario, buses),
        "stopped lifecycle capture failed");
    expect(std::ranges::any_of(stopped.blocks, [](const auto &block) {
               return std::abs(block.endpoint.engine_speed_rpm) < 1.0e-9;
           }),
           "lifecycle endpoints did not retain a zero-RPM state");
    verify_exact_block_and_cycle_accounting(stopped);

    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "cold-start-crank-catch-0rpm.json",
        shorten_lifecycle);
    auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "lifecycle capture failed");

    bool saw_ignition_off = false;
    bool saw_ignition_on = false;
    bool saw_starter_on = false;
    bool saw_starter_off_after_on = false;
    for (const auto &block : capture.blocks) {
        const auto flags = block.endpoint.state_flags;
        const bool ignition =
            (flags & engine_cycle_state_flag_mask(
                         EngineCycleStateFlag::ignition_enabled)) != 0U;
        const bool starter =
            (flags & engine_cycle_state_flag_mask(
                         EngineCycleStateFlag::starter_enabled)) != 0U;
        saw_ignition_off |= !ignition;
        saw_ignition_on |= ignition;
        saw_starter_on |= starter;
        saw_starter_off_after_on |= saw_starter_on && !starter;
    }
    expect(capture.preparation_block_count == 0U,
           "lifecycle capture invented a preparation horizon");
    expect(saw_ignition_off && saw_ignition_on,
           "lifecycle endpoints lost the ignition transition");
    expect(saw_starter_on && saw_starter_off_after_on,
           "lifecycle endpoints lost the starter transition");
    verify_exact_block_and_cycle_accounting(capture);
    return capture;
}

void test_cancellation_and_fail_closed_validation(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture,
    const responsive::FiniteResponsiveCapture &valid_capture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "cold-start-crank-catch-0rpm.json",
        shorten_lifecycle);
    constexpr std::array<std::string_view, 1> buses{
        "master.engine.audition"};
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = responsive::capture_finite_responsive_session(
        scenario, buses, stop.get_token());
    const auto *cancel_error =
        std::get_if<responsive::FiniteResponsiveCaptureError>(&cancelled);
    expect(cancel_error != nullptr &&
               cancel_error->code ==
                   responsive::FiniteResponsiveCaptureErrorCode::cancelled,
           "pre-cancelled capture did not terminate as cancelled");

    auto malformed_pcm = valid_capture;
    malformed_pcm.buses.front().audible_interleaved_samples.front() =
        std::numeric_limits<float>::quiet_NaN();
    expect(responsive::validate_finite_responsive_capture(malformed_pcm)
               .has_value(),
           "standalone validator accepted non-finite PCM");

    auto malformed_endpoint = valid_capture;
    malformed_endpoint.blocks.front().endpoint.engine_speed_rpm =
        std::numeric_limits<double>::infinity();
    expect(responsive::validate_finite_responsive_capture(malformed_endpoint)
               .has_value(),
           "standalone validator accepted a non-finite reduced endpoint");

    auto malformed_range = valid_capture;
    ++malformed_range.blocks.front().first_delivery_frame;
    expect(responsive::validate_finite_responsive_capture(malformed_range)
               .has_value(),
           "standalone validator accepted a discontinuous frame range");

    auto malformed_events = valid_capture;
    ++malformed_events.blocks.front()
          .event_counters.total_event_record_count;
    expect(responsive::validate_finite_responsive_capture(malformed_events)
               .has_value(),
           "standalone validator accepted inconsistent event counters");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "usage: responsive_finite_capture_test <repo-root>");
        const std::filesystem::path repository_root = argv[1];
        const auto fixture = compile_engine_fixture(repository_root);
        test_held_multi_bus_capture(repository_root, fixture);
        test_directional_capture(repository_root, fixture);
        const auto lifecycle =
            test_lifecycle_capture(repository_root, fixture);
        test_cancellation_and_fail_closed_validation(repository_root, fixture,
                                                     lifecycle);
        std::cout << "responsive finite capture tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "responsive finite capture tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
