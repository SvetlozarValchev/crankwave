#include "simulation/diesel_combustion_v1.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crankwave::simulation {
namespace {

[[nodiscard]] bool positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool valid_parameters(const DieselCombustionParameters &p) noexcept {
    return positive(p.lower_heating_value_j_per_kg) &&
           positive(p.stoichiometric_oxygen_kg_per_kg_fuel) &&
           positive(p.preparation_time_s) && positive(p.premixed_burn_time_s) &&
           positive(p.reference_ignition_delay_s) &&
           positive(p.reference_temperature_k) &&
           positive(p.reference_pressure_pa_abs) &&
           positive(p.reference_oxygen_mass_fraction) &&
           p.reference_oxygen_mass_fraction <= 1.0 &&
           nonnegative(p.activation_temperature_k) &&
           nonnegative(p.pressure_exponent) && nonnegative(p.oxygen_exponent);
}

[[nodiscard]] bool valid_conditions(const DieselCombustionConditions &c) noexcept {
    return positive(c.temperature_k) && positive(c.pressure_pa_abs) &&
           positive(c.charge_mass_kg) && nonnegative(c.oxygen_mass_kg) &&
           c.oxygen_mass_kg <= c.charge_mass_kg;
}

[[nodiscard]] DieselCombustionError numerical_failure() noexcept {
    return {DieselCombustionErrorCode::numerical_failure, 0U};
}

// Normalized convolution weights for a linearly varying injection rate through
// a first-order fuel-preparation reservoir. w0 + w1 = (1 - exp(-x)) / x.
// The series avoids subtracting nearly equal terms for tiny integration steps.
struct PreparationWeights {
    double decay = 0.0;
    double start_rate = 0.0;
    double end_rate = 0.0;
};

[[nodiscard]] PreparationWeights preparation_weights(double x) noexcept {
    PreparationWeights w;
    w.decay = std::exp(-x);
    if (x < 0.001) {
        w.start_rate =
            0.5 + x * (-1.0 / 3.0 + x * (1.0 / 8.0 + x * (-1.0 / 30.0 + x / 144.0)));
        w.end_rate =
            0.5 + x * (-1.0 / 6.0 + x * (1.0 / 24.0 + x * (-1.0 / 120.0 + x / 720.0)));
    } else {
        const double one_minus_decay = -std::expm1(-x);
        w.start_rate = (one_minus_decay / x - w.decay) / x;
        w.end_rate = (1.0 - one_minus_decay / x) / x;
    }
    return w;
}

// The only clipping is an explicitly bounded binary64 roundoff correction on
// analytically nonnegative reservoir masses. Larger violations are fatal.
[[nodiscard]] bool bound_roundoff(double &value, double upper) noexcept {
    if (!std::isfinite(value) || !nonnegative(upper)) {
        return false;
    }
    const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() * upper;
    if (value < -tolerance || value > upper + tolerance) {
        return false;
    }
    value = std::clamp(value, 0.0, upper);
    return true;
}

[[nodiscard]] bool valid_state(const DieselCombustionState &s) noexcept {
    if (!nonnegative(s.time_s) || !nonnegative(s.injected_fuel_mass_kg) ||
        !nonnegative(s.unprepared_fuel_mass_kg) ||
        !nonnegative(s.premixed_fuel_mass_kg) ||
        !nonnegative(s.diffusion_fuel_mass_kg) || !nonnegative(s.burned_fuel_mass_kg) ||
        !nonnegative(s.ignition_progress_01) || s.ignition_progress_01 > 1.0 ||
        (s.ignition_time_s &&
         (!nonnegative(*s.ignition_time_s) || *s.ignition_time_s > s.time_s))) {
        return false;
    }
    const double accounted = s.unprepared_fuel_mass_kg + s.premixed_fuel_mass_kg +
                             s.diffusion_fuel_mass_kg + s.burned_fuel_mass_kg;
    // Accumulated accounting is diagnostic; the pools themselves are never
    // renormalized to hide missing mass.
    const double tolerance = 1.0e-10 * std::max(s.injected_fuel_mass_kg, accounted);
    return std::isfinite(accounted) &&
           std::abs(accounted - s.injected_fuel_mass_kg) <= tolerance;
}

} // namespace

