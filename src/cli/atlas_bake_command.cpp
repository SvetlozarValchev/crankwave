#include "atlas_bake_command.hpp"

#include "native_input_files.hpp"

#include "determinism/renderer_determinism_envelope.hpp"
#include "crankwave/artifacts/audio_atlas_directory_publisher.hpp"
#include "crankwave/atlas_assembly.hpp"
#include "crankwave/atlas_bake.hpp"
#include "crankwave/atlas_capture.hpp"
#include "crankwave/compile.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::cli {
namespace {

[[nodiscard]] int input_exit_code(const NativeInputErrorKind kind) noexcept {
    switch (kind) {
    case NativeInputErrorKind::data_error:
        return kExitDataError;
    case NativeInputErrorKind::no_input:
        return kExitNoInput;
    case NativeInputErrorKind::unavailable:
        return kExitUnavailable;
    case NativeInputErrorKind::software:
        return kExitSoftware;
    }
    return kExitSoftware;
}

[[nodiscard]] int output_exit_code(const NativeOutputErrorKind kind) noexcept {
    return kind == NativeOutputErrorKind::cant_create ? kExitCantCreate
                                                       : kExitTemporaryFailure;
}

[[nodiscard]] int report_error(std::ostream &stream, const int exit_code,
                               const std::string_view stage,
                               const std::string_view message) {
    stream << "error: " << stage << " failed: " << message << '\n';
    return exit_code;
}

[[nodiscard]] int report_diagnostics(
    std::ostream &stream, const std::string_view stage,
    const authoring::DiagnosticReport &report) {
    stream << "error: " << stage << " failed\n";
    for (const auto &diagnostic : report.diagnostics) {
        stream << "  ";
        if (!diagnostic.json_pointer.empty()) {
            stream << diagnostic.json_pointer << ": ";
        }
        stream << diagnostic.message << '\n';
    }
    return kExitDataError;
}

[[nodiscard]] int report_input_error(std::ostream &stream,
                                     const std::string_view stage,
                                     const NativeInputError &error) {
    if (error.diagnostics.has_value()) {
        return report_diagnostics(stream, stage, *error.diagnostics);
    }
    return report_error(stream, input_exit_code(error.kind), stage, error.message);
}

void append_u64(std::vector<std::byte> &bytes, const std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes.push_back(static_cast<std::byte>(value >> (index * 8U)));
    }
}

