#include "experimental/physical_exhaust_network.hpp"

#include "simulation/legacy_gas_primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace engine_sim_offline::experimental {
namespace {

constexpr double kGamma = 1.4;
constexpr double kMolarMassKgPerMol = 0.02897;
constexpr double kUniversalGasConstantJPerMolK = 8.31446261815324;
constexpr double kSpecificGasConstantJPerKgK =
    kUniversalGasConstantJPerMolK / kMolarMassKgPerMol;
constexpr double kSpecificHeatAtConstantPressureJPerKgK =
    (kGamma / (kGamma - 1.0)) * kSpecificGasConstantJPerKgK;

constexpr double kStepS = 1.0 / static_cast<double>(kPhysicalExhaustSampleRateHz);
constexpr double kMaximumCfl = 0.45;
constexpr double kValveSonicFraction = 0.9;

constexpr double kPrimaryDiameterM = 0.042;
constexpr double kPrimaryLengthM = 0.300;
constexpr double kPrimaryWallTemperatureK = 800.0;
constexpr double kTailpipeDiameterM = 0.046;
constexpr double kTailpipeLengthM = 1.500;
constexpr double kTailpipeWallTemperatureK = 600.0;
constexpr double kJunctionInitialTemperatureK = 700.0;
constexpr double kDarcyFrictionFactor = 0.02;
constexpr double kWallHeatTransferWPerM2K = 50.0;
constexpr double kObserverDistanceM = 1.0;

constexpr double kPrimaryAreaM2 =
    std::numbers::pi * kPrimaryDiameterM * kPrimaryDiameterM * 0.25;
constexpr double kTailpipeAreaM2 =
    std::numbers::pi * kTailpipeDiameterM * kTailpipeDiameterM * 0.25;
constexpr double kPrimaryCellLengthM = kPrimaryLengthM / 12.0;
constexpr double kTailpipeCellLengthM = kTailpipeLengthM / 60.0;
constexpr double kJunctionVolumeM3 = 3.0 * kPrimaryAreaM2 * kPrimaryDiameterM;

} // namespace

double PhysicalExhaustNetwork::ReflectionFilter::process(double input) noexcept {
    const double result = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = result;
    return result;
}

bool PhysicalExhaustNetwork::finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool PhysicalExhaustNetwork::primitive(const Conserved &state,
                                       Primitive &value) noexcept {
    if (!finite_positive(state.density_kg_m3) ||
        !std::isfinite(state.momentum_kg_m2_s) ||
        !finite_positive(state.total_energy_j_m3)) {
        return false;
    }

    value.density_kg_m3 = state.density_kg_m3;
    value.velocity_m_s = state.momentum_kg_m2_s / state.density_kg_m3;
    const double kinetic_energy_j_m3 =
        0.5 * state.density_kg_m3 * value.velocity_m_s * value.velocity_m_s;
    value.pressure_pa =
        (kGamma - 1.0) * (state.total_energy_j_m3 - kinetic_energy_j_m3);
    value.temperature_k =
        value.pressure_pa / (state.density_kg_m3 * kSpecificGasConstantJPerKgK);
    value.sound_speed_m_s = std::sqrt(kGamma * value.pressure_pa / state.density_kg_m3);

    return std::isfinite(value.velocity_m_s) && finite_positive(value.pressure_pa) &&
           finite_positive(value.temperature_k) &&
           finite_positive(value.sound_speed_m_s);
}

PhysicalExhaustNetwork::Conserved
PhysicalExhaustNetwork::conserved(double density_kg_m3, double velocity_m_s,
                                  double pressure_pa) noexcept {
    return {
        density_kg_m3,
        density_kg_m3 * velocity_m_s,
        pressure_pa / (kGamma - 1.0) +
            0.5 * density_kg_m3 * velocity_m_s * velocity_m_s,
    };
}

PhysicalExhaustNetwork::Flux
PhysicalExhaustNetwork::physical_flux(const Conserved &state,
                                      const Primitive &value) noexcept {
    return {
        state.momentum_kg_m2_s,
        state.momentum_kg_m2_s * value.velocity_m_s + value.pressure_pa,
        value.velocity_m_s * (state.total_energy_j_m3 + value.pressure_pa),
    };
}