DieselIgnitionDelayCalculation
diesel_ignition_delay_s(const DieselCombustionParameters &p,
                        const DieselCombustionConditions &c) noexcept {
    if (!valid_parameters(p)) {
        return DieselCombustionError{DieselCombustionErrorCode::invalid_parameters};
    }
    if (!valid_conditions(c)) {
        return DieselCombustionError{DieselCombustionErrorCode::invalid_conditions};
    }
    if (c.oxygen_mass_kg == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    // Log differences avoid overflow/underflow in intermediate pressure and
    // oxygen ratios. Evaluate disabled dependencies as exact zero.
    const double thermal =
        p.activation_temperature_k == 0.0
            ? 0.0
            : p.activation_temperature_k *
                  (1.0 / c.temperature_k - 1.0 / p.reference_temperature_k);
    const double pressure =
        p.pressure_exponent == 0.0
            ? 0.0
            : p.pressure_exponent *
                  (std::log(p.reference_pressure_pa_abs) - std::log(c.pressure_pa_abs));
    const double oxygen =
        p.oxygen_exponent == 0.0
            ? 0.0
            : p.oxygen_exponent *
                  (std::log(p.reference_oxygen_mass_fraction) +
                   std::log(c.charge_mass_kg) - std::log(c.oxygen_mass_kg));
    const double exponent = thermal + pressure + oxygen;
    // Preserve the reference value exactly, which also provides a useful
    // independent constant-delay numerical fixture.
    const double delay =
        exponent == 0.0 ? p.reference_ignition_delay_s
                        : std::exp(std::log(p.reference_ignition_delay_s) + exponent);
    if (!std::isfinite(exponent) || !positive(delay)) {
        return numerical_failure();
    }
    return delay;
}

std::variant<DieselCombustionSession, DieselCombustionError>
DieselCombustionSession::compile(
    const DieselCombustionParameters &parameters,
    std::span<const DieselInjectionPulse> pulses) noexcept {
    if (!valid_parameters(parameters)) {
        return DieselCombustionError{DieselCombustionErrorCode::invalid_parameters};
    }
    if (pulses.size() > kMaximumDieselInjectionPulses) {
        return DieselCombustionError{DieselCombustionErrorCode::too_many_pulses};
    }
    DieselCombustionSession result;
    result.parameters_ = parameters;
    result.pulse_count_ = pulses.size();
    for (std::size_t i = 0U; i < pulses.size(); ++i) {
        const auto &p = pulses[i];
        if (p.id == 0U || !nonnegative(p.start_time_s) || !positive(p.duration_s) ||
            !nonnegative(p.fuel_mass_kg) || !nonnegative(p.opening_fraction_01) ||
            !nonnegative(p.closing_fraction_01) ||
            p.opening_fraction_01 + p.closing_fraction_01 > 1.0) {
            return DieselCombustionError{DieselCombustionErrorCode::invalid_pulse, i};
        }
        for (std::size_t j = 0U; j < i; ++j) {
            if (pulses[j].id == p.id) {
                return DieselCombustionError{
                    DieselCombustionErrorCode::duplicate_pulse_id, i};
            }
        }
        auto &compiled = result.pulses_[i];
        compiled.start_s = p.start_time_s;
        compiled.end_s = p.start_time_s + p.duration_s;
        compiled.opening_end_s = p.start_time_s + p.duration_s * p.opening_fraction_01;
        compiled.closing_start_s =
            compiled.end_s - p.duration_s * p.closing_fraction_01;
        if (p.opening_fraction_01 + p.closing_fraction_01 == 1.0) {
            compiled.closing_start_s = compiled.opening_end_s;
        }
        if (!positive(compiled.end_s) || compiled.end_s <= compiled.start_s ||
            compiled.opening_end_s > compiled.closing_start_s ||
            (p.opening_fraction_01 > 0.0 &&
             compiled.opening_end_s <= compiled.start_s) ||
            (p.closing_fraction_01 > 0.0 &&
             compiled.closing_start_s >= compiled.end_s)) {
            return DieselCombustionError{
                DieselCombustionErrorCode::unrepresentable_pulse, i};
        }
        if (i > 0U && compiled.start_s < result.pulses_[i - 1U].end_s) {
            return DieselCombustionError{
                DieselCombustionErrorCode::overlapping_or_unordered_pulses, i};
        }
        // Normalize the actually represented knot intervals, so their integral
        // is the authored mass even when absolute start times round a duration.
        const double area_s = 0.5 * (compiled.opening_end_s - compiled.start_s) +
                              (compiled.closing_start_s - compiled.opening_end_s) +
                              0.5 * (compiled.end_s - compiled.closing_start_s);
        compiled.peak_mass_flow_kg_s = p.fuel_mass_kg / area_s;
        if (!positive(area_s) || !nonnegative(compiled.peak_mass_flow_kg_s) ||
            (p.fuel_mass_kg > 0.0 && compiled.peak_mass_flow_kg_s == 0.0) ||
            !std::isfinite(p.fuel_mass_kg * parameters.lower_heating_value_j_per_kg)) {
            return DieselCombustionError{
                DieselCombustionErrorCode::unrepresentable_pulse, i};
        }
    }
    return result;
}

const DieselCombustionState &DieselCombustionSession::state() const noexcept {
    return state_;
}

DieselCombustionAdvanceResult
DieselCombustionSession::advance_to(double end_time_s,
                                    const DieselCombustionConditions &conditions,
                                    bool fuel_enabled) noexcept {
    if (!positive(end_time_s) || end_time_s <= state_.time_s) {
        return DieselCombustionError{DieselCombustionErrorCode::invalid_end_time};
    }
    const auto delay_result = diesel_ignition_delay_s(parameters_, conditions);
    if (const auto *error = std::get_if<DieselCombustionError>(&delay_result)) {
        return *error;
    }
    const double delay_s = std::get<double>(delay_result);
    auto next = state_;
    DieselCombustionStep result;
    result.start_time_s = next.time_s;
    result.end_time_s = end_time_s;
    double oxygen_kg = conditions.oxygen_mass_kg;

    // At most four knots per pulse, one ignition split, and the final endpoint.
    constexpr std::size_t maximum_segments = 4U * kMaximumDieselInjectionPulses + 2U;
    for (std::size_t segment = 0U;
         segment < maximum_segments && next.time_s < end_time_s; ++segment) {
        const double begin_s = next.time_s;
        double stop_s = end_time_s;
        for (std::size_t i = 0U; i < pulse_count_; ++i) {
            const auto &p = pulses_[i];
            for (const double knot :
                 {p.start_s, p.opening_end_s, p.closing_start_s, p.end_s}) {
                if (knot > begin_s && knot < stop_s) {
                    stop_s = knot;
                }
            }
        }

        const Pulse *active = nullptr;
        if (fuel_enabled) {
            for (std::size_t i = 0U; i < pulse_count_; ++i) {
                const auto &p = pulses_[i];
                if (begin_s >= p.start_s && begin_s < p.end_s &&
                    p.peak_mass_flow_kg_s > 0.0) {
                    active = &p;
                    break;
                }
            }
        }

        const bool can_ignite =
            !next.ignition_time_s && std::isfinite(delay_s) &&
            (active != nullptr || next.unprepared_fuel_mass_kg > 0.0 ||
             next.premixed_fuel_mass_kg > 0.0);
        bool ignite_at_stop = false;
        if (can_ignite) {
            const double time_to_ignition = (1.0 - next.ignition_progress_01) * delay_s;
            if (!nonnegative(time_to_ignition)) {
                return numerical_failure();
            }
            if (time_to_ignition <= stop_s - begin_s) {
                const double ignition_s = begin_s + time_to_ignition;
                if (ignition_s == begin_s &&
                    1.0 - next.ignition_progress_01 <=
                        8.0 * std::numeric_limits<double>::epsilon()) {
                    // A threshold coincident with a hydraulic knee can leave
                    // a fraction of an ULP of progress. Its nearest represented
                    // timestamp is this boundary; do not create a zero step.
                    next.ignition_progress_01 = 1.0;
                    next.ignition_time_s = begin_s;
                    result.ignition_time_s = begin_s;
                } else {
                    if (ignition_s <= begin_s) {
                        return numerical_failure();
                    }
                    stop_s = ignition_s;
                    ignite_at_stop = true;
                }
            }
        }

        const double dt = stop_s - begin_s;
        if (!positive(dt)) {
            return numerical_failure();
        }
        double start_rate = 0.0;
        double end_rate = 0.0;
        if (active != nullptr) {
            const auto &p = *active;
            if (begin_s < p.opening_end_s) {
                const double opening_s = p.opening_end_s - p.start_s;
                start_rate =
                    p.peak_mass_flow_kg_s * ((begin_s - p.start_s) / opening_s);
                end_rate = p.peak_mass_flow_kg_s * ((stop_s - p.start_s) / opening_s);
            } else if (begin_s >= p.closing_start_s) {
                const double closing_s = p.end_s - p.closing_start_s;
                start_rate = p.peak_mass_flow_kg_s * ((p.end_s - begin_s) / closing_s);
                end_rate = p.peak_mass_flow_kg_s * ((p.end_s - stop_s) / closing_s);
            } else {
                start_rate = end_rate = p.peak_mass_flow_kg_s;
            }
        }
        const double injected = (0.5 * start_rate + 0.5 * end_rate) * dt;
        const double x = dt / parameters_.preparation_time_s;
        if (!nonnegative(injected) || !positive(x)) {
            return numerical_failure();
        }
        const auto w = preparation_weights(x);
        const double available = next.unprepared_fuel_mass_kg + injected;
        double unprepared = next.unprepared_fuel_mass_kg * w.decay +
                            (start_rate * w.start_rate + end_rate * w.end_rate) * dt;
        if (!bound_roundoff(unprepared, available)) {
            return numerical_failure();
        }
        const double prepared = available - unprepared;
        next.unprepared_fuel_mass_kg = unprepared;
        next.injected_fuel_mass_kg += injected;
        result.injected_fuel_mass_kg += injected;

        if (!next.ignition_time_s) {
            next.premixed_fuel_mass_kg += prepared;
            if (can_ignite) {
                if (ignite_at_stop) {
                    next.ignition_progress_01 = 1.0;
                    next.ignition_time_s = stop_s;
                    result.ignition_time_s = stop_s;
                } else {
                    next.ignition_progress_01 += dt / delay_s;
                    // Reaching the threshold through rounding still belongs to
                    // this endpoint, never to a zero-duration subsequent step.
                    if (next.ignition_progress_01 >= 1.0) {
                        next.ignition_progress_01 = 1.0;
                        next.ignition_time_s = stop_s;
                        result.ignition_time_s = stop_s;
                    }
                }
            }
        } else {
            const double burn_x = dt / parameters_.premixed_burn_time_s;
            if (!positive(burn_x)) {
                return numerical_failure();
            }
            const double requested_premixed =
                next.premixed_fuel_mass_kg * -std::expm1(-burn_x);
            next.diffusion_fuel_mass_kg += prepared;
            const double requested_diffusion = next.diffusion_fuel_mass_kg;
            const double requested = requested_premixed + requested_diffusion;
            const double oxygen_fuel_capacity =
                oxygen_kg / parameters_.stoichiometric_oxygen_kg_per_kg_fuel;
            if (!nonnegative(requested) || !nonnegative(oxygen_fuel_capacity)) {
                return numerical_failure();
            }
            const bool limited = requested > oxygen_fuel_capacity;
            const double fraction = limited ? oxygen_fuel_capacity / requested : 1.0;
            // Scale each pool independently. Computing diffusion as total minus
            // premixed can overdraw it by an ULP near the stoichiometric boundary.
            const double premixed = requested_premixed * fraction;
            const double diffusion = requested_diffusion * fraction;
            const double burned = premixed + diffusion;
            next.premixed_fuel_mass_kg -= premixed;
            next.diffusion_fuel_mass_kg -= diffusion;
            next.burned_fuel_mass_kg += burned;
            double consumed = burned * parameters_.stoichiometric_oxygen_kg_per_kg_fuel;
            if (!bound_roundoff(consumed, oxygen_kg)) {
                return numerical_failure();
            }
            oxygen_kg -= consumed;
            if (!bound_roundoff(oxygen_kg, conditions.oxygen_mass_kg)) {
                return numerical_failure();
            }
            result.premixed_burned_fuel_mass_kg += premixed;
            result.diffusion_burned_fuel_mass_kg += diffusion;
            result.heat_release_j += burned * parameters_.lower_heating_value_j_per_kg;
            result.oxygen_limited = result.oxygen_limited || limited;
        }
        next.time_s = stop_s;
        if (!valid_state(next) || !nonnegative(result.heat_release_j)) {
            return numerical_failure();
        }
    }
    if (next.time_s != end_time_s) {
        return numerical_failure();
    }
    // Publish the change in the bounded inventory, avoiding a separately rounded
    // reduction that could report more oxygen than the caller supplied.
    result.consumed_oxygen_mass_kg = conditions.oxygen_mass_kg - oxygen_kg;
    state_ = next;
    return result;
}

} // namespace crankwave::simulation
