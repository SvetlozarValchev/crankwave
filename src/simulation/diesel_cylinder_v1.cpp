#include "simulation/diesel_cylinder_v1.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace crankwave::simulation {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double cycle_angle = 4.0 * pi;

bool positive(double x) noexcept {
    return std::isfinite(x) && x > 0.0;
}
bool nonnegative(double x) noexcept {
    return std::isfinite(x) && x >= 0.0;
}
bool fraction(double x) noexcept {
    return nonnegative(x) && x <= 1.0;
}
bool valid_boundary(const DieselGasBoundary &b) noexcept {
    return positive(b.pressure_pa_abs) && positive(b.temperature_k) &&
           fraction(b.oxygen_mass_fraction);
}
bool valid_valve(const DieselValveWindow &v) noexcept {
    return nonnegative(v.open_angle_atdc_rad) &&
           v.close_angle_atdc_rad > v.open_angle_atdc_rad &&
           v.close_angle_atdc_rad < cycle_angle &&
           nonnegative(v.maximum_effective_area_m2);
}

DieselPortTransfer difference(const DieselPortTransfer &a,
                              const DieselPortTransfer &b) noexcept {
    return {a.mass_into_cylinder_kg - b.mass_into_cylinder_kg,
            a.oxygen_into_cylinder_kg - b.oxygen_into_cylinder_kg,
            a.enthalpy_into_cylinder_j - b.enthalpy_into_cylinder_j,
            a.unburned_fuel_out_kg - b.unburned_fuel_out_kg};
}
DieselCylinderAccounting difference(const DieselCylinderAccounting &a,
                                    const DieselCylinderAccounting &b) noexcept {
    return {difference(a.intake, b.intake),
            difference(a.exhaust, b.exhaust),
            a.injected_fuel_mass_kg - b.injected_fuel_mass_kg,
            a.burned_fuel_mass_kg - b.burned_fuel_mass_kg,
            a.consumed_oxygen_mass_kg - b.consumed_oxygen_mass_kg,
            a.heat_release_j - b.heat_release_j,
            a.fuel_sensible_energy_j - b.fuel_sensible_energy_j,
            a.wall_heat_out_j - b.wall_heat_out_j,
            a.volume_work_out_j - b.volume_work_out_j};
}

double valve_area(const DieselValveWindow &v, double angle) noexcept {
    if (angle <= v.open_angle_atdc_rad || angle >= v.close_angle_atdc_rad) {
        return 0.0;
    }
    const double s = std::sin(pi * (angle - v.open_angle_atdc_rad) /
                              (v.close_angle_atdc_rad - v.open_angle_atdc_rad));
    return v.maximum_effective_area_m2 * s * s;
}

// Quasi-steady isentropic nozzle, with the upstream reservoir treated as stagnant.
// The pressure-ratio floor is the sonic throat condition, not a mass limiter.
double nozzle_flow(double area, double upstream_p, double downstream_p,
                   double upstream_t, double gas_constant, double gamma) noexcept {
    if (area == 0.0 || upstream_p == downstream_p) {
        return 0.0;
    }
    const double critical = std::pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
    const double ratio = std::max(downstream_p / upstream_p, critical);
    const double log_ratio = std::log(ratio);
    const double term = 2.0 * gamma / (gamma - 1.0) *
                        std::exp(2.0 / gamma * log_ratio) *
                        -std::expm1((gamma - 1.0) / gamma * log_ratio);
    return area * upstream_p / std::sqrt(gas_constant * upstream_t) * std::sqrt(term);
}

bool finite_accounting(const DieselCylinderAccounting &a) noexcept {
    for (const auto &port : {a.intake, a.exhaust}) {
        if (!std::isfinite(port.mass_into_cylinder_kg) ||
            !std::isfinite(port.oxygen_into_cylinder_kg) ||
            !std::isfinite(port.enthalpy_into_cylinder_j) ||
            !nonnegative(port.unburned_fuel_out_kg)) {
            return false;
        }
    }
    return nonnegative(a.injected_fuel_mass_kg) && nonnegative(a.burned_fuel_mass_kg) &&
           nonnegative(a.consumed_oxygen_mass_kg) && nonnegative(a.heat_release_j) &&
           nonnegative(a.fuel_sensible_energy_j) && std::isfinite(a.wall_heat_out_j) &&
           std::isfinite(a.volume_work_out_j);
}
} // namespace

