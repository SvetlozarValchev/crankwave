#include "legacy_combustion_primitives.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crankwave::simulation {
namespace {

[[nodiscard]] constexpr auto make_mean_speed_to_turbulence_table() noexcept {
    std::array<LegacyTrianglePoint, 30U> points{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double x = static_cast<double>(index);
        points[index] = {x, 0.5 * x};
    }
    return points;
}

inline constexpr auto kMeanSpeedToTurbulence = make_mean_speed_to_turbulence_table();

[[nodiscard]] constexpr double source_motoring_pressure_pa() noexcept {
    const double centimetre_source = 1.0 / 100.0;
    const double inch_source = centimetre_source * 2.54;
    const double pounds_force_source = 1.0 * 4.44822;
    const double psi_source = pounds_force_source / (inch_source * inch_source);
    return 160.0 * psi_source;
}

[[nodiscard]] double legacy_clamp(double value, double lower, double upper) noexcept {
    if (value <= lower) {
        return lower;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

[[nodiscard]] double
legacy_firing_pressure(std::span<const double, kLegacyCombustionHistorySampleCount>
                           pressure_history_pa) noexcept {
    double firing_pressure_pa = 0.0;
    for (const double pressure_pa : pressure_history_pa) {
        if (pressure_pa > firing_pressure_pa) {
            firing_pressure_pa = pressure_pa;
        }
    }
    return firing_pressure_pa;
}

} // namespace

LegacyPcg32::LegacyPcg32() noexcept = default;

bool LegacyPcg32::seed(std::uint64_t initial_state, std::uint64_t stream) noexcept {
    if (stream > kMaximumLegacyPcg32Stream) {
        return false;
    }

    generator_ = dsp::Pcg32{initial_state, stream};
    return true;
}

std::uint32_t LegacyPcg32::next_u32() noexcept {
    return generator_.next_u32();
}

double LegacyPcg32::uniform_binary64() noexcept {
    return generator_.uniform_double();
}

std::uint64_t LegacyPcg32::state() const noexcept {
    return generator_.state();
}

std::uint64_t LegacyPcg32::increment() const noexcept {
    return generator_.increment();
}

double
legacy_mean_piston_speed(std::span<const double, kLegacyCombustionHistorySampleCount>
                             piston_speed_history_m_s) noexcept {
    double mean_piston_speed_m_s = 0.0;
    for (const double speed_m_s : piston_speed_history_m_s) {
        mean_piston_speed_m_s += speed_m_s;
    }
    mean_piston_speed_m_s /= static_cast<double>(kLegacyCombustionHistorySampleCount);
    return mean_piston_speed_m_s;
}

double legacy_piston_turbulence(double mean_piston_speed_m_s) noexcept {
    return legacy_triangle_sample(kMeanSpeedToTurbulence, mean_piston_speed_m_s, 1.0);
}

double
legacy_source_laminar_flame_speed(double molecular_afr, double temperature_k,
                                  double pressure_pa_abs,
                                  const LegacyGasolineFuelParameters &fuel) noexcept {
    const double equivalence_source = molecular_afr / fuel.molecular_afr;
    const double alpha = 2.4 - 0.271 * std::pow(equivalence_source, 3.51);
    const double beta = -0.357 + 0.14 * std::pow(equivalence_source, 2.77);
    const double speed_at_reference_m_s =
        0.305 + (-0.549) * (equivalence_source - 1.21) * (equivalence_source - 1.21);

    double laminar_speed_m_s = fuel.lbv_multiplier * speed_at_reference_m_s;
    laminar_speed_m_s *= std::pow(temperature_k / 298.0, alpha);
    laminar_speed_m_s *= std::pow(pressure_pa_abs / 101325.0, beta);
    return laminar_speed_m_s;
}

double legacy_source_flame_speed(double turbulence_m_s, double molecular_afr,
                                 double temperature_k, double pressure_pa_abs,
                                 double firing_pressure_history_max_pa,
                                 double motoring_pressure_pa,
                                 const LegacyGasolineFuelParameters &fuel) noexcept {
    static_cast<void>(firing_pressure_history_max_pa);
    static_cast<void>(motoring_pressure_pa);
    const double laminar_speed_m_s = legacy_source_laminar_flame_speed(
        molecular_afr, temperature_k, pressure_pa_abs, fuel);
    const double pressure_adjustment = 1.0;
    const double ratio = legacy_triangle_sample(
        fuel.turbulence_to_flame_speed_ratio,
        (turbulence_m_s / laminar_speed_m_s) * pressure_adjustment,
        fuel.turbulence_to_flame_speed_ratio_triangle_radius);
    return ratio * laminar_speed_m_s;
}

LegacyIgnitionResult legacy_try_ignite(
    LegacyFlameState &flame, LegacyPcg32 &random, const LegacyGasCell &cell,
    double current_geometric_volume_m3,
    std::span<const double, kLegacyCombustionHistorySampleCount>
        piston_speed_history_m_s,
    std::span<const double, kLegacyCombustionHistorySampleCount> pressure_history_pa,
    const LegacyGasolineFuelParameters &fuel) noexcept {
    LegacyIgnitionResult result;
    if (flame.active) {
        result.disposition = LegacyIgnitionDisposition::rejected_active_flame;
        return result;
    }
    if (cell.mixture.fuel_fraction == 0.0) {
        result.disposition = LegacyIgnitionDisposition::rejected_no_fuel;
        return result;
    }

    const double molecular_afr =
        cell.mixture.oxygen_fraction / cell.mixture.fuel_fraction;
    result.equivalence_source = molecular_afr / fuel.molecular_afr;
    if (result.equivalence_source < 0.5) {
        result.disposition = LegacyIgnitionDisposition::rejected_mixture_low;
        return result;
    }
    if (result.equivalence_source > 1.9) {
        result.disposition = LegacyIgnitionDisposition::rejected_mixture_high;
        return result;
    }

    const double ideal_inert = cell.mixture.oxygen_fraction / 0.7;
    const double dilution = cell.mixture.inert_fraction / ideal_inert - 1.0;

    flame.last_volume_m3 = current_geometric_volume_m3;
    flame.radial_travel_m = 0.0;
    flame.axial_travel_m = 0.0;
    flame.lit_amount_mol = 0.0;
    flame.diagnostic_total_amount_mol = cell.amount_mol;
    flame.percentage_lit_01 = 0.0;
    flame.global_mixture = cell.mixture;
    flame.active = true;

    result.turbulence_m_s =
        legacy_piston_turbulence(legacy_mean_piston_speed(piston_speed_history_m_s));
    const double mixing_factor =
        1.0 - (legacy_clamp(result.turbulence_m_s / fuel.maximum_turbulence_effect, 0.0,
                            1.0) *
               legacy_clamp(1.0 - dilution / fuel.maximum_dilution_effect, 0.0, 1.0));
    const double random_attenuation =
        fuel.low_efficiency_attenuation_01 *
        ((1.0 - fuel.burning_efficiency_randomness_01) +
         fuel.burning_efficiency_randomness_01 * random.uniform_binary64());
    const double efficiency_attenuation =
        mixing_factor * random_attenuation + (1.0 - mixing_factor);
    flame.efficiency_01 = efficiency_attenuation * fuel.maximum_burning_efficiency_01;

    result.firing_pressure_history_max_pa = legacy_firing_pressure(pressure_history_pa);
    flame.flame_speed_m_s = legacy_source_flame_speed(
        result.turbulence_m_s, molecular_afr, legacy_gas_temperature_k(cell),
        legacy_gas_pressure_pa(cell), result.firing_pressure_history_max_pa,
        source_motoring_pressure_pa(), fuel);

    result.disposition = LegacyIgnitionDisposition::accepted;
    result.efficiency_01 = flame.efficiency_01;
    result.flame_speed_m_s = flame.flame_speed_m_s;
    return result;
}

LegacyCombustionReactionResult
legacy_apply_gasoline_reaction(LegacyGasCell &cell,
                               double requested_reaction_amount_mol,
                               const LegacyGasMixture &global_mixture,
                               const LegacyGasolineFuelParameters &fuel) noexcept {
    LegacyCombustionReactionResult result;
    result.requested_reaction_amount_mol = requested_reaction_amount_mol;
    result.requested_fuel_mol =
        global_mixture.fuel_fraction * requested_reaction_amount_mol;
    result.requested_oxygen_mol =
        global_mixture.oxygen_fraction * requested_reaction_amount_mol;

    const double current_fuel_mol = legacy_gas_fuel_amount_mol(cell);
    const double current_oxygen_mol = legacy_gas_oxygen_amount_mol(cell);
    const double current_inert_mol = legacy_gas_inert_amount_mol(cell);
    const double current_amount_mol = cell.amount_mol;

    result.burned_fuel_mol =
        std::fmin(std::fmin(current_fuel_mol, result.requested_fuel_mol),
                  (2.0 / 25.0) * result.requested_oxygen_mol);
    result.burned_oxygen_mol =
        std::fmin(std::fmin(current_oxygen_mol, result.requested_oxygen_mol),
                  (25.0 / 2.0) * result.requested_fuel_mol);

    result.reactants_mol = result.burned_fuel_mol + result.burned_oxygen_mol;
    result.products_mol = ((16.0 + 18.0) / (25.0 + 2.0)) * result.reactants_mol;
    result.amount_delta_mol = result.products_mol - result.reactants_mol;

    cell.amount_mol += result.amount_delta_mol;

    const double new_fuel_mol = current_fuel_mol - result.burned_fuel_mol;
    const double new_oxygen_mol = current_oxygen_mol - result.burned_oxygen_mol;
    const double new_inert_mol = current_inert_mol + result.products_mol;
    const double new_amount_mol = current_amount_mol + result.amount_delta_mol;

    if (new_amount_mol != 0.0) {
        cell.mixture.fuel_fraction = new_fuel_mol / new_amount_mol;
        cell.mixture.inert_fraction = new_inert_mol / new_amount_mol;
        cell.mixture.oxygen_fraction = new_oxygen_mol / new_amount_mol;
    } else {
        cell.mixture.fuel_fraction = 0.0;
        cell.mixture.inert_fraction = 0.0;
        cell.mixture.oxygen_fraction = 0.0;
    }

    result.burned_fuel_mass_kg =
        result.burned_fuel_mol * fuel.molecular_mass_kg_per_mol;
    result.energy_release_j = result.burned_fuel_mass_kg * fuel.energy_density_j_per_kg;
    legacy_add_gas_thermal_energy(cell, result.energy_release_j);
    return result;
}

LegacyFlameAdvanceResult
legacy_advance_gasoline_flame(LegacyFlameState &flame, LegacyGasCell &cell,
                              double gas_step_s, double cylinder_bore_m,
                              double piston_area_m2,
                              const LegacyGasolineFuelParameters &fuel) noexcept {
    LegacyFlameAdvanceResult result;
    if (!flame.active) {
        return result;
    }

    const double volume_m3 = cell.volume_m3;
    const double maximum_radial_travel_m = cylinder_bore_m / 2.0;
    const double maximum_axial_travel_m = volume_m3 / piston_area_m2;
    const double expansion = volume_m3 / flame.last_volume_m3;
    const double previous_radial_travel_m = flame.radial_travel_m;
    const double previous_axial_travel_m = flame.axial_travel_m * expansion;

    flame.radial_travel_m =
        std::fmin(previous_radial_travel_m + gas_step_s * flame.flame_speed_m_s,
                  maximum_radial_travel_m);
    flame.axial_travel_m =
        std::fmin(previous_axial_travel_m + gas_step_s * flame.flame_speed_m_s,
                  maximum_axial_travel_m);

    if (previous_radial_travel_m < flame.radial_travel_m ||
        previous_axial_travel_m < flame.axial_travel_m) {
        result.disposition = LegacyFlameAdvanceDisposition::advanced;
        result.burned_volume_m3 = flame.radial_travel_m * flame.radial_travel_m *
                                  kLegacyPi * flame.axial_travel_m;
        result.previous_burned_volume_m3 = previous_radial_travel_m *
                                           previous_radial_travel_m * kLegacyPi *
                                           previous_axial_travel_m;
        result.flame_volume_delta_m3 =
            result.burned_volume_m3 - result.previous_burned_volume_m3;
        result.swept_amount_mol =
            (result.flame_volume_delta_m3 / volume_m3) * cell.amount_mol;

        result.reaction = legacy_apply_gasoline_reaction(
            cell, result.swept_amount_mol * flame.efficiency_01, flame.global_mixture,
            fuel);

        flame.lit_amount_mol += result.swept_amount_mol;
        result.flame_volume_fraction_delta = result.flame_volume_delta_m3 / volume_m3;
        flame.percentage_lit_01 += result.flame_volume_fraction_delta;
    } else {
        flame.active = false;
        result.disposition =
            LegacyFlameAdvanceDisposition::extinguished_no_geometric_progress;
    }

    flame.last_volume_m3 = volume_m3;
    return result;
}

bool legacy_extinguish_flame_for_intake_transfer(
    LegacyFlameState &flame, double signed_intake_transfer_mol) noexcept {
    if (std::abs(signed_intake_transfer_mol) > kLegacyIntakeFlameExtinctionAmountMol &&
        flame.active) {
        flame.active = false;
        return true;
    }
    return false;
}

} // namespace crankwave::simulation
