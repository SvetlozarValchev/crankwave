#include "simulation/diesel_cylinder_v1.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crankwave::simulation;
constexpr double pi = std::numbers::pi;
constexpr double degree = pi / 180.0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}
void near(double actual, double expected, double tolerance,
          const std::string &message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual)};
    }
}

// Synthetic numerical configuration; no real engine or injector calibration.
DieselCylinderParameters parameters() {
    DieselCylinderParameters p;
    p.bore_m = 0.09;
    p.stroke_m = 0.1;
    p.connecting_rod_length_m = 0.16;
    p.compression_ratio = 18.0;
    p.held_speed_rpm = 1500.0;
    p.gas_constant_j_per_kg_k = 287.0;
    p.specific_heat_ratio = 1.36;
    p.injected_fuel_sensible_energy_j_per_kg = 287.0 / 0.36 * 350.0;
    p.wall_temperature_k = 450.0;
    p.wall_conductance_w_per_k = 5.0;
    p.crankcase_pressure_pa_abs = 101325.0;
    p.initial_charge = {101325.0, 350.0, 0.23};
    p.intake = {101325.0, 320.0, 0.23};
    p.exhaust = {105000.0, 700.0, 0.08};
    p.exhaust_valve = {150.0 * degree, 360.0 * degree, 0.00018};
    p.intake_valve = {360.0 * degree, 540.0 * degree, 0.00015};
    p.combustion = {42.5e6, 3.4,  0.0004, 0.00008, 0.0003, 800.0,
                    4.0e6,  0.23, 4000.0, 0.5,     0.5};
    return p;
}

std::array<DieselCrankInjectionPulse, 1U> main_pulse(double mass = 30.0e-6) {
    return {{{1U, -10.0 * degree, 10.8 * degree, mass, 0.1, 0.2}}};
}

DieselCylinderSession session(const DieselCylinderParameters &p,
                              std::span<const DieselCrankInjectionPulse> pulses,
                              DieselCylinderControl control = {}) {
    auto result = DieselCylinderSession::compile(p, pulses, control.fueling_01);
    if (const auto *error = std::get_if<DieselCylinderError>(&result)) {
        throw std::runtime_error{"compile failed: " +
                                 std::to_string(static_cast<int>(error->code))};
    }
    return std::get<DieselCylinderSession>(result);
}

DieselCylinderStep advance(DieselCylinderSession &s,
                           DieselCylinderControl control = {}) {
    const auto result = s.advance(control);
    if (const auto *error = std::get_if<DieselCylinderError>(&result)) {
        throw std::runtime_error{
            "advance failed at frame " + std::to_string(s.state().frame_index) +
            " time " + std::to_string(s.state().time_s) + ": " +
            std::to_string(static_cast<int>(error->code)) +
            (error->combustion ? " combustion " + std::to_string(static_cast<int>(
                                                      error->combustion->code))
                               : "")};
    }
    return std::get<DieselCylinderStep>(result);
}

void balances(const DieselCylinderState &s, const DieselCylinderState &initial,
              const DieselCylinderParameters &p) {
    const auto &a = s.total;
    near(s.charge_mass_kg,
         initial.charge_mass_kg + a.intake.mass_into_cylinder_kg +
             a.exhaust.mass_into_cylinder_kg + a.injected_fuel_mass_kg,
         2.0e-14, "charge mass ledger");
    near(s.oxygen_mass_kg,
         initial.oxygen_mass_kg + a.intake.oxygen_into_cylinder_kg +
             a.exhaust.oxygen_into_cylinder_kg - a.consumed_oxygen_mass_kg,
         2.0e-14, "oxygen ledger");
    near(s.internal_energy_j,
         initial.internal_energy_j + a.intake.enthalpy_into_cylinder_j +
             a.exhaust.enthalpy_into_cylinder_j + a.fuel_sensible_energy_j +
             a.heat_release_j - a.wall_heat_out_j - a.volume_work_out_j,
         2.0e-7, "first-law energy ledger");
    near(a.injected_fuel_mass_kg,
         a.burned_fuel_mass_kg + s.reacting_fuel_mass_kg + s.unburned_tracer_mass_kg +
             a.intake.unburned_fuel_out_kg + a.exhaust.unburned_fuel_out_kg,
         2.0e-14, "fuel survives charge boundaries and reverse flow");
    near(a.heat_release_j,
         a.burned_fuel_mass_kg * p.combustion.lower_heating_value_j_per_kg, 1.0e-7,
         "heat has one chemical source");
    expect(s.oxygen_mass_kg >= 0.0 && s.charge_mass_kg > 0.0 &&
               s.pressure_pa_abs > 0.0 && s.temperature_k > 0.0 &&
               s.unburned_tracer_mass_kg >= 0.0,
           "positive thermodynamic state");
}

