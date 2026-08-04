#pragma once

#include "engine_sim_offline/contract/parity_model.hpp"
#include "simulation/legacy_gas_primitives.hpp"

namespace engine_sim_offline::simulation {

// Converts the authored standard-CFM number used by the legacy flow-bench
// calibration into the matching fixed-air mass-flow rate. Keep the operation order
// aligned with legacy_flow_bench_restriction_coefficient(): the source-era SCFM
// conversion produces mol/s first, then the admitted fixed air molar mass produces
// kg/s.
[[nodiscard]] inline constexpr double
legacy_standard_cfm_mass_flow_kg_s(double source_rating_cfm) noexcept {
    constexpr double one_source_scfm_mol_s = 0.002641 * 453.59237 / 60.0;
    const double source_flow_mol_s = source_rating_cfm * one_source_scfm_mol_s;
    return source_flow_mol_s * kLegacyAirMolarMassKgPerMol;
}

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
