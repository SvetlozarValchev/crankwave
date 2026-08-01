#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::identity {

inline constexpr std::string_view kSimulationRequestIdentityWireSchemaV6 =
    "engine-sim-offline.simulation-request-identity.v6";

struct SimulationRequestIdentityEncoding {
    std::vector<std::byte> bytes;
    contract::Sha256Digest sha256;

    friend bool operator==(const SimulationRequestIdentityEncoding &,
                           const SimulationRequestIdentityEncoding &) = default;
};

enum class SimulationRequestIdentityErrorCode : std::uint8_t {
    wire_unrepresentable,
    wire_invalid_utf8,
    wire_nonfinite,
    wire_size_exceeded,
    allocation_failure,
    encoding_exception,
};

struct SimulationRequestIdentityError {
    SimulationRequestIdentityErrorCode code =
        SimulationRequestIdentityErrorCode::wire_unrepresentable;
    std::string detail_code;
    std::string message;

    friend bool operator==(const SimulationRequestIdentityError &,
                           const SimulationRequestIdentityError &) = default;
};

using SimulationRequestIdentityEncodingResult =
    std::variant<SimulationRequestIdentityEncoding, SimulationRequestIdentityError>;

// Encodes the complete resolved simulation factory identity without presentation
// or run facts. The random plan is the exact canonical plan consumed by execution,
// including every provisioned component lane. The digest covers the exact canonical
// v6 bytes, including final LF.
[[nodiscard]] SimulationRequestIdentityEncodingResult
encode_simulation_request_identity_v6(const contract::EngineSpec &engine,
                                      const contract::RenderScenario &scenario,
                                      const contract::RandomPlan &random_plan,
                                      const contract::ProvenanceBundleRef &provenance);

} // namespace engine_sim_offline::identity