std::vector<DieselCylinderCycle> run(DieselCylinderSession &s, std::size_t count,
                                     const DieselCylinderParameters &p,
                                     DieselCylinderControl control = {}) {
    const auto initial = s.state();
    std::vector<DieselCylinderCycle> cycles;
    while (cycles.size() < count) {
        auto step = advance(s, control);
        if (initial.frame_index == 0U) {
            balances(s.state(), initial, p);
        }
        if (step.completed_cycle) {
            cycles.push_back(*step.completed_cycle);
        }
    }
    return cycles;
}

void test_admission() {
    using Field = double DieselCylinderParameters::*;
    const std::array fields{&DieselCylinderParameters::bore_m,
                            &DieselCylinderParameters::stroke_m,
                            &DieselCylinderParameters::connecting_rod_length_m,
                            &DieselCylinderParameters::compression_ratio,
                            &DieselCylinderParameters::held_speed_rpm,
                            &DieselCylinderParameters::gas_constant_j_per_kg_k,
                            &DieselCylinderParameters::specific_heat_ratio,
                            &DieselCylinderParameters::wall_temperature_k,
                            &DieselCylinderParameters::crankcase_pressure_pa_abs};
    for (const Field field : fields) {
        for (const double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            auto p = parameters();
            p.*field = bad;
            expect(std::holds_alternative<DieselCylinderError>(
                       DieselCylinderSession::compile(p, main_pulse())),
                   "bad coefficient rejected");
        }
    }
    for (int change = 0; change < 9; ++change) {
        auto p = parameters();
        switch (change) {
        case 0:
            p.connecting_rod_length_m = p.stroke_m / 2.0;
            break;
        case 1:
            p.intake.oxygen_mass_fraction = 1.1;
            break;
        case 2:
            p.exhaust_valve.close_angle_atdc_rad =
                p.intake_valve.close_angle_atdc_rad;
            break;
        case 3:
            p.gas_substeps = 0U;
            break;
        case 4:
            p.gas_substeps = 65U;
            break;
        case 5:
            p.physics_rate_hz = 0U;
            break;
        case 6:
            p.physics_rate_hz = 1U;
            break;
        case 7:
            p.wall_conductance_w_per_k = -1.0;
            break;
        case 8:
            p.combustion.preparation_time_s = 0.0;
            break;
        }
        expect(std::holds_alternative<DieselCylinderError>(
                   DieselCylinderSession::compile(p, main_pulse())),
               "unsupported or invalid configuration rejected");
    }
    for (int change = 0; change < 8; ++change) {
        auto pulse = main_pulse();
        switch (change) {
        case 0:
            pulse[0].start_angle_atdc_rad = -181.0 * degree;
            break;
        case 1:
            pulse[0].duration_rad = 200.0 * degree;
            break;
        case 2:
            pulse[0].full_fuel_mass_kg = -1.0;
            break;
        case 3:
            pulse[0].opening_fraction_01 = 1.1;
            break;
        case 4:
            pulse[0].id = 0U;
            break;
        case 5:
            pulse[0].minimum_duration_fraction_01 = 0;
            break;
        case 6:
            pulse[0].minimum_duration_fraction_01 = 1.01;
            break;
        case 7:
            pulse[0].minimum_duration_fraction_01 =
                std::numeric_limits<double>::quiet_NaN();
            break;
        }
        expect(std::holds_alternative<DieselCylinderError>(
                   DieselCylinderSession::compile(parameters(), pulse, 0.0)),
               "full injection plan checked even with zero requested fuel");
    }
    auto s = session(parameters(), main_pulse());
    const auto before = s.state();
    expect(std::holds_alternative<DieselCylinderError>(s.advance({-0.1, true})),
           "bad control rejected");
    expect(s.state() == before, "invalid control leaves state untouched");
}

