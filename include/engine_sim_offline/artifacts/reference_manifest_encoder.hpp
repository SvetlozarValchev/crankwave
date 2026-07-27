#pragma once

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"

#include <string_view>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kReferenceManifestWireSchemaV2 =
    "engine-sim-offline.render-manifest.reference-presentation.v2";
inline constexpr std::string_view kReferenceManifestRelativePathV2 =
    "manifest/render-manifest.v2.json";

// Encodes a complete reference-presentation RenderManifest using the canonical v2
// JSON contract. Its input alternative remains ReferencePresentationInputsV1 because
// the fixture/input shape did not change. This checks wire representability only. The
// owning render session must first perform complete semantic validation with its
// provenance ledger and source matrix. SimulationManifestInputs remain deliberately
// unencodable until M3.
[[nodiscard]] ManifestEncodingResult
encode_reference_manifest_v2(const contract::RenderManifest &manifest);

} // namespace engine_sim_offline::artifacts
