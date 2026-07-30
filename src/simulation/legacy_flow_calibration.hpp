#pragma once

#include "engine_sim_offline/contract/parity_model.hpp"

namespace engine_sim_offline::simulation {

[[nodiscard]] bool known_legacy_flow_calibration(
    contract::LegacyRestrictionCalibration calibration) noexcept;

[[nodiscard]] double legacy_flow_calibration_pressure_drop_pa(
    contract::LegacyRestrictionCalibration calibration) noexcept;

// Reproduces the exact source-era SCFM-to-restriction-coefficient operation used by
// the admitted low-order gas model. source_rating retains its authored flow-bench
// number; it is not an uncalibrated SI volume-flow value.
[[nodiscard]] double legacy_flow_bench_restriction_coefficient(
    contract::LegacyRestrictionCalibration calibration,
    double source_rating) noexcept;

} // namespace engine_sim_offline::simulation