PhysicalExhaustNetwork::Flux
PhysicalExhaustNetwork::hll_flux(const Conserved &left, const Conserved &right,
                                 const Primitive &left_value,
                                 const Primitive &right_value) noexcept {
    const auto left_flux = physical_flux(left, left_value);
    const auto right_flux = physical_flux(right, right_value);
    const double left_speed =
        std::min(left_value.velocity_m_s - left_value.sound_speed_m_s,
                 right_value.velocity_m_s - right_value.sound_speed_m_s);
    const double right_speed =
        std::max(left_value.velocity_m_s + left_value.sound_speed_m_s,
                 right_value.velocity_m_s + right_value.sound_speed_m_s);

    if (left_speed >= 0.0) {
        return left_flux;
    }
    if (right_speed <= 0.0) {
        return right_flux;
    }

    const double inverse_span = 1.0 / (right_speed - left_speed);
    return {
        (right_speed * left_flux.mass_kg_m2_s - left_speed * right_flux.mass_kg_m2_s +
         left_speed * right_speed * (right.density_kg_m3 - left.density_kg_m3)) *
            inverse_span,
        (right_speed * left_flux.momentum_pa - left_speed * right_flux.momentum_pa +
         left_speed * right_speed * (right.momentum_kg_m2_s - left.momentum_kg_m2_s)) *
            inverse_span,
        (right_speed * left_flux.energy_w_m2 - left_speed * right_flux.energy_w_m2 +
         left_speed * right_speed *
             (right.total_energy_j_m3 - left.total_energy_j_m3)) *
            inverse_span,
    };
}

void PhysicalExhaustNetwork::initialize(double ambient_pressure_pa,
                                        double ambient_temperature_k) {
    output_ = {};
    failure_reason_ = {};
    initialized_ = false;
    previous_outlet_volume_flow_m3_s_.fill(0.0);

    if (!finite_positive(ambient_pressure_pa) ||
        !finite_positive(ambient_temperature_k)) {
        failure_reason_ = "physical-exhaust-invalid-ambient";
        return;
    }

    ambient_pressure_pa_ = ambient_pressure_pa;
    ambient_temperature_k_ = ambient_temperature_k;
    ambient_density_kg_m3_ =
        ambient_pressure_pa_ / (kSpecificGasConstantJPerKgK * ambient_temperature_k_);
    tailpipe_reference_density_kg_m3_ =
        ambient_pressure_pa_ /
        (kSpecificGasConstantJPerKgK * kTailpipeWallTemperatureK);
    tailpipe_reference_sound_speed_m_s_ =
        std::sqrt(kGamma * kSpecificGasConstantJPerKgK * kTailpipeWallTemperatureK);
    tailpipe_characteristic_impedance_pa_s_m_ =
        tailpipe_reference_density_kg_m3_ * tailpipe_reference_sound_speed_m_s_;

    const auto primary_initial = conserved(
        ambient_pressure_pa_ / (kSpecificGasConstantJPerKgK * kPrimaryWallTemperatureK),
        0.0, ambient_pressure_pa_);
    const auto tailpipe_initial =
        conserved(tailpipe_reference_density_kg_m3_, 0.0, ambient_pressure_pa_);
    for (auto &primary : primaries_) {
        primary.fill(primary_initial);
    }
    next_primaries_ = primaries_;
    for (auto &tailpipe : tailpipes_) {
        tailpipe.fill(tailpipe_initial);
    }
    next_tailpipes_ = tailpipes_;

    const double junction_mass_kg =
        ambient_pressure_pa_ * kJunctionVolumeM3 /
        (kSpecificGasConstantJPerKgK * kJunctionInitialTemperatureK);
    const double junction_energy_j =
        ambient_pressure_pa_ * kJunctionVolumeM3 / (kGamma - 1.0);
    junctions_.fill({junction_mass_kg, junction_energy_j});
    next_junctions_ = junctions_;

    const double radius_m = 0.5 * kTailpipeDiameterM;
    const double tau_s = radius_m / tailpipe_reference_sound_speed_m_s_;
    const double q = (2.0 / kStepS) * tau_s;
    const double denominator = 1.0 + 1.393 * q + 0.457 * q * q;
    ReflectionFilter reflection;
    reflection.b0 = -(1.0 + 0.167 * q) / denominator;
    reflection.b1 = -2.0 / denominator;
    reflection.b2 = -(1.0 - 0.167 * q) / denominator;
    reflection.a1 = (2.0 - 2.0 * 0.457 * q * q) / denominator;
    reflection.a2 = (1.0 - 1.393 * q + 0.457 * q * q) / denominator;
    outlet_reflections_.fill(reflection);

    initialized_ = true;
}