void test_closed_motoring() {
    auto p = parameters();
    p.intake_valve.maximum_effective_area_m2 = 0.0;
    p.exhaust_valve.maximum_effective_area_m2 = 0.0;
    p.wall_conductance_w_per_k = 0.0;
    auto s = session(p, {});
    const auto initial = s.state();
    std::size_t cycles = 0U;
    while (cycles < 3U) {
        const auto step = advance(s);
        balances(s.state(), initial, p);
        near(s.state().pressure_pa_abs,
             initial.pressure_pa_abs * std::pow(initial.volume_m3 / s.state().volume_m3,
                                                p.specific_heat_ratio),
             2.0e-5, "motoring follows independent pV^gamma solution");
        near(s.state().time_s,
             static_cast<double>(s.state().frame_index) / p.physics_rate_hz, 1.0e-16,
             "publication time from integer index");
        if (step.completed_cycle) {
            near(step.completed_cycle->accounting.volume_work_out_j, 0.0, 1.0e-7,
                 "closed motoring cycle work");
            expect(!step.completed_cycle->ignition_angle_atdc_rad,
                   "motoring does not ignite");
            ++cycles;
        }
    }
}

void test_nozzle_and_reverse_transport() {
    auto p = parameters();
    p.gas_substeps = 1U;
    p.wall_conductance_w_per_k = 0.0;
    p.exhaust.pressure_pa_abs = 1000.0;
    p.exhaust_valve.maximum_effective_area_m2 = 1.0e-8;
    auto a = session(p, {});
    auto lower = p;
    lower.exhaust.pressure_pa_abs = 100.0;
    auto b = session(lower, {});
    while (a.state().unwrapped_angle_rad < 200.0 * degree) {
        advance(a);
        advance(b);
    }
    const auto before = a.state();
    const auto step = advance(a);
    const auto lower_step = advance(b);
    near(step.accounting.exhaust.mass_into_cylinder_kg,
         lower_step.accounting.exhaust.mass_into_cylinder_kg, 0.0,
         "choked discharge is independent of further downstream pressure reduction");

    const double dt = 1.0 / p.physics_rate_hz;
    const double angle =
        before.unwrapped_angle_rad + p.held_speed_rpm * (2.0 * pi / 60.0) * dt / 2.0;
    const double r = p.stroke_m / 2.0;
    const double l = p.connecting_rod_length_m;
    const double area = pi * p.bore_m * p.bore_m / 4.0;
    const double swept = area * p.stroke_m;
    // Independent conventional slider-crank expression and Mach-one flow law.
    const double volume =
        swept / (p.compression_ratio - 1.0) +
        area * (r + l - r * std::cos(angle) -
                std::sqrt(l * l - r * r * std::sin(angle) * std::sin(angle)));
    const double temperature =
        before.temperature_k *
        std::pow(before.volume_m3 / volume, p.specific_heat_ratio - 1.0);
    const double pressure = before.pressure_pa_abs *
                            std::pow(before.volume_m3 / volume, p.specific_heat_ratio);
    const double opening = std::sin(
        pi * (angle - p.exhaust_valve.open_angle_atdc_rad) /
        (p.exhaust_valve.close_angle_atdc_rad - p.exhaust_valve.open_angle_atdc_rad));
    const double throat = p.exhaust_valve.maximum_effective_area_m2 * opening * opening;
    const double mass = dt * throat * pressure / std::sqrt(temperature) *
                        std::sqrt(p.specific_heat_ratio / p.gas_constant_j_per_kg_k) *
                        std::pow((p.specific_heat_ratio + 1.0) / 2.0,
                                 -(p.specific_heat_ratio + 1.0) /
                                     (2.0 * (p.specific_heat_ratio - 1.0)));
    near(-step.accounting.exhaust.mass_into_cylinder_kg, mass, mass * 1.0e-10,
         "discharge matches independent sonic nozzle solution");
    const double expected_enthalpy = mass * p.gas_constant_j_per_kg_k *
                                     p.specific_heat_ratio /
                                     (p.specific_heat_ratio - 1.0) * temperature;
    near(-step.accounting.exhaust.enthalpy_into_cylinder_j, expected_enthalpy,
         expected_enthalpy * 1.0e-10, "exiting gas carries cp*T enthalpy");

    // A closed exhaust traps a starved, fuel-bearing charge, which reverses
    // through the intake at its opening. Check tracer transport on that edge.
    auto reverse = parameters();
    reverse.exhaust_valve.maximum_effective_area_m2 = 0.0;
    reverse.initial_charge.oxygen_mass_fraction = 0.0;
    reverse.intake.oxygen_mass_fraction = 0.0;
    auto s = session(reverse, main_pulse());
    const auto initial = s.state();
    bool intake_reverse = false;
    bool intake_forward = false;
    while (s.state().cycle_index < 2U) {
        const auto transfer = advance(s).accounting.intake;
        intake_reverse = intake_reverse || transfer.mass_into_cylinder_kg < 0.0;
        intake_forward = intake_forward || transfer.mass_into_cylinder_kg > 0.0;
        balances(s.state(), initial, reverse);
    }
    expect(intake_reverse && intake_forward, "intake supports both flow directions");
    expect(s.state().total.intake.unburned_fuel_out_kg > 0.0,
           "reverse intake flow exports unburned fuel instead of deleting it");
}

