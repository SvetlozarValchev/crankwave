#pragma once

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"

#include <string_view>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kReferenceManifestWireSchemaV1 =
    "engine-sim-offline.render-manifest.reference-presentation.v1";
inline constexpr std::string_view kReferenceManifestRelativePathV1 =
    "manifest/render-manifest.v1.json";

// Encodes the complete reference_presentation_v1 RenderManifest using the canonical
// JSON contract. This checks wire representability only. The owning render session
// must first perform complete semantic validation with its provenance ledger and
// source matrix. SimulationManifestInputs remain deliberately unencodable until M3.
[[nodiscard]] ManifestEncodingResult
encode_reference_manifest_v1(const contract::RenderManifest &manifest);

} // namespace engine_sim_offline::artifacts
