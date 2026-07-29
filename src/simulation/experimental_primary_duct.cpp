#include "simulation/experimental_primary_duct.hpp"

#include "simulation/legacy_gas_primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kGamma = 1.4;
constexpr double kGasConstantSpecific =
    kLegacyGasConstantJPerMolK / kLegacyAirMolarMassKgPerMol;
constexpr double kMinimumPressurePa = 1000.0;
constexpr double kMinimumDensityKgM3 = 0.01;
constexpr double kDarcyFrictionFactor = 0.02;
constexpr double kWallHeatTransferWPerM2K = 50.0;
constexpr double kListeningSpikePressureGain = 0.25;

struct Primitive {
    double density_kg_m3 = 0.0;
    double velocity_m_s = 0.0;
    double pressure_pa = 0.0;
    double temperature_k = 0.0;
    double sound_speed_m_s = 0.0;
};

struct Flux {
    double mass_kg_m2_s = 0.0;
    double momentum_pa = 0.0;
    double energy_w_m2 = 0.0;
};

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool primitive(const ExperimentalPrimaryDuct::Conserved &state,
                             Primitive &output) noexcept {
    if (!finite_positive(state.density_kg_m3) ||
        !std::isfinite(state.momentum_kg_m2_s) ||
        !finite_positive(state.total_energy_j_m3)) {
        return false;
    }
    output.density_kg_m3 = state.density_kg_m3;
    output.velocity_m_s = state.momentum_kg_m2_s / state.density_kg_m3;
    const double kinetic_energy =
        0.5 * state.density_kg_m3 * output.velocity_m_s * output.velocity_m_s;
    output.pressure_pa = (kGamma - 1.0) * (state.total_energy_j_m3 - kinetic_energy);
    output.temperature_k =
        output.pressure_pa / (state.density_kg_m3 * kGasConstantSpecific);
    output.sound_speed_m_s =
        std::sqrt(kGamma * output.pressure_pa / state.density_kg_m3);
    return finite_positive(output.pressure_pa) &&
           finite_positive(output.temperature_k) &&
           finite_positive(output.sound_speed_m_s);
}

[[nodiscard]] ExperimentalPrimaryDuct::Conserved
conserved(double density_kg_m3, double velocity_m_s, double pressure_pa) noexcept {
    return {
        density_kg_m3,
        density_kg_m3 * velocity_m_s,
        pressure_pa / (kGamma - 1.0) +
            0.5 * density_kg_m3 * velocity_m_s * velocity_m_s,
    };
}

[[nodiscard]] Flux physical_flux(const ExperimentalPrimaryDuct::Conserved &state,
                                 const Primitive &value) noexcept {
    return {
        state.momentum_kg_m2_s,
        state.momentum_kg_m2_s * value.velocity_m_s + value.pressure_pa,
        value.velocity_m_s * (state.total_energy_j_m3 + value.pressure_pa),
    };
}

[[nodiscard]] Flux hll_flux(const ExperimentalPrimaryDuct::Conserved &left,
                            const ExperimentalPrimaryDuct::Conserved &right,
                            const Primitive &left_value,
                            const Primitive &right_value) noexcept {
    const auto left_flux = physical_flux(left, left_value);
    const auto right_flux = physical_flux(right, right_value);
    const double speed_left =
        std::min(left_value.velocity_m_s - left_value.sound_speed_m_s,
                 right_value.velocity_m_s - right_value.sound_speed_m_s);
    const double speed_right =
        std::max(left_value.velocity_m_s + left_value.sound_speed_m_s,
                 right_value.velocity_m_s + right_value.sound_speed_m_s);
    if (speed_left >= 0.0) {
        return left_flux;
    }
    if (speed_right <= 0.0) {
        return right_flux;
    }
    const double inverse_span = 1.0 / (speed_right - speed_left);
    return {
        (speed_right * left_flux.mass_kg_m2_s - speed_left * right_flux.mass_kg_m2_s +
         speed_left * speed_right * (right.density_kg_m3 - left.density_kg_m3)) *
            inverse_span,
        (speed_right * left_flux.momentum_pa - speed_left * right_flux.momentum_pa +
         speed_left * speed_right * (right.momentum_kg_m2_s - left.momentum_kg_m2_s)) *
            inverse_span,
        (speed_right * left_flux.energy_w_m2 - speed_left * right_flux.energy_w_m2 +
         speed_left * speed_right *
             (right.total_energy_j_m3 - left.total_energy_j_m3)) *
            inverse_span,
    };
}

} // namespace

double ExperimentalPrimaryDuct::ReflectionFilter::process(double input) noexcept {
    const double output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;
    return output;
}

