#pragma once

#include "engine_sim_offline/artifacts/manifest_encoder.hpp"
#include "engine_sim_offline/contract/audio_package.hpp"

#include <string_view>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kAudioPackageManifestRelativePath =
    "package.json";

// Produces one deterministic, runtime-readable JSON document. Binary64 quantities
// are finite shortest-round-trip JSON numbers rather than internal bit strings.
[[nodiscard]] ManifestEncodingResult
encode_audio_package_manifest(const contract::AudioPackageManifest &manifest);

} // namespace engine_sim_offline::artifacts
