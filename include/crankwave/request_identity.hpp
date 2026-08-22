#pragma once

#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/provenance.hpp"
#include "crankwave/contract/randomness.hpp"
#include "crankwave/contract/scenario.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::identity {

inline constexpr std::string_view kSimulationRequestIdentityWireSchemaV7 =
    "crankwave.simulation-request-identity.v7";

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
// v7 bytes, including final LF.
[[nodiscard]] SimulationRequestIdentityEncodingResult
encode_simulation_request_identity_v7(const contract::EngineSpec &engine,
                                      const contract::RenderScenario &scenario,
                                      const contract::RandomPlan &random_plan,
                                      const contract::ProvenanceBundleRef &provenance);

} // namespace crankwave::identity