void test_repeated_firing_and_load() {
    const auto p = parameters();
    auto light = session(p, main_pulse(), {1.0 / 3.0, true});
    auto heavy = session(p, main_pulse());
    const auto low = run(light, 10U, p, {1.0 / 3.0, true});
    const auto high = run(heavy, 10U, p);
    for (const auto &cycle : high) {
        near(cycle.accounting.injected_fuel_mass_kg, 30.0e-6, 1.0e-15,
             "one main pulse each cycle");
        expect(cycle.ignition_angle_atdc_rad.has_value(),
               "each charge independently autoignites");
        expect(*cycle.ignition_angle_atdc_rad > -10.0 * degree,
               "ignition follows hydraulic SOI");
        expect(cycle.accounting.intake.mass_into_cylinder_kg > 0.0 &&
                   cycle.accounting.exhaust.mass_into_cylinder_kg < 0.0,
               "four-stroke gas exchange");
    }
    expect(high.back().peak_pressure_pa_abs > low.back().peak_pressure_pa_abs * 1.2,
           "fuel command changes pressure at identical intake boundary pressure");
    expect(high.back().accounting.volume_work_out_j >
               low.back().accounting.volume_work_out_j * 1.5,
           "fuel command changes indicated work");
    near(high[9].accounting.volume_work_out_j, high[8].accounting.volume_work_out_j,
         0.05, "warm repeated cycle settles");
    expect(heavy.state().oxygen_mass_kg / heavy.state().charge_mass_kg <
               p.intake.oxygen_mass_fraction,
           "intake closure retains exhaust instead of resetting to fresh air");
    std::cout << "synthetic steady 1500 RPM: light work="
              << low.back().accounting.volume_work_out_j
              << " J, heavy work=" << high.back().accounting.volume_work_out_j
              << " J, heavy peak=" << high.back().peak_pressure_pa_abs / 1.0e5
              << " bar\n";
}