bool PhysicalExhaustNetwork::valve_flux(const PhysicalValveBoundary &valve,
                                        const Primitive &primary_inlet,
                                        Flux &result) noexcept {
    if (!finite_positive(valve.pressure_pa_abs) ||
        !finite_positive(valve.temperature_k) ||
        !std::isfinite(valve.molar_flow_conductance) ||
        valve.molar_flow_conductance < 0.0) {
        return fail("physical-exhaust-invalid-valve-boundary");
    }

    if (valve.molar_flow_conductance == 0.0) {
        result = {0.0, primary_inlet.pressure_pa, 0.0};
        return true;
    }

    const double molar_rate = simulation::legacy_restriction_molar_rate(
        valve.molar_flow_conductance, valve.pressure_pa_abs, primary_inlet.pressure_pa,
        valve.temperature_k, primary_inlet.temperature_k);
    if (!std::isfinite(molar_rate)) {
        return fail("physical-exhaust-nonfinite-valve-flow");
    }

    const bool into_primary = molar_rate >= 0.0;
    const double upstream_pressure_pa =
        into_primary ? valve.pressure_pa_abs : primary_inlet.pressure_pa;
    const double upstream_temperature_k =
        into_primary ? valve.temperature_k : primary_inlet.temperature_k;
    const double upstream_density_kg_m3 =
        upstream_pressure_pa / (kSpecificGasConstantJPerKgK * upstream_temperature_k);
    const double upstream_sound_speed_m_s =
        std::sqrt(kGamma * kSpecificGasConstantJPerKgK * upstream_temperature_k);
    if (!finite_positive(upstream_density_kg_m3) ||
        !finite_positive(upstream_sound_speed_m_s)) {
        return fail("physical-exhaust-invalid-valve-upstream-state");
    }

    double mass_flux_kg_m2_s = molar_rate * kMolarMassKgPerMol / kPrimaryAreaM2;
    const double sonic_mass_flux_kg_m2_s =
        kValveSonicFraction * upstream_density_kg_m3 * upstream_sound_speed_m_s;
    mass_flux_kg_m2_s = std::clamp(mass_flux_kg_m2_s, -sonic_mass_flux_kg_m2_s,
                                   sonic_mass_flux_kg_m2_s);
    const double jet_velocity_m_s = mass_flux_kg_m2_s / upstream_density_kg_m3;
    const double stagnation_enthalpy_j_kg =
        kSpecificHeatAtConstantPressureJPerKgK * upstream_temperature_k +
        0.5 * jet_velocity_m_s * jet_velocity_m_s;

    result = {
        mass_flux_kg_m2_s,
        mass_flux_kg_m2_s * jet_velocity_m_s + primary_inlet.pressure_pa,
        mass_flux_kg_m2_s * stagnation_enthalpy_j_kg,
    };
    if (!std::isfinite(result.mass_kg_m2_s) || !std::isfinite(result.momentum_pa) ||
        !std::isfinite(result.energy_w_m2)) {
        return fail("physical-exhaust-nonfinite-valve-flux");
    }
    return true;
}

bool PhysicalExhaustNetwork::outlet_flux(std::size_t bank,
                                         const Conserved &tailpipe_outlet,
                                         const Primitive &tailpipe_outlet_value,
                                         Flux &result,
                                         double &volume_flow_m3_s) noexcept {
    const double outgoing_pressure_pa =
        0.5 * ((tailpipe_outlet_value.pressure_pa - ambient_pressure_pa_) +
               tailpipe_characteristic_impedance_pa_s_m_ *
                   tailpipe_outlet_value.velocity_m_s);
    const double incoming_pressure_pa =
        outlet_reflections_[bank].process(outgoing_pressure_pa);
    const double ghost_pressure_pa =
        ambient_pressure_pa_ + outgoing_pressure_pa + incoming_pressure_pa;
    const double ghost_velocity_m_s = (outgoing_pressure_pa - incoming_pressure_pa) /
                                      tailpipe_characteristic_impedance_pa_s_m_;
    const double ghost_density_kg_m3 =
        ghost_pressure_pa /
        (kSpecificGasConstantJPerKgK * tailpipe_outlet_value.temperature_k);
    if (!finite_positive(ghost_pressure_pa) || !std::isfinite(ghost_velocity_m_s) ||
        !finite_positive(ghost_density_kg_m3)) {
        return fail("physical-exhaust-nonphysical-outlet-boundary");
    }

    const auto ghost =
        conserved(ghost_density_kg_m3, ghost_velocity_m_s, ghost_pressure_pa);
    Primitive ghost_value;
    if (!primitive(ghost, ghost_value)) {
        return fail("physical-exhaust-invalid-outlet-ghost");
    }
    result = hll_flux(tailpipe_outlet, ghost, tailpipe_outlet_value, ghost_value);
    volume_flow_m3_s = kTailpipeAreaM2 * ghost_velocity_m_s;
    if (!std::isfinite(volume_flow_m3_s)) {
        return fail("physical-exhaust-nonfinite-outlet-flow");
    }
    return true;
}

