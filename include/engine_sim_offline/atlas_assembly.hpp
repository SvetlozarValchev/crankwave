#pragma once

#include "engine_sim_offline/contract/audio_atlas.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace engine_sim_offline {

struct AudioAtlasArtifactPayload {
    std::string artifact_id;
    std::vector<std::byte> bytes;

    friend bool operator==(const AudioAtlasArtifactPayload &,
                           const AudioAtlasArtifactPayload &) = default;
};

struct AssembledAudioAtlas {
    contract::AudioAtlasManifest manifest;
    // Payload order is the manifest artifact order. Publication validates the
    // identity, length, digest, and path before writing anything.
    std::vector<AudioAtlasArtifactPayload> payloads;
};

} // namespace engine_sim_offline
