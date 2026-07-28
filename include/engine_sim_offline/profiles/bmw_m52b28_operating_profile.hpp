#pragma once

#include "engine_sim_offline/contract/engine.hpp"

#include <variant>

namespace engine_sim_offline::profiles {

// The canonical BMW M52B28 engine/profile pair for positive-speed warm operating
// points. A held-speed scenario is intentionally not part of this checkpoint: its
// convergence policy becomes canonical only with the executable held-point producer.
struct BmwM52b28OperatingProfile {
    contract::EngineSpec engine;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28OperatingProfile &,
                           const BmwM52b28OperatingProfile &) = default;
};

using BmwM52b28OperatingProfileResult =
    std::variant<BmwM52b28OperatingProfile, contract::ValidationReport>;

// Constructs and validates the one frozen warm-stock BMW operating profile.
[[nodiscard]] BmwM52b28OperatingProfileResult
make_bmw_m52b28_operating_profile();

// Applies generic engine/provenance checks plus exact BMW profile, coefficient,
// accessory, method, and accounting pins.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_operating_profile(
    const BmwM52b28OperatingProfile &profile);

} // namespace engine_sim_offline::profiles
