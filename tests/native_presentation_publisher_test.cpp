#include "render/native_presentation_publisher.hpp"

#include "engine_sim_offline/authoring/parse.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::render_detail;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{std::string{message}};
}

template <class Function>
void expect_sink_failure(Function &&function, const RenderSinkError &expected,
                         std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const NativePresentationSinkFailure &failure) {
        expect(failure.sink_error() == expected, message);
        return;
    } catch (...) {
        throw std::runtime_error{std::string{message}};
    }
    throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = digits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

class CapturingSink final : public RenderSink {
  public:
    bool reject_begin = false;
    std::optional<RenderSinkError> next_write_error;

    std::size_t begin_calls = 0;
    std::size_t commit_calls = 0;
    std::size_t abort_calls = 0;
    std::vector<PendingArtifact> declarations;
    std::vector<contract::ArtifactRecord> seals;
    std::map<std::string, std::vector<std::byte>> payloads;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &) override {
        ++begin_calls;
        if (reject_begin) {
            return RenderSinkError{
                RenderSinkErrorKind::publication_failure,
                "injected-begin",
                "injected begin failure",
            };
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override {
        declarations.push_back(artifact);
        payloads.try_emplace(artifact.role);
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override {
        if (next_write_error.has_value()) {
            auto error = std::move(next_write_error);
            next_write_error.reset();
            return error;
        }
        const auto found = payloads.find(std::string{chunk.role});
        if (found == payloads.end() || chunk.byte_offset != found->second.size()) {
            return RenderSinkError{
                RenderSinkErrorKind::protocol_violation,
                "capturing-sink-offset",
                "capturing sink received a noncontiguous chunk",
            };
        }
        found->second.insert(found->second.end(), chunk.bytes.begin(),
                             chunk.bytes.end());
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override {
        seals.push_back(record);
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus commit(const contract::RenderManifest &) override {
        ++commit_calls;
        return std::nullopt;
    }

    void abort() noexcept override {
        ++abort_calls;
    }
};

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
                            std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
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

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

[[nodiscard]] compile::CompiledScenario compile_short_held_scenario(
    const std::filesystem::path &repository_root,
    std::optional<double> audition_volume_linear = std::nullopt) {
    const auto engine_path = repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_path = repository_root / "data/engines/bmw-m52b28/scenarios/"
                                                 "inertial-dyno-1500-6500rpm.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "publisher fixture engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(read_text(scenario_path)),
                "publisher fixture scenario parse failed");
    if (audition_volume_linear.has_value()) {
        engine_document.presentation.audition.volume_linear =
            *audition_volume_linear;
    }

    const auto *inertial =
        std::get_if<authoring::InertialDynoMode>(&scenario_document.mode);
    auto *preparation =
        std::get_if<authoring::FixedHorizonPreparation>(&scenario_document.preparation);
    expect(inertial != nullptr && preparation != nullptr,
           "publisher fixture source scenario changed");
    const auto held_throttle = inertial->throttle_01;
    scenario_document.id.value = "test-native-publisher-short-held";
    scenario_document.mode = authoring::HeldSpeedMode{
        scenario_document.initial_state.engine_speed,
        held_throttle,
    };
    preparation =
        std::get_if<authoring::FixedHorizonPreparation>(&scenario_document.preparation);
    preparation->preparation_duration.value = 0.32;
    preparation->trailing_complete_cycle_count = 1U;
    scenario_document.audible_start.value = 0.32;
    scenario_document.audible_duration.value = 0.04;
    scenario_document.total_duration.value = 0.36;

    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{
            "publisher fixture cross-document validation failed: " +
            diagnostics(references)};
    }

    std::vector<OwnedAsset> assets;
    assets.reserve(engine_document.presentation.assets.size() +
                   engine_document.engine.accessory_configurations.size());
    for (const auto &asset : engine_document.presentation.assets) {
        assets.push_back({
            compile::AssetKind::audio,
            asset.id.value,
            read_bytes(engine_path.parent_path() / asset.uri),
        });
    }
    for (const auto &asset : engine_document.engine.accessory_configurations) {
        assets.push_back({
            compile::AssetKind::accessory_configuration,
            asset.id.value,
            read_bytes(engine_path.parent_path() / asset.uri),
        });
    }
    std::vector<compile::AssetPayloadView> asset_views;
    asset_views.reserve(assets.size());
    for (const auto &asset : assets) {
        asset_views.push_back({asset.kind, asset.id, asset.bytes});
    }

    auto engine = require(compile::compile_engine(engine_document, asset_views),
                          "publisher fixture engine compilation failed");
    return require(compile::compile_scenario(engine, scenario_document),
                   "publisher fixture scenario compilation failed");
}

[[nodiscard]] std::string session_error_text(const EngineSessionError &error) {
    auto result = error.detail_code + ": " + error.message;
    if (error.simulation_failure.has_value()) {
        result += "; " + error.simulation_failure->detail_code + ": " +
                  error.simulation_failure->state_summary;
    }
    return result;
}

[[nodiscard]] EngineSession require_session(const compile::CompiledScenario &scenario) {
    auto result =
        create_engine_session(scenario, EngineSessionExecutionKind::finite_scenario);
    if (const auto *error = std::get_if<EngineSessionError>(&result)) {
        throw std::runtime_error{"publisher fixture session creation failed: " +
                                 session_error_text(*error)};
    }
    return std::get<EngineSession>(std::move(result));
}

[[nodiscard]] const EngineAudioBusDescriptor &
require_master_bus(const EngineSessionDescriptor &session, EngineAudioBusKind kind) {
    const auto found =
        std::ranges::find(session.audio_buses, kind, &EngineAudioBusDescriptor::kind);
    if (found == session.audio_buses.end() || found->route_id.has_value() ||
        found->source_route_kind != contract::SourceRouteKind::unspecified ||
        found->signal_disposition != EngineAudioSignalDisposition::active) {
        throw std::runtime_error{
            "publisher fixture session lacks a required master bus"};
    }
    return *found;
}

[[nodiscard]] const EngineAudioBusDescriptor &
require_route_bus(const EngineSessionDescriptor &session, contract::RouteId route_id,
                  EngineAudioBusKind kind, contract::SourceRouteKind source_route_kind,
                  EngineAudioSignalDisposition signal_disposition) {
    const auto found = std::ranges::find_if(session.audio_buses, [&](const auto &bus) {
        return bus.kind == kind && bus.source_route_kind == source_route_kind &&
               bus.signal_disposition == signal_disposition &&
               bus.route_id == std::optional<contract::RouteId>{route_id};
    });
    if (found == session.audio_buses.end()) {
        throw std::runtime_error{
            "publisher fixture session lacks a required route bus"};
    }
    return *found;
}

[[nodiscard]] NativePresentationPublicationPlan
make_plan(const EngineSessionDescriptor &session) {
    expect(session.total_block_count > session.preparation_block_count,
           "publisher fixture session has no audible interval");
    const auto audible_blocks =
        session.total_block_count - session.preparation_block_count;
    const auto audible_frames = audible_blocks * kEngineSessionDeliveryFramesPerBlock;

    contract::OutputContract output;
    output.source_matrix_id = "test.native-publisher.session";
    output.distribution = contract::DistributionIntent::local_evaluation;
    const contract::AudioContract float_audio{
        kEngineSessionDeliveryRateHz,
        audible_frames,
        "mono",
        "float32le",
    };
    const contract::AudioContract audition_audio{
        kEngineSessionDeliveryRateHz,
        audible_frames,
        "mono",
        "pcm_s24le",
    };

    std::vector<contract::RouteId> route_ids;
    for (const auto &bus : session.audio_buses) {
        if (bus.kind == EngineAudioBusKind::source_route_dry &&
            bus.source_route_kind == contract::SourceRouteKind::exhaust_outlet &&
            bus.route_id.has_value()) {
            route_ids.push_back(*bus.route_id);
        }
    }
    expect(!route_ids.empty(), "publisher fixture has no exhaust routes");

    std::vector<NativePresentationRoutePublicationPlan> routes;
    routes.reserve(route_ids.size());
    std::size_t artifact_index = 0;
    const auto pending = [&](std::string role, const contract::AudioContract &audio) {
        const auto path =
            "audio/native-publisher-" + std::to_string(artifact_index++) + ".wav";
        output.required_artifacts.push_back({
            role,
            contract::ArtifactKind::audio,
            audio,
            false,
        });
        return PendingArtifact{
            std::move(role), contract::ArtifactKind::audio, path, audio, false,
        };
    };

    for (std::size_t index = 0; index < route_ids.size(); ++index) {
        const auto route_id = route_ids[index];
        const auto dry =
            require_route_bus(session, route_id, EngineAudioBusKind::source_route_dry,
                              contract::SourceRouteKind::exhaust_outlet,
                              EngineAudioSignalDisposition::active);
        const auto configured = require_route_bus(
            session, route_id, EngineAudioBusKind::source_route_configured_transfer,
            contract::SourceRouteKind::exhaust_outlet,
            EngineAudioSignalDisposition::active);
        const auto selected = require_route_bus(
            session, route_id, EngineAudioBusKind::source_route_selected,
            contract::SourceRouteKind::exhaust_outlet,
            EngineAudioSignalDisposition::active);
        NativePresentationRouteArtifacts artifacts{
            pending(std::string{dry.id}, float_audio),
            pending(std::string{configured.id}, float_audio),
            pending(std::string{selected.id}, float_audio),
        };
        const auto semantic_id =
            "test.native-publisher.exhaust-" + std::to_string(index + 1U);
        output.required_source_routes.push_back({
            semantic_id,
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {
                artifacts.dry.role,
                artifacts.configured_transfer.role,
                artifacts.selected.role,
            },
        });
        routes.push_back({
            route_id,
            semantic_id,
            std::move(artifacts),
        });
    }

    const auto &raw_descriptor =
        require_master_bus(session, EngineAudioBusKind::engine_raw_master);
    const auto &audition_descriptor =
        require_master_bus(session, EngineAudioBusKind::engine_audition_master);
    auto raw = pending("test.native-publisher.master.raw", float_audio);
    auto audition = pending("test.native-publisher.master.audition", audition_audio);
    output.required_output_buses = {
        {
            std::string{raw_descriptor.id},
            contract::OutputBusKind::master_engine_raw,
            {raw.role},
        },
        {
            std::string{audition_descriptor.id},
            contract::OutputBusKind::master_engine_audition,
            {audition.role},
        },
    };

    const auto fade_frames = std::min<std::uint64_t>(
        kEngineSessionDeliveryFramesPerBlock, audible_frames / 2U);
    return {
        std::move(output),
        {
            session.total_block_count,
            session.preparation_block_count,
            NativePresentationTailPolicy::truncate_at_timeline_end,
        },
        presentation::implemented_presentation_method_identities(),
        std::move(routes),
        {
            std::move(route_ids),
            {
                audible_frames,
                fade_frames,
                fade_frames,
            },
            {
                "Short public EngineSession publication fixture",
                "Native publisher",
                "engine-sim-offline-test",
            },
            std::move(raw),
            std::move(audition),
        },
    };
}

struct PublicationResult {
    NativePresentationPublicationStats stats;
    std::vector<contract::ArtifactRecord> records;
    std::map<std::string, std::vector<std::byte>> payloads;
};

[[nodiscard]] PublicationResult
publish_complete_session(const compile::CompiledScenario &scenario) {
    auto session = require_session(scenario);
    const auto descriptor = session.descriptor();
    CapturingSink sink;
    PublicationResult result;
    {
        NativePresentationPublisher publisher{
            sink,
            descriptor,
            make_plan(descriptor),
        };
        while (true) {
            auto next = session.process_block();
            if (const auto *error = std::get_if<EngineSessionError>(&next)) {
                throw std::runtime_error{"publisher fixture processing failed: " +
                                         session_error_text(*error)};
            }
            if (std::holds_alternative<EngineSessionCompleted>(next)) {
                break;
            }
            publisher.process(std::get<EngineSessionBlockView>(next));
        }
        auto evidence = publisher.finish();
        result.stats = evidence.stats();
        result.records.assign(evidence.artifacts().begin(), evidence.artifacts().end());
        expect(publisher.state() == NativePresentationPublisherState::sealed,
               "native publisher did not seal");
    }
    expect(sink.abort_calls == 1U && sink.commit_calls == 0U,
           "sealed publisher did not abort its uncommitted transaction once");
    result.payloads = std::move(sink.payloads);
    return result;
}

void test_public_session_byte_golden(const compile::CompiledScenario &scenario) {
    const auto published = publish_complete_session(scenario);
    expect(published.stats ==
               NativePresentationPublicationStats{
                   7200U,
                   18U,
                   16U,
                   2U,
                   69120U,
                   61440U,
                   7680U,
                   0U,
               },
           "native publisher short-session timeline accounting changed");
    expect(published.records.size() == 8U && published.payloads.size() == 8U,
           "native publisher emitted the wrong artifact set");

    // These hashes freeze the complete WAVE payloads produced by the short,
    // public EngineSession fixture. They replace the temporary runtime dependency
    // on the fused presentation-session oracle used during the exact split proof.
    constexpr std::array<std::string_view, 8> kExpectedSha256{
        "29d3bc39ddf523a415f8496dfc66390aa0fc7a8cc520a96bfd8123ead0e5ccb5",
        "96fb05c49867f581c33f9387400b13e8e4cd3fff6121c76eb189eb1711e2a0f8",
        "96fb05c49867f581c33f9387400b13e8e4cd3fff6121c76eb189eb1711e2a0f8",
        "8662ef655c86e41ecfb26e38bcf6b90cf6238134c81094a378465ee60658148e",
        "6e72dbdd2d12e13d748816cc97f24f984f904ba720e6c848b6163cd8f4caf2de",
        "6e72dbdd2d12e13d748816cc97f24f984f904ba720e6c848b6163cd8f4caf2de",
        "0f7d73ef90617b52aef89131dc564b8ea9ecf162a175b5bbfa927037891dc92b",
        "a8a94537a531645a93c445ba80b0acb38fda437dc92424047023bac35fc117d6",
    };
    std::vector<std::string> actual_sha256;
    actual_sha256.reserve(published.records.size());
    for (const auto &record : published.records) {
        actual_sha256.push_back(digest_hex(record.payload_sha256));
    }
    if (!std::equal(actual_sha256.begin(), actual_sha256.end(),
                    kExpectedSha256.begin())) {
        std::string message{"native publisher WAVE golden set changed:"};
        for (const auto &actual : actual_sha256) {
            message += " " + actual;
        }
        throw std::runtime_error{std::move(message)};
    }
}

void test_prebinding_and_transaction_failures(
    const compile::CompiledScenario &scenario) {
    {
        auto session = require_session(scenario);
        auto descriptor = session.descriptor();
        std::vector<EngineAudioBusDescriptor> invalid_buses{
            descriptor.audio_buses.begin(), descriptor.audio_buses.end()};
        invalid_buses.front().sample_rate = {48000U, 1U};
        descriptor.audio_buses = invalid_buses;
        CapturingSink sink;
        expect_throw<std::invalid_argument>(
            [&] {
                NativePresentationPublisher publisher{
                    sink,
                    descriptor,
                    make_plan(session.descriptor()),
                };
            },
            "native publisher accepted a mismatched session descriptor");
        expect(sink.begin_calls == 0U && sink.abort_calls == 0U,
               "invalid bus binding reached the sink transaction");
    }

    {
        auto session = require_session(scenario);
        auto descriptor = session.descriptor();
        std::vector<EngineAudioBusDescriptor> invalid_buses{
            descriptor.audio_buses.begin(), descriptor.audio_buses.end()};
        const auto route_bus = std::ranges::find_if(
            invalid_buses, [](const auto &bus) { return bus.route_id.has_value(); });
        expect(route_bus != invalid_buses.end(),
               "publisher fixture has no source-route bus");
        route_bus->source_route_kind = contract::SourceRouteKind::intake_inlet;
        descriptor.audio_buses = invalid_buses;
        CapturingSink sink;
        expect_throw<std::invalid_argument>(
            [&] {
                NativePresentationPublisher publisher{
                    sink,
                    descriptor,
                    make_plan(session.descriptor()),
                };
            },
            "native publisher accepted a mismatched source-route kind");
        expect(sink.begin_calls == 0U && sink.abort_calls == 0U,
               "invalid source-route kind reached the sink transaction");
    }

    {
        auto session = require_session(scenario);
        auto descriptor = session.descriptor();
        std::vector<EngineAudioBusDescriptor> invalid_buses{
            descriptor.audio_buses.begin(), descriptor.audio_buses.end()};
        const auto route_bus = std::ranges::find_if(invalid_buses, [](const auto &bus) {
            return bus.source_route_kind == contract::SourceRouteKind::exhaust_outlet &&
                   bus.route_id.has_value();
        });
        expect(route_bus != invalid_buses.end(),
               "publisher fixture has no active exhaust-route bus");
        route_bus->signal_disposition = EngineAudioSignalDisposition::declared_silent;
        descriptor.audio_buses = invalid_buses;
        CapturingSink sink;
        expect_throw<std::invalid_argument>(
            [&] {
                NativePresentationPublisher publisher{
                    sink,
                    descriptor,
                    make_plan(session.descriptor()),
                };
            },
            "native publisher accepted a mismatched signal disposition");
        expect(sink.begin_calls == 0U && sink.abort_calls == 0U,
               "invalid signal disposition reached the sink transaction");
    }

    {
        auto session = require_session(scenario);
        auto descriptor = session.descriptor();
        descriptor.physics_rate = {10000U, 1U};
        descriptor.physics_frames_per_block = 200U;
        CapturingSink sink;
        expect_throw<std::invalid_argument>(
            [&] {
                NativePresentationPublisher publisher{
                    sink,
                    descriptor,
                    make_plan(session.descriptor()),
                };
            },
            "native publisher accepted the retired 10 kHz session quantum");
        expect(sink.begin_calls == 0U && sink.abort_calls == 0U,
               "retired session rate reached the sink transaction");
    }

    {
        auto session = require_session(scenario);
        const auto descriptor = session.descriptor();
        CapturingSink sink;
        sink.reject_begin = true;
        const RenderSinkError begin_error{
            RenderSinkErrorKind::publication_failure,
            "injected-begin",
            "injected begin failure",
        };
        expect_sink_failure(
            [&] {
                NativePresentationPublisher publisher{
                    sink,
                    descriptor,
                    make_plan(descriptor),
                };
            },
            begin_error, "native publisher accepted a failed transaction begin");
        expect(sink.begin_calls == 1U && sink.abort_calls == 0U &&
                   sink.declarations.empty(),
               "failed native begin aborted or declared artifacts");
    }

    {
        auto session = require_session(scenario);
        const auto descriptor = session.descriptor();
        CapturingSink sink;
        NativePresentationPublisher publisher{
            sink,
            descriptor,
            make_plan(descriptor),
        };
        const RenderSinkError payload_error{
            RenderSinkErrorKind::protocol_violation,
            "injected-payload",
            "injected payload failure",
        };
        sink.next_write_error = payload_error;
        while (true) {
            auto next = session.process_block();
            if (const auto *error = std::get_if<EngineSessionError>(&next)) {
                throw std::runtime_error{"payload failure fixture session failed: " +
                                         session_error_text(*error)};
            }
            expect(!std::holds_alternative<EngineSessionCompleted>(next),
                   "payload failure fixture completed before audible output");
            const auto &block = std::get<EngineSessionBlockView>(next);
            if (block.phase() == EngineSessionBlockPhase::preparation) {
                publisher.process(block);
                continue;
            }
            expect_sink_failure([&] { publisher.process(block); }, payload_error,
                                "native payload rejection lost the exact sink failure");
            break;
        }
        expect(publisher.state() == NativePresentationPublisherState::aborted &&
                   sink.abort_calls == 1U,
               "native payload rejection did not abort exactly once");
    }

    {
        auto session = require_session(scenario);
        const auto descriptor = session.descriptor();
        CapturingSink sink;
        {
            NativePresentationPublisher publisher{
                sink,
                descriptor,
                make_plan(descriptor),
            };
            expect_throw<std::logic_error>(
                [&] { static_cast<void>(publisher.finish()); },
                "native publisher sealed an incomplete schedule");
            expect(publisher.state() == NativePresentationPublisherState::aborted &&
                       sink.abort_calls == 1U,
                   "incomplete native schedule did not abort exactly once");
        }
        expect(sink.abort_calls == 1U,
               "incomplete publisher destruction aborted twice");
    }
}

void test_extreme_volume_is_soft_limited_before_pcm24(
    const std::filesystem::path &repository_root) {
    const auto scenario = compile_short_held_scenario(repository_root, 1.0e9);
    const auto published = publish_complete_session(scenario);
    expect(published.stats.audition_saturated_sample_count == 0U,
           "tanh-bounded audition master reached PCM24 saturation");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{
                "usage: native_presentation_publisher_test <repository-root>"};
        }
        const auto scenario =
            compile_short_held_scenario(std::filesystem::path{argv[1]});
        test_public_session_byte_golden(scenario);
        test_prebinding_and_transaction_failures(scenario);
        test_extreme_volume_is_soft_limited_before_pcm24(
            std::filesystem::path{argv[1]});
    } catch (const std::exception &error) {
        std::cerr << "native presentation publisher test failed: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