std::variant<DieselCylinderSession, DieselCylinderError>
DieselCylinderSession::compile(const DieselCylinderParameters &p,
                               std::span<const DieselCrankInjectionPulse> pulses,
                               double initial_fueling_01) noexcept {
    if (!fraction(initial_fueling_01)) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_control, {}};
    }
    if (!positive(p.bore_m) || !positive(p.stroke_m) ||
        !positive(p.connecting_rod_length_m) ||
        p.connecting_rod_length_m <= p.stroke_m / 2.0 ||
        !positive(p.compression_ratio) || p.compression_ratio <= 1.0 ||
        !positive(p.held_speed_rpm) || !positive(p.gas_constant_j_per_kg_k) ||
        !positive(p.specific_heat_ratio) || p.specific_heat_ratio <= 1.0 ||
        p.specific_heat_ratio > 2.0 ||
        !nonnegative(p.injected_fuel_sensible_energy_j_per_kg) ||
        !positive(p.wall_temperature_k) || !nonnegative(p.wall_conductance_w_per_k) ||
        !positive(p.crankcase_pressure_pa_abs) || !valid_boundary(p.initial_charge) ||
        !valid_boundary(p.intake) || !valid_boundary(p.exhaust) ||
        !valid_valve(p.intake_valve) || !valid_valve(p.exhaust_valve) ||
        p.intake_valve.close_angle_atdc_rad <= 2.0 * pi ||
        p.exhaust_valve.open_angle_atdc_rad >= p.intake_valve.open_angle_atdc_rad ||
        p.exhaust_valve.close_angle_atdc_rad >= p.intake_valve.close_angle_atdc_rad ||
        p.physics_rate_hz == 0U || p.gas_substeps == 0U || p.gas_substeps > 64U) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_parameters, {}};
    }
    if (pulses.size() > kMaximumDieselInjectionPulses) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_injection_plan, {}};
    }
    DieselCylinderSession s;
    s.parameters_ = p;
    s.angular_speed_rad_s_ = p.held_speed_rpm * (2.0 * pi / 60.0);
    s.start_angle_rad_ = p.intake_valve.close_angle_atdc_rad - cycle_angle;
    s.cycle_duration_s_ = cycle_angle / s.angular_speed_rad_s_;
    s.exhaust_open_time_s_ =
        (p.exhaust_valve.open_angle_atdc_rad - s.start_angle_rad_) /
        s.angular_speed_rad_s_;
    s.swept_volume_m3_ = pi / 4.0 * p.bore_m * p.bore_m * p.stroke_m;
    s.clearance_volume_m3_ = s.swept_volume_m3_ / (p.compression_ratio - 1.0);
    s.specific_heat_cv_ = p.gas_constant_j_per_kg_k / (p.specific_heat_ratio - 1.0);
    // At most one completed cycle per frame, with a stricter angular resolution
    // bound. Finer heat/flow resolution remains the caller's convergence duty.
    if (!positive(s.angular_speed_rad_s_) || !positive(s.cycle_duration_s_) ||
        s.angular_speed_rad_s_ / p.physics_rate_hz > pi / 2.0 ||
        !positive(s.clearance_volume_m3_) || !positive(s.swept_volume_m3_) ||
        !positive(s.specific_heat_cv_)) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_parameters, {}};
    }
    s.pulse_count_ = pulses.size();
    std::copy(pulses.begin(), pulses.end(), s.pulses_.begin());
    for (const auto &pulse : pulses) {
        const double start =
            (pulse.start_angle_atdc_rad - s.start_angle_rad_) / s.angular_speed_rad_s_;
        const double duration = pulse.duration_rad / s.angular_speed_rad_s_;
        if (!std::isfinite(pulse.start_angle_atdc_rad) || !positive(duration) ||
            !nonnegative(start) || start + duration > s.exhaust_open_time_s_ ||
            !positive(pulse.minimum_duration_fraction_01) ||
            pulse.minimum_duration_fraction_01 > 1.0) {
            return DieselCylinderError{DieselCylinderErrorCode::invalid_injection_plan,
                                       {}};
        }
    }
    // Validate the full train even when the initial command requests zero fuel.
    if (const auto error = s.start_cycle(1.0)) {
        return *error;
    }
    s.state_.volume_m3 = s.volume_at(0.0);
    s.state_.charge_mass_kg =
        p.initial_charge.pressure_pa_abs * s.state_.volume_m3 /
        (p.gas_constant_j_per_kg_k * p.initial_charge.temperature_k);
    s.state_.oxygen_mass_kg =
        s.state_.charge_mass_kg * p.initial_charge.oxygen_mass_fraction;
    s.state_.internal_energy_j =
        s.state_.charge_mass_kg * s.specific_heat_cv_ * p.initial_charge.temperature_k;
    s.state_.unwrapped_angle_rad = s.start_angle_rad_;
    if (!s.update_thermodynamics()) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_parameters, {}};
    }
    if (const auto error = s.start_cycle(initial_fueling_01)) {
        return *error;
    }
    return s;
}

