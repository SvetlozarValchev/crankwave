#include "legacy_gas_primitives.hpp"

#include <cmath>

namespace crankwave::simulation {
namespace {

[[nodiscard]] double legacy_clamp(double value, double lower, double upper) noexcept {
    if (value <= lower) {
        return lower;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

} // namespace

LegacyGasCell legacy_initialize_gas_cell(double pressure_pa, double volume_m3,
                                         double temperature_k,
                                         const LegacyGasMixture &mixture) noexcept {
    LegacyGasCell cell;
    cell.amount_mol =
        pressure_pa * volume_m3 / (kLegacyGasConstantJPerMolK * temperature_k);
    cell.volume_m3 = volume_m3;
    cell.thermal_energy_j =
        temperature_k * (0.5 * kLegacyGasDegreesOfFreedom * cell.amount_mol *
                         kLegacyGasConstantJPerMolK);
    cell.mixture = mixture;
    cell.momentum_x_kg_m_s = 0.0;
    cell.momentum_y_kg_m_s = 0.0;
    return cell;
}

void legacy_reset_gas_cell(LegacyGasCell &cell, double pressure_pa,
                           double temperature_k,
                           const LegacyGasMixture &mixture) noexcept {
    cell.amount_mol =
        pressure_pa * cell.volume_m3 / (kLegacyGasConstantJPerMolK * temperature_k);
    cell.thermal_energy_j =
        temperature_k * (0.5 * kLegacyGasDegreesOfFreedom * cell.amount_mol *
                         kLegacyGasConstantJPerMolK);
    cell.mixture = mixture;
    cell.momentum_x_kg_m_s = 0.0;
    cell.momentum_y_kg_m_s = 0.0;
}

void legacy_change_gas_volume(LegacyGasCell &cell, double delta_volume_m3) noexcept {
    const double volume_m3 = cell.volume_m3;
    const double length_m = std::pow(volume_m3 + delta_volume_m3, 1 / 3.0);
    const double surface_area_m2 = length_m * length_m;
    const double delta_length_m = -delta_volume_m3 / surface_area_m2;
    const double work_j =
        delta_length_m * legacy_gas_pressure_pa(cell) * surface_area_m2;

    cell.volume_m3 += delta_volume_m3;
    cell.thermal_energy_j += work_j;
}

void legacy_set_gas_volume(LegacyGasCell &cell, double volume_m3) noexcept {
    legacy_change_gas_volume(cell, volume_m3 - cell.volume_m3);
}

void legacy_add_gas_thermal_energy(LegacyGasCell &cell,
                                   double delta_energy_j) noexcept {
    cell.thermal_energy_j += delta_energy_j;
}

void legacy_set_gas_mixture(LegacyGasCell &cell,
                            const LegacyGasMixture &mixture) noexcept {
    cell.mixture = mixture;
}

void legacy_remove_gas_amount(LegacyGasCell &cell, double amount_mol,
                              double thermal_energy_per_mol_j) noexcept {
    cell.thermal_energy_j -= thermal_energy_per_mol_j * amount_mol;
    cell.amount_mol -= amount_mol;

    if (cell.amount_mol < 0.0) {
        cell.amount_mol = 0.0;
    }
}

void legacy_add_gas_amount(LegacyGasCell &cell, double amount_mol,
                           double thermal_energy_per_mol_j,
                           const LegacyGasMixture &incoming_mixture) noexcept {
    const double next_amount_mol = cell.amount_mol + amount_mol;
    const double current_amount_mol = cell.amount_mol;

    cell.thermal_energy_j += amount_mol * thermal_energy_per_mol_j;
    cell.amount_mol = next_amount_mol;

    if (next_amount_mol != 0.0) {
        cell.mixture.fuel_fraction = (cell.mixture.fuel_fraction * current_amount_mol +
                                      amount_mol * incoming_mixture.fuel_fraction) /
                                     next_amount_mol;
        cell.mixture.inert_fraction =
            (cell.mixture.inert_fraction * current_amount_mol +
             amount_mol * incoming_mixture.inert_fraction) /
            next_amount_mol;
        cell.mixture.oxygen_fraction =
            (cell.mixture.oxygen_fraction * current_amount_mol +
             amount_mol * incoming_mixture.oxygen_fraction) /
            next_amount_mol;
    } else {
        cell.mixture.fuel_fraction = 0.0;
        cell.mixture.inert_fraction = 0.0;
        cell.mixture.oxygen_fraction = 0.0;
    }
}

double legacy_gas_pressure_pa(const LegacyGasCell &cell) noexcept {
    return cell.volume_m3 != 0.0
               ? cell.thermal_energy_j /
                     (0.5 * kLegacyGasDegreesOfFreedom * cell.volume_m3)
               : 0.0;
}

double legacy_gas_temperature_k(const LegacyGasCell &cell) noexcept {
    if (cell.amount_mol == 0.0) {
        return 0.0;
    }
    return cell.thermal_energy_j / (0.5 * kLegacyGasDegreesOfFreedom * cell.amount_mol *
                                    kLegacyGasConstantJPerMolK);
}

double legacy_gas_mass_kg(const LegacyGasCell &cell) noexcept {
    return kLegacyAirMolarMassKgPerMol * cell.amount_mol;
}

double legacy_gas_density_kg_m3(const LegacyGasCell &cell) noexcept {
    return (kLegacyAirMolarMassKgPerMol * cell.amount_mol) / cell.volume_m3;
}

double legacy_gas_velocity_x_m_s(const LegacyGasCell &cell) noexcept {
    if (cell.amount_mol == 0.0) {
        return 0.0;
    }
    return cell.momentum_x_kg_m_s / legacy_gas_mass_kg(cell);
}

double legacy_gas_velocity_y_m_s(const LegacyGasCell &cell) noexcept {
    if (cell.amount_mol == 0.0) {
        return 0.0;
    }
    return cell.momentum_y_kg_m_s / legacy_gas_mass_kg(cell);
}

double legacy_gas_bulk_kinetic_energy_j(const LegacyGasCell &cell) noexcept {
    const double mass_kg = legacy_gas_mass_kg(cell);
    if (mass_kg == 0.0) {
        return 0.0;
    }

    const double velocity_x_m_s = cell.momentum_x_kg_m_s / mass_kg;
    const double velocity_y_m_s = cell.momentum_y_kg_m_s / mass_kg;
    const double velocity_squared_m2_s2 =
        velocity_x_m_s * velocity_x_m_s + velocity_y_m_s * velocity_y_m_s;
    return 0.5 * mass_kg * velocity_squared_m2_s2;
}

double legacy_gas_total_energy_j(const LegacyGasCell &cell) noexcept {
    if (cell.amount_mol == 0.0) {
        return 0.0;
    }

    const double inverse_mass = 1.0 / legacy_gas_mass_kg(cell);
    const double velocity_x_m_s = cell.momentum_x_kg_m_s * inverse_mass;
    const double velocity_y_m_s = cell.momentum_y_kg_m_s * inverse_mass;
    const double velocity_squared_m2_s2 =
        velocity_x_m_s * velocity_x_m_s + velocity_y_m_s * velocity_y_m_s;
    return cell.thermal_energy_j +
           0.5 * legacy_gas_mass_kg(cell) * velocity_squared_m2_s2;
}

double legacy_gas_sonic_velocity_m_s(const LegacyGasCell &cell) noexcept {
    if (cell.amount_mol == 0.0 || cell.thermal_energy_j == 0.0) {
        return 0.0;
    }

    const double static_pressure_pa = legacy_gas_pressure_pa(cell);
    const double density_kg_m3 = legacy_gas_density_kg_m3(cell);
    const double sonic_velocity_m_s = std::sqrt(
        static_pressure_pa * legacy_gas_heat_capacity_ratio() / density_kg_m3);
    return sonic_velocity_m_s;
}

double legacy_gas_directional_dynamic_pressure_pa(const LegacyGasCell &cell,
                                                  double direction_x,
                                                  double direction_y) noexcept {
    if (cell.amount_mol == 0.0 || cell.thermal_energy_j == 0.0) {
        return 0.0;
    }

    const double inverse_mass = 1.0 / legacy_gas_mass_kg(cell);
    const double directional_velocity_m_s =
        inverse_mass *
        (direction_x * cell.momentum_x_kg_m_s + direction_y * cell.momentum_y_kg_m_s);

    if (directional_velocity_m_s <= 0.0) {
        return 0.0;
    }

    const double heat_capacity_ratio = legacy_gas_heat_capacity_ratio();
    const double static_pressure_pa = legacy_gas_pressure_pa(cell);
    const double density_kg_m3 = legacy_gas_density_kg_m3(cell);
    const double sonic_velocity_squared_m2_s2 =
        static_pressure_pa * heat_capacity_ratio / density_kg_m3;
    const double mach_squared = directional_velocity_m_s * directional_velocity_m_s /
                                sonic_velocity_squared_m2_s2;
    const double z = 1.0 + ((heat_capacity_ratio - 1.0) / 2.0) * mach_squared;
    const double z2 = z * z;
    const double z3 = z2 * z;
    const double z7 = z3 * z3 * z;
    return static_pressure_pa * (std::sqrt(z7) - 1.0);
}

double legacy_gas_fuel_amount_mol(const LegacyGasCell &cell) noexcept {
    return cell.mixture.fuel_fraction * cell.amount_mol;
}

double legacy_gas_inert_amount_mol(const LegacyGasCell &cell) noexcept {
    return cell.mixture.inert_fraction * cell.amount_mol;
}

double legacy_gas_oxygen_amount_mol(const LegacyGasCell &cell) noexcept {
    return cell.mixture.oxygen_fraction * cell.amount_mol;
}

double legacy_gas_choked_pressure_ratio() noexcept {
    const double heat_capacity_ratio = legacy_gas_heat_capacity_ratio();
    return std::pow(2.0 / (heat_capacity_ratio + 1.0),
                    heat_capacity_ratio / (heat_capacity_ratio - 1.0));
}

double legacy_gas_choked_flow_factor() noexcept {
    const double heat_capacity_ratio = legacy_gas_heat_capacity_ratio();
    return std::sqrt(heat_capacity_ratio) *
           std::pow(2.0 / (heat_capacity_ratio + 1.0),
                    (heat_capacity_ratio + 1.0) / (2.0 * (heat_capacity_ratio - 1.0)));
}

double legacy_restriction_molar_rate(double restriction_coefficient,
                                     double endpoint_0_pressure_pa,
                                     double endpoint_1_pressure_pa,
                                     double endpoint_0_temperature_k,
                                     double endpoint_1_temperature_k) noexcept {
    if (restriction_coefficient == 0.0) {
        return 0.0;
    }

    double direction;
    double upstream_temperature_k;
    double upstream_pressure_pa;
    double downstream_pressure_pa;
    if (endpoint_0_pressure_pa > endpoint_1_pressure_pa) {
        direction = 1.0;
        upstream_temperature_k = endpoint_0_temperature_k;
        upstream_pressure_pa = endpoint_0_pressure_pa;
        downstream_pressure_pa = endpoint_1_pressure_pa;
    } else {
        direction = -1.0;
        upstream_temperature_k = endpoint_1_temperature_k;
        upstream_pressure_pa = endpoint_1_pressure_pa;
        downstream_pressure_pa = endpoint_0_pressure_pa;
    }

    const double pressure_ratio = downstream_pressure_pa / upstream_pressure_pa;
    double molar_rate = 0.0;
    if (pressure_ratio <= legacy_gas_choked_pressure_ratio()) {
        molar_rate = legacy_gas_choked_flow_factor();
        molar_rate /= std::sqrt(kLegacyGasConstantJPerMolK * upstream_temperature_k);
    } else {
        const double heat_capacity_ratio = legacy_gas_heat_capacity_ratio();
        const double s = std::pow(pressure_ratio, 1.0 / heat_capacity_ratio);

        molar_rate = (2.0 * heat_capacity_ratio) / (heat_capacity_ratio - 1.0);
        molar_rate *= s * (s - pressure_ratio);
        molar_rate = std::sqrt(std::fmax(molar_rate, 0.0) /
                               (kLegacyGasConstantJPerMolK * upstream_temperature_k));
    }

    molar_rate *= direction * upstream_pressure_pa;
    return molar_rate * restriction_coefficient;
}

double legacy_restriction_coefficient(double target_source_flow_mol_s,
                                      double upstream_pressure_pa,
                                      double pressure_drop_pa,
                                      double temperature_k) noexcept {
    const double downstream_pressure_pa = upstream_pressure_pa - pressure_drop_pa;
    const double pressure_ratio = downstream_pressure_pa / upstream_pressure_pa;
    const double heat_capacity_ratio = legacy_gas_heat_capacity_ratio();

    double molar_rate = 0.0;
    if (pressure_ratio <= legacy_gas_choked_pressure_ratio()) {
        molar_rate = std::sqrt(heat_capacity_ratio);
        molar_rate *=
            std::pow(2.0 / (heat_capacity_ratio + 1.0),
                     (heat_capacity_ratio + 1.0) / (2.0 * (heat_capacity_ratio - 1.0)));
    } else {
        molar_rate = (2.0 * heat_capacity_ratio) / (heat_capacity_ratio - 1.0);
        molar_rate *= 1.0 - std::pow(pressure_ratio,
                                     (heat_capacity_ratio - 1.0) / heat_capacity_ratio);
        molar_rate = std::sqrt(molar_rate);
        molar_rate *= std::pow(pressure_ratio, 1.0 / heat_capacity_ratio);
    }

    molar_rate *=
        upstream_pressure_pa / std::sqrt(kLegacyGasConstantJPerMolK * temperature_k);
    return target_source_flow_mol_s / molar_rate;
}

LegacyFiniteGasTransferResult
legacy_transfer_gas(LegacyGasCell &endpoint_0, LegacyGasCell &endpoint_1,
                    const LegacyFiniteGasTransferParameters &parameters) noexcept {
    LegacyFiniteGasTransferResult result;
    result.endpoint_0_directional_pressure_pa =
        legacy_gas_pressure_pa(endpoint_0) +
        legacy_gas_directional_dynamic_pressure_pa(endpoint_0, parameters.direction_x,
                                                   parameters.direction_y);
    result.endpoint_1_directional_pressure_pa =
        legacy_gas_pressure_pa(endpoint_1) +
        legacy_gas_directional_dynamic_pressure_pa(endpoint_1, -parameters.direction_x,
                                                   -parameters.direction_y);

    LegacyGasCell *source = nullptr;
    LegacyGasCell *sink = nullptr;
    double source_pressure_pa = 0.0;
    double sink_pressure_pa = 0.0;
    double direction_x = 0.0;
    double direction_y = 0.0;
    double source_cross_section_area_m2 = 0.0;
    double sink_cross_section_area_m2 = 0.0;
    double direction_sign = 0.0;

    if (result.endpoint_0_directional_pressure_pa >
        result.endpoint_1_directional_pressure_pa) {
        direction_x = parameters.direction_x;
        direction_y = parameters.direction_y;
        source = &endpoint_0;
        sink = &endpoint_1;
        source_pressure_pa = result.endpoint_0_directional_pressure_pa;
        sink_pressure_pa = result.endpoint_1_directional_pressure_pa;
        source_cross_section_area_m2 = parameters.endpoint_0_cross_section_area_m2;
        sink_cross_section_area_m2 = parameters.endpoint_1_cross_section_area_m2;
        direction_sign = 1.0;
        result.source_endpoint = LegacyGasEndpoint::endpoint_0;
    } else {
        direction_x = -parameters.direction_x;
        direction_y = -parameters.direction_y;
        source = &endpoint_1;
        sink = &endpoint_0;
        source_pressure_pa = result.endpoint_1_directional_pressure_pa;
        sink_pressure_pa = result.endpoint_0_directional_pressure_pa;
        source_cross_section_area_m2 = parameters.endpoint_1_cross_section_area_m2;
        sink_cross_section_area_m2 = parameters.endpoint_0_cross_section_area_m2;
        direction_sign = -1.0;
        result.source_endpoint = LegacyGasEndpoint::endpoint_1;
    }

    double amount_mol =
        parameters.step_s * legacy_restriction_molar_rate(
                                parameters.restriction_coefficient, source_pressure_pa,
                                sink_pressure_pa, legacy_gas_temperature_k(*source),
                                legacy_gas_temperature_k(*sink));
    amount_mol = legacy_clamp(amount_mol, 0.0, 0.9 * source->amount_mol);

    const double fraction = amount_mol / source->amount_mol;
    const double fraction_volume_m3 = fraction * source->volume_m3;
    const double fraction_mass_kg = fraction * legacy_gas_mass_kg(*source);

    if (amount_mol != 0.0) {
        const double source_bulk_energy_before_j =
            legacy_gas_bulk_kinetic_energy_j(*source);
        const double sink_bulk_energy_before_j =
            legacy_gas_bulk_kinetic_energy_j(*sink);

        const double source_thermal_energy_per_mol_j =
            source->thermal_energy_j / source->amount_mol;
        legacy_add_gas_amount(*sink, amount_mol, source_thermal_energy_per_mol_j,
                              source->mixture);
        legacy_remove_gas_amount(*source, amount_mol, source_thermal_energy_per_mol_j);

        const double delta_momentum_x = source->momentum_x_kg_m_s * fraction;
        const double delta_momentum_y = source->momentum_y_kg_m_s * fraction;
        source->momentum_x_kg_m_s -= delta_momentum_x;
        source->momentum_y_kg_m_s -= delta_momentum_y;
        sink->momentum_x_kg_m_s += delta_momentum_x;
        sink->momentum_y_kg_m_s += delta_momentum_y;

        const double source_bulk_energy_after_j =
            legacy_gas_bulk_kinetic_energy_j(*source);
        const double sink_bulk_energy_after_j = legacy_gas_bulk_kinetic_energy_j(*sink);
        sink->thermal_energy_j -=
            (source_bulk_energy_after_j + sink_bulk_energy_after_j) -
            (source_bulk_energy_before_j + sink_bulk_energy_before_j);
    }

    const double source_mass_kg = legacy_gas_mass_kg(*source);
    const double inverse_source_mass = 1.0 / source_mass_kg;
    const double sink_mass_kg = legacy_gas_mass_kg(*sink);
    const double inverse_sink_mass = 1.0 / sink_mass_kg;
    const double source_sonic_velocity_m_s = legacy_gas_sonic_velocity_m_s(*source);
    const double sink_sonic_velocity_m_s = legacy_gas_sonic_velocity_m_s(*sink);
    const double source_initial_momentum_x = source->momentum_x_kg_m_s;
    const double source_initial_momentum_y = source->momentum_y_kg_m_s;
    const double sink_initial_momentum_x = sink->momentum_x_kg_m_s;
    const double sink_initial_momentum_y = sink->momentum_y_kg_m_s;

    if (sink_cross_section_area_m2 != 0.0) {
        const double jet_speed_m_s = legacy_clamp(
            (fraction_volume_m3 / sink_cross_section_area_m2) / parameters.step_s, 0.0,
            sink_sonic_velocity_m_s);
        const double jet_velocity_x_m_s = jet_speed_m_s * direction_x;
        const double jet_velocity_y_m_s = jet_speed_m_s * direction_y;
        const double jet_momentum_x = jet_velocity_x_m_s * fraction_mass_kg;
        const double jet_momentum_y = jet_velocity_y_m_s * fraction_mass_kg;
        sink->momentum_x_kg_m_s += jet_momentum_x;
        sink->momentum_y_kg_m_s += jet_momentum_y;
    }

    if (source_cross_section_area_m2 != 0.0 && source_mass_kg != 0.0) {
        const double jet_speed_m_s = legacy_clamp(
            (fraction_volume_m3 / source_cross_section_area_m2) / parameters.step_s,
            0.0, source_sonic_velocity_m_s);
        const double jet_velocity_x_m_s = jet_speed_m_s * direction_x;
        const double jet_velocity_y_m_s = jet_speed_m_s * direction_y;
        const double jet_momentum_x = jet_velocity_x_m_s * fraction_mass_kg;
        const double jet_momentum_y = jet_velocity_y_m_s * fraction_mass_kg;
        source->momentum_x_kg_m_s += jet_momentum_x;
        source->momentum_y_kg_m_s += jet_momentum_y;
    }

    if (source_mass_kg != 0.0) {
        const double source_velocity_0_x =
            source_initial_momentum_x * inverse_source_mass;
        const double source_velocity_0_y =
            source_initial_momentum_y * inverse_source_mass;
        const double source_velocity_1_x =
            source->momentum_x_kg_m_s * inverse_source_mass;
        const double source_velocity_1_y =
            source->momentum_y_kg_m_s * inverse_source_mass;
        source->thermal_energy_j -= 0.5 * source_mass_kg *
                                    (source_velocity_1_x * source_velocity_1_x -
                                     source_velocity_0_x * source_velocity_0_x);
        source->thermal_energy_j -= 0.5 * source_mass_kg *
                                    (source_velocity_1_y * source_velocity_1_y -
                                     source_velocity_0_y * source_velocity_0_y);
    }

    if (sink_mass_kg > 0.0) {
        const double sink_velocity_0_x = sink_initial_momentum_x * inverse_sink_mass;
        const double sink_velocity_0_y = sink_initial_momentum_y * inverse_sink_mass;
        const double sink_velocity_1_x = sink->momentum_x_kg_m_s * inverse_sink_mass;
        const double sink_velocity_1_y = sink->momentum_y_kg_m_s * inverse_sink_mass;
        sink->thermal_energy_j -= 0.5 * sink_mass_kg *
                                  (sink_velocity_1_x * sink_velocity_1_x -
                                   sink_velocity_0_x * sink_velocity_0_x);
        sink->thermal_energy_j -= 0.5 * sink_mass_kg *
                                  (sink_velocity_1_y * sink_velocity_1_y -
                                   sink_velocity_0_y * sink_velocity_0_y);
    }

    legacy_floor_negative_gas_thermal_energy(*sink);
    legacy_floor_negative_gas_thermal_energy(*source);

    result.signed_amount_mol = amount_mol * direction_sign;
    return result;
}

double legacy_environment_pressure_equilibrium_amount_mol(
    const LegacyGasCell &cell, double environment_pressure_pa,
    double environment_temperature_k) noexcept {
    if (legacy_gas_pressure_pa(cell) > environment_pressure_pa) {
        return -(environment_pressure_pa *
                     (0.5 * kLegacyGasDegreesOfFreedom * cell.volume_m3) -
                 cell.thermal_energy_j) /
               (cell.thermal_energy_j / cell.amount_mol);
    }

    const double environment_thermal_energy_per_mol_j =
        0.5 * environment_temperature_k * kLegacyGasConstantJPerMolK *
        kLegacyGasDegreesOfFreedom;
    return -(environment_pressure_pa *
                 (0.5 * kLegacyGasDegreesOfFreedom * cell.volume_m3) -
             cell.thermal_energy_j) /
           environment_thermal_energy_per_mol_j;
}

LegacyEnvironmentGasTransferResult legacy_transfer_gas_environment(
    LegacyGasCell &cell,
    const LegacyEnvironmentGasTransferParameters &parameters) noexcept {
    LegacyEnvironmentGasTransferResult result;
    result.pressure_equilibrium_amount_mol =
        legacy_environment_pressure_equilibrium_amount_mol(
            cell, parameters.environment.pressure_pa,
            parameters.environment.temperature_k);
    result.signed_amount_mol =
        parameters.step_s *
        legacy_restriction_molar_rate(
            parameters.restriction_coefficient, legacy_gas_pressure_pa(cell),
            parameters.environment.pressure_pa, legacy_gas_temperature_k(cell),
            parameters.environment.temperature_k);

    if (std::abs(result.signed_amount_mol) >
        std::abs(result.pressure_equilibrium_amount_mol)) {
        result.signed_amount_mol = result.pressure_equilibrium_amount_mol;
    }

    if (result.signed_amount_mol < 0.0) {
        const double bulk_energy_before_j = legacy_gas_bulk_kinetic_energy_j(cell);
        legacy_add_gas_amount(
            cell, -result.signed_amount_mol,
            legacy_gas_thermal_energy_per_mol(parameters.environment.temperature_k),
            parameters.environment.mixture);
        const double bulk_energy_after_j = legacy_gas_bulk_kinetic_energy_j(cell);
        cell.thermal_energy_j += bulk_energy_after_j - bulk_energy_before_j;
    } else {
        const double starting_amount_mol = cell.amount_mol;
        legacy_remove_gas_amount(cell, result.signed_amount_mol,
                                 cell.thermal_energy_j / cell.amount_mol);
        cell.momentum_x_kg_m_s -=
            (result.signed_amount_mol / starting_amount_mol) * cell.momentum_x_kg_m_s;
        cell.momentum_y_kg_m_s -=
            (result.signed_amount_mol / starting_amount_mol) * cell.momentum_y_kg_m_s;
    }

    return result;
}

void legacy_limit_gas_to_sonic_velocity(LegacyGasCell &cell) noexcept {
    const double velocity_x_m_s = legacy_gas_velocity_x_m_s(cell);
    const double velocity_y_m_s = legacy_gas_velocity_y_m_s(cell);
    const double velocity_squared_m2_s2 =
        velocity_x_m_s * velocity_x_m_s + velocity_y_m_s * velocity_y_m_s;
    const double sonic_velocity_m_s = legacy_gas_sonic_velocity_m_s(cell);
    const double sonic_velocity_squared_m2_s2 = sonic_velocity_m_s * sonic_velocity_m_s;

    if (sonic_velocity_squared_m2_s2 >= velocity_squared_m2_s2 ||
        velocity_squared_m2_s2 == 0.0) {
        return;
    }

    const double scale_squared = sonic_velocity_squared_m2_s2 / velocity_squared_m2_s2;
    const double scale = std::sqrt(scale_squared);
    cell.momentum_x_kg_m_s *= scale;
    cell.momentum_y_kg_m_s *= scale;
    cell.thermal_energy_j += 0.5 * legacy_gas_mass_kg(cell) *
                             (velocity_squared_m2_s2 - sonic_velocity_squared_m2_s2);
    legacy_floor_negative_gas_thermal_energy(cell);
}

void legacy_apply_gas_self_impulse(LegacyGasCell &cell,
                                   const LegacyGasCellGeometry &geometry, double step_s,
                                   double beta) noexcept {
    if (cell.amount_mol == 0.0) {
        return;
    }

    const double depth_m = cell.volume_m3 / (geometry.width_m * geometry.height_m);
    const double pressure_0_pa = legacy_gas_directional_dynamic_pressure_pa(
        cell, geometry.direction_x, geometry.direction_y);
    const double pressure_1_pa = legacy_gas_directional_dynamic_pressure_pa(
        cell, -geometry.direction_x, -geometry.direction_y);
    const double pressure_2_pa = legacy_gas_directional_dynamic_pressure_pa(
        cell, geometry.direction_y, geometry.direction_x);
    const double pressure_3_pa = legacy_gas_directional_dynamic_pressure_pa(
        cell, -geometry.direction_y, -geometry.direction_x);

    const double force_0_n = pressure_0_pa * (geometry.height_m * depth_m);
    const double force_1_n = pressure_1_pa * (geometry.height_m * depth_m);
    const double force_2_n = pressure_2_pa * (geometry.width_m * depth_m);
    const double force_3_n = pressure_3_pa * (geometry.width_m * depth_m);

    double delta_momentum_x = 0.0;
    double delta_momentum_y = 0.0;
    delta_momentum_x += force_0_n * geometry.direction_x;
    delta_momentum_y += force_0_n * geometry.direction_y;
    delta_momentum_x -= force_1_n * geometry.direction_x;
    delta_momentum_y -= force_1_n * geometry.direction_y;
    delta_momentum_x += force_2_n * geometry.direction_y;
    delta_momentum_y += force_2_n * geometry.direction_x;
    delta_momentum_x -= force_3_n * geometry.direction_y;
    delta_momentum_y -= force_3_n * geometry.direction_x;

    const double mass_kg = legacy_gas_mass_kg(cell);
    const double inverse_mass = 1.0 / mass_kg;
    const double velocity_0_x = cell.momentum_x_kg_m_s * inverse_mass;
    const double velocity_0_y = cell.momentum_y_kg_m_s * inverse_mass;

    cell.momentum_x_kg_m_s -= delta_momentum_x * step_s * beta;
    cell.momentum_y_kg_m_s -= delta_momentum_y * step_s * beta;

    const double velocity_1_x = cell.momentum_x_kg_m_s * inverse_mass;
    const double velocity_1_y = cell.momentum_y_kg_m_s * inverse_mass;
    cell.thermal_energy_j -=
        0.5 * mass_kg * (velocity_1_x * velocity_1_x - velocity_0_x * velocity_0_x);
    cell.thermal_energy_j -=
        0.5 * mass_kg * (velocity_1_y * velocity_1_y - velocity_0_y * velocity_0_y);
    legacy_floor_negative_gas_thermal_energy(cell);
}

void legacy_apply_gas_velocity_decay(LegacyGasCell &cell, double step_s,
                                     double time_constant_s) noexcept {
    if (cell.amount_mol == 0.0) {
        return;
    }

    const double inverse_mass = 1.0 / legacy_gas_mass_kg(cell);
    const double velocity_0_x = cell.momentum_x_kg_m_s * inverse_mass;
    const double velocity_0_y = cell.momentum_y_kg_m_s * inverse_mass;
    const double velocity_squared_0 =
        velocity_0_x * velocity_0_x + velocity_0_y * velocity_0_y;
    const double decay_fraction = step_s / (step_s + time_constant_s);
    cell.momentum_x_kg_m_s = cell.momentum_x_kg_m_s * (1.0 - decay_fraction);
    cell.momentum_y_kg_m_s = cell.momentum_y_kg_m_s * (1.0 - decay_fraction);
    const double velocity_1_x = cell.momentum_x_kg_m_s * inverse_mass;
    const double velocity_1_y = cell.momentum_y_kg_m_s * inverse_mass;
    const double velocity_squared_1 =
        velocity_1_x * velocity_1_x + velocity_1_y * velocity_1_y;
    const double delta_thermal_energy_j =
        0.5 * legacy_gas_mass_kg(cell) * (velocity_squared_0 - velocity_squared_1);
    cell.thermal_energy_j += delta_thermal_energy_j;
}

void legacy_floor_negative_gas_thermal_energy(LegacyGasCell &cell) noexcept {
    if (cell.thermal_energy_j < 0.0) {
        cell.thermal_energy_j = 0.0;
    }
}

} // namespace crankwave::simulation
