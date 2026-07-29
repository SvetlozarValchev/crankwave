#pragma once

#include <cstdint>

namespace engine_sim_offline::simulation {

inline constexpr double kLegacyGasConstantJPerMolK = 8.31446261815324;
inline constexpr double kLegacyAirMolarMassKgPerMol = 0.02897;
inline constexpr double kLegacyDryAirOxygenMolarFraction = 0.25;
inline constexpr int kLegacyGasDegreesOfFreedom = 5;

// Converts the legacy pseudo-gas fuel's molecular stoichiometric ratio into the
// conventional mass-AFR metadata carried by RenderScenario. The gas solver consumes
// the molecular representation; this exact written-order conversion prevents the
// two quantities from being mislabeled as equal.
[[nodiscard]] inline double legacy_pseudo_gas_stoichiometric_mass_afr(
    double molecular_afr, double fuel_molecular_mass_kg_per_mol) noexcept {
    return (molecular_afr / kLegacyDryAirOxygenMolarFraction) *
           (kLegacyAirMolarMassKgPerMol / fuel_molecular_mass_kg_per_mol);
}

// Molar fractions are intentionally stored independently. The legacy method never
// performs a final normalization pass after an admitted transfer or reaction.
struct LegacyGasMixture {
    double fuel_fraction = 0.0;
    double inert_fraction = 1.0;
    double oxygen_fraction = 0.0;

    friend bool operator==(const LegacyGasMixture &,
                           const LegacyGasMixture &) = default;
};

// Mutable finite-cell state for legacy_low_order_v1. Geometry is kept separate so
// changing a cylinder's thermodynamic volume cannot silently change its fixed
// planar self-impulse geometry.
struct LegacyGasCell {
    double amount_mol = 0.0;
    double thermal_energy_j = 0.0;
    double volume_m3 = 0.0;
    double momentum_x_kg_m_s = 0.0;
    double momentum_y_kg_m_s = 0.0;
    LegacyGasMixture mixture{};

    friend bool operator==(const LegacyGasCell &, const LegacyGasCell &) = default;
};

struct LegacyGasCellGeometry {
    double width_m = 0.0;
    double height_m = 0.0;
    double direction_x = 0.0;
    double direction_y = 0.0;

    friend bool operator==(const LegacyGasCellGeometry &,
                           const LegacyGasCellGeometry &) = default;
};

enum class LegacyGasEndpoint : std::uint8_t {
    endpoint_0 = 0,
    endpoint_1 = 1,
};

struct LegacyFiniteGasTransferParameters {
    double restriction_coefficient = 0.0;
    double step_s = 0.0;
    double direction_x = 0.0;
    double direction_y = 0.0;
    double endpoint_0_cross_section_area_m2 = 0.0;
    double endpoint_1_cross_section_area_m2 = 0.0;
};

struct LegacyFiniteGasTransferResult {
    // Positive is endpoint 0 -> endpoint 1; negative is endpoint 1 -> endpoint 0.
    // Equality follows the legacy endpoint-1 tie and can therefore return -0.0.
    double signed_amount_mol = 0.0;
    double endpoint_0_directional_pressure_pa = 0.0;
    double endpoint_1_directional_pressure_pa = 0.0;
    LegacyGasEndpoint source_endpoint = LegacyGasEndpoint::endpoint_0;

    friend bool operator==(const LegacyFiniteGasTransferResult &,
                           const LegacyFiniteGasTransferResult &) = default;
};

struct LegacyGasEnvironment {
    double pressure_pa = 0.0;
    double temperature_k = 0.0;
    LegacyGasMixture mixture{};
};

struct LegacyEnvironmentGasTransferParameters {
    double restriction_coefficient = 0.0;
    double step_s = 0.0;
    LegacyGasEnvironment environment{};
};

struct LegacyEnvironmentGasTransferResult {
    // Positive is finite cell -> environment; negative is environment -> cell.
    double signed_amount_mol = 0.0;
    double pressure_equilibrium_amount_mol = 0.0;