void test_starved_fuel_carryover() {
    auto p = parameters();
    p.initial_charge.oxygen_mass_fraction = 0.0;
    p.intake.oxygen_mass_fraction = 0.0;
    p.exhaust.oxygen_mass_fraction = 0.0;
    auto s = session(p, main_pulse());
    const auto cycles = run(s, 5U, p);
    expect(s.state().total.heat_release_j == 0.0, "no oxygen means no chemical heat");
    expect(s.state().unburned_tracer_mass_kg > 0.0, "unburned carryover retained");
    expect(s.state().total.exhaust.unburned_fuel_out_kg > 0.0,
           "unburned fuel leaves with exhaust");
    for (const auto &cycle : cycles) {
        expect(!cycle.ignition_angle_atdc_rad, "no artificial ignition at next charge");
    }
}

void test_cut_and_cycle_control() {
    const auto p = parameters();
    auto s = session(p, main_pulse());
    while (s.state().total.injected_fuel_mass_kg < 8.0e-6) {
        advance(s);
    }
    const double before_cut = s.state().total.injected_fuel_mass_kg;
    const double heat_before_cut = s.state().total.heat_release_j;
    while (s.state().time_s < 0.03) {
        advance(s, {1.0, false});
    }
    near(s.state().total.injected_fuel_mass_kg, before_cut, 0.0,
         "cut immediately stops delivery");
    expect(s.state().total.heat_release_j > heat_before_cut,
           "cut permits already delivered fuel to burn");
    while (s.state().cycle_index == 0U) {
        advance(s, {0.25, true});
    }
    near(s.state().total.injected_fuel_mass_kg, before_cut, 0.0,
         "restoring fuel does not replay pulse");
    std::optional<DieselCylinderCycle> second;
    while (!second) {
        second = advance(s, {0.9, true}).completed_cycle;
    }
    near(second->accounting.injected_fuel_mass_kg, 7.5e-6, 1.0e-15,
         "train mass sampled at previous IVC; mid-cycle command deferred");
}

void test_metered_delivery() {
    const auto p = parameters();
    auto metered_pulse = main_pulse();
    metered_pulse[0].minimum_duration_fraction_01 = 0.08;
    auto metered = session(p, metered_pulse, {0.12, true});
    auto fixed = session(p, main_pulse(), {0.12, true});
    // At -7 degrees the short idle delivery has ended; fixed duration has not.
    while (metered.state().unwrapped_angle_rad < -7 * degree) {
        advance(metered, {0.12, true});
        advance(fixed, {0.12, true});
    }
    near(metered.state().total.injected_fuel_mass_kg, 3.6e-6, 1e-14,
         "metering delivers the complete commanded mass early");
    expect(fixed.state().total.injected_fuel_mass_kg < 1.8e-6,
           "fixed-duration fixture still spreads fuel across the window");
    const auto cycles = run(metered, 3, p, {0.12, true});
    for (const auto &cycle : cycles)
        near(cycle.accounting.injected_fuel_mass_kg, 3.6e-6, 1e-14,
             "short pulses conserve delivered mass on repeated cycles");
    // New rack quantity latches at the next IVC, rebuilding the knot schedule.
    const auto full = run(metered, 3, p, {1.0, true});
    near(full.back().accounting.injected_fuel_mass_kg, 30e-6, 1e-14,
         "changed quantity restores full duration and mass");
    auto a = session(p, metered_pulse);
    auto b = session(p, main_pulse());
    for (int i = 0; i < 4000; ++i) {
        advance(a);
        advance(b);
        expect(a.state() == b.state(), "full-fuel metering preserves original physics");
    }
}

