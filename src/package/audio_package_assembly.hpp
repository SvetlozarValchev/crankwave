#pragma once

#include "engine_sim_offline/contract/audio_package.hpp"
#include "engine_sim_offline/package_bake.hpp"
#include "package/package_source_capture_set.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {

struct AudioPackageSourceScenarioIdentity {
    // The authored package source ID and the compiled scenario ID are distinct
    // identities. Retaining both makes authored-order binding explicit.
    std::string source_id;
    contract::AudioPackageContentIdentity scenario;
};

struct AudioPackageAssemblyIdentity {
    contract::AudioPackageContentIdentity engine;
    contract::AudioPackageContentIdentity bake_plan;
    contract::AudioPackageContentIdentity renderer_build;
    contract::ProvenanceBundleRef source_inputs;
    // Exact CompiledPackageBake::scenario_sources() order.
    std::vector<AudioPackageSourceScenarioIdentity> source_scenarios;
};

struct AudioPackagePayloadFile {
    std::string relative_path;
    std::vector<std::byte> bytes;
};

struct AssembledAudioPackage {
    contract::AudioPackageManifest manifest;
    std::vector<std::byte> package_json;
    // WAVE payloads only. package_json is deliberately separate so the native
    // publisher can commit the root manifest last.
    std::vector<AudioPackagePayloadFile> payload_files;
};

enum class AudioPackageAssemblyErrorCode : std::uint8_t {
    invalid_identity,
    invalid_capture_set,
    unsupported_audio_bus,
    invalid_audio_payload,
    cycle_bank_failed,
    inconsistent_load_calibration,
    wave_encoding_failed,
    manifest_validation_failed,
    manifest_encoding_failed,
    resource_limit,
    internal_failure,
};

struct AudioPackageAssemblyError {
    AudioPackageAssemblyErrorCode code =
        AudioPackageAssemblyErrorCode::internal_failure;
    std::string path;
    std::string detail_code;
    std::string message;
};

using AudioPackageAssemblyResult =
    std::variant<AssembledAudioPackage, AudioPackageAssemblyError>;

// Pure package construction. This function neither captures a session nor touches
// a filesystem. Every payload is an exact containerization of captured Float32 PCM;
// no fade, gain, DSP, resampling, or quantization occurs here.
[[nodiscard]] AudioPackageAssemblyResult assemble_audio_package(
    const CompiledPackageBake &plan, const PackageSourceCaptureSet &captures,
    const AudioPackageAssemblyIdentity &identity) noexcept;

} // namespace engine_sim_offline::package_detail
