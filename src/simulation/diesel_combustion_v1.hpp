#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace crankwave::simulation {

inline constexpr std::string_view kDieselCombustionV1MethodId =
    "diesel-prepared-fuel-combustion-v1";
inline constexpr std::size_t kMaximumDieselInjectionPulses = 8U;

// Hydraulic delivery times in seconds on the cylinder transaction's local clock.
// The upstream crank scheduler owns the conversion from unwrapped injection
// angles to these times. Fuel mass is per pulse, not a flow rate or an AFR.
struct DieselInjectionPulse {
    std::uint32_t id = 0U;
    double start_time_s = 0.0;
    double duration_s = 0.0;
    double fuel_mass_kg = 0.0;
    double opening_fraction_01 = 0.0;
    double closing_fraction_01 = 0.0;

    friend bool operator==(const DieselInjectionPulse &,
                           const DieselInjectionPulse &) = default;
};

// All coefficients are explicit. No diesel calibration is inferred from a fuel
// name. These are phenomenological preparation/reaction parameters, not spray CFD.
struct DieselCombustionParameters {
    double lower_heating_value_j_per_kg = 0.0;
    double stoichiometric_oxygen_kg_per_kg_fuel = 0.0;
    double preparation_time_s = 0.0;
    double premixed_burn_time_s = 0.0;
    double reference_ignition_delay_s = 0.0;
    double reference_temperature_k = 0.0;
    double reference_pressure_pa_abs = 0.0;
    double reference_oxygen_mass_fraction = 0.0;
    double activation_temperature_k = 0.0;
    double pressure_exponent = 0.0;
    double oxygen_exponent = 0.0;

    friend bool operator==(const DieselCombustionParameters &,
                           const DieselCombustionParameters &) = default;
};

// The gas owner holds these conditions over one advance and applies the returned
// injected fuel, oxygen consumption, and heat to its thermodynamic/species state.
// Re-evaluate them at gas integration boundaries; this component does not solve
// cylinder pressure, volume work, heat loss, or gas exchange.
struct DieselCombustionConditions {
    double temperature_k = 0.0;
    double pressure_pa_abs = 0.0;
    double charge_mass_kg = 0.0;
    double oxygen_mass_kg = 0.0;

    friend bool operator==(const DieselCombustionConditions &,
                           const DieselCombustionConditions &) = default;
};

struct DieselCombustionState {
    double time_s = 0.0;
    double injected_fuel_mass_kg = 0.0;
    double unprepared_fuel_mass_kg = 0.0;
    // Fuel prepared before ignition burns on premixed_burn_time_s.
    double premixed_fuel_mass_kg = 0.0;
    // Fuel prepared after ignition is mixing-controlled. Oxygen-limited fuel
    // remains here; it is never silently burned or discarded.
    double diffusion_fuel_mass_kg = 0.0;
    double burned_fuel_mass_kg = 0.0;
    double ignition_progress_01 = 0.0;
    std::optional<double> ignition_time_s;

    friend bool operator==(const DieselCombustionState &,
                           const DieselCombustionState &) = default;
};

struct DieselCombustionStep {
    double start_time_s = 0.0;
    double end_time_s = 0.0;
    double injected_fuel_mass_kg = 0.0;
    double premixed_burned_fuel_mass_kg = 0.0;
    double diffusion_burned_fuel_mass_kg = 0.0;
    double consumed_oxygen_mass_kg = 0.0;
    double heat_release_j = 0.0;
    bool oxygen_limited = false;
    // Present only in the step that accepts ignition. A second pulse does not
    // reset the chamber or independently ignite an already burning charge.
    std::optional<double> ignition_time_s;

    friend bool operator==(const DieselCombustionStep &,
                           const DieselCombustionStep &) = default;
};

enum class DieselCombustionErrorCode : std::uint8_t {
    invalid_parameters,
    too_many_pulses,
    invalid_pulse,
    duplicate_pulse_id,
    overlapping_or_unordered_pulses,
    unrepresentable_pulse,
    invalid_conditions,
    invalid_end_time,
    numerical_failure,
};

struct DieselCombustionError {
    DieselCombustionErrorCode code = DieselCombustionErrorCode::invalid_parameters;
    // Relevant pulse index for injection-plan errors, otherwise zero.
    std::size_t pulse_index = 0U;

    friend bool operator==(const DieselCombustionError &,
                           const DieselCombustionError &) = default;
};

using DieselIgnitionDelayCalculation = std::variant<double, DieselCombustionError>;

// Reference-normalized pressure/oxygen power laws and Arrhenius temperature law.
// Zero oxygen yields +infinity (no ignition); arithmetic overflow is an error.
[[nodiscard]] DieselIgnitionDelayCalculation
diesel_ignition_delay_s(const DieselCombustionParameters &parameters,
                        const DieselCombustionConditions &conditions) noexcept;

using DieselCombustionAdvanceResult =
    std::variant<DieselCombustionStep, DieselCombustionError>;

// One finite injection train and its closed fuel inventory. No allocation occurs
// during advance. Construct a new session at the next cylinder charge boundary;
// the gas owner must account for any remaining fuel before replacing the state.
// The current low-order model retains ignition through the train: extinction,
// reignition, wall films, soot and crevice fuel are outside this method's domain.
class DieselCombustionSession final {
  public:
    [[nodiscard]] static std::variant<DieselCombustionSession, DieselCombustionError>
    compile(const DieselCombustionParameters &parameters,
            std::span<const DieselInjectionPulse> pulses) noexcept;

    // Conditions/control are right-continuous at state().time_s. Internal steps
    // split at hydraulic pulse knees and ignition onset. Key-off stops subsequent
    // delivery, including the remainder of a pulse; already injected fuel remains.
    // end_time_s is absolute so the caller can use an integer/rational time grid.
    // On any error, state is unchanged. Callers can therefore terminate their
    // owning transaction without publishing a partial combustion step.
    [[nodiscard]] DieselCombustionAdvanceResult
    advance_to(double end_time_s, const DieselCombustionConditions &conditions,
               bool fuel_enabled = true) noexcept;

    [[nodiscard]] const DieselCombustionState &state() const noexcept;

  private:
    struct Pulse {
        double start_s = 0.0;
        double opening_end_s = 0.0;
        double closing_start_s = 0.0;
        double end_s = 0.0;
        double peak_mass_flow_kg_s = 0.0;
    };

    DieselCombustionSession() = default;
    DieselCombustionParameters parameters_;
    std::array<Pulse, kMaximumDieselInjectionPulses> pulses_{};
    std::size_t pulse_count_ = 0U;
    DieselCombustionState state_;
};

} // namespace crankwave::simulation
