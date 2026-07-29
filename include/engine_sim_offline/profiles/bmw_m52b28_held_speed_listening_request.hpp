#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <variant>

namespace engine_sim_offline::profiles {

// The first canonical M4 listening point. The factory owns the engine, held-speed
// test-cell scenario, and their one fresh provenance ledger; it never relabels an M3
// request or copies a completed operating-profile ledger.
struct BmwM52b28HeldSpeedListeningRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28HeldSpeedListeningRequest &,
                           const BmwM52b28HeldSpeedListeningRequest &) = default;
};

using BmwM52b28HeldSpeedListeningRequestResult =
    std::variant<BmwM52b28HeldSpeedListeningRequest, contract::ValidationReport>;

// Constructs and validates the frozen 3000 rpm, 0.85-throttle BMW listening
// request. Its fixed preparation horizon and trailing sample size are profile-owned,
// not caller controls.
[[nodiscard]] BmwM52b28HeldSpeedListeningRequestResult
make_bmw_m52b28_held_speed_listening_request();

// Applies generic engine/scenario/provenance validation and the exact canonical
// listening-request pins.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_held_speed_listening_request(
    const BmwM52b28HeldSpeedListeningRequest &request);

} // namespace engine_sim_offline::profiles
