#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/package_bake.hpp"
#include "package/audio_package_assembly.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
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
using namespace engine_sim_offline;
using namespace engine_sim_offline::package_detail;

constexpr auto kRunningState =
    engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled);
constexpr auto kNaturalIdleState =
    engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled);
constexpr std::uint64_t kFirstGlobalFrame = 10000U;
constexpr std::uint64_t kTapeFrameCount = 35000U;
constexpr contract::TorqueTermMask kBmwIncludedTorqueTerms =
    contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
    contract::torque_term_mask(contract::TorqueTerm::crank_friction) |
    contract::torque_term_mask(contract::TorqueTerm::piston_ring_friction) |
    contract::torque_term_mask(contract::TorqueTerm::starter);
constexpr contract::TorqueTermMask kBmwOmittedTorqueTerms =
    contract::known_torque_term_mask() & ~kBmwIncludedTorqueTerms;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(
    const std::filesystem::path &path) {
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
    return result;
}

template <class Value>
[[nodiscard]] Value require(std::variant<Value, authoring::DiagnosticReport> result,
                            const std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

struct CompiledFixture {
    CompiledPackageBake plan;
};

[[nodiscard]] CompiledFixture load_fixture(
    const std::filesystem::path &repository_root) {
    const auto root =
        repository_root / "data/engines/bmw-m52tub28-cleanroom";
    auto engine_document = require(
        authoring::parse_engine_document(read_text(root / "engine.json")),
        "engine parse failed");
    auto bake_document = require(authoring::parse_package_bake_document(
                                     read_text(root / "package-bake.json")),
                                 "package-bake parse failed");

    std::vector<OwnedAsset> assets;
    for (const auto &asset : engine_document.presentation.assets) {
        assets.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(root / asset.uri)});
    }
    for (const auto &asset : engine_document.engine.accessory_configurations) {
        assets.push_back({compile::AssetKind::accessory_configuration,
                          asset.id.value, read_bytes(root / asset.uri)});
    }
    std::vector<compile::AssetPayloadView> asset_views;
    for (const auto &asset : assets) {
        asset_views.push_back({asset.kind, asset.id, asset.bytes});
    }
    auto engine = require(compile::compile_engine(engine_document, asset_views),
                          "engine compile failed");

    std::vector<authoring::ScenarioDocument> scenario_documents;
    std::vector<PackageBakeScenarioInputView> scenario_inputs;
    scenario_documents.reserve(bake_document.scenario_sources.size());
    scenario_inputs.reserve(bake_document.scenario_sources.size());
    for (const auto &source : bake_document.scenario_sources) {
        scenario_documents.push_back(require(authoring::parse_scenario_document(
                                                  read_text(root / source.uri)),
                                              "scenario parse failed"));
    }
    for (std::size_t index = 0; index < scenario_documents.size(); ++index) {
        scenario_inputs.push_back(
            {bake_document.scenario_sources[index].id.value,
             &scenario_documents[index]});
    }
    return {require(compile_package_bake(bake_document, engine, scenario_inputs),
                    "package-bake compile failed")};
}

[[nodiscard]] EngineCompletedCycleEvidence
cycle(const std::uint64_t ordinal, const double rpm, const double torque,
      const double throttle, const double local_start) {
    const auto global_start = static_cast<double>(kFirstGlobalFrame) + local_start;
    const auto global_end = global_start + 50.0;
    const auto start_time = global_start / 192000.0;
    const auto end_time = global_end / 192000.0;
    const auto lattice = static_cast<std::int64_t>(ordinal + 1000U);
    const EngineCycleBoundaryEvidence start{
        lattice, ordinal * 2U, ordinal * 2U + 1U, 0.25,
        static_cast<double>(lattice) * 4.0 * std::numbers::pi,
        start_time, global_start,
    };
    const EngineCycleBoundaryEvidence end{
        lattice + 1, ordinal * 2U + 2U, ordinal * 2U + 3U, 0.75,
        static_cast<double>(lattice + 1) * 4.0 * std::numbers::pi,
        end_time, global_end,
    };
    const EngineCycleControlEvidence control{throttle, throttle, throttle, 0U};
    const EngineCycleNetShaftEvidence shaft{
        torque * 4.0 * std::numbers::pi,
        torque,
        contract::Availability::available,
        contract::Completeness::incomplete,
        contract::QuantityUnavailableReason::none,
        kBmwIncludedTorqueTerms,
        kBmwOmittedTorqueTerms,
    };
    return {
        ordinal, start, end, end_time - start_time, rpm, control, control,
        control, shaft, kRunningState, kRunningState, 0U,
    };
}

