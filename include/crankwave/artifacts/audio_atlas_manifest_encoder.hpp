#pragma once

#include "crankwave/artifacts/manifest_encoder.hpp"
#include "crankwave/contract/audio_atlas.hpp"

namespace crankwave::artifacts {

// Encodes the sole current audio-atlas schema. Authored array order is retained,
// uint64 values use the schema's canonical decimal-string representation, and
// binary64 values use deterministic round-trip decimal JSON numbers.
//
// Invalid manifests and values that cannot be represented by the wire contract are
// returned as typed RenderSinkError alternatives; they are never partially encoded.
[[nodiscard]] ManifestEncodingResult
encode_audio_atlas_manifest(const contract::AudioAtlasManifest &manifest);

} // namespace crankwave::artifacts