std::optional<DieselCylinderError>
DieselCylinderSession::start_cycle(double fueling_01) noexcept {
    std::array<DieselInjectionPulse, kMaximumDieselInjectionPulses> train{};
    for (std::size_t i = 0U; i < pulse_count_; ++i) {
        const auto &p = pulses_[i];
        train[i] = {p.id,
                    (p.start_angle_atdc_rad - start_angle_rad_) / angular_speed_rad_s_,
                    p.duration_rad *
                        (p.minimum_duration_fraction_01 +
                         (1.0 - p.minimum_duration_fraction_01) * fueling_01) /
                        angular_speed_rad_s_,
                    p.full_fuel_mass_kg * fueling_01,
                    p.opening_fraction_01,
                    p.closing_fraction_01};
    }
    auto compiled = DieselCombustionSession::compile(parameters_.combustion,
                                                     {train.data(), pulse_count_});
    if (const auto *error = std::get_if<DieselCombustionError>(&compiled)) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_injection_plan,
                                   *error};
    }
    combustion_ = std::get<DieselCombustionSession>(compiled);
    // Metering changes the hydraulic edges every cycle. Gas integration must
    // split at the same represented knots as combustion, including fuel cut.
    event_count_ = 0U;
    for (std::size_t i = 0U; i < pulse_count_; ++i) {
        const auto &pulse = train[i];
        const double end = pulse.start_time_s + pulse.duration_s;
        const double opening_end =
            pulse.start_time_s + pulse.duration_s * pulse.opening_fraction_01;
        const double closing_start =
            pulse.opening_fraction_01 + pulse.closing_fraction_01 == 1.0
                ? opening_end
                : end - pulse.duration_s * pulse.closing_fraction_01;
        for (const double knot : {pulse.start_time_s, opening_end, closing_start, end})
            if (knot > 0.0)
                events_[event_count_++] = knot;
    }
    for (const double angle : {parameters_.exhaust_valve.open_angle_atdc_rad,
                               parameters_.exhaust_valve.close_angle_atdc_rad,
                               parameters_.intake_valve.open_angle_atdc_rad})
        events_[event_count_++] = (angle - start_angle_rad_) / angular_speed_rad_s_;
    events_[event_count_++] = cycle_duration_s_;
    std::sort(events_.begin(), events_.begin() + event_count_);
    event_count_ = static_cast<std::size_t>(
        std::unique(events_.begin(), events_.begin() + event_count_) - events_.begin());

    local_time_s_ = 0.0;
    next_event_ = 0U;
    cycle_ = {};
    cycle_.index = state_.cycle_index;
    cycle_.peak_pressure_pa_abs = state_.pressure_pa_abs;
    cycle_start_total_ = state_.total;
    return {};
}

