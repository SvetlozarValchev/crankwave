#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include <array>
#include <cstddef>
#include <variant>

namespace engine_sim_offline::profiles {

inline constexpr std::size_t kBmwM52b28FullThrottleTorqueSweepPointCount = 9U;

inline constexpr std::array<double, kBmwM52b28FullThrottleTorqueSweepPointCount>
    kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm{
        1500.0, 2500.0, 3000.0, 3500.0, 3950.0, 4500.0, 5300.0, 6000.0, 6500.0,
    };

// One independent held-speed point in the canonical M4 full-throttle torque
// sweep. Every point owns a fresh engine, scenario, and provenance ledger; no
// mutable simulation state is shared between points.
struct BmwM52b28FullThrottleTorqueSweepRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;

    friend bool operator==(const BmwM52b28FullThrottleTorqueSweepRequest &,
                           const BmwM52b28FullThrottleTorqueSweepRequest &) = default;
};

using BmwM52b28FullThrottleTorqueSweepRequestSet =
    std::array<BmwM52b28FullThrottleTorqueSweepRequest,
               kBmwM52b28FullThrottleTorqueSweepPointCount>;

using BmwM52b28FullThrottleTorqueSweepRequestSetResult =
    std::variant<BmwM52b28FullThrottleTorqueSweepRequestSet,
                 contract::ValidationReport>;

// Constructs and validates the frozen nine-point BMW M52B28 M4 torque sweep in
// ascending-RPM order. The factory exposes no caller-controlled calibration.
[[nodiscard]] BmwM52b28FullThrottleTorqueSweepRequestSetResult
make_bmw_m52b28_full_throttle_torque_sweep_request_set();

// Accepts exactly one normative point from the canonical set.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_full_throttle_torque_sweep_request(
    const BmwM52b28FullThrottleTorqueSweepRequest &request);

// Applies point validation and enforces exact point membership, order, and
// cardinality for the complete normative set.
[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_full_throttle_torque_sweep_request_set(
    const BmwM52b28FullThrottleTorqueSweepRequestSet &requests);

} // namespace engine_sim_offline::profiles
