#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <variant>
#include <vector>

namespace engine_sim_offline::profiles {

struct BmwM52b28ParityRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28ParityRequest &,
                           const BmwM52b28ParityRequest &) = default;
};

using BmwM52b28ParityRequestResult =
    std::variant<BmwM52b28ParityRequest, contract::ValidationReport>;

// Constructs the one frozen M3 parity request. The caller supplies only the owned
// post-step RPM lane; every engine value, control boundary, and component stream is
// constructed from the normative request record. Invalid or noncanonical input is
// returned as a validation report rather than a partial request.
[[nodiscard]] BmwM52b28ParityRequestResult
make_bmw_m52b28_parity_request(std::vector<double> post_step_rpm);

// Applies generic contract checks and the mutation-sensitive exact BMW parity pins.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_parity_request(const BmwM52b28ParityRequest &request);

} // namespace engine_sim_offline::profiles