void ExperimentalPrimaryDuct::initialize(double ambient_pressure_pa,
                                         double initial_temperature_k, double length_m,
                                         double cross_section_area_m2,
                                         double gas_step_s) noexcept {
    ambient_pressure_pa_ = ambient_pressure_pa;
    ambient_temperature_k_ = std::max(initial_temperature_k, 600.0);
    wall_temperature_k_ = ambient_temperature_k_;
    cross_section_area_m2_ = cross_section_area_m2;
    diameter_m_ = 2.0 * std::sqrt(cross_section_area_m2 / std::numbers::pi);
    cell_length_m_ = length_m / static_cast<double>(kCellCount);
    internal_step_s_ = gas_step_s / static_cast<double>(kInternalSubstepCount);
    ambient_density_kg_m3_ =
        ambient_pressure_pa_ / (kGasConstantSpecific * ambient_temperature_k_);
    ambient_sound_speed_m_s_ =
        std::sqrt(kGamma * kGasConstantSpecific * ambient_temperature_k_);
    characteristic_impedance_pa_s_m_ =
        ambient_density_kg_m3_ * ambient_sound_speed_m_s_;

    const auto initial = conserved(ambient_density_kg_m3_, 0.0, ambient_pressure_pa_);
    cells_.fill(initial);
    next_.fill(initial);

    // Silva et al. unflanged circular-pipe reflection approximation:
    // R(s)=-(1+0.167*tau*s)/(1+1.393*tau*s+0.457*tau^2*s^2).
    const double radius_m = diameter_m_ * 0.5;
    const double tau_s = radius_m / ambient_sound_speed_m_s_;
    const double q = (2.0 / internal_step_s_) * tau_s;
    const double denominator_0 = 1.0 + 1.393 * q + 0.457 * q * q;
    outlet_reflection_.b0 = -(1.0 + 0.167 * q) / denominator_0;
    outlet_reflection_.b1 = -2.0 / denominator_0;
    outlet_reflection_.b2 = -(1.0 - 0.167 * q) / denominator_0;
    outlet_reflection_.a1 = (2.0 - 2.0 * 0.457 * q * q) / denominator_0;
    outlet_reflection_.a2 = (1.0 - 1.393 * q + 0.457 * q * q) / denominator_0;
    outlet_reflection_.x1 = 0.0;
    outlet_reflection_.x2 = 0.0;
    outlet_reflection_.y1 = 0.0;
    outlet_reflection_.y2 = 0.0;

    initialized_ =
        finite_positive(ambient_pressure_pa_) &&
        finite_positive(ambient_temperature_k_) && finite_positive(length_m) &&
        finite_positive(cross_section_area_m2_) && finite_positive(diameter_m_) &&
        finite_positive(cell_length_m_) && finite_positive(internal_step_s_);
}