void test_multiple_pulses_and_nonintegral_clock() {
    auto p = parameters();
    p.held_speed_rpm = 1379.0;
    const std::array<DieselCrankInjectionPulse, 2U> pulses{
        {{1U, -22.0 * degree, 2.0 * degree, 3.0e-6, 0.2, 0.8},
         {2U, -10.0 * degree, 10.8 * degree, 27.0e-6, 0.1, 0.2}}};
    auto s = session(p, pulses);
    const auto cycles = run(s, 40U, p);
    for (std::size_t i = 0U; i < cycles.size(); ++i) {
        expect(cycles[i].index == i, "no duplicate or missed charge crossing");
        near(cycles[i].accounting.injected_fuel_mass_kg, 30.0e-6, 2.0e-15,
             "pilot and main repeat across nonintegral frame/cycle boundaries");
    }
    auto a = session(p, pulses);
    auto b = a;
    for (int block = 0; block < 60; ++block) {
        for (int i = 0; i < 137; ++i) {
            advance(a);
        }
    }
    for (int i = 0; i < 8220; ++i) {
        advance(b);
    }
    expect(a.state() == b.state(), "callback grouping preserves exact state");
}

void test_convergence_and_rollback() {
    const auto p = parameters();
    std::array<DieselCylinderCycle, 3U> final;
    for (std::size_t i = 0U; i < final.size(); ++i) {
        auto refined = p;
        refined.gas_substeps = 4U << i;
        auto s = session(refined, main_pulse());
        final[i] = run(s, 6U, refined).back();
    }
    const double coarse_error = std::abs(final[0].accounting.volume_work_out_j -
                                         final[2].accounting.volume_work_out_j);
    const double middle_error = std::abs(final[1].accounting.volume_work_out_j -
                                         final[2].accounting.volume_work_out_j);
    expect(middle_error < coarse_error * 0.7, "work converges with finer gas grid");
    near(final[1].accounting.volume_work_out_j, final[2].accounting.volume_work_out_j,
         std::abs(final[2].accounting.volume_work_out_j) * 0.003,
         "default work convergence");
    near(final[1].peak_pressure_pa_abs, final[2].peak_pressure_pa_abs,
         final[2].peak_pressure_pa_abs * 0.003, "default peak pressure convergence");
    near(*final[1].ignition_angle_atdc_rad, *final[2].ignition_angle_atdc_rad,
         0.03 * degree, "default ignition-angle convergence");
    std::cout << "default vs refined: work delta=" << middle_error << " J, peak delta="
              << std::abs(final[1].peak_pressure_pa_abs -
                          final[2].peak_pressure_pa_abs) /
                     1.0e5
              << " bar, ignition delta="
              << std::abs(*final[1].ignition_angle_atdc_rad -
                          *final[2].ignition_angle_atdc_rad) /
                     degree
              << " deg\n";

    auto stiff = p;
    stiff.exhaust_valve.maximum_effective_area_m2 = 10.0;
    auto broken = session(stiff, main_pulse());
    bool failed = false;
    for (int i = 0; i < 1600; ++i) {
        const auto before = broken.state();
        auto peer = broken;
        const auto result = broken.advance();
        if (const auto *error = std::get_if<DieselCylinderError>(&result)) {
            expect(error->code == DieselCylinderErrorCode::integration_step_too_large,
                   "underresolved flow gives explicit diagnostic");
            expect(broken.state() == before, "late numerical failure rolls back frame");
            expect(std::holds_alternative<DieselCylinderError>(peer.advance()),
                   "retry is reproducible");
            expect(broken.state() == peer.state(),
                   "rollback preserves internal future behavior");
            failed = true;
            break;
        }
    }
    expect(failed, "stiff fixture exercises rollback");
}