void append_string(std::vector<std::byte> &bytes, const std::string_view value) {
    append_u64(bytes, value.size());
    const auto payload = std::as_bytes(
        std::span<const char>{value.data(), value.size()});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void append_digest(std::vector<std::byte> &bytes,
                   const contract::Sha256Digest &digest) {
    const auto payload = std::as_bytes(std::span{digest.bytes});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

[[nodiscard]] contract::Sha256Digest capture_configuration_digest(
    const NativeAtlasBakeInput &input,
    const CompiledAtlasBakeMovingSegment &segment,
    const NativeAtlasScenarioInput &source) {
    std::vector<std::byte> bytes;
    bytes.reserve(192U);
    append_string(bytes,
                  "crankwave.audio-atlas-capture-configuration.v1");
    append_digest(bytes, input.source.sha256);
    append_string(bytes, segment.capture.id.value);
    append_string(bytes, segment.scenario.id());
    append_digest(bytes, source.source.sha256);
    return contract::sha256(bytes);
}

[[nodiscard]] contract::ProvenanceBundleRef source_input_identity(
    const NativeAtlasBakeInput &input, const CompiledAtlasBake &bake) {
    std::vector<std::byte> bytes;
    bytes.reserve(96U + input.scenarios.size() * 128U);
    append_string(bytes, "crankwave.audio-atlas-source-inputs.v1");
    append_u64(bytes, input.scenarios.size());
    for (const auto &source : input.scenarios) {
        append_string(bytes, source.source_id);
        append_string(bytes, source.document.id.value);
        append_digest(bytes, source.source.sha256);
    }
    return {std::string{bake.id()} + ".source-inputs", contract::sha256(bytes)};
}

[[nodiscard]] int publisher_exit_code(
    const artifacts::AudioAtlasDirectoryPublicationErrorCode code) noexcept {
    using enum artifacts::AudioAtlasDirectoryPublicationErrorCode;
    switch (code) {
    case destination_exists:
    case parent_unavailable:
    case staging_failure:
    case write_failure:
    case publication_failure:
        return kExitCantCreate;
    case synchronization_failure:
        return kExitTemporaryFailure;
    case unsupported_platform:
        return kExitUnavailable;
    case invalid_request:
    case manifest_rejected:
    case payload_mismatch:
    case resource_limit:
    case internal_failure:
        return kExitSoftware;
    }
    return kExitSoftware;
}

} // namespace

int execute_bake_atlas(const BakeAtlasCommand &command,
                       std::ostream &standard_out,
                       std::ostream &standard_error) {
    const auto started = std::chrono::steady_clock::now();

    auto engine_input_result =
        load_native_engine_input(std::filesystem::path{command.engine_path},
                                 std::filesystem::path{command.asset_root});
    if (const auto *error = std::get_if<NativeInputError>(&engine_input_result)) {
        return report_input_error(standard_error, "engine input", *error);
    }
    auto engine_input =
        std::get<NativeEngineInput>(std::move(engine_input_result));
    const auto assets = engine_input.asset_views();
    auto engine_result = compile::compile_engine(engine_input.document, assets);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&engine_result)) {
        return report_diagnostics(standard_error, "engine compilation", *report);
    }
    auto engine = std::get<compile::CompiledEngine>(std::move(engine_result));

    auto atlas_input_result = load_native_atlas_bake_input(
        std::filesystem::path{command.atlas_bake_path});
    if (const auto *error = std::get_if<NativeInputError>(&atlas_input_result)) {
        return report_input_error(standard_error, "atlas input", *error);
    }
    auto atlas_input =
        std::get<NativeAtlasBakeInput>(std::move(atlas_input_result));
    std::vector<AtlasBakeScenarioInputView> source_views;
    source_views.reserve(atlas_input.scenarios.size());
    for (const auto &source : atlas_input.scenarios) {
        source_views.push_back({source.source_id, &source.document});
    }
    auto bake_result =
        compile_atlas_bake(atlas_input.document, engine, source_views);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&bake_result)) {
        return report_diagnostics(standard_error, "atlas compilation", *report);
    }
    auto bake = std::get<CompiledAtlasBake>(std::move(bake_result));

    const auto output_result = preflight_native_output_directory(
        std::filesystem::path{command.output_directory});
    if (const auto *error = std::get_if<NativeOutputError>(&output_result)) {
        return report_error(standard_error, output_exit_code(error->kind),
                            "output preflight", error->message);
    }
    const auto output = std::get<NativeOutputDirectory>(output_result);

    auto determinism_result = determinism::renderer_determinism_envelope();
    const auto *determinism_envelope =
        std::get_if<determinism::RendererDeterminismEnvelope>(
            &determinism_result);
    if (determinism_envelope == nullptr ||
        !determinism_envelope->production_observation()) {
        return report_error(
            standard_error, kExitUnavailable, "renderer identity",
            "the current build cannot publish a clean production identity");
    }

    std::vector<std::string_view> bus_ids;
    bus_ids.reserve(bake.audio_buses().size());
    for (const auto &bus : bake.audio_buses()) {
        bus_ids.push_back(bus.session_bus_id);
    }

    const auto capture_started = std::chrono::steady_clock::now();
    std::vector<AtlasMovingLaneCapture> captures;
    captures.reserve(bake.moving_segments().size());
    for (const auto &segment : bake.moving_segments()) {
        auto capture_result = capture_atlas_moving_lane(
            segment.scenario, bus_ids, segment.capture.load_coordinate);
        if (const auto *error =
                std::get_if<AtlasMovingLaneCaptureError>(&capture_result)) {
            return report_error(standard_error, kExitSoftware, "atlas capture",
                                error->detail_code + ": " + error->message);
        }
        captures.push_back(
            std::get<AtlasMovingLaneCapture>(std::move(capture_result)));
    }
    const auto capture_finished = std::chrono::steady_clock::now();

    std::vector<AtlasMovingLaneAssemblyInputView> moving_inputs;
    moving_inputs.reserve(captures.size());
    const auto planned_segments = bake.moving_segments();
    const auto planned_sources = bake.scenario_sources();
    for (std::size_t index = 0U; index < captures.size(); ++index) {
        const auto &segment = planned_segments[index];
        if (segment.scenario_source_index >= atlas_input.scenarios.size() ||
            segment.scenario_source_index >= planned_sources.size()) {
            return report_error(standard_error, kExitSoftware, "atlas assembly",
                                "compiled source index is outside the loaded input");
        }
        const auto &source = atlas_input.scenarios[segment.scenario_source_index];
        moving_inputs.push_back(
            {segment.capture.id.value,
             &captures[index],
             {std::string{segment.scenario.id()}, source.source.sha256},
             {segment.capture.id.value + ".capture",
              capture_configuration_digest(atlas_input, segment, source)}});
    }

    AudioAtlasAssemblyProvenance provenance{
        {std::string{engine.id()}, engine.provenance().bundle.sha256},
        {std::string{bake.id()}, atlas_input.source.sha256},
        {"crankwave-renderer-build",
         determinism_envelope->source_stamp().source_closure_sha256},
        source_input_identity(atlas_input, bake),
    };
    auto assembly_result =
        assemble_moving_audio_atlas(bake, moving_inputs, provenance);
    if (const auto *error =
            std::get_if<AudioAtlasAssemblyError>(&assembly_result)) {
        return report_error(standard_error, kExitSoftware, "atlas assembly",
                            error->detail_code + ": " + error->message);
    }
    auto atlas = std::get<AssembledAudioAtlas>(std::move(assembly_result));

    auto publication_result = artifacts::publish_audio_atlas_directory(
        output.publication_root, output.publication_name, atlas);
    if (const auto *error =
            std::get_if<artifacts::AudioAtlasDirectoryPublicationError>(
                &publication_result)) {
        return report_error(standard_error, publisher_exit_code(error->code),
                            "atlas publication",
                            error->detail_code + ": " + error->message);
    }
    const auto &publication =
        std::get<artifacts::AudioAtlasDirectoryPublication>(publication_result);
    const auto finished = std::chrono::steady_clock::now();
    const auto capture_seconds =
        std::chrono::duration<double>(capture_finished - capture_started).count();
    const auto elapsed_seconds =
        std::chrono::duration<double>(finished - started).count();
    standard_out << "output_directory=" << publication.publication_path.string()
                 << '\n'
                 << "manifest="
                 << (publication.publication_path / "atlas.json").string() << '\n'
                 << "moving_segments=" << captures.size() << '\n'
                 << "capture_seconds=" << capture_seconds << '\n'
                 << "elapsed_seconds=" << elapsed_seconds << '\n';
    return kExitSuccess;
}

} // namespace crankwave::cli
