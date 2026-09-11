#include "simulation/diesel_combustion_v1.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace crankwave::simulation;

void expect(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void near(double actual, double expected, double absolute_tolerance,
          const std::string &message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > absolute_tolerance) {
        throw std::runtime_error{message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual)};
    }
}

// Synthetic numerical coefficients, deliberately not a real engine calibration.
DieselCombustionParameters parameters() {
    return {42.5e6, 3.4, 0.0004, 0.00008, 0.0003, 800.0, 4.0e6, 0.23, 0.0, 0.0, 0.0};
}

DieselCombustionConditions conditions() {
    return {800.0, 4.0e6, 0.001, 0.00023};
}

DieselCombustionSession session(const DieselCombustionParameters &p,
                                std::span<const DieselInjectionPulse> pulses) {
    auto result = DieselCombustionSession::compile(p, pulses);
    if (const auto *error = std::get_if<DieselCombustionError>(&result)) {
        throw std::runtime_error{"fixture compile failed: " +
                                 std::to_string(static_cast<int>(error->code))};
    }
    return std::get<DieselCombustionSession>(std::move(result));
}

DieselCombustionStep advance(DieselCombustionSession &s, double end,
                             const DieselCombustionConditions &c = conditions(),
                             bool fuel_enabled = true) {
    auto result = s.advance_to(end, c, fuel_enabled);
    if (const auto *error = std::get_if<DieselCombustionError>(&result)) {
        throw std::runtime_error{"advance failed at " + std::to_string(end) + ": " +
                                 std::to_string(static_cast<int>(error->code))};
    }
    return std::get<DieselCombustionStep>(result);
}

void fuel_balance(const DieselCombustionState &s) {
    near(s.unprepared_fuel_mass_kg + s.premixed_fuel_mass_kg +
             s.diffusion_fuel_mass_kg + s.burned_fuel_mass_kg,
         s.injected_fuel_mass_kg, 1.0e-15, "fuel inventory must close");
}