void test_valve_overlap() {
    auto p = parameters();
    p.bore_m = 0.13;
    p.stroke_m = 0.14;
    p.connecting_rod_length_m = 0.26;
    p.compression_ratio = 16.5;
    p.intake_valve = {340.0 * degree, 596.0 * degree, 0.0014};
    p.exhaust_valve = {124.0 * degree, 380.0 * degree, 0.0012};
    p.intake.pressure_pa_abs = 120000.0;
    auto pulses = main_pulse(83e-6);
    std::array<DieselCylinderCycle, 3U> final;
    for (std::size_t refinement = 0; refinement < final.size(); ++refinement) {
        p.gas_substeps = 8U << refinement;
        auto s = session(p, pulses);
        const auto initial = s.state();
        bool flow_through = false;
        while (s.state().cycle_index < 8U) {
            const auto step = advance(s);
            balances(s.state(), initial, p);
            // With the intake reservoir above exhaust pressure, both ports
            // must transport gas in the same frame during overlap.
            if (s.state().cycle_index >= 4U &&
                step.accounting.intake.mass_into_cylinder_kg > 0.0 &&
                step.accounting.exhaust.mass_into_cylinder_kg < 0.0)
                flow_through = true;
            if (step.completed_cycle)
                final[refinement] = *step.completed_cycle;
        }
        expect(flow_through, "overlapping ports permit simultaneous flow through");
    }
    const auto &fine = final.back();
    const double coarse_error = std::abs(final[0].accounting.volume_work_out_j -
                                         fine.accounting.volume_work_out_j);
    const double middle_error = std::abs(final[1].accounting.volume_work_out_j -
                                         fine.accounting.volume_work_out_j);
    expect(middle_error < coarse_error * 0.7, "overlap work converges on refinement");
    near(final[1].accounting.volume_work_out_j, fine.accounting.volume_work_out_j,
         std::abs(fine.accounting.volume_work_out_j) * 0.003,
         "overlap work is resolved");
    near(final[1].end_charge_mass_kg, fine.end_charge_mass_kg,
         fine.end_charge_mass_kg * 0.003, "overlap charge is resolved at exact IVC");
    near(final[1].peak_pressure_pa_abs, fine.peak_pressure_pa_abs,
         fine.peak_pressure_pa_abs * 0.003, "overlap peak pressure is resolved");
}

void write_trace(const std::string &path) {
    std::ofstream output{path};
    expect(output.good(), "open cycle diagnostic CSV");
    output << "case,time_s,cycle,angle_atdc_deg,pressure_bar,temperature_k,oxygen_kg,"
              "unburned_kg,"
              "injected_kg,heat_j,work_j,intake_mass_into_kg,exhaust_mass_into_kg\n"
           << std::setprecision(17);
    const auto p = parameters();
    for (const double load : {0.0, 1.0 / 3.0, 1.0}) {
        auto s = session(p, main_pulse(), {load, true});
        while (s.state().cycle_index < 10U) {
            const auto step = advance(s, {load, true});
            const auto &state = s.state();
            output << load << ',' << state.time_s << ',' << state.cycle_index << ','
                   << (state.unwrapped_angle_rad -
                       static_cast<double>(state.cycle_index) * 4.0 * pi) /
                          degree
                   << ',' << state.pressure_pa_abs / 1.0e5 << ',' << state.temperature_k
                   << ',' << state.oxygen_mass_kg << ','
                   << state.unburned_tracer_mass_kg + state.reacting_fuel_mass_kg << ','
                   << step.accounting.injected_fuel_mass_kg << ','
                   << step.accounting.heat_release_j << ','
                   << step.accounting.volume_work_out_j << ','
                   << step.accounting.intake.mass_into_cylinder_kg << ','
                   << step.accounting.exhaust.mass_into_cylinder_kg << '\n';
        }
    }
    expect(output.good(), "write cycle diagnostic CSV");
}
} // namespace

int main(int argc, char **argv) {
    const std::array tests{
        std::pair{"admission", &test_admission},
        std::pair{"closed motoring", &test_closed_motoring},
        std::pair{"nozzle and reverse transport", &test_nozzle_and_reverse_transport},
        std::pair{"repeated firing and load", &test_repeated_firing_and_load},
        std::pair{"starved fuel carryover", &test_starved_fuel_carryover},
        std::pair{"cut and cycle control", &test_cut_and_cycle_control},
        std::pair{"metered delivery", &test_metered_delivery},
        std::pair{"multiple pulses and clock",
                  &test_multiple_pulses_and_nonintegral_clock},
        std::pair{"convergence and rollback", &test_convergence_and_rollback},
        std::pair{"valve overlap", &test_valve_overlap}};
    try {
        for (const auto &[name, test] : tests) {
            test();
            std::cout << "PASS " << name << '\n';
        }
        if (argc == 2) {
            write_trace(argv[1]);
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
