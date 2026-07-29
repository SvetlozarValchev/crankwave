#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <variant>

namespace engine_sim_offline::profiles {

inline constexpr std::size_t kBmwM52b28HeldIdleLowLoadPointCount = 2U;

// One independent point in the canonical BMW M52B28 held idle-region and
// low-load regression set. Every point owns a fresh engine, scenario, and
// provenance ledger.
struct BmwM52b28HeldIdleLowLoadRequest {
    std::string point_key;
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28HeldIdleLowLoadRequest &,
                           const BmwM52b28HeldIdleLowLoadRequest &) = default;
};

using BmwM52b28HeldIdleLowLoadRequestSet =
    std::array<BmwM52b28HeldIdleLowLoadRequest, kBmwM52b28HeldIdleLowLoadPointCount>;

using BmwM52b28HeldIdleLowLoadRequestSetResult =
    std::variant<BmwM52b28HeldIdleLowLoadRequestSet, contract::ValidationReport>;

// Constructs and validates the exact two-point BMW M52B28 held idle-region
// and low-load set. The factory exposes no caller-controlled calibration.
[[nodiscard]] BmwM52b28HeldIdleLowLoadRequestSetResult
make_bmw_m52b28_held_idle_low_load_request_set();

// Enforces exact point membership, order, identities, conditions, and
// cardinality for the complete normative set.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_held_idle_low_load_request_set(
    const BmwM52b28HeldIdleLowLoadRequestSet &request_set);

} // namespace engine_sim_offline::profiles
