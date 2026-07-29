#pragma once

#include "engine_sim_offline/artifacts/manifest_encoder.hpp"
#include <string_view>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kSimulationManifestWireSchemaV6 =
    "engine-sim-offline.render-manifest.simulation.v6";
inline constexpr std::string_view kSimulationManifestRelativePathV6 =
    "manifest/render-manifest.v6.json";
// Encodes a completed SimulationManifestInputs render manifest. The owning render
// session performs semantic admission first; this boundary checks canonical wire
// representability and verifies every fixed-rate RPM vector against its stored digest.
[[nodiscard]] ManifestEncodingResult
encode_simulation_manifest_v6(const contract::RenderManifest &manifest);

} // namespace engine_sim_offline::artifacts