void test_plan_admission() {
    const auto valid = parameters();
    using Field = double DieselCombustionParameters::*;
    constexpr std::array<Field, 11U> fields{
        &DieselCombustionParameters::lower_heating_value_j_per_kg,
        &DieselCombustionParameters::stoichiometric_oxygen_kg_per_kg_fuel,
        &DieselCombustionParameters::preparation_time_s,
        &DieselCombustionParameters::premixed_burn_time_s,
        &DieselCombustionParameters::reference_ignition_delay_s,
        &DieselCombustionParameters::reference_temperature_k,
        &DieselCombustionParameters::reference_pressure_pa_abs,
        &DieselCombustionParameters::reference_oxygen_mass_fraction,
        &DieselCombustionParameters::activation_temperature_k,
        &DieselCombustionParameters::pressure_exponent,
        &DieselCombustionParameters::oxygen_exponent,
    };
    for (const auto field : fields) {
        for (const double bad : {-1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            auto p = valid;
            p.*field = bad;
            const auto result = DieselCombustionSession::compile(p, {});
            expect(std::holds_alternative<DieselCombustionError>(result),
                   "invalid coefficient must fail compilation");
        }
    }
    for (std::size_t i = 0U; i < 8U; ++i) {
        auto p = valid;
        p.*fields[i] = 0.0;
        expect(std::holds_alternative<DieselCombustionError>(
                   DieselCombustionSession::compile(p, {})),
               "zero dimensional/reference coefficient must be rejected");
    }

    const DieselInjectionPulse pulse{1U, 0.001, 0.002, 30.0e-6, 0.1, 0.2};
    for (const auto field :
         {&DieselInjectionPulse::start_time_s, &DieselInjectionPulse::duration_s,
          &DieselInjectionPulse::fuel_mass_kg,
          &DieselInjectionPulse::opening_fraction_01,
          &DieselInjectionPulse::closing_fraction_01}) {
        for (const double bad : {-0.001, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            auto copy = pulse;
            copy.*field = bad;
            expect(std::holds_alternative<DieselCombustionError>(
                       DieselCombustionSession::compile(valid, std::span{&copy, 1U})),
                   "invalid pulse field must fail compilation");
        }
    }
    std::array<DieselInjectionPulse, 2U> pair{pulse, pulse};
    auto result = DieselCombustionSession::compile(valid, pair);
    expect(std::get<DieselCombustionError>(result).code ==
               DieselCombustionErrorCode::duplicate_pulse_id,
           "duplicate identities must fail");
    pair[1].id = 2U;
    result = DieselCombustionSession::compile(valid, pair);
    expect(std::get<DieselCombustionError>(result).code ==
               DieselCombustionErrorCode::overlapping_or_unordered_pulses,
           "overlapping delivery from one injector must fail");
    pair[1].start_time_s = 0.0005;
    expect(std::holds_alternative<DieselCombustionError>(
               DieselCombustionSession::compile(valid, pair)),
           "unsorted pulses must fail");
    pair[1].start_time_s = pulse.start_time_s + pulse.duration_s;
    expect(std::holds_alternative<DieselCombustionSession>(
               DieselCombustionSession::compile(valid, pair)),
           "adjacent pulses with zero hydraulic dwell are valid");

    auto copy = pulse;
    copy.opening_fraction_01 = 0.9;
    copy.closing_fraction_01 = 0.9;
    expect(std::holds_alternative<DieselCombustionError>(
               DieselCombustionSession::compile(valid, std::span{&copy, 1U})),
           "negative plateau must fail");
    copy = pulse;
    copy.start_time_s = 1.0e20;
    expect(std::get<DieselCombustionError>(
               DieselCombustionSession::compile(valid, std::span{&copy, 1U}))
                   .code == DieselCombustionErrorCode::unrepresentable_pulse,
           "rounded-away duration must fail");
    std::array<DieselInjectionPulse, kMaximumDieselInjectionPulses + 1U> too_many{};
    expect(std::get<DieselCombustionError>(
               DieselCombustionSession::compile(valid, too_many))
                   .code == DieselCombustionErrorCode::too_many_pulses,
           "bounded pulse capacity must be enforced");
}

void test_ignition_delay_dependencies() {
    auto p = parameters();
    auto c = conditions();
    expect(std::get<double>(diesel_ignition_delay_s(p, c)) ==
               p.reference_ignition_delay_s,
           "reference delay must be exact when dependencies are disabled");
    p.pressure_exponent = 0.5;
    c.pressure_pa_abs *= 4.0;
    near(std::get<double>(diesel_ignition_delay_s(p, c)),
         p.reference_ignition_delay_s / 2.0, 1.0e-18, "pressure delay dependence");
    c = conditions();
    p = parameters();
    p.oxygen_exponent = 1.0;
    c.oxygen_mass_kg /= 2.0;
    near(std::get<double>(diesel_ignition_delay_s(p, c)),
         p.reference_ignition_delay_s * 2.0, 1.0e-17, "dilution delay dependence");
    p = parameters();
    p.activation_temperature_k = 4000.0;
    c = conditions();
    c.temperature_k = 1000.0;
    near(std::get<double>(diesel_ignition_delay_s(p, c)),
         p.reference_ignition_delay_s / std::exp(1.0), 1.0e-18,
         "temperature delay dependence");
    c.oxygen_mass_kg = 0.0;
    expect(std::isinf(std::get<double>(diesel_ignition_delay_s(p, c))),
           "oxygen-free charge cannot ignite");
    c = conditions();
    c.temperature_k = 1.0;
    expect(std::holds_alternative<DieselCombustionError>(diesel_ignition_delay_s(p, c)),
           "overflowing calibration must fail explicitly");
}

void test_exact_delivery_and_preparation() {
    const DieselInjectionPulse pulse{1U, 0.001, 0.002, 40.0e-6, 0.0, 0.0};
    auto c = conditions();
    c.oxygen_mass_kg = 0.0;
    auto s = session(parameters(), std::span{&pulse, 1U});
    auto step = advance(s, 0.001, c);
    expect(step.injected_fuel_mass_kg == 0.0 && !s.state().ignition_time_s,
           "pulse start endpoint must not create instantaneous fuel");
    step = advance(s, 0.002, c);
    near(step.injected_fuel_mass_kg, 20.0e-6, 1.0e-19, "half rectangular pulse mass");
    advance(s, 0.003, c);
    const double rate = pulse.fuel_mass_kg / pulse.duration_s;
    const double tau = parameters().preparation_time_s;
    const double unprepared = rate * tau * (1.0 - std::exp(-pulse.duration_s / tau));
    near(s.state().injected_fuel_mass_kg, pulse.fuel_mass_kg, 1.0e-19,
         "full rectangular pulse mass");
    near(s.state().unprepared_fuel_mass_kg, unprepared, 1.0e-19,
         "first-order reservoir has an independent analytical solution");
    const auto tail = advance(s, 0.004, c);
    near(s.state().unprepared_fuel_mass_kg, unprepared * std::exp(-0.001 / tau),
         1.0e-19, "unprepared fuel continues mixing after end of injection");
    expect(tail.injected_fuel_mass_kg == 0.0 && tail.heat_release_j == 0.0,
           "zero oxygen permits preparation but no combustion");
    fuel_balance(s.state());
}

void test_trapezoid_triangle_and_small_steps() {
    auto c = conditions();
    c.oxygen_mass_kg = 0.0;
    const DieselInjectionPulse pulse{1U, 0.0, 0.004, 60.0e-6, 0.25, 0.25};
    auto s = session(parameters(), std::span{&pulse, 1U});
    auto step = advance(s, 0.0005, c);
    near(step.injected_fuel_mass_kg, 2.5e-6, 1.0e-20, "opening ramp half-area");
    advance(s, 0.001, c);
    near(s.state().injected_fuel_mass_kg, 10.0e-6, 1.0e-19, "opening ramp area");
    advance(s, 0.004, c);
    near(s.state().injected_fuel_mass_kg, pulse.fuel_mass_kg, 1.0e-19,
         "trapezoid must be normalized by area");

    for (const double opening : {0.0, 0.2, 0.5, 0.8, 1.0}) {
        const DieselInjectionPulse triangle{1U,      0.001,   0.004,
                                            10.0e-6, opening, 1.0 - opening};
        auto triangle_session = session(parameters(), std::span{&triangle, 1U});
        advance(triangle_session, 0.01, c);
        near(triangle_session.state().injected_fuel_mass_kg, triangle.fuel_mass_kg,
             1.0e-19, "triangular pulse mass including one-sided ramps");
    }
    auto slow_preparation = parameters();
    slow_preparation.preparation_time_s = 1.0;
    auto tiny = session(slow_preparation, std::span{&pulse, 1U});
    advance(tiny, 1.0e-9, c);
    fuel_balance(tiny.state());
    expect(tiny.state().unprepared_fuel_mass_kg >= 0.0 &&
               tiny.state().premixed_fuel_mass_kg >= 0.0,
           "tiny ramp interval must not create negative fuel through cancellation");
}

void test_ignition_and_conservation() {
    const auto p = parameters();
    const DieselInjectionPulse pulse{1U, 0.001, 0.001, 30.0e-6, 0.0, 0.0};
    auto s = session(p, std::span{&pulse, 1U});
    auto step = advance(s, 0.0012);
    expect(step.heat_release_j == 0.0 && !step.ignition_time_s,
           "fuel cannot release heat before ignition delay");
    step = advance(s, 0.012);
    expect(step.ignition_time_s.has_value(), "ignition onset must be observable");
    near(*step.ignition_time_s, 0.0013, 1.0e-18, "ignition split inside outer step");
    near(s.state().burned_fuel_mass_kg, pulse.fuel_mass_kg, 1.0e-15,
         "long hot tail should burn the delivered fuel");
    near(step.heat_release_j,
         s.state().burned_fuel_mass_kg * p.lower_heating_value_j_per_kg, 1.0e-10,
         "chemical energy must equal burned fuel times heating value");
    near(step.consumed_oxygen_mass_kg,
         s.state().burned_fuel_mass_kg * p.stoichiometric_oxygen_kg_per_kg_fuel,
         1.0e-18, "oxygen stoichiometry");
    expect(step.premixed_burned_fuel_mass_kg > 0.0 &&
               step.diffusion_burned_fuel_mass_kg > 0.0,
           "both premixed and mixing-controlled contributions must execute");
    fuel_balance(s.state());
    const auto more = advance(s, 0.013);
    expect(!more.ignition_time_s, "ignition event must not repeat");

    auto delayed = p;
    delayed.reference_ignition_delay_s = 0.003;
    auto late = session(delayed, std::span{&pulse, 1U});
    const auto late_step = advance(late, 0.005);
    near(*late_step.ignition_time_s, 0.004, 1.0e-18,
         "ignition may occur after hydraulic injection has ended");
}

void test_oxygen_limit_and_recovery() {
    const DieselInjectionPulse pulse{1U, 0.0, 0.001, 50.0e-6, 0.0, 0.0};
    auto s = session(parameters(), std::span{&pulse, 1U});
    auto c = conditions();
    const double fuel_capacity = 10.0e-6;
    c.oxygen_mass_kg =
        fuel_capacity * parameters().stoichiometric_oxygen_kg_per_kg_fuel;
    auto step = advance(s, 0.02, c);
    expect(step.oxygen_limited, "oxygen limit must be reported");
    near(s.state().burned_fuel_mass_kg, fuel_capacity, 1.0e-19,
         "cannot burn more fuel than oxygen permits");
    near(step.consumed_oxygen_mass_kg, c.oxygen_mass_kg, 1.0e-19,
         "available oxygen must be consumed exactly at the limit");
    fuel_balance(s.state());
    c.oxygen_mass_kg = 0.0;
    step = advance(s, 0.021, c);
    expect(step.heat_release_j == 0.0 && step.consumed_oxygen_mass_kg == 0.0,
           "already ignited charge cannot burn without oxygen");
    step = advance(s, 0.03, conditions());
    expect(!step.ignition_time_s && step.heat_release_j > 0.0,
           "retained prepared fuel can burn when the gas owner supplies oxygen");
    near(s.state().burned_fuel_mass_kg, pulse.fuel_mass_kg, 1.0e-15,
         "oxygen starvation must not discard unburned fuel");
    fuel_balance(s.state());

    // Regression: both prepared pools are almost fully consumed at a budget
    // one ULP below stoichiometric. Neither pool may be overdrawn by subtracting
    // an imprecise proportional remainder from the other pool's allocation.
    auto boundary = session(parameters(), std::span{&pulse, 1U});
    auto oxygen_free = conditions();
    oxygen_free.oxygen_mass_kg = 0.0;
    advance(boundary, 0.00075, oxygen_free);
    auto almost_stoichiometric = conditions();
    almost_stoichiometric.oxygen_mass_kg = std::nextafter(
        pulse.fuel_mass_kg * parameters().stoichiometric_oxygen_kg_per_kg_fuel, 0.0);
    const auto near_limit = advance(boundary, 0.05, almost_stoichiometric);
    expect(
        near_limit.oxygen_limited && boundary.state().premixed_fuel_mass_kg >= 0.0 &&
            boundary.state().diffusion_fuel_mass_kg >= 0.0 &&
            near_limit.consumed_oxygen_mass_kg <= almost_stoichiometric.oxygen_mass_kg,
        "near-stoichiometric burning must keep each fuel and oxygen pool nonnegative");
    fuel_balance(boundary.state());
}

void test_keyoff_and_no_fuel() {
    const DieselInjectionPulse pulse{1U, 0.001, 0.004, 40.0e-6, 0.0, 0.0};
    auto s = session(parameters(), std::span{&pulse, 1U});
    advance(s, 0.002);
    const double injected = s.state().injected_fuel_mass_kg;
    const auto cut = advance(s, 0.003, conditions(), false);
    expect(cut.injected_fuel_mass_kg == 0.0 && cut.heat_release_j > 0.0,
           "key-off prevents delivery while previously injected fuel burns");
    near(injected, 10.0e-6, 1.0e-19, "fuel delivered before cut");
    advance(s, 0.008);
    near(s.state().injected_fuel_mass_kg, 30.0e-6, 1.0e-19,
         "re-enable must not replay the skipped pulse portion");
    fuel_balance(s.state());

    auto disabled = session(parameters(), std::span{&pulse, 1U});
    const auto all_cut = advance(disabled, 0.01, conditions(), false);
    expect(all_cut.injected_fuel_mass_kg == 0.0 && all_cut.heat_release_j == 0.0 &&
               !disabled.state().ignition_time_s,
           "disabled fueling cannot create combustion");
    auto empty = session(parameters(), {});
    expect(advance(empty, 1.0).heat_release_j == 0.0 &&
               empty.state().ignition_progress_01 == 0.0,
           "no pulse train must remain an unlit empty inventory");
    auto zero_pulse = pulse;
    zero_pulse.fuel_mass_kg = 0.0;
    auto zero = session(parameters(), std::span{&zero_pulse, 1U});
    expect(advance(zero, 1.0).heat_release_j == 0.0 && !zero.state().ignition_time_s,
           "zero-mass pulse must not arm ignition");
}

void test_multi_pulse_and_partitioning() {
    const std::array<DieselInjectionPulse, 2U> pulses{
        DieselInjectionPulse{1U, 0.001, 0.0003, 3.0e-6, 0.2, 0.3},
        DieselInjectionPulse{2U, 0.0018, 0.001, 27.0e-6, 0.1, 0.1},
    };
    auto coarse = session(parameters(), pulses);
    const auto complete = advance(coarse, 0.01);
    auto fine = session(parameters(), pulses);
    double heat = 0.0;
    double oxygen = 0.0;
    std::size_t ignition_events = 0U;
    for (std::uint32_t i = 1U; i <= 1000U; ++i) {
        const auto part = advance(fine, static_cast<double>(i) / 100000.0);
        heat += part.heat_release_j;
        oxygen += part.consumed_oxygen_mass_kg;
        ignition_events += part.ignition_time_s.has_value() ? 1U : 0U;
        fuel_balance(fine.state());
    }
    near(fine.state().injected_fuel_mass_kg, 30.0e-6, 1.0e-18,
         "pilot and main must retain their combined delivered mass");
    expect(ignition_events == 1U, "main pulse must reuse the pilot's burning state");
    near(*fine.state().ignition_time_s, *coarse.state().ignition_time_s, 1.0e-17,
         "ignition timing under constant conditions must converge across partitions");
    near(heat, complete.heat_release_j, 1.0e-9, "constant-condition heat partitioning");
    near(oxygen, complete.consumed_oxygen_mass_kg, 1.0e-17, "oxygen partitioning");
    near(fine.state().unprepared_fuel_mass_kg, coarse.state().unprepared_fuel_mass_kg,
         1.0e-19, "analytical preparation must agree across pulse-knee partitions");

    auto repeated = session(parameters(), pulses);
    for (std::uint32_t block = 0U; block < 20U; ++block) {
        for (std::uint32_t frame = 1U; frame <= 50U; ++frame) {
            advance(repeated, static_cast<double>(block * 50U + frame) / 100000.0);
        }
    }
    expect(repeated.state() == fine.state(),
           "regrouping identical integration steps into callbacks must be exact");

    std::array<DieselInjectionPulse, kMaximumDieselInjectionPulses> maximum{};
    for (std::size_t i = 0U; i < maximum.size(); ++i) {
        maximum[i] = {static_cast<std::uint32_t>(i + 1U),
                      0.001 * static_cast<double>(i),
                      0.0008,
                      2.0e-6,
                      0.2,
                      0.3};
    }
    auto full = session(parameters(), maximum);
    advance(full, 0.02);
    near(full.state().injected_fuel_mass_kg, 16.0e-6, 1.0e-18,
         "all eight pulses and their knees must fit bounded stepping");
    fuel_balance(full.state());
}

void test_errors_are_transactional() {
    const DieselInjectionPulse pulse{1U, 0.001, 0.001, 30.0e-6, 0.0, 0.0};
    auto s = session(parameters(), std::span{&pulse, 1U});
    advance(s, 0.0011);
    const auto before = s.state();
    for (const double bad : {0.0, -1.0, 0.0011, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        expect(std::holds_alternative<DieselCombustionError>(
                   s.advance_to(bad, conditions())) &&
                   s.state() == before,
               "bad timeline endpoint must leave all state unchanged");
    }
    for (const auto field : {&DieselCombustionConditions::temperature_k,
                             &DieselCombustionConditions::pressure_pa_abs,
                             &DieselCombustionConditions::charge_mass_kg,
                             &DieselCombustionConditions::oxygen_mass_kg}) {
        for (const double bad : {-1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            auto c = conditions();
            c.*field = bad;
            expect(
                std::holds_alternative<DieselCombustionError>(s.advance_to(0.002, c)) &&
                    s.state() == before,
                "bad thermodynamic input must leave state unchanged");
        }
    }
    auto c = conditions();
    c.oxygen_mass_kg = c.charge_mass_kg * 2.0;
    expect(std::holds_alternative<DieselCombustionError>(s.advance_to(0.002, c)) &&
               s.state() == before,
           "oxygen cannot exceed total charge mass");

    // Fail after one valid hydraulic segment, to exercise rollback of intermediate
    // injected/prepared mass rather than only rejecting the initial arguments.
    auto fragile = parameters();
    fragile.preparation_time_s = 1.0e-308;
    const DieselInjectionPulse early{1U, 0.0, 1.0e-308, 1.0e-310, 0.0, 0.0};
    auto overflow = session(fragile, std::span{&early, 1U});
    const auto initial = overflow.state();
    const auto failed = overflow.advance_to(100.0, conditions());
    expect(std::holds_alternative<DieselCombustionError>(failed) &&
               overflow.state() == initial,
           "late arithmetic failure must roll back the entire advance");
}

struct CylinderSample {
    double angle_deg = 0.0;
    double pressure_pa = 0.0;
    double temperature_k = 0.0;
    double heat_release_j = 0.0;
    double injected_fuel_kg = 0.0;
};

struct CylinderRun {
    double peak_pressure_pa = 0.0;
    double heat_release_j = 0.0;
    double work_j = 0.0;
    double initial_energy_j = 0.0;
    double final_energy_j = 0.0;
    double injected_sensible_energy_j = 0.0;
    double ignition_angle_deg = 0.0;
    DieselCombustionState fuel;
    std::vector<CylinderSample> samples;
};

// Closed-valve compression/expansion verification cell. Ideal gas with fixed
// cp/cv and no wall heat transfer. This is not a four-stroke engine or an engine
// calibration. Splitting adiabatic volume work around heat addition gives an
// independent energy-accounting and pressure-feedback integration fixture.
CylinderRun run_closed_cylinder(std::uint32_t steps, double fuel_mass,
                                bool pilot_main = false) {
    constexpr double pi = std::numbers::pi;
    constexpr double rpm = 1500.0;
    constexpr double omega = rpm * 2.0 * pi / 60.0;
    constexpr double total_time = 60.0 / rpm;
    constexpr double swept_volume = pi * 0.09 * 0.09 / 4.0 * 0.1;
    constexpr double clearance_volume = swept_volume / 17.0;
    constexpr double gas_constant = 287.0;
    constexpr double gamma = 1.36;
    constexpr double cv = gas_constant / (gamma - 1.0);
    const auto volume = [](double angle) {
        constexpr double crank = 0.05;
        constexpr double rod = 0.16;
        const double sine = std::sin(angle);
        const double travel = crank * (1.0 - std::cos(angle)) + rod -
                              std::sqrt(rod * rod - crank * crank * sine * sine);
        return clearance_volume + swept_volume * travel / (2.0 * crank);
    };
    auto p = parameters();
    p.activation_temperature_k = 4000.0;
    p.pressure_exponent = 0.5;
    p.oxygen_exponent = 0.5;
    const double main_start = (pi - 10.0 * pi / 180.0) / omega;
    std::array<DieselInjectionPulse, 2U> pulses{};
    std::size_t pulse_count = 1U;
    pulses[0] = {1U, main_start, 0.0012, fuel_mass, 0.1, 0.2};
    if (pilot_main) {
        pulse_count = 2U;
        pulses[0] = {1U, main_start - 0.0012, 0.00025, fuel_mass * 0.1, 0.2, 0.2};
        pulses[1] = {2U, main_start, 0.0012, fuel_mass * 0.9, 0.1, 0.2};
    }
    auto combustion = session(p, std::span{pulses}.first(pulse_count));
    double current_volume = volume(-pi);
    double mass = 101325.0 * current_volume / (gas_constant * 350.0);
    double oxygen = mass * 0.23;
    double energy = mass * cv * 350.0;
    CylinderRun result;
    result.initial_energy_j = energy;
    for (std::uint32_t i = 1U; i <= steps; ++i) {
        const double begin = static_cast<double>(i - 1U) * total_time / steps;
        const double end = static_cast<double>(i) * total_time / steps;
        const double midpoint_volume = volume(-pi + omega * ((begin + end) * 0.5));
        const double end_volume = volume(-pi + omega * end);
        const double compressed_energy =
            energy * std::pow(current_volume / midpoint_volume, gamma - 1.0);
        result.work_j += energy - compressed_energy;
        energy = compressed_energy;
        const DieselCombustionConditions gas{energy / (mass * cv),
                                             (gamma - 1.0) * energy / midpoint_volume,
                                             mass, oxygen};
        const auto step = advance(combustion, end, gas);
        if (step.ignition_time_s) {
            result.ignition_angle_deg =
                (-pi + omega * *step.ignition_time_s) * 180.0 / pi;
        }
        const double fuel_sensible_energy = step.injected_fuel_mass_kg * cv * 350.0;
        mass += step.injected_fuel_mass_kg;
        oxygen -= step.consumed_oxygen_mass_kg;
        expect(oxygen >= 0.0, "combustion must not overdraw the gas owner's oxygen");
        energy += step.heat_release_j + fuel_sensible_energy;
        result.heat_release_j += step.heat_release_j;
        result.injected_sensible_energy_j += fuel_sensible_energy;
        const double expanded_energy =
            energy * std::pow(midpoint_volume / end_volume, gamma - 1.0);
        result.work_j += energy - expanded_energy;
        energy = expanded_energy;
        current_volume = end_volume;
        const double pressure = (gamma - 1.0) * energy / current_volume;
        result.peak_pressure_pa = std::max(result.peak_pressure_pa, pressure);
        result.samples.push_back({(-pi + omega * end) * 180.0 / pi, pressure,
                                  energy / (mass * cv), step.heat_release_j,
                                  combustion.state().injected_fuel_mass_kg});
    }
    result.final_energy_j = energy;
    result.fuel = combustion.state();
    return result;
}

void test_closed_cylinder_pressure_work_and_convergence() {
    const auto motoring = run_closed_cylinder(4000U, 0.0);
    near(motoring.peak_pressure_pa, 101325.0 * std::pow(18.0, 1.36), 1.0e-6,
         "motored compression must match the independent adiabatic relation");
    near(motoring.work_j, 0.0, 1.0e-8, "adiabatic motoring returns compression work");
    expect(motoring.heat_release_j == 0.0 && !motoring.fuel.ignition_time_s,
           "motoring must produce pressure without combustion");
    const auto light = run_closed_cylinder(4000U, 10.0e-6);
    const auto heavy = run_closed_cylinder(4000U, 30.0e-6);
    expect(heavy.work_j > light.work_j && light.work_j > 0.0 &&
               heavy.peak_pressure_pa > light.peak_pressure_pa,
           "at identical trapped air and speed, fueling must change pressure and work");
    for (const auto *run : {&light, &heavy}) {
        near(run->final_energy_j - run->initial_energy_j,
             run->heat_release_j + run->injected_sensible_energy_j - run->work_j,
             1.0e-8, "closed-cylinder first-law energy balance");
        near(run->heat_release_j,
             run->fuel.burned_fuel_mass_kg * parameters().lower_heating_value_j_per_kg,
             1.0e-8, "gas heat addition must match burned-fuel energy");
        fuel_balance(run->fuel);
    }
    const auto coarse = run_closed_cylinder(800U, 30.0e-6);
    const auto medium = run_closed_cylinder(1600U, 30.0e-6);
    const auto fine = run_closed_cylinder(6400U, 30.0e-6);
    expect(std::abs(medium.work_j - fine.work_j) <
               std::abs(coarse.work_j - fine.work_j),
           "work should converge as the coupled gas integration is refined");
    near(heavy.ignition_angle_deg, fine.ignition_angle_deg, 0.05,
         "ignition angle must converge, independently of mean work");
    near(heavy.peak_pressure_pa, fine.peak_pressure_pa, fine.peak_pressure_pa * 0.005,
         "peak pressure must converge under refinement");
    const auto pilot = run_closed_cylinder(4000U, 30.0e-6, true);
    near(pilot.fuel.injected_fuel_mass_kg, heavy.fuel.injected_fuel_mass_kg, 1.0e-17,
         "pilot/main comparison must keep total delivered fuel fixed");
    expect(pilot.fuel.ignition_time_s < heavy.fuel.ignition_time_s &&
               pilot.peak_pressure_pa != heavy.peak_pressure_pa,
           "pilot heat must couple through the same chamber state");
}

void write_probe(const std::string &path) {
    const auto motoring = run_closed_cylinder(4000U, 0.0);
    const auto light = run_closed_cylinder(4000U, 10.0e-6);
    const auto heavy = run_closed_cylinder(4000U, 30.0e-6);
    const auto pilot = run_closed_cylinder(4000U, 30.0e-6, true);
    std::ofstream output{path};
    expect(output.good(), "cannot create requested probe CSV");
    output << "angle_deg,motoring_pressure_pa,light_pressure_pa,heavy_pressure_pa,"
              "pilot_main_pressure_pa,heavy_temperature_k,heavy_heat_release_j,"
              "heavy_injected_fuel_kg\n";
    output << std::setprecision(17);
    for (std::size_t i = 0U; i < heavy.samples.size(); ++i) {
        const auto &h = heavy.samples[i];
        output << h.angle_deg << ',' << motoring.samples[i].pressure_pa << ','
               << light.samples[i].pressure_pa << ',' << h.pressure_pa << ','
               << pilot.samples[i].pressure_pa << ',' << h.temperature_k << ','
               << h.heat_release_j << ',' << h.injected_fuel_kg << '\n';
    }
    output.close();
    expect(output.good(), "probe CSV could not be completely written");
    std::cout << "Synthetic closed-cylinder probe: " << path << '\n'
              << "Mechanical heavy: ignition " << heavy.ignition_angle_deg
              << " deg ATDC, peak " << heavy.peak_pressure_pa / 1.0e5
              << " bar, closed-cycle work " << heavy.work_j << " J\n";
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 1 && argc != 2) {
            throw std::runtime_error{
                "usage: crankwave_diesel_combustion_v1_tests [probe.csv]"};
        }
        using Test = std::pair<const char *, void (*)()>;
        const std::array tests{
            Test{"plan admission", test_plan_admission},
            Test{"ignition delay", test_ignition_delay_dependencies},
            Test{"delivery and preparation", test_exact_delivery_and_preparation},
            Test{"pulse shapes", test_trapezoid_triangle_and_small_steps},
            Test{"ignition and conservation", test_ignition_and_conservation},
            Test{"oxygen limit", test_oxygen_limit_and_recovery},
            Test{"key-off", test_keyoff_and_no_fuel},
            Test{"multiple pulses", test_multi_pulse_and_partitioning},
            Test{"transactional errors", test_errors_are_transactional},
            Test{"coupled cylinder",
                 test_closed_cylinder_pressure_work_and_convergence},
        };
        for (const auto &[name, test] : tests) {
            try {
                test();
            } catch (const std::exception &error) {
                throw std::runtime_error{std::string{name} + ": " + error.what()};
            }
        }
        if (argc == 2) {
            write_probe(argv[1]);
        }
        std::cout << "diesel combustion v1 tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "diesel combustion v1 tests failed: " << error.what() << '\n';
        return 1;
    }
}