double DieselCylinderSession::volume_at(double local_time_s) const noexcept {
    const double angle = start_angle_rad_ + angular_speed_rad_s_ * local_time_s;
    const double radius = parameters_.stroke_m / 2.0;
    const double rod = parameters_.connecting_rod_length_m;
    const double transverse = radius * std::sin(angle);
    // Rationalized rod shortening avoids subtracting almost equal rod lengths.
    const double root = std::sqrt((rod - transverse) * (rod + transverse));
    const double travel =
        radius * (1.0 - std::cos(angle)) + transverse * transverse / (rod + root);
    return clearance_volume_m3_ + swept_volume_m3_ / parameters_.stroke_m * travel;
}

bool DieselCylinderSession::update_thermodynamics() noexcept {
    if (!positive(state_.charge_mass_kg) || !positive(state_.internal_energy_j) ||
        !positive(state_.volume_m3) || !nonnegative(state_.oxygen_mass_kg) ||
        !nonnegative(state_.reacting_fuel_mass_kg) ||
        !nonnegative(state_.unburned_tracer_mass_kg) ||
        state_.oxygen_mass_kg + state_.reacting_fuel_mass_kg +
                state_.unburned_tracer_mass_kg >
            state_.charge_mass_kg) {
        return false;
    }
    state_.temperature_k =
        state_.internal_energy_j / (state_.charge_mass_kg * specific_heat_cv_);
    state_.pressure_pa_abs = (parameters_.specific_heat_ratio - 1.0) *
                             state_.internal_energy_j / state_.volume_m3;
    const double angle = start_angle_rad_ + angular_speed_rad_s_ * local_time_s_;
    const double r = parameters_.stroke_m / 2.0;
    const double l = parameters_.connecting_rod_length_m;
    const double x = r * std::sin(angle);
    const double derivative = r * std::sin(angle) + r * r * std::sin(angle) *
                                                        std::cos(angle) /
                                                        std::sqrt((l - x) * (l + x));
    state_.pressure_torque_nm =
        (state_.pressure_pa_abs - parameters_.crankcase_pressure_pa_abs) *
        swept_volume_m3_ / parameters_.stroke_m * derivative;
    return positive(state_.pressure_pa_abs) && positive(state_.temperature_k) &&
           std::isfinite(state_.pressure_torque_nm) && finite_accounting(state_.total);
}