    friend bool operator==(const LegacyEnvironmentGasTransferResult &,
                           const LegacyEnvironmentGasTransferResult &) = default;
};

[[nodiscard]] constexpr double legacy_gas_heat_capacity_ratio() noexcept {
    return 1.0 + (2.0 / static_cast<double>(kLegacyGasDegreesOfFreedom));
}

[[nodiscard]] constexpr double
legacy_gas_thermal_energy_per_mol(double temperature_k) noexcept {
    return 0.5 * temperature_k * kLegacyGasConstantJPerMolK *
           kLegacyGasDegreesOfFreedom;
}

[[nodiscard]] LegacyGasCell
legacy_initialize_gas_cell(double pressure_pa, double volume_m3, double temperature_k,
                           const LegacyGasMixture &mixture = {}) noexcept;

// Reset retains the cell's volume. Planar momentum is cleared.
void legacy_reset_gas_cell(LegacyGasCell &cell, double pressure_pa,
                           double temperature_k,
                           const LegacyGasMixture &mixture = {}) noexcept;

void legacy_change_gas_volume(LegacyGasCell &cell, double delta_volume_m3) noexcept;
void legacy_set_gas_volume(LegacyGasCell &cell, double volume_m3) noexcept;
void legacy_add_gas_thermal_energy(LegacyGasCell &cell, double delta_energy_j) noexcept;
void legacy_set_gas_mixture(LegacyGasCell &cell,
                            const LegacyGasMixture &mixture) noexcept;

// The caller supplies the exact per-mole thermal energy chosen by the enclosing
// transfer/reaction operation. These helpers deliberately do not adjust momentum.
void legacy_remove_gas_amount(LegacyGasCell &cell, double amount_mol,
                              double thermal_energy_per_mol_j) noexcept;
void legacy_add_gas_amount(LegacyGasCell &cell, double amount_mol,
                           double thermal_energy_per_mol_j,
                           const LegacyGasMixture &incoming_mixture = {}) noexcept;

[[nodiscard]] double legacy_gas_pressure_pa(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_temperature_k(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_mass_kg(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_density_kg_m3(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_velocity_x_m_s(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_velocity_y_m_s(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double
legacy_gas_bulk_kinetic_energy_j(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_total_energy_j(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_sonic_velocity_m_s(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_directional_dynamic_pressure_pa(
    const LegacyGasCell &cell, double direction_x, double direction_y) noexcept;
[[nodiscard]] double legacy_gas_fuel_amount_mol(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_inert_amount_mol(const LegacyGasCell &cell) noexcept;
[[nodiscard]] double legacy_gas_oxygen_amount_mol(const LegacyGasCell &cell) noexcept;

[[nodiscard]] double legacy_gas_choked_pressure_ratio() noexcept;
[[nodiscard]] double legacy_gas_choked_flow_factor() noexcept;

// Signed molar rate. Positive is endpoint 0 -> endpoint 1. Equal pressures take
// the endpoint-1 branch and retain the legacy negative-zero result when K != 0.
[[nodiscard]] double legacy_restriction_molar_rate(
    double restriction_coefficient, double endpoint_0_pressure_pa,
    double endpoint_1_pressure_pa, double endpoint_0_temperature_k,
    double endpoint_1_temperature_k) noexcept;

// Source-era calibration helper. target_source_flow_mol_s is already converted
// from the legacy quantity named SCFM.
[[nodiscard]] double legacy_restriction_coefficient(double target_source_flow_mol_s,
                                                    double upstream_pressure_pa,
                                                    double pressure_drop_pa,
                                                    double temperature_k) noexcept;

[[nodiscard]] LegacyFiniteGasTransferResult
legacy_transfer_gas(LegacyGasCell &endpoint_0, LegacyGasCell &endpoint_1,
                    const LegacyFiniteGasTransferParameters &parameters) noexcept;

[[nodiscard]] double legacy_environment_pressure_equilibrium_amount_mol(
    const LegacyGasCell &cell, double environment_pressure_pa,
    double environment_temperature_k) noexcept;

[[nodiscard]] LegacyEnvironmentGasTransferResult legacy_transfer_gas_environment(
    LegacyGasCell &cell,
    const LegacyEnvironmentGasTransferParameters &parameters) noexcept;

void legacy_limit_gas_to_sonic_velocity(LegacyGasCell &cell) noexcept;
void legacy_apply_gas_self_impulse(LegacyGasCell &cell,
                                   const LegacyGasCellGeometry &geometry, double step_s,
                                   double beta) noexcept;
void legacy_apply_gas_velocity_decay(LegacyGasCell &cell, double step_s,
                                     double time_constant_s) noexcept;

// These are the only generic thermal floors admitted by the legacy gas kernel.
// Call sites retain ownership of their model-specified ordering.
void legacy_floor_negative_gas_thermal_energy(LegacyGasCell &cell) noexcept;

} // namespace engine_sim_offline::simulation
