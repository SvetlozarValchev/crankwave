#pragma once

#include "simulation/diesel_combustion_v1.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace crankwave::simulation {

inline constexpr std::string_view kDieselCylinderV1MethodId =
    "diesel-held-speed-cylinder-v1";

struct DieselGasBoundary {
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double oxygen_mass_fraction = 0.0;
};

// Angles refer to firing TDC at zero. Exhaust opens before intake and closes
// before IVC; their windows may overlap. Cd is included in effective area.
struct DieselValveWindow {
    double open_angle_atdc_rad = 0.0;
    double close_angle_atdc_rad = 0.0;
    double maximum_effective_area_m2 = 0.0;
};

struct DieselCrankInjectionPulse {
    std::uint32_t id = 0U;
    double start_angle_atdc_rad = 0.0;
    double duration_rad = 0.0;
    double full_fuel_mass_kg = 0.0;
    double opening_fraction_01 = 0.0;
    double closing_fraction_01 = 0.0;
    // Rack-metered delivery: duration scales from this fraction at zero fuel
    // to the full authored duration at full fuel. 1 preserves fixed duration.
    double minimum_duration_fraction_01 = 1.0;
};

struct DieselCylinderParameters {
    double bore_m = 0.0;
    double stroke_m = 0.0;
    double connecting_rod_length_m = 0.0;
    double compression_ratio = 0.0;
    double held_speed_rpm = 0.0;
    double gas_constant_j_per_kg_k = 0.0;
    double specific_heat_ratio = 0.0;
    double injected_fuel_sensible_energy_j_per_kg = 0.0;
    double wall_temperature_k = 0.0;
    double wall_conductance_w_per_k = 0.0;
    double crankcase_pressure_pa_abs = 0.0;
    DieselGasBoundary initial_charge;
    DieselGasBoundary intake;
    DieselGasBoundary exhaust;
    DieselValveWindow intake_valve;
    DieselValveWindow exhaust_valve;
    DieselCombustionParameters combustion;
    // Integer scheduled publication clock; finer rates are for convergence.
    std::uint32_t physics_rate_hz = 20000U;
    std::uint32_t gas_substeps = 8U;
};

struct DieselCylinderControl {
    // Sampled at intake-valve closure for the next injection train. Air boundary
    // and valve area are independent of this fuel command.
    double fueling_01 = 1.0;
    // Immediate cut over this frame; already injected fuel can still burn.
    bool fuel_enabled = true;
};

struct DieselPortTransfer {
    // Positive INTO the cylinder for both ports. Reverse flow retains its sign.
    double mass_into_cylinder_kg = 0.0;
    double oxygen_into_cylinder_kg = 0.0;
    double enthalpy_into_cylinder_j = 0.0;
    double unburned_fuel_out_kg = 0.0;
    friend bool operator==(const DieselPortTransfer &,
                           const DieselPortTransfer &) = default;
};

struct DieselCylinderAccounting {
    DieselPortTransfer intake;
    DieselPortTransfer exhaust;
    double injected_fuel_mass_kg = 0.0;
    double burned_fuel_mass_kg = 0.0;
    double consumed_oxygen_mass_kg = 0.0;
    double heat_release_j = 0.0;
    double fuel_sensible_energy_j = 0.0;
    double wall_heat_out_j = 0.0;
    double volume_work_out_j = 0.0;
    friend bool operator==(const DieselCylinderAccounting &,
                           const DieselCylinderAccounting &) = default;
};

struct DieselCylinderState {
    std::uint64_t frame_index = 0U;
    std::uint64_t cycle_index = 0U;
    double time_s = 0.0;
    double unwrapped_angle_rad = 0.0;
    double volume_m3 = 0.0;
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double charge_mass_kg = 0.0;
    double oxygen_mass_kg = 0.0;
    double internal_energy_j = 0.0;
    double reacting_fuel_mass_kg = 0.0;
    // Nonreacting carryover after exhaust opens; advected with the gas.
    double unburned_tracer_mass_kg = 0.0;
    double pressure_torque_nm = 0.0;
    DieselCylinderAccounting total;
    friend bool operator==(const DieselCylinderState &,
                           const DieselCylinderState &) = default;
};