std::optional<DieselCylinderError>
DieselCylinderSession::integrate(double end_s, double local_end_s,
                                 bool fuel_enabled) noexcept {
    const double dt = local_end_s - local_time_s_;
    const double midpoint = (local_end_s + local_time_s_) / 2.0;
    if (!positive(dt) || !(end_s > state_.time_s)) {
        return DieselCylinderError{DieselCylinderErrorCode::clock_exhausted, {}};
    }
    const auto move_volume = [this](double new_volume) {
        const double new_energy =
            state_.internal_energy_j * std::pow(state_.volume_m3 / new_volume,
                                                parameters_.specific_heat_ratio - 1.0);
        state_.total.volume_work_out_j += state_.internal_energy_j - new_energy;
        state_.internal_energy_j = new_energy;
        state_.volume_m3 = new_volume;
    };
    move_volume(volume_at(midpoint));
    if (!update_thermodynamics()) {
        return DieselCylinderError{DieselCylinderErrorCode::numerical_failure, {}};
    }
    const double angle = start_angle_rad_ + angular_speed_rad_s_ * midpoint;
    const auto exchange =
        [&](const DieselValveWindow &valve, const DieselGasBoundary &boundary,
            DieselPortTransfer &ledger) -> std::optional<DieselCylinderError> {
        const double area = valve_area(valve, angle);
        const bool inward = boundary.pressure_pa_abs > state_.pressure_pa_abs;
        const double upstream_p =
            inward ? boundary.pressure_pa_abs : state_.pressure_pa_abs;
        const double downstream_p =
            inward ? state_.pressure_pa_abs : boundary.pressure_pa_abs;
        const double upstream_t =
            inward ? boundary.temperature_k : state_.temperature_k;
        const double amount =
            dt * nozzle_flow(area, upstream_p, downstream_p, upstream_t,
                             parameters_.gas_constant_j_per_kg_k,
                             parameters_.specific_heat_ratio);
        const double enthalpy =
            amount * specific_heat_cv_ * parameters_.specific_heat_ratio * upstream_t;
        if (!nonnegative(amount) || !nonnegative(enthalpy)) {
            return DieselCylinderError{DieselCylinderErrorCode::numerical_failure, {}};
        }
        // Explicit exchange must resolve both mass and energy. Fail transactionally
        // instead of silently clipping a flow and changing the physical model.
        if (amount > 0.02 * state_.charge_mass_kg ||
            enthalpy > 0.02 * state_.internal_energy_j) {
            return DieselCylinderError{
                DieselCylinderErrorCode::integration_step_too_large, {}};
        }
        const double oxygen =
            amount * (inward ? boundary.oxygen_mass_fraction
                             : state_.oxygen_mass_kg / state_.charge_mass_kg);
        const double fuel_out =
            inward ? 0.0
                   : amount * state_.unburned_tracer_mass_kg / state_.charge_mass_kg;
        const double sign = inward ? 1.0 : -1.0;
        state_.charge_mass_kg += sign * amount;
        state_.oxygen_mass_kg += sign * oxygen;
        state_.internal_energy_j += sign * enthalpy;
        state_.unburned_tracer_mass_kg -= fuel_out;
        ledger.mass_into_cylinder_kg += sign * amount;
        ledger.oxygen_into_cylinder_kg += sign * oxygen;
        ledger.enthalpy_into_cylinder_j += sign * enthalpy;
        ledger.unburned_fuel_out_kg += fuel_out;
        if (!update_thermodynamics()) {
            return DieselCylinderError{DieselCylinderErrorCode::numerical_failure, {}};
        }
        return {};
    };
    if (const auto error = exchange(parameters_.exhaust_valve, parameters_.exhaust,
                                    state_.total.exhaust)) {
        return error;
    }
    if (const auto error = exchange(parameters_.intake_valve, parameters_.intake,
                                    state_.total.intake)) {
        return error;
    }
    if (combustion_) {
        auto result =
            combustion_->advance_to(local_end_s,
                                    {state_.temperature_k, state_.pressure_pa_abs,
                                     state_.charge_mass_kg, state_.oxygen_mass_kg},
                                    fuel_enabled);
        if (const auto *error = std::get_if<DieselCombustionError>(&result)) {
            return DieselCylinderError{DieselCylinderErrorCode::combustion_failure,
                                       *error};
        }
        const auto &burn = std::get<DieselCombustionStep>(result);
        const double sensible = burn.injected_fuel_mass_kg *
                                parameters_.injected_fuel_sensible_energy_j_per_kg;
        state_.charge_mass_kg += burn.injected_fuel_mass_kg;
        state_.oxygen_mass_kg -= burn.consumed_oxygen_mass_kg;
        state_.internal_energy_j += burn.heat_release_j + sensible;
        const auto &fuel = combustion_->state();
        state_.reacting_fuel_mass_kg = fuel.unprepared_fuel_mass_kg +
                                       fuel.premixed_fuel_mass_kg +
                                       fuel.diffusion_fuel_mass_kg;
        state_.total.injected_fuel_mass_kg += burn.injected_fuel_mass_kg;
        state_.total.burned_fuel_mass_kg +=
            burn.premixed_burned_fuel_mass_kg + burn.diffusion_burned_fuel_mass_kg;
        state_.total.consumed_oxygen_mass_kg += burn.consumed_oxygen_mass_kg;
        state_.total.heat_release_j += burn.heat_release_j;
        state_.total.fuel_sensible_energy_j += sensible;
        if (burn.ignition_time_s) {
            cycle_.ignition_angle_atdc_rad =
                start_angle_rad_ + angular_speed_rad_s_ * *burn.ignition_time_s;
        }
    }
    const double wall_equilibrium =
        state_.charge_mass_kg * specific_heat_cv_ * parameters_.wall_temperature_k;
    const double wall_heat = (state_.internal_energy_j - wall_equilibrium) *
                             -std::expm1(-parameters_.wall_conductance_w_per_k * dt /
                                         (state_.charge_mass_kg * specific_heat_cv_));
    state_.internal_energy_j -= wall_heat;
    state_.total.wall_heat_out_j += wall_heat;
    move_volume(volume_at(local_end_s));
    state_.time_s = end_s;
    local_time_s_ = local_end_s;
    state_.unwrapped_angle_rad = static_cast<double>(state_.cycle_index) * cycle_angle +
                                 start_angle_rad_ + angular_speed_rad_s_ * local_end_s;
    if (!update_thermodynamics()) {
        return DieselCylinderError{DieselCylinderErrorCode::numerical_failure, {}};
    }
    cycle_.peak_pressure_pa_abs =
        std::max(cycle_.peak_pressure_pa_abs, state_.pressure_pa_abs);
    return {};
}

