#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <variant>

namespace engine_sim_offline::profiles {

inline constexpr std::size_t kBmwM52b28HeldRegressionPointCount = 4U;

// One independent point in the canonical M4 held operating-regression matrix.
// Every point owns a fresh engine, scenario, and provenance ledger; no mutable
// simulation or presentation state is shared between points.
struct BmwM52b28HeldRegressionRequest {
    std::string point_key;
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28HeldRegressionRequest &,
                           const BmwM52b28HeldRegressionRequest &) = default;
};

using BmwM52b28HeldRegressionRequestSet =
    std::array<BmwM52b28HeldRegressionRequest, kBmwM52b28HeldRegressionPointCount>;

using BmwM52b28HeldRegressionRequestSetResult =
    std::variant<BmwM52b28HeldRegressionRequestSet, contract::ValidationReport>;

// Constructs and validates the exact four-point BMW M52B28 held operating-
// regression matrix. The factory exposes no caller-controlled calibration.
[[nodiscard]] BmwM52b28HeldRegressionRequestSetResult
make_bmw_m52b28_held_regression_request_set();

// Enforces exact point membership, order, identities, conditions, and
// cardinality for the complete normative set.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_held_regression_request_set(
    const BmwM52b28HeldRegressionRequestSet &request_set);

} // namespace engine_sim_offline::profiles