bool PhysicalExhaustNetwork::apply_losses(Conserved &state, double diameter_m,
                                          double wall_temperature_k) noexcept {
    Primitive value;
    if (!primitive(state, value)) {
        return fail("physical-exhaust-nonphysical-finite-volume-state");
    }

    const double friction_denominator = 1.0 + kStepS * kDarcyFrictionFactor *
                                                  std::abs(value.velocity_m_s) /
                                                  (2.0 * diameter_m);
    const double damped_velocity_m_s = value.velocity_m_s / friction_denominator;
    state.momentum_kg_m2_s = state.density_kg_m3 * damped_velocity_m_s;
    if (!primitive(state, value)) {
        return fail("physical-exhaust-invalid-friction-update");
    }

    state.total_energy_j_m3 += kStepS * (4.0 * kWallHeatTransferWPerM2K / diameter_m) *
                               (wall_temperature_k - value.temperature_k);
    if (!primitive(state, value)) {
        return fail("physical-exhaust-invalid-wall-heat-update");
    }
    return true;
}

bool PhysicalExhaustNetwork::advance(
    const std::array<PhysicalValveBoundary, 6> &valves) {
    if (!initialized_) {
        return fail(failure_reason_.empty()
                        ? std::string_view{"physical-exhaust-not-initialized"}
                        : failure_reason_);
    }
    if (!failure_reason_.empty()) {
        return false;
    }

    std::array<std::array<Primitive, kPrimaryCellCount>, kCylinderCount>
        primary_values{};
    std::array<std::array<Primitive, kTailpipeCellCount>, kBankCount> tailpipe_values{};
    std::array<Primitive, kBankCount> junction_values{};
    std::array<Conserved, kBankCount> junction_states{};
    double step_maximum_cfl = 0.0;

    for (std::size_t cylinder = 0; cylinder < kCylinderCount; ++cylinder) {
        for (std::size_t cell = 0; cell < kPrimaryCellCount; ++cell) {
            auto &value = primary_values[cylinder][cell];
            if (!primitive(primaries_[cylinder][cell], value)) {
                return fail("physical-exhaust-invalid-primary-state");
            }
            step_maximum_cfl = std::max(
                step_maximum_cfl,
                kStepS * (std::abs(value.velocity_m_s) + value.sound_speed_m_s) /
                    kPrimaryCellLengthM);
        }
    }
    for (std::size_t bank = 0; bank < kBankCount; ++bank) {
        for (std::size_t cell = 0; cell < kTailpipeCellCount; ++cell) {
            auto &value = tailpipe_values[bank][cell];
            if (!primitive(tailpipes_[bank][cell], value)) {
                return fail("physical-exhaust-invalid-tailpipe-state");
            }
            step_maximum_cfl = std::max(
                step_maximum_cfl,
                kStepS * (std::abs(value.velocity_m_s) + value.sound_speed_m_s) /
                    kTailpipeCellLengthM);
        }

        const auto &junction = junctions_[bank];
        if (!finite_positive(junction.mass_kg) ||
            !finite_positive(junction.total_energy_j)) {
            return fail("physical-exhaust-invalid-junction-state");
        }
        const double junction_density_kg_m3 = junction.mass_kg / kJunctionVolumeM3;
        const double junction_energy_j_m3 = junction.total_energy_j / kJunctionVolumeM3;
        const double junction_pressure_pa = (kGamma - 1.0) * junction_energy_j_m3;
        junction_states[bank] =
            conserved(junction_density_kg_m3, 0.0, junction_pressure_pa);
        if (!primitive(junction_states[bank], junction_values[bank])) {
            return fail("physical-exhaust-nonphysical-junction-state");
        }
    }

    output_.maximum_cfl = std::max(output_.maximum_cfl, step_maximum_cfl);
    if (!std::isfinite(step_maximum_cfl) || step_maximum_cfl > kMaximumCfl) {
        return fail("physical-exhaust-cfl-limit-exceeded");
    }

    std::array<std::array<Flux, kPrimaryCellCount + 1U>, kCylinderCount>
        primary_fluxes{};
    std::array<std::array<Flux, kTailpipeCellCount + 1U>, kBankCount> tailpipe_fluxes{};
    std::array<double, kBankCount> outlet_volume_flow_m3_s{};

    for (std::size_t cylinder = 0; cylinder < kCylinderCount; ++cylinder) {
        const std::size_t bank = cylinder / kCylindersPerBank;
        if (!valve_flux(valves[cylinder], primary_values[cylinder].front(),
                        primary_fluxes[cylinder].front())) {
            return false;
        }
        for (std::size_t interface = 1; interface < kPrimaryCellCount; ++interface) {
            primary_fluxes[cylinder][interface] = hll_flux(
                primaries_[cylinder][interface - 1U], primaries_[cylinder][interface],
                primary_values[cylinder][interface - 1U],
                primary_values[cylinder][interface]);
        }
        primary_fluxes[cylinder].back() =
            hll_flux(primaries_[cylinder].back(), junction_states[bank],
                     primary_values[cylinder].back(), junction_values[bank]);
    }

    for (std::size_t bank = 0; bank < kBankCount; ++bank) {
        tailpipe_fluxes[bank].front() =
            hll_flux(junction_states[bank], tailpipes_[bank].front(),
                     junction_values[bank], tailpipe_values[bank].front());
        for (std::size_t interface = 1; interface < kTailpipeCellCount; ++interface) {
            tailpipe_fluxes[bank][interface] =
                hll_flux(tailpipes_[bank][interface - 1U], tailpipes_[bank][interface],
                         tailpipe_values[bank][interface - 1U],
                         tailpipe_values[bank][interface]);
        }
        if (!outlet_flux(bank, tailpipes_[bank].back(), tailpipe_values[bank].back(),
                         tailpipe_fluxes[bank].back(), outlet_volume_flow_m3_s[bank])) {
            return false;
        }
    }

    for (std::size_t cylinder = 0; cylinder < kCylinderCount; ++cylinder) {
        for (std::size_t cell = 0; cell < kPrimaryCellCount; ++cell) {
            const auto &left_flux = primary_fluxes[cylinder][cell];
            const auto &right_flux = primary_fluxes[cylinder][cell + 1U];
            next_primaries_[cylinder][cell] = {
                primaries_[cylinder][cell].density_kg_m3 -
                    (kStepS / kPrimaryCellLengthM) *
                        (right_flux.mass_kg_m2_s - left_flux.mass_kg_m2_s),
                primaries_[cylinder][cell].momentum_kg_m2_s -
                    (kStepS / kPrimaryCellLengthM) *
                        (right_flux.momentum_pa - left_flux.momentum_pa),
                primaries_[cylinder][cell].total_energy_j_m3 -
                    (kStepS / kPrimaryCellLengthM) *
                        (right_flux.energy_w_m2 - left_flux.energy_w_m2),
            };
            if (!apply_losses(next_primaries_[cylinder][cell], kPrimaryDiameterM,
                              kPrimaryWallTemperatureK)) {
                return false;
            }
        }
    }

    for (std::size_t bank = 0; bank < kBankCount; ++bank) {
        for (std::size_t cell = 0; cell < kTailpipeCellCount; ++cell) {
            const auto &left_flux = tailpipe_fluxes[bank][cell];
            const auto &right_flux = tailpipe_fluxes[bank][cell + 1U];
            next_tailpipes_[bank][cell] = {
                tailpipes_[bank][cell].density_kg_m3 -
                    (kStepS / kTailpipeCellLengthM) *
                        (right_flux.mass_kg_m2_s - left_flux.mass_kg_m2_s),
                tailpipes_[bank][cell].momentum_kg_m2_s -
                    (kStepS / kTailpipeCellLengthM) *
                        (right_flux.momentum_pa - left_flux.momentum_pa),
                tailpipes_[bank][cell].total_energy_j_m3 -
                    (kStepS / kTailpipeCellLengthM) *
                        (right_flux.energy_w_m2 - left_flux.energy_w_m2),
            };
            if (!apply_losses(next_tailpipes_[bank][cell], kTailpipeDiameterM,
                              kTailpipeWallTemperatureK)) {
                return false;
            }
        }

        double junction_mass_rate_kg_s =
            -kTailpipeAreaM2 * tailpipe_fluxes[bank].front().mass_kg_m2_s;
        double junction_energy_rate_w =
            -kTailpipeAreaM2 * tailpipe_fluxes[bank].front().energy_w_m2;
        const std::size_t first_cylinder = bank * kCylindersPerBank;
        for (std::size_t lane = 0; lane < kCylindersPerBank; ++lane) {
            const auto &primary_outlet = primary_fluxes[first_cylinder + lane].back();
            junction_mass_rate_kg_s += kPrimaryAreaM2 * primary_outlet.mass_kg_m2_s;
            junction_energy_rate_w += kPrimaryAreaM2 * primary_outlet.energy_w_m2;
        }
        next_junctions_[bank] = {
            junctions_[bank].mass_kg + kStepS * junction_mass_rate_kg_s,
            junctions_[bank].total_energy_j + kStepS * junction_energy_rate_w,
        };
        if (!finite_positive(next_junctions_[bank].mass_kg) ||
            !finite_positive(next_junctions_[bank].total_energy_j)) {
            return fail("physical-exhaust-nonphysical-junction-update");
        }
        const double next_junction_pressure_pa =
            (kGamma - 1.0) * next_junctions_[bank].total_energy_j / kJunctionVolumeM3;
        if (!finite_positive(next_junction_pressure_pa)) {
            return fail("physical-exhaust-invalid-junction-pressure");
        }
    }

    double next_state_maximum_cfl = 0.0;
    for (const auto &primary : next_primaries_) {
        for (const auto &cell : primary) {
            Primitive value;
            if (!primitive(cell, value)) {
                return fail("physical-exhaust-invalid-next-primary-state");
            }
            next_state_maximum_cfl = std::max(
                next_state_maximum_cfl,
                kStepS * (std::abs(value.velocity_m_s) + value.sound_speed_m_s) /
                    kPrimaryCellLengthM);
        }
    }
    for (const auto &tailpipe : next_tailpipes_) {
        for (const auto &cell : tailpipe) {
            Primitive value;
            if (!primitive(cell, value)) {
                return fail("physical-exhaust-invalid-next-tailpipe-state");
            }
            next_state_maximum_cfl = std::max(
                next_state_maximum_cfl,
                kStepS * (std::abs(value.velocity_m_s) + value.sound_speed_m_s) /
                    kTailpipeCellLengthM);
        }
    }
    output_.maximum_cfl = std::max(output_.maximum_cfl, next_state_maximum_cfl);
    if (!std::isfinite(next_state_maximum_cfl) ||
        next_state_maximum_cfl > kMaximumCfl) {
        return fail("physical-exhaust-next-state-cfl-limit-exceeded");
    }

    primaries_.swap(next_primaries_);
    tailpipes_.swap(next_tailpipes_);
    junctions_.swap(next_junctions_);

    for (std::size_t bank = 0; bank < kBankCount; ++bank) {
        const double volume_flow_derivative_m3_s2 =
            (outlet_volume_flow_m3_s[bank] - previous_outlet_volume_flow_m3_s_[bank]) /
            kStepS;
        output_.radiated_pressure_pa[bank] =
            (ambient_density_kg_m3_ / (4.0 * std::numbers::pi * kObserverDistanceM)) *
            volume_flow_derivative_m3_s2;
        previous_outlet_volume_flow_m3_s_[bank] = outlet_volume_flow_m3_s[bank];
        if (!std::isfinite(output_.radiated_pressure_pa[bank])) {
            return fail("physical-exhaust-nonfinite-radiated-pressure");
        }
    }
    output_.coherent_mono_pressure_pa =
        output_.radiated_pressure_pa[0] + output_.radiated_pressure_pa[1];
    if (!std::isfinite(output_.coherent_mono_pressure_pa)) {
        return fail("physical-exhaust-nonfinite-mono-pressure");
    }
    return true;
}

const PhysicalExhaustOutput &PhysicalExhaustNetwork::output() const {
    return output_;
}

std::string_view PhysicalExhaustNetwork::failure_reason() const {
    return failure_reason_;
}

bool PhysicalExhaustNetwork::fail(std::string_view reason) noexcept {
    if (failure_reason_.empty()) {
        failure_reason_ = reason;
    }
    return false;
}

} // namespace engine_sim_offline::experimental