std::optional<DieselCylinderError>
DieselCylinderSession::advance_impl(DieselCylinderControl control,
                                    DieselCylinderStep &step) noexcept {
    constexpr std::uint64_t exact_integer_limit = 1ULL << 53U;
    if (state_.frame_index >= exact_integer_limit / parameters_.gas_substeps - 1U) {
        return DieselCylinderError{DieselCylinderErrorCode::clock_exhausted, {}};
    }
    const double rate =
        static_cast<double>(parameters_.physics_rate_hz) * parameters_.gas_substeps;
    for (std::uint32_t substep = 1U; substep <= parameters_.gas_substeps; ++substep) {
        const auto tick = state_.frame_index * parameters_.gas_substeps + substep;
        const double target = static_cast<double>(tick) / rate;
        while (state_.time_s < target) {
            const double cycle_start =
                static_cast<double>(state_.cycle_index) * cycle_duration_s_;
            const double event_local = events_[next_event_];
            const bool cycle_boundary = next_event_ + 1U == event_count_;
            const double event_global =
                cycle_boundary
                    ? static_cast<double>(state_.cycle_index + 1U) * cycle_duration_s_
                    : cycle_start + event_local;
            const bool dispatch = event_global <= target;
            const double end = dispatch ? event_global : target;
            const double local_end = dispatch ? event_local : target - cycle_start;
            if (const auto error = integrate(end, local_end, control.fuel_enabled)) {
                return error;
            }
            if (dispatch) {
                if (event_local == exhaust_open_time_s_) {
                    state_.unburned_tracer_mass_kg += state_.reacting_fuel_mass_kg;
                    state_.reacting_fuel_mass_kg = 0.0;
                    combustion_.reset();
                }
                ++next_event_;
                if (cycle_boundary) {
                    cycle_.accounting = difference(state_.total, cycle_start_total_);
                    cycle_.indicated_mean_effective_pressure_pa =
                        cycle_.accounting.volume_work_out_j / swept_volume_m3_;
                    cycle_.end_temperature_k = state_.temperature_k;
                    cycle_.end_charge_mass_kg = state_.charge_mass_kg;
                    cycle_.end_oxygen_mass_kg = state_.oxygen_mass_kg;
                    cycle_.end_unburned_tracer_mass_kg = state_.unburned_tracer_mass_kg;
                    step.completed_cycle = cycle_;
                    ++state_.cycle_index;
                    if (const auto error = start_cycle(control.fueling_01)) {
                        return error;
                    }
                }
            }
        }
    }
    ++state_.frame_index;
    return {};
}

std::variant<DieselCylinderStep, DieselCylinderError>
DieselCylinderSession::advance(DieselCylinderControl control) noexcept {
    if (!fraction(control.fueling_01)) {
        return DieselCylinderError{DieselCylinderErrorCode::invalid_control, {}};
    }
    auto next = *this;
    DieselCylinderStep step;
    if (const auto error = next.advance_impl(control, step)) {
        return *error;
    }
    step.accounting = difference(next.state_.total, state_.total);
    *this = next;
    return step;
}

} // namespace crankwave::simulation
