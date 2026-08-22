#pragma once

#include "dsp/pcg32.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace crankwave::simulation {

inline constexpr std::size_t kLegacyCombustionHistorySampleCount = 256U;
inline constexpr std::uint64_t kMaximumLegacyPcg32Stream = dsp::kMaximumPcg32Stream;
inline constexpr double kLegacyIntakeFlameExtinctionAmountMol = 1.0e-9;

// PCG-XSH-RR with the source generator's two-draw seed sequence and 53-bit
// binary64 conversion. seed() rejects selectors that cannot be shifted into the
// odd increment without losing a bit, and leaves the prior stream untouched.
class LegacyPcg32 final {
  public:
    LegacyPcg32() noexcept;

    [[nodiscard]] bool seed(std::uint64_t initial_state, std::uint64_t stream) noexcept;
    [[nodiscard]] std::uint32_t next_u32() noexcept;
    [[nodiscard]] double uniform_binary64() noexcept;

    [[nodiscard]] std::uint64_t state() const noexcept;
    [[nodiscard]] std::uint64_t increment() const noexcept;

  private:
    dsp::Pcg32 generator_{0U, 1U};
};

// Invocation-scoped, admitted fuel data. The ratio table is sorted by turbulence
// and remains caller-owned for the duration of an ignition attempt.
struct LegacyGasolineFuelParameters {
    double molecular_mass_kg_per_mol = 0.0;
    double energy_density_j_per_kg = 0.0;
    double molecular_afr = 0.0;
    double maximum_burning_efficiency_01 = 0.0;
    double burning_efficiency_randomness_01 = 0.0;
    double low_efficiency_attenuation_01 = 0.0;
    double maximum_turbulence_effect = 0.0;
    double maximum_dilution_effect = 0.0;
    double lbv_multiplier = 0.0;
    double turbulence_to_flame_speed_ratio_triangle_radius = 0.0;
    std::span<const LegacyTrianglePoint> turbulence_to_flame_speed_ratio;
};

struct LegacyFlameState {
    bool active = false;
    double lit_amount_mol = 0.0;
    double diagnostic_total_amount_mol = 0.0;
    double percentage_lit_01 = 0.0;
    double efficiency_01 = 1.0;
    double flame_speed_m_s = 0.0;
    double last_volume_m3 = 0.0;
    double radial_travel_m = 0.0;
    double axial_travel_m = 0.0;
    LegacyGasMixture global_mixture;

    friend bool operator==(const LegacyFlameState &,
                           const LegacyFlameState &) = default;
};

enum class LegacyIgnitionDisposition : std::uint8_t {
    accepted,
    rejected_active_flame,
    rejected_no_fuel,
    rejected_mixture_low,
    rejected_mixture_high,
};

struct LegacyIgnitionResult {
    LegacyIgnitionDisposition disposition =
        LegacyIgnitionDisposition::rejected_active_flame;
    double equivalence_source = 0.0;
    double turbulence_m_s = 0.0;
    double firing_pressure_history_max_pa = 0.0;
    double efficiency_01 = 0.0;
    double flame_speed_m_s = 0.0;

    [[nodiscard]] bool accepted() const noexcept {
        return disposition == LegacyIgnitionDisposition::accepted;
    }

    friend bool operator==(const LegacyIgnitionResult &,
                           const LegacyIgnitionResult &) = default;
};

struct LegacyCombustionReactionResult {
    double requested_reaction_amount_mol = 0.0;
    double requested_fuel_mol = 0.0;
    double requested_oxygen_mol = 0.0;
    double burned_fuel_mol = 0.0;
    double burned_oxygen_mol = 0.0;
    double reactants_mol = 0.0;
    double products_mol = 0.0;
    double amount_delta_mol = 0.0;
    double burned_fuel_mass_kg = 0.0;
    double energy_release_j = 0.0;

    friend bool operator==(const LegacyCombustionReactionResult &,
                           const LegacyCombustionReactionResult &) = default;
};

enum class LegacyFlameAdvanceDisposition : std::uint8_t {
    inactive,
    advanced,
    extinguished_no_geometric_progress,
};

struct LegacyFlameAdvanceResult {
    LegacyFlameAdvanceDisposition disposition = LegacyFlameAdvanceDisposition::inactive;
    double burned_volume_m3 = 0.0;
    double previous_burned_volume_m3 = 0.0;
    double flame_volume_delta_m3 = 0.0;
    double swept_amount_mol = 0.0;
    double flame_volume_fraction_delta = 0.0;
    LegacyCombustionReactionResult reaction;

    friend bool operator==(const LegacyFlameAdvanceResult &,
                           const LegacyFlameAdvanceResult &) = default;
};

// These primitives deliberately assume admitted finite physical state. The session
// layer owns the M3 transactional nonphysical-state checks; adding local clamps or
// recovery branches here would change source arithmetic.
[[nodiscard]] double
legacy_mean_piston_speed(std::span<const double, kLegacyCombustionHistorySampleCount>
                             piston_speed_history_m_s) noexcept;

[[nodiscard]] double legacy_piston_turbulence(double mean_piston_speed_m_s) noexcept;

[[nodiscard]] double
legacy_source_laminar_flame_speed(double molecular_afr, double temperature_k,
                                  double pressure_pa_abs,
                                  const LegacyGasolineFuelParameters &fuel) noexcept;

[[nodiscard]] double legacy_source_flame_speed(
    double turbulence_m_s, double molecular_afr, double temperature_k,
    double pressure_pa_abs, double firing_pressure_history_max_pa,
    double motoring_pressure_pa, const LegacyGasolineFuelParameters &fuel) noexcept;

// A rejected attempt mutates neither flame nor generator. An accepted attempt
// snapshots the old thermodynamic cell and the current post-mechanism geometric
// volume, then consumes exactly one binary64 uniform (two next_u32 calls).
[[nodiscard]] LegacyIgnitionResult legacy_try_ignite(
    LegacyFlameState &flame, LegacyPcg32 &random, const LegacyGasCell &cell,
    double current_geometric_volume_m3,
    std::span<const double, kLegacyCombustionHistorySampleCount>
        piston_speed_history_m_s,
    std::span<const double, kLegacyCombustionHistorySampleCount> pressure_history_pa,
    const LegacyGasolineFuelParameters &fuel) noexcept;

// Applies the source species-limited gasoline reaction directly to cell, including
// heat release. It does not rescale momentum, normalize fractions, or alter volume.
[[nodiscard]] LegacyCombustionReactionResult
legacy_apply_gasoline_reaction(LegacyGasCell &cell,
                               double requested_reaction_amount_mol,
                               const LegacyGasMixture &global_mixture,
                               const LegacyGasolineFuelParameters &fuel) noexcept;

// Implements the post-flow geometric flame step. Progress fields in flame accumulate
// the unattenuated swept amount and geometric volume fraction, matching the source.
[[nodiscard]] LegacyFlameAdvanceResult
legacy_advance_gasoline_flame(LegacyFlameState &flame, LegacyGasCell &cell,
                              double gas_step_s, double cylinder_bore_m,
                              double piston_area_m2,
                              const LegacyGasolineFuelParameters &fuel) noexcept;

// The intake-flow cancellation check precedes geometric advance in the chamber
// schedule. The signed transfer amount is the result of that single gas substep.
[[nodiscard]] bool
legacy_extinguish_flame_for_intake_transfer(LegacyFlameState &flame,
                                            double signed_intake_transfer_mol) noexcept;

} // namespace crankwave::simulation
