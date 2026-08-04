#pragma once

#include "engine_sim_offline/atlas_bake.hpp"
#include "engine_sim_offline/atlas_capture.hpp"
#include "engine_sim_offline/contract/audio_atlas.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline {

// The caller owns the canonical bytes behind these identities. Assembly never
// guesses a digest from a compiled handle or consults the filesystem.
struct AudioAtlasAssemblyProvenance {
    contract::AudioAtlasContentIdentity engine;
    contract::AudioAtlasContentIdentity bake_document;
    contract::AudioAtlasContentIdentity renderer_build;
    contract::ProvenanceBundleRef source_inputs;
};

// Inputs are supplied in the exact authored moving_segments order. Repeating the
// segment identity here prevents an accidentally reordered capture vector from
// producing a valid-looking atlas.
struct AtlasMovingLaneAssemblyInputView {
    std::string_view moving_segment_id;
    const AtlasMovingLaneCapture *capture = nullptr;
    contract::AudioAtlasContentIdentity source_scenario;
    contract::AudioAtlasContentIdentity capture_configuration;
};

struct AudioAtlasArtifactPayload {
    std::string artifact_id;
    // Headerless mono IEEE-754 binary32, encoded explicitly little-endian.
    std::vector<std::byte> f32le_bytes;

    friend bool operator==(const AudioAtlasArtifactPayload &,
                           const AudioAtlasArtifactPayload &) = default;
};

struct AssembledAudioAtlas {
    contract::AudioAtlasManifest manifest;
    // Same order as manifest.artifacts. Publication is a separate filesystem seam.
    std::vector<AudioAtlasArtifactPayload> payloads;
};

enum class AudioAtlasAssemblyErrorCode : std::uint8_t {
    invalid_request,
    identity_mismatch,
    capture_mismatch,
    traversal_not_unique,
    timeline_invalid,
    slope_outside_envelope,
    payload_invalid,
    contract_rejected,
    resource_limit,
    internal_failure,
};

struct AudioAtlasAssemblyError {
    AudioAtlasAssemblyErrorCode code = AudioAtlasAssemblyErrorCode::invalid_request;
    std::string detail_code;
    std::string message;
    contract::ValidationReport contract_report;
};

using AudioAtlasAssemblyResult =
    std::variant<AssembledAudioAtlas, AudioAtlasAssemblyError>;

// Pure assembly: selects one authored directional traversal, crops chronological
// PCM without processing it, rebases metadata, and validates the sole atlas
// contract. It performs no capture, resampling, DSP, filesystem access, or I/O.
[[nodiscard]] AudioAtlasAssemblyResult assemble_moving_audio_atlas(
    const CompiledAtlasBake &bake,
    std::span<const AtlasMovingLaneAssemblyInputView> moving_inputs,
    const AudioAtlasAssemblyProvenance &provenance) noexcept;

} // namespace engine_sim_offline