bool ExperimentalPrimaryDuct::advance(double chamber_pressure_pa_abs,
                                      double chamber_temperature_k,
                                      double exhaust_valve_restriction_k) noexcept {
    if (!initialized_ || !finite_positive(chamber_pressure_pa_abs) ||
        !finite_positive(chamber_temperature_k) ||
        !std::isfinite(exhaust_valve_restriction_k) ||
        exhaust_valve_restriction_k < 0.0) {
        return false;
    }

    for (std::size_t substep = 0; substep < kInternalSubstepCount; ++substep) {
        std::array<Primitive, kCellCount> values{};
        double maximum_signal_speed_m_s = 0.0;
        for (std::size_t cell = 0; cell < kCellCount; ++cell) {
            if (!primitive(cells_[cell], values[cell])) {
                return false;
            }
            maximum_signal_speed_m_s =
                std::max(maximum_signal_speed_m_s, std::abs(values[cell].velocity_m_s) +
                                                       values[cell].sound_speed_m_s);
        }
        if (!std::isfinite(maximum_signal_speed_m_s) ||
            internal_step_s_ * maximum_signal_speed_m_s / cell_length_m_ > 0.48) {
            return false;
        }

        std::array<Flux, kCellCount + 1U> interfaces{};
        const auto &inlet = values.front();
        if (exhaust_valve_restriction_k == 0.0) {
            interfaces.front() = {0.0, inlet.pressure_pa, 0.0};
        } else {
            const double molar_rate = legacy_restriction_molar_rate(
                exhaust_valve_restriction_k, chamber_pressure_pa_abs, inlet.pressure_pa,
                chamber_temperature_k, inlet.temperature_k);
            double mass_flux =
                molar_rate * kLegacyAirMolarMassKgPerMol / cross_section_area_m2_;
            const bool into_duct = mass_flux >= 0.0;
            const double upstream_pressure_pa =
                into_duct ? chamber_pressure_pa_abs : inlet.pressure_pa;
            const double upstream_temperature_k =
                into_duct ? chamber_temperature_k : inlet.temperature_k;
            const double upstream_density =
                upstream_pressure_pa / (kGasConstantSpecific * upstream_temperature_k);
            const double upstream_sound_speed =
                std::sqrt(kGamma * upstream_pressure_pa / upstream_density);
            double boundary_velocity = mass_flux / upstream_density;
            const double velocity_limit = 0.9 * upstream_sound_speed;
            boundary_velocity =
                std::clamp(boundary_velocity, -velocity_limit, velocity_limit);
            mass_flux = boundary_velocity * upstream_density;
            const double stagnation_enthalpy =
                (kGamma / (kGamma - 1.0)) * (upstream_pressure_pa / upstream_density) +
                0.5 * boundary_velocity * boundary_velocity;
            interfaces.front() = {
                mass_flux,
                mass_flux * boundary_velocity + inlet.pressure_pa,
                mass_flux * stagnation_enthalpy,
            };
        }

        for (std::size_t interface = 1; interface < kCellCount; ++interface) {
            interfaces[interface] = hll_flux(cells_[interface - 1U], cells_[interface],
                                             values[interface - 1U], values[interface]);
        }

        const auto &outlet = values.back();
        const double outgoing_pressure =
            0.5 * ((outlet.pressure_pa - ambient_pressure_pa_) +
                   characteristic_impedance_pa_s_m_ * outlet.velocity_m_s);
        const double incoming_pressure = outlet_reflection_.process(outgoing_pressure);
        const double ghost_pressure =
            std::max(kMinimumPressurePa,
                     ambient_pressure_pa_ + outgoing_pressure + incoming_pressure);
        const double ghost_velocity =
            (outgoing_pressure - incoming_pressure) / characteristic_impedance_pa_s_m_;
        const double ghost_density =
            std::max(kMinimumDensityKgM3,
                     ghost_pressure / (kGasConstantSpecific *
                                       std::max(outlet.temperature_k, 200.0)));
        const auto ghost = conserved(ghost_density, ghost_velocity, ghost_pressure);
        Primitive ghost_value;
        if (!primitive(ghost, ghost_value)) {
            return false;
        }
        interfaces.back() = hll_flux(cells_.back(), ghost, outlet, ghost_value);

        const double step_over_length = internal_step_s_ / cell_length_m_;
        for (std::size_t cell = 0; cell < kCellCount; ++cell) {
            next_[cell] = {
                cells_[cell].density_kg_m3 -
                    step_over_length * (interfaces[cell + 1U].mass_kg_m2_s -
                                        interfaces[cell].mass_kg_m2_s),
                cells_[cell].momentum_kg_m2_s -
                    step_over_length * (interfaces[cell + 1U].momentum_pa -
                                        interfaces[cell].momentum_pa),
                cells_[cell].total_energy_j_m3 -
                    step_over_length * (interfaces[cell + 1U].energy_w_m2 -
                                        interfaces[cell].energy_w_m2),
            };

            Primitive updated;
            if (!primitive(next_[cell], updated)) {
                return false;
            }
            const double friction_denominator =
                1.0 + internal_step_s_ * kDarcyFrictionFactor *
                          std::abs(updated.velocity_m_s) / (2.0 * diameter_m_);
            const double damped_velocity = updated.velocity_m_s / friction_denominator;
            next_[cell].momentum_kg_m2_s = next_[cell].density_kg_m3 * damped_velocity;
            next_[cell].total_energy_j_m3 +=
                internal_step_s_ * (4.0 * kWallHeatTransferWPerM2K / diameter_m_) *
                (wall_temperature_k_ - updated.temperature_k);
            if (!primitive(next_[cell], updated)) {
                return false;
            }
        }
        cells_.swap(next_);
    }
    return true;
}

ExperimentalPrimaryDuct::Observation
ExperimentalPrimaryDuct::observation() const noexcept {
    Primitive value;
    if (!initialized_ || !primitive(cells_.front(), value)) {
        return {};
    }
    const double sonic_velocity_squared =
        kGamma * value.pressure_pa / value.density_kg_m3;
    const double mach_squared =
        value.velocity_m_s * value.velocity_m_s / sonic_velocity_squared;
    const double dynamic_pressure =
        value.pressure_pa *
        (std::pow(1.0 + 0.5 * (kGamma - 1.0) * mach_squared, kGamma / (kGamma - 1.0)) -
         1.0);
    return {
        ambient_pressure_pa_ +
            kListeningSpikePressureGain * (value.pressure_pa - ambient_pressure_pa_),
        value.velocity_m_s >= 0.0 ? kListeningSpikePressureGain * dynamic_pressure
                                  : 0.0,
        value.velocity_m_s < 0.0 ? kListeningSpikePressureGain * dynamic_pressure : 0.0,
    };
}

} // namespace engine_sim_offline::simulation
