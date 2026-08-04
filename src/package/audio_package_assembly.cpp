#include "package/audio_package_assembly.hpp"

#include "engine_sim_offline/artifacts/audio_package_manifest_encoder.hpp"
#include "engine_sim_offline/artifacts/wav_encoder.hpp"
#include "package/uniform_cycle_bank.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {
namespace {

[[nodiscard]] AudioPackageAssemblyError
error(const AudioPackageAssemblyErrorCode code, std::string path,
      std::string detail_code, std::string message) {
    return {code, std::move(path), std::move(detail_code), std::move(message)};
}

[[nodiscard]] std::string indexed(const std::string_view path,
                                  const std::size_t index) {
    return std::string{path} + "[" + std::to_string(index) + "]";
}

[[nodiscard]] bool valid_identity(
    const contract::AudioPackageContentIdentity &identity) noexcept {
    return contract::is_valid_semantic_id(identity.id) &&
           !identity.sha256.is_zero();
}

[[nodiscard]] std::optional<AudioPackageAssemblyError>
validate_identity(const CompiledPackageBake &plan,
                  const AudioPackageAssemblyIdentity &identity) {
    if (!valid_identity(identity.engine) || identity.engine.id != plan.engine().id()) {
        return error(AudioPackageAssemblyErrorCode::invalid_identity,
                     "identity.engine", "audio-package-engine-identity-mismatch",
                     "engine identity must be nonzero and name the compiled package engine");
    }
    if (!valid_identity(identity.bake_plan) ||
        identity.bake_plan.id != plan.id()) {
        return error(AudioPackageAssemblyErrorCode::invalid_identity,
                     "identity.bake_plan",
                     "audio-package-bake-plan-identity-mismatch",
                     "bake-plan identity must be nonzero and name the compiled package plan");
    }
    if (!valid_identity(identity.renderer_build)) {
        return error(AudioPackageAssemblyErrorCode::invalid_identity,
                     "identity.renderer_build",
                     "audio-package-renderer-identity-invalid",
                     "renderer-build identity must have a canonical ID and nonzero digest");
    }
    if (!contract::is_valid_semantic_id(identity.source_inputs.id) ||
        identity.source_inputs.sha256.is_zero()) {
        return error(AudioPackageAssemblyErrorCode::invalid_identity,
                     "identity.source_inputs",
                     "audio-package-source-inputs-identity-invalid",
                     "source-input bundle must have a canonical ID and nonzero digest");
    }

    const auto sources = plan.scenario_sources();
    if (identity.source_scenarios.size() != sources.size()) {
        return error(AudioPackageAssemblyErrorCode::invalid_identity,
                     "identity.source_scenarios",
                     "audio-package-source-identity-count-mismatch",
                     "one authored-order source-scenario identity is required per plan source");
    }
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto &actual = identity.source_scenarios[index];
        const auto &expected = sources[index];
        const auto path = indexed("identity.source_scenarios", index);
        if (actual.source_id != expected.id) {
            return error(AudioPackageAssemblyErrorCode::invalid_identity,
                         path + ".source_id",
                         "audio-package-source-identity-order-mismatch",
                         "source identity does not occupy its authored plan slot");
        }
        if (!valid_identity(actual.scenario) ||
            actual.scenario.id != expected.scenario.id()) {
            return error(AudioPackageAssemblyErrorCode::invalid_identity,
                         path + ".scenario",
                         "audio-package-scenario-identity-mismatch",
                         "scenario identity must be nonzero and name the compiled source scenario");
        }
    }
    return std::nullopt;
}

[[nodiscard]] contract::RationalRateHz
audio_rate(const CompiledPackageBake &plan) noexcept {
    const auto rate = plan.audio_sample_rate();
    return {rate.numerator_hz, rate.denominator};
}