struct DieselCylinderCycle {
    std::uint64_t index = 0U;
    DieselCylinderAccounting accounting;
    double peak_pressure_pa_abs = 0.0;
    std::optional<double> ignition_angle_atdc_rad;
    // Equals net indicated work / swept volume over this complete four-stroke
    // cycle. Mechanical friction and accessories are not part of this model.
    double indicated_mean_effective_pressure_pa = 0.0;
    // Exact IVC state, before any remainder of the publication frame advances.
    double end_temperature_k = 0.0;
    double end_charge_mass_kg = 0.0;
    double end_oxygen_mass_kg = 0.0;
    double end_unburned_tracer_mass_kg = 0.0;
};

struct DieselCylinderStep {
    DieselCylinderAccounting accounting;
    std::optional<DieselCylinderCycle> completed_cycle;
};

enum class DieselCylinderErrorCode : std::uint8_t {
    invalid_parameters,
    invalid_control,
    invalid_injection_plan,
    clock_exhausted,
    integration_step_too_large,
    combustion_failure,
    numerical_failure,
};

struct DieselCylinderError {
    DieselCylinderErrorCode code = DieselCylinderErrorCode::invalid_parameters;
    std::optional<DieselCombustionError> combustion;
};

// Experimental internal gas owner, initialized at intake-valve closure. Constant
// speed, fixed reservoirs, calorically perfect gas, no pressure waves.
// Allocation-free advances are transactional, including cycle transitions.
class DieselCylinderSession final {
  public:
    [[nodiscard]] static std::variant<DieselCylinderSession, DieselCylinderError>
    compile(const DieselCylinderParameters &parameters,
            std::span<const DieselCrankInjectionPulse> pulses,
            double initial_fueling_01 = 1.0) noexcept;

    [[nodiscard]] std::variant<DieselCylinderStep, DieselCylinderError>
    advance(DieselCylinderControl control = {}) noexcept;

    [[nodiscard]] const DieselCylinderState &state() const noexcept {
        return state_;
    }

  private:
    DieselCylinderSession() = default;
    [[nodiscard]] std::optional<DieselCylinderError>
    start_cycle(double fueling_01) noexcept;
    [[nodiscard]] std::optional<DieselCylinderError>
    integrate(double end_s, double local_end_s, bool fuel_enabled) noexcept;
    [[nodiscard]] std::optional<DieselCylinderError>
    advance_impl(DieselCylinderControl control, DieselCylinderStep &step) noexcept;
    [[nodiscard]] bool update_thermodynamics() noexcept;
    [[nodiscard]] double volume_at(double local_time_s) const noexcept;

    DieselCylinderParameters parameters_;
    std::array<DieselCrankInjectionPulse, kMaximumDieselInjectionPulses> pulses_{};
    std::size_t pulse_count_ = 0U;
    // Hydraulic knots plus four valve boundaries and the charge boundary.
    std::array<double, 4U * kMaximumDieselInjectionPulses + 5U> events_{};
    std::size_t event_count_ = 0U;
    std::size_t next_event_ = 0U;
    double angular_speed_rad_s_ = 0.0;
    double start_angle_rad_ = 0.0;
    double cycle_duration_s_ = 0.0;
    double exhaust_open_time_s_ = 0.0;
    double swept_volume_m3_ = 0.0;
    double clearance_volume_m3_ = 0.0;
    double specific_heat_cv_ = 0.0;
    double local_time_s_ = 0.0;
    std::optional<DieselCombustionSession> combustion_;
    DieselCylinderState state_;
    DieselCylinderCycle cycle_;
    DieselCylinderAccounting cycle_start_total_;
};

} // namespace crankwave::simulation
