#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <variant>

namespace engine_sim_offline::profiles {

// The first canonical M4 inertial-dyno listening pull. The factory owns the
// operating engine, physical test-cell scenario, and one fresh provenance ledger;
// the target speed is evidence and never becomes a prescribed RPM trajectory.
struct BmwM52b28InertialDynoListeningRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28InertialDynoListeningRequest &,
                           const BmwM52b28InertialDynoListeningRequest &) = default;
};

using BmwM52b28InertialDynoListeningRequestResult =
    std::variant<BmwM52b28InertialDynoListeningRequest,
                 contract::ValidationReport>;

// Constructs and validates the frozen 1500-to-6500 rpm BMW inertial listening
// request. Inertia, passive brake, and convergence calibration are not caller
// controls.
[[nodiscard]] BmwM52b28InertialDynoListeningRequestResult
make_bmw_m52b28_inertial_dyno_listening_request();

// Applies generic engine/scenario/provenance validation and the exact canonical
// listening-request pins.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_inertial_dyno_listening_request(
    const BmwM52b28InertialDynoListeningRequest &request);

} // namespace engine_sim_offline::profiles