[[nodiscard]] std::optional<AudioPackageAssemblyError>
validate_capture(const CompiledPackageBake &plan,
                 const PackageSourceCaptureSet &captures) {
    const auto sources = plan.scenario_sources();
    const auto buses = plan.audio_buses();
    const auto expected_rate = audio_rate(plan);
    if (captures.sources.size() != sources.size()) {
        return error(AudioPackageAssemblyErrorCode::invalid_capture_set,
                     "captures.sources",
                     "audio-package-capture-count-mismatch",
                     "capture set must contain every plan source in authored order");
    }
    if (buses.empty()) {
        return error(AudioPackageAssemblyErrorCode::unsupported_audio_bus,
                     "plan.audio_buses", "audio-package-no-audition-bus",
                     "the first audio-package gate requires an audition bus");
    }
    for (std::size_t bus_index = 0; bus_index < buses.size(); ++bus_index) {
        if (buses[bus_index].kind !=
            contract::OutputBusKind::master_engine_audition) {
            return error(AudioPackageAssemblyErrorCode::unsupported_audio_bus,
                         indexed("plan.audio_buses", bus_index) + ".kind",
                         "audio-package-unsupported-bus-kind",
                         "the first package audition admits only master-engine-audition buses");
        }
    }

    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
        const auto &capture = captures.sources[source_index];
        const auto &source = sources[source_index];
        const auto path = indexed("captures.sources", source_index);
        if (capture.engine_id != plan.engine().id() ||
            capture.scenario_id != source.scenario.id()) {
            return error(AudioPackageAssemblyErrorCode::invalid_capture_set, path,
                         "audio-package-capture-identity-mismatch",
                         "captured engine or scenario does not occupy its authored plan slot");
        }
        if (capture.sample_rate != expected_rate ||
            capture.audible_delivery_frame_count == 0U ||
            capture.audible_delivery_frame_count >
                contract::kMaximumResolvedFrameIndex) {
            return error(AudioPackageAssemblyErrorCode::invalid_capture_set, path,
                         "audio-package-capture-clock-invalid",
                         "captured lane must be a nonempty runtime-safe tape at the package rate");
        }
        if (capture.usable_cycles.empty() ||
            capture.usable_cycles.size() !=
                capture.usable_cycle_lane_boundaries.size()) {
            return error(AudioPackageAssemblyErrorCode::invalid_capture_set,
                         path + ".usable_cycles",
                         "audio-package-capture-cycle-shape-invalid",
                         "captured lane must retain matching exact-cycle evidence and tape coordinates");
        }
        if (capture.buses.size() != buses.size()) {
            return error(AudioPackageAssemblyErrorCode::invalid_capture_set,
                         path + ".buses",
                         "audio-package-capture-bus-count-mismatch",
                         "captured lane must contain every package bus in authored order");
        }
        for (std::size_t bus_index = 0; bus_index < buses.size(); ++bus_index) {
            const auto &captured_bus = capture.buses[bus_index];
            const auto &planned_bus = buses[bus_index];
            const auto bus_path = indexed(path + ".buses", bus_index);
            if (captured_bus.id != planned_bus.session_bus_id ||
                captured_bus.kind != EngineAudioBusKind::engine_audition_master ||
                captured_bus.signal_disposition !=
                    EngineAudioSignalDisposition::active ||
                captured_bus.source_route_kind !=
                    contract::SourceRouteKind::unspecified ||
                captured_bus.route_id.has_value() ||
                captured_bus.channel_count != 1U ||
                captured_bus.sample_rate != expected_rate ||
                captured_bus.samples.size() !=
                    capture.audible_delivery_frame_count) {
                return error(AudioPackageAssemblyErrorCode::invalid_capture_set,
                             bus_path,
                             "audio-package-captured-bus-contract-mismatch",
                             "captured session bus does not match the authored mono audition bus contract");
            }
            for (std::size_t sample_index = 0;
                 sample_index < captured_bus.samples.size(); ++sample_index) {
                const auto sample = captured_bus.samples[sample_index];
                if (!std::isfinite(sample) || std::abs(sample) > 1.0F) {
                    return error(
                        AudioPackageAssemblyErrorCode::invalid_audio_payload,
                        bus_path + ".samples[" + std::to_string(sample_index) + "]",
                        std::isfinite(sample)
                            ? "audio-package-captured-sample-clips"
                            : "audio-package-captured-sample-nonfinite",
                        std::isfinite(sample)
                            ? "first-audition package rejects samples outside [-1, 1]"
                            : "first-audition package rejects nonfinite samples");
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::variant<std::vector<std::byte>, AudioPackageAssemblyError>
encode_wave(const PackageSourceLaneBusCapture &bus,
            const std::uint64_t frame_count, const std::string_view path) {
    const contract::AudioContract contract{
        bus.sample_rate,
        frame_count,
        "mono",
        "float32le",
    };
    auto created = artifacts::make_wav_encoder(contract);
    if (const auto *failure = std::get_if<artifacts::WavEncodingError>(&created)) {
        return error(AudioPackageAssemblyErrorCode::wave_encoding_failed,
                     std::string{path} + "." + failure->path,
                     "audio-package-wave-create-failed", failure->message);
    }
    auto encoder = std::get<artifacts::WavEncoder>(std::move(created));
    if (encoder.expected_byte_count() >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return error(AudioPackageAssemblyErrorCode::resource_limit,
                     std::string{path},
                     "audio-package-wave-address-space-exceeded",
                     "encoded WAVE exceeds this runtime's addressable memory");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(encoder.expected_byte_count()));
    const artifacts::WavChunkConsumer consume =
        [&](const std::uint64_t offset, const std::span<const std::byte> chunk) {
            if (offset != bytes.size()) {
                return false;
            }
            bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            return true;
        };
    const auto run = [&](artifacts::WavEncodingStatus status,
                         const std::string_view detail_code)
        -> std::optional<AudioPackageAssemblyError> {
        if (!status.has_value()) {
            return std::nullopt;
        }
        return error(AudioPackageAssemblyErrorCode::wave_encoding_failed,
                     std::string{path} + "." + status->path,
                     std::string{detail_code}, status->message);
    };
    if (auto failure = run(encoder.begin(consume),
                           "audio-package-wave-header-failed")) {
        return std::move(*failure);
    }
    if (auto failure = run(encoder.write_float32_interleaved(bus.samples, consume),
                           "audio-package-wave-payload-failed")) {
        return std::move(*failure);
    }
    if (auto failure = run(encoder.finish(consume),
                           "audio-package-wave-finish-failed")) {
        return std::move(*failure);
    }
    if (bytes.size() != encoder.expected_byte_count()) {
        return error(AudioPackageAssemblyErrorCode::wave_encoding_failed,
                     std::string{path}, "audio-package-wave-size-mismatch",
                     "encoded WAVE byte count differs from its exact contract");
    }
    return bytes;
}

[[nodiscard]] contract::AudioPackageRunningDirection package_direction(
    const authoring::PackageBakeRunningDirection direction) noexcept {
    return direction == authoring::PackageBakeRunningDirection::rising
               ? contract::AudioPackageRunningDirection::rising
               : contract::AudioPackageRunningDirection::falling;
}

[[nodiscard]] UniformCycleLaneView
cycle_lane(const PackageSourceLaneCapture &capture) noexcept {
    return {
        capture.usable_cycles,
        capture.audible_first_delivery_frame,
        capture.audible_delivery_frame_count,
    };
}

struct LanePayloadAssembly {
    std::vector<contract::AudioPackageLaneArtifactRef> references;
    std::vector<contract::AudioPackageArtifact> artifacts;
    std::vector<AudioPackagePayloadFile> files;
};

[[nodiscard]] std::variant<LanePayloadAssembly, AudioPackageAssemblyError>
assemble_lane_payloads(const CompiledPackageBake &plan,
                       const PackageSourceLaneCapture &capture,
                       const std::string_view lane_id,
                       const std::string_view lane_path) {
    LanePayloadAssembly result;
    const auto planned_buses = plan.audio_buses();
    result.references.reserve(planned_buses.size());
    result.artifacts.reserve(planned_buses.size());
    result.files.reserve(planned_buses.size());
    for (std::size_t index = 0; index < planned_buses.size(); ++index) {
        const auto &planned_bus = planned_buses[index];
        const auto &captured_bus = capture.buses[index];
        const auto artifact_id = std::string{lane_id} + "." + planned_bus.id;
        const auto relative_path =
            "audio/" + std::string{lane_path} + "/" + planned_bus.id + ".wav";
        auto wave_result = encode_wave(
            captured_bus, capture.audible_delivery_frame_count,
            "payload_files." + relative_path);
        if (auto *failure =
                std::get_if<AudioPackageAssemblyError>(&wave_result)) {
            return std::move(*failure);
        }
        auto bytes = std::get<std::vector<std::byte>>(std::move(wave_result));
        result.references.push_back({planned_bus.id, artifact_id});
        result.artifacts.push_back({
            artifact_id,
            relative_path,
            capture.audible_delivery_frame_count,
            static_cast<std::uint64_t>(bytes.size()),
            contract::sha256(bytes),
        });
        result.files.push_back({relative_path, std::move(bytes)});
    }
    return result;
}

void append_lane_payloads(AssembledAudioPackage &package,
                          LanePayloadAssembly payloads) {
    package.manifest.artifacts.insert(
        package.manifest.artifacts.end(),
        std::make_move_iterator(payloads.artifacts.begin()),
        std::make_move_iterator(payloads.artifacts.end()));
    package.payload_files.insert(package.payload_files.end(),
                                 std::make_move_iterator(payloads.files.begin()),
                                 std::make_move_iterator(payloads.files.end()));
}

} // namespace

AudioPackageAssemblyResult assemble_audio_package(
    const CompiledPackageBake &plan, const PackageSourceCaptureSet &captures,
    const AudioPackageAssemblyIdentity &identity) noexcept {
    try {
        if (auto failure = validate_identity(plan, identity)) {
            return std::move(*failure);
        }
        if (auto failure = validate_capture(plan, captures)) {
            return std::move(*failure);
        }

        AssembledAudioPackage package;
        auto &manifest = package.manifest;
        manifest.identity = {std::string{plan.id()}, identity.engine,
                             identity.bake_plan};
        manifest.provenance = {identity.renderer_build, identity.source_inputs};
        manifest.audio = {
            audio_rate(plan),
            contract::AudioPackageAudioContainer::wav,
            contract::AudioSampleEncoding::float32le,
            contract::AudioChannelLayout::mono,
        };
        manifest.buses.reserve(plan.audio_buses().size());
        for (const auto &bus : plan.audio_buses()) {
            manifest.buses.push_back({
                bus.id,
                contract::AudioPackageBusKind::master_engine_audition,
                contract::AudioPackageBusDisposition::monitor_mix,
                std::nullopt,
            });
        }

        const auto &geometry = plan.method_geometry();
        const auto &rpm_range = plan.rpm_range();
        manifest.running.cycle_revolutions = 2U;
        manifest.running.selector_seed = plan.public_seed();
        manifest.running.cycle_signal_alignment_frames =
            geometry.cycle_signal_alignment_frames;
        manifest.running.rpm_grid = {
            rpm_range.padded_minimum_rpm,
            rpm_range.playback_minimum_rpm,
            rpm_range.playback_maximum_rpm,
            rpm_range.padded_maximum_rpm,
            geometry.rpm_grid_spacing,
            geometry.padding_rows_per_side,
            geometry.neighbor_radius_rows,
            geometry.edge_guard_frames,
            geometry.maximum_assignment_error_rpm,
        };

        const auto planes = plan.running_planes();
        manifest.running.planes.reserve(planes.size());
        for (std::size_t plane_index = 0; plane_index < planes.size();
             ++plane_index) {
            const auto &compiled_plane = planes[plane_index];
            const auto source_index = compiled_plane.scenario_source_index;
            const auto &capture = captures.sources[source_index];
            auto bank_result = assign_uniform_running_cycle_bank({
                cycle_lane(capture),
                geometry,
                rpm_range.padded_minimum_rpm,
                rpm_range.padded_maximum_rpm,
                compiled_plane.load_coordinate,
                compiled_plane.direction,
            });
            if (auto *failure = std::get_if<UniformCycleBankError>(&bank_result)) {
                return error(
                    AudioPackageAssemblyErrorCode::cycle_bank_failed,
                    indexed("running.planes", plane_index) + "." + failure->path,
                    "audio-package-running-cycle-bank-failed", failure->detail);
            }

            const auto lane_id = "running." + compiled_plane.id;
            const auto lane_path = "running/" + compiled_plane.id;
            auto payload_result = assemble_lane_payloads(
                plan, capture, lane_id, lane_path);
            if (auto *failure =
                    std::get_if<AudioPackageAssemblyError>(&payload_result)) {
                return std::move(*failure);
            }
            auto payloads =
                std::get<LanePayloadAssembly>(std::move(payload_result));
            contract::AudioPackageRunningPlane plane{
                compiled_plane.id,
                compiled_plane.load_coordinate,
                package_direction(compiled_plane.direction),
                identity.source_scenarios[source_index].scenario,
                std::move(payloads.references),
                std::get<UniformCycleBank>(std::move(bank_result)).units,
            };
            append_lane_payloads(package, std::move(payloads));
            manifest.running.planes.push_back(std::move(plane));
        }

        const auto idle_source_index = plan.idle_scenario_source_index();
        const auto &idle_capture = captures.sources[idle_source_index];
        auto idle_bank_result = retain_uniform_idle_cycle_pool({
            cycle_lane(idle_capture),
            geometry,
            rpm_range.playback_minimum_rpm,
            0.0,
        });
        if (auto *failure =
                std::get_if<UniformCycleBankError>(&idle_bank_result)) {
            return error(AudioPackageAssemblyErrorCode::cycle_bank_failed,
                         "running.idle." + failure->path,
                         "audio-package-idle-cycle-pool-failed", failure->detail);
        }
        auto idle_payload_result =
            assemble_lane_payloads(plan, idle_capture, "idle", "idle");
        if (auto *failure =
                std::get_if<AudioPackageAssemblyError>(&idle_payload_result)) {
            return std::move(*failure);
        }
        auto idle_payloads =
            std::get<LanePayloadAssembly>(std::move(idle_payload_result));
        manifest.running.idle = {
            identity.source_scenarios[idle_source_index].scenario,
            std::move(idle_payloads.references),
            std::get<UniformCycleBank>(std::move(idle_bank_result)).units,
        };
        append_lane_payloads(package, std::move(idle_payloads));

        std::ranges::sort(manifest.artifacts, {},
                          &contract::AudioPackageArtifact::id);
        std::ranges::sort(package.payload_files, {},
                          &AudioPackagePayloadFile::relative_path);
        const auto validation = contract::validate(manifest);
        if (!validation.ok()) {
            const auto &issue = validation.issues.front();
            return error(AudioPackageAssemblyErrorCode::manifest_validation_failed,
                         issue.path,
                         "audio-package-manifest-contract-rejected", issue.message);
        }
        auto manifest_result = artifacts::encode_audio_package_manifest(manifest);
        if (auto *failure = std::get_if<RenderSinkError>(&manifest_result)) {
            return error(AudioPackageAssemblyErrorCode::manifest_encoding_failed,
                         "package.json", failure->detail_code, failure->message);
        }
        package.package_json =
            std::get<artifacts::ManifestEncoding>(std::move(manifest_result)).bytes;
        return package;
    } catch (const std::bad_alloc &) {
        return error(AudioPackageAssemblyErrorCode::resource_limit, {},
                     "audio-package-assembly-allocation-failed",
                     "allocation failed while assembling the in-memory audio package");
    } catch (...) {
        return error(AudioPackageAssemblyErrorCode::internal_failure, {},
                     "audio-package-assembly-unexpected-failure",
                     "an unexpected failure interrupted in-memory audio package assembly");
    }
}

} // namespace engine_sim_offline::package_detail