[[nodiscard]] PackageSourceLaneCapture capture(
    const CompiledPackageBakeScenarioSource &source,
    const bool falling, const bool idle, const double torque,
    const double throttle) {
    PackageSourceLaneCapture result;
    result.engine_id = source.scenario.engine().id();
    result.scenario_id = source.scenario.id();
    result.sample_rate = {192000U, 1U};
    result.audible_first_delivery_frame = kFirstGlobalFrame;
    result.audible_delivery_frame_count = kTapeFrameCount;
    PackageSourceLaneBusCapture bus;
    bus.id = "master.engine.audition";
    bus.kind = EngineAudioBusKind::engine_audition_master;
    bus.signal_disposition = EngineAudioSignalDisposition::active;
    bus.samples.resize(kTapeFrameCount, 0.0F);
    bus.samples.front() = 0.125F;
    result.buses.push_back(std::move(bus));

    const std::size_t count = idle ? 4U : 239U;
    result.usable_cycles.reserve(count);
    result.usable_cycle_lane_boundaries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto index_f64 = static_cast<double>(index);
        const double rpm = idle ? 700.0
                                : (falling ? 6575.0 - 25.0 * index_f64
                                           : 625.0 + 25.0 * index_f64);
        const double local_start = 5000.0 + 100.0 * index_f64;
        auto evidence = cycle(
            static_cast<std::uint64_t>(index + 1U), rpm, torque, throttle,
            local_start);
        if (idle) {
            evidence.start_state_flags = kNaturalIdleState;
            evidence.end_state_flags = kNaturalIdleState;
        }
        result.usable_cycles.push_back(std::move(evidence));
        result.usable_cycle_lane_boundaries.push_back(
            {local_start, local_start + 50.0});
    }
    return result;
}

[[nodiscard]] contract::Sha256Digest digest(const std::uint8_t byte) {
    contract::Sha256Digest result;
    result.bytes.front() = byte;
    return result;
}

[[nodiscard]] AudioPackageAssemblyIdentity identities(
    const CompiledPackageBake &plan) {
    AudioPackageAssemblyIdentity result;
    result.engine = {std::string{plan.engine().id()}, digest(1U)};
    result.bake_plan = {std::string{plan.id()}, digest(2U)};
    result.renderer_build = {"engine-sim-offline-build", digest(3U)};
    result.source_inputs = {"bmw-package-source-inputs", digest(4U)};
    for (std::size_t index = 0; index < plan.scenario_sources().size(); ++index) {
        const auto &source = plan.scenario_sources()[index];
        result.source_scenarios.push_back(
            {source.id, {std::string{source.scenario.id()},
                         digest(static_cast<std::uint8_t>(10U + index))}});
    }
    return result;
}

[[nodiscard]] PackageSourceCaptureSet captures(const CompiledPackageBake &plan) {
    PackageSourceCaptureSet result;
    const auto sources = plan.scenario_sources();
    result.sources.push_back(capture(sources[0], true, false, -100.0, 0.04));
    result.sources.push_back(capture(sources[1], false, false, 0.0, 0.45));
    result.sources.push_back(capture(sources[2], false, false, 100.0, 1.0));
    result.sources.push_back(capture(sources[3], false, true, 0.0, 0.0));
    return result;
}

[[nodiscard]] AssembledAudioPackage require_package(
    AudioPackageAssemblyResult result) {
    if (const auto *failure = std::get_if<AudioPackageAssemblyError>(&result)) {
        throw std::runtime_error{"package assembly failed at " + failure->path +
                                 ": " + failure->message};
    }
    return std::get<AssembledAudioPackage>(std::move(result));
}

