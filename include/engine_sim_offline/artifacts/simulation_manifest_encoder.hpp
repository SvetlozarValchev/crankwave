#pragma once

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"
#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::artifacts {

inline constexpr std::string_view kSimulationManifestWireSchemaV3 =
    "engine-sim-offline.render-manifest.simulation.v3";
inline constexpr std::string_view kSimulationManifestRelativePathV3 =
    "manifest/render-manifest.v3.json";
inline constexpr std::string_view kSimulationRequestIdentityWireSchemaV1 =
    "engine-sim-offline.simulation-request-identity.v1";

struct SimulationRequestIdentityEncoding {
    std::vector<std::byte> bytes;
    contract::Sha256Digest sha256;

    friend bool operator==(const SimulationRequestIdentityEncoding &,
                           const SimulationRequestIdentityEncoding &) = default;
};

using SimulationRequestIdentityEncodingResult =
    std::variant<SimulationRequestIdentityEncoding, RenderSinkError>;

// Encodes a completed SimulationManifestInputs render manifest. The owning render
// session performs semantic admission first; this boundary checks canonical wire
// representability and verifies every fixed-rate RPM vector against its stored digest.
[[nodiscard]] ManifestEncodingResult
encode_simulation_manifest_v3(const contract::RenderManifest &manifest);

// Encodes the resolved engine/scenario factory identity without presentation or run
// facts. The returned digest covers the complete canonical bytes, including final LF.
[[nodiscard]] SimulationRequestIdentityEncodingResult
encode_simulation_request_identity_v1(const contract::EngineSpec &engine,
                                      const contract::RenderScenario &scenario,
                                      const contract::ProvenanceBundleRef &provenance);

} // namespace engine_sim_offline::artifacts
