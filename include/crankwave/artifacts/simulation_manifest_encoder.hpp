#pragma once

#include "crankwave/artifacts/manifest_encoder.hpp"
#include <string_view>

namespace crankwave::artifacts {

inline constexpr std::string_view kSimulationManifestWireSchemaV10 =
    "crankwave.render-manifest.simulation.v10";
inline constexpr std::string_view kSimulationManifestRelativePathV10 =
    "manifest/render-manifest.v10.json";
// Encodes a completed SimulationManifestInputs render manifest. The owning render
// session performs semantic admission first; this boundary checks canonical wire
// representability and verifies every fixed-rate RPM vector against its stored digest.
[[nodiscard]] ManifestEncodingResult
encode_simulation_manifest_v10(const contract::RenderManifest &manifest);

} // namespace crankwave::artifacts