void test_exact_deterministic_package(const CompiledPackageBake &plan) {
    const auto source_captures = captures(plan);
    const auto package = require_package(
        assemble_audio_package(plan, source_captures, identities(plan)));
    expect(contract::validate(package.manifest).ok() &&
               !package.package_json.empty() && package.payload_files.size() == 4U &&
               package.manifest.artifacts.size() == 4U,
           "assembled package is incomplete or invalid");
    expect(package.manifest.running.load_calibration ==
               contract::AudioPackageLoadCalibration{
                   contract::Completeness::incomplete,
                   kBmwIncludedTorqueTerms,
                   kBmwOmittedTorqueTerms} &&
               std::string{reinterpret_cast<const char *>(
                               package.package_json.data()),
                           package.package_json.size()}
                       .find("\"signal\":\"cycle-mean-integrated-"
                             "instantaneous-net-shaft\"") != std::string::npos,
           "assembly did not publish the exact typed BMW load calibration");
    expect(!package.manifest.running.idle.units.empty() &&
               package.manifest.running.idle.units.front()
                   .average_net_torque_nm.has_value() &&
               *package.manifest.running.idle.units.front().average_net_torque_nm ==
                   0.0,
           "assembly lost available natural-idle FreeEngine torque evidence");
    expect(std::ranges::is_sorted(package.payload_files, {},
                                  &AudioPackagePayloadFile::relative_path) &&
               std::ranges::is_sorted(package.manifest.artifacts, {},
                                      &contract::AudioPackageArtifact::id),
           "package payloads or artifacts are not deterministic and sorted");
    const auto &idle_wave = package.payload_files.front();
    expect(idle_wave.relative_path ==
               "audio/idle/master-engine-audition.wav" &&
               idle_wave.bytes.size() == kTapeFrameCount * sizeof(float) + 58U,
           "portable package-facing payload path or exact WAVE size changed");
    std::uint32_t first_sample = 0U;
    for (std::size_t index = 0; index < sizeof(first_sample); ++index) {
        first_sample |= static_cast<std::uint32_t>(
                            std::to_integer<std::uint8_t>(idle_wave.bytes[58U + index]))
                        << (index * 8U);
    }
    expect(first_sample == std::bit_cast<std::uint32_t>(0.125F),
           "assembly altered captured Float32 PCM while containerizing it");
}

void test_identity_and_clipping_fail_closed(const CompiledPackageBake &plan) {
    auto identity = identities(plan);
    identity.source_scenarios[0].source_id = "wrong-source";
    const auto wrong_identity = assemble_audio_package(plan, captures(plan), identity);
    expect(std::holds_alternative<AudioPackageAssemblyError>(wrong_identity) &&
               std::get<AudioPackageAssemblyError>(wrong_identity).code ==
                   AudioPackageAssemblyErrorCode::invalid_identity,
           "misordered source identity was accepted");

    auto clipped = captures(plan);
    clipped.sources[2].buses[0].samples[123U] = 1.01F;
    const auto clipping =
        assemble_audio_package(plan, clipped, identities(plan));
    expect(std::holds_alternative<AudioPackageAssemblyError>(clipping) &&
               std::get<AudioPackageAssemblyError>(clipping).code ==
                   AudioPackageAssemblyErrorCode::invalid_audio_payload,
           "clipping first-audition PCM was published");
}

void test_directional_load_calibration_must_be_uniform(
    const CompiledPackageBake &plan) {
    auto mismatched = captures(plan);
    for (auto &cycle : mismatched.sources[2].usable_cycles) {
        cycle.instantaneous_net_shaft.completeness =
            contract::Completeness::complete;
        cycle.instantaneous_net_shaft.included_terms =
            contract::known_torque_term_mask();
        cycle.instantaneous_net_shaft.omitted_terms = 0U;
    }
    const auto result =
        assemble_audio_package(plan, mismatched, identities(plan));
    expect(std::holds_alternative<AudioPackageAssemblyError>(result) &&
               std::get<AudioPackageAssemblyError>(result).code ==
                   AudioPackageAssemblyErrorCode::inconsistent_load_calibration &&
               std::get<AudioPackageAssemblyError>(result).path ==
                   "running.planes[2].units",
           "directional planes with unlike modeled-torque scopes were accepted");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const auto fixture = load_fixture(argv[1]);
        test_exact_deterministic_package(fixture.plan);
        test_identity_and_clipping_fail_closed(fixture.plan);
        test_directional_load_calibration_must_be_uniform(fixture.plan);
        std::cout << "audio package assembly tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "audio package assembly tests failed: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
