#include "simulation/legacy_combustion_primitives.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using namespace crankwave::simulation;

constexpr std::array<LegacyTrianglePoint, 10U> kFlameSpeedRatioTable{{
    {0.0, 3.0},
    {5.0, 7.5},
    {10.0, 15.0},
    {15.0, 22.5},
    {20.0, 30.0},
    {25.0, 37.5},
    {30.0, 45.0},
    {35.0, 52.5},
    {40.0, 60.0},
    {45.0, 67.5},
}};

[[nodiscard]] std::uint64_t bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_bits(double actual, double expected, const std::string &message) {
    if (bits(actual) == bits(expected)) {
        return;
    }

    std::ostringstream detail;
    detail << message << "; expected bits 0x" << std::hex << bits(expected)
           << ", got 0x" << bits(actual);
    throw std::runtime_error{detail.str()};
}

[[nodiscard]] LegacyGasolineFuelParameters gasoline_fuel() noexcept {
    return {
        0.100, 48.1e6, 12.5, 0.8, 0.5, 0.6, 4.0, 10.0, 1.0, 5.0, kFlameSpeedRatioTable,
    };
}

[[nodiscard]] double source_clamp(double value, double lower, double upper) noexcept {
    if (value <= lower) {
        return lower;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

void test_exact_pcg32_vectors_and_draw_width() {
    LegacyPcg32 generator;
    expect(generator.seed(UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)),
           "valid legacy PCG32 stream was rejected");
    expect(generator.state() == UINT64_C(0x4b403cc62ed140ae) &&
               generator.increment() == UINT64_C(0x35cddc6032c07577),
           "legacy PCG32 seeded state or odd increment changed");

    constexpr std::array<std::uint32_t, 4U> expected_outputs{
        UINT32_C(0x623402e1),
        UINT32_C(0xdd0b7d37),
        UINT32_C(0x5ee862d8),
        UINT32_C(0x69f404a3),
    };
    for (const std::uint32_t expected : expected_outputs) {
        expect(generator.next_u32() == expected,
               "legacy PCG32 raw output vector changed");
    }

    LegacyPcg32 floating;
    expect(floating.seed(UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)),
           "valid floating-draw stream was rejected");
    LegacyPcg32 two_raw_draws = floating;
    static_cast<void>(two_raw_draws.next_u32());
    static_cast<void>(two_raw_draws.next_u32());
    expect(bits(floating.uniform_binary64()) == UINT64_C(0x3fd88d00bee85be8),
           "legacy PCG32 53-bit binary64 construction changed");
    expect(floating.state() == two_raw_draws.state(),
           "one binary64 uniform did not consume exactly two u32 draws");
}

void test_oversized_stream_rejection_is_transactional() {
    LegacyPcg32 generator;
    expect(generator.seed(UINT64_C(0x123456789abcdef0), UINT64_C(17)),
           "valid stream setup failed");
    const std::uint64_t old_state = generator.state();
    const std::uint64_t old_increment = generator.increment();

    expect(!generator.seed(UINT64_C(99), UINT64_C(1) << 63U),
           "oversized PCG32 stream selector was accepted");
    expect(generator.state() == old_state && generator.increment() == old_increment,
           "rejected PCG32 stream mutated the active generator");

    expect(generator.seed(UINT64_C(0), kMaximumLegacyPcg32Stream),
           "maximum representable PCG32 stream was rejected");
    expect(generator.increment() == UINT64_MAX,
           "maximum PCG32 stream did not encode the all-ones increment");
}

struct IgnitionFixture {
    LegacyGasolineFuelParameters fuel = gasoline_fuel();
    std::array<double, kLegacyCombustionHistorySampleCount> speed_history{};
    std::array<double, kLegacyCombustionHistorySampleCount> pressure_history{};
    LegacyPcg32 random;

    IgnitionFixture() {
        speed_history.fill(4.0);
        pressure_history[17] = 125000.0;
        pressure_history[201] = 175000.0;
        expect(random.seed(UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)),
               "ignition fixture random stream was rejected");
    }
};

void expect_rejected_without_mutation(IgnitionFixture &fixture,
                                      LegacyIgnitionDisposition expected_disposition,
                                      LegacyFlameState flame, const LegacyGasCell &cell,
                                      const std::string &message) {
    const LegacyFlameState old_flame = flame;
    const LegacyGasCell old_cell = cell;
    const std::uint64_t old_random_state = fixture.random.state();
    const std::uint64_t old_random_increment = fixture.random.increment();

    const LegacyIgnitionResult result =
        legacy_try_ignite(flame, fixture.random, cell, 0.00049, fixture.speed_history,
                          fixture.pressure_history, fixture.fuel);
    expect(result.disposition == expected_disposition, message + " classification");
    expect(flame == old_flame, message + " mutated flame state");
    expect(cell == old_cell, message + " mutated gas state");
    expect(fixture.random.state() == old_random_state &&
               fixture.random.increment() == old_random_increment,
           message + " consumed a random draw");
}

void test_ignition_rejection_order_and_immutability() {
    IgnitionFixture fixture;
    const LegacyGasCell no_fuel = legacy_initialize_gas_cell(
        101325.0, 0.0005, 350.0, LegacyGasMixture{0.0, 1.0, 0.0});

    LegacyFlameState already_active;
    already_active.active = true;
    already_active.efficiency_01 = 0.37;
    expect_rejected_without_mutation(
        fixture, LegacyIgnitionDisposition::rejected_active_flame, already_active,
        no_fuel, "active flame did not take precedence over the no-fuel gate");

    expect_rejected_without_mutation(
        fixture, LegacyIgnitionDisposition::rejected_no_fuel, LegacyFlameState{},
        no_fuel, "zero-fuel ignition rejection");

    const LegacyGasCell low_mixture = legacy_initialize_gas_cell(
        101325.0, 0.0005, 350.0, LegacyGasMixture{0.10, 0.80, 0.10});
    expect_rejected_without_mutation(
        fixture, LegacyIgnitionDisposition::rejected_mixture_low, LegacyFlameState{},
        low_mixture, "low-mixture ignition rejection");

    const LegacyGasCell high_mixture = legacy_initialize_gas_cell(
        101325.0, 0.0005, 350.0, LegacyGasMixture{0.01, 0.49, 0.50});
    expect_rejected_without_mutation(
        fixture, LegacyIgnitionDisposition::rejected_mixture_high, LegacyFlameState{},
        high_mixture, "high-mixture ignition rejection");
}

void test_accepted_ignition_snapshot_and_two_draws() {
    IgnitionFixture fixture;
    const LegacyGasCell cell = legacy_initialize_gas_cell(
        101325.0, 0.0005, 350.0, LegacyGasMixture{0.04, 0.46, 0.50});
    LegacyFlameState flame;
    flame.lit_amount_mol = 9.0;
    flame.diagnostic_total_amount_mol = 8.0;
    flame.percentage_lit_01 = 0.7;
    flame.efficiency_01 = 0.3;
    flame.flame_speed_m_s = 12.0;
    flame.last_volume_m3 = 0.1;
    flame.radial_travel_m = 0.2;
    flame.axial_travel_m = 0.3;
    flame.global_mixture = {0.2, 0.3, 0.5};

    LegacyPcg32 expected_random = fixture.random;
    const double uniform = expected_random.uniform_binary64();
    const double geometric_volume_m3 = 0.00049;
    const LegacyIgnitionResult result = legacy_try_ignite(
        flame, fixture.random, cell, geometric_volume_m3, fixture.speed_history,
        fixture.pressure_history, fixture.fuel);

    expect(result.accepted(), "eligible spark ignition was rejected");
    expect(fixture.random.state() == expected_random.state(),
           "accepted ignition did not consume exactly two u32 draws");
    expect(flame.active && flame.global_mixture == cell.mixture,
           "accepted ignition did not snapshot mixture and active state");
    expect_bits(flame.diagnostic_total_amount_mol, cell.amount_mol,
                "accepted ignition did not snapshot old gas amount");
    expect_bits(flame.last_volume_m3, geometric_volume_m3,
                "accepted ignition did not snapshot post-mechanism volume");
    expect(flame.radial_travel_m == 0.0 && flame.axial_travel_m == 0.0 &&
               flame.lit_amount_mol == 0.0 && flame.percentage_lit_01 == 0.0,
           "accepted ignition did not reset flame progress");
    expect_bits(result.equivalence_source, 1.0,
                "accepted ignition equivalence value changed");
    expect_bits(result.turbulence_m_s, 2.0,
                "mean-piston-speed turbulence mapping changed");
    expect_bits(result.firing_pressure_history_max_pa, 175000.0,
                "ignition firing-pressure history maximum changed");

    const double ideal_inert = cell.mixture.oxygen_fraction / 0.7;
    const double dilution = cell.mixture.inert_fraction / ideal_inert - 1.0;
    const double mixing =
        1.0 -
        (source_clamp(result.turbulence_m_s / fixture.fuel.maximum_turbulence_effect,
                      0.0, 1.0) *
         source_clamp(1.0 - dilution / fixture.fuel.maximum_dilution_effect, 0.0, 1.0));
    const double random_attenuation =
        fixture.fuel.low_efficiency_attenuation_01 *
        ((1.0 - fixture.fuel.burning_efficiency_randomness_01) +
         fixture.fuel.burning_efficiency_randomness_01 * uniform);
    const double expected_efficiency = (mixing * random_attenuation + (1.0 - mixing)) *
                                       fixture.fuel.maximum_burning_efficiency_01;
    expect_bits(flame.efficiency_01, expected_efficiency,
                "accepted ignition efficiency arithmetic changed");
    expect_bits(result.efficiency_01, flame.efficiency_01,
                "accepted result did not publish snapshotted efficiency");

    const double expected_flame_speed = legacy_source_flame_speed(
        result.turbulence_m_s,
        cell.mixture.oxygen_fraction / cell.mixture.fuel_fraction,
        legacy_gas_temperature_k(cell), legacy_gas_pressure_pa(cell),
        result.firing_pressure_history_max_pa, 0.0, fixture.fuel);
    expect_bits(flame.flame_speed_m_s, expected_flame_speed,
                "accepted ignition flame-speed snapshot changed");
    expect_bits(result.flame_speed_m_s, flame.flame_speed_m_s,
                "accepted result did not publish snapshotted flame speed");
}

void test_species_limited_reaction_and_no_momentum_rescale() {
    const LegacyGasolineFuelParameters fuel = gasoline_fuel();
    LegacyGasCell cell{
        1.0, 1000.0, 0.002, 0.75, -0.25, {0.10, 0.70, 0.20},
    };
    const LegacyGasCell old_cell = cell;
    const LegacyGasMixture global_mixture{0.05, 0.325, 0.625};
    const double request_mol = 0.20;

    const double requested_fuel = global_mixture.fuel_fraction * request_mol;
    const double requested_oxygen = global_mixture.oxygen_fraction * request_mol;
    const double current_fuel = old_cell.mixture.fuel_fraction * old_cell.amount_mol;
    const double current_oxygen =
        old_cell.mixture.oxygen_fraction * old_cell.amount_mol;
    const double current_inert = old_cell.mixture.inert_fraction * old_cell.amount_mol;
    const double burned_fuel = std::fmin(std::fmin(current_fuel, requested_fuel),
                                         (2.0 / 25.0) * requested_oxygen);
    const double burned_oxygen = std::fmin(std::fmin(current_oxygen, requested_oxygen),
                                           (25.0 / 2.0) * requested_fuel);
    const double reactants = burned_fuel + burned_oxygen;
    const double products = ((16.0 + 18.0) / (25.0 + 2.0)) * reactants;
    const double amount_delta = products - reactants;
    const double next_amount = old_cell.amount_mol + amount_delta;
    const double next_fuel = current_fuel - burned_fuel;
    const double next_oxygen = current_oxygen - burned_oxygen;
    const double next_inert = current_inert + products;
    const double burned_mass = burned_fuel * fuel.molecular_mass_kg_per_mol;
    const double released_energy = burned_mass * fuel.energy_density_j_per_kg;

    const LegacyCombustionReactionResult result =
        legacy_apply_gasoline_reaction(cell, request_mol, global_mixture, fuel);
    expect_bits(result.requested_fuel_mol, requested_fuel,
                "reaction requested-fuel arithmetic changed");
    expect_bits(result.requested_oxygen_mol, requested_oxygen,
                "reaction requested-oxygen arithmetic changed");
    expect_bits(result.burned_fuel_mol, burned_fuel, "reaction fuel limiting changed");
    expect_bits(result.burned_oxygen_mol, burned_oxygen,
                "reaction oxygen limiting changed");
    expect_bits(result.reactants_mol, reactants, "reaction reactant sum changed");
    expect_bits(result.products_mol, products, "reaction product ratio changed");
    expect_bits(result.amount_delta_mol, amount_delta,
                "reaction molar contraction changed");
    expect_bits(cell.amount_mol, next_amount, "reaction gas amount mutation changed");
    expect_bits(cell.mixture.fuel_fraction, next_fuel / next_amount,
                "reaction fuel fraction changed");
    expect_bits(cell.mixture.inert_fraction, next_inert / next_amount,
                "reaction inert fraction changed");
    expect_bits(cell.mixture.oxygen_fraction, next_oxygen / next_amount,
                "reaction oxygen fraction changed");
    expect_bits(result.burned_fuel_mass_kg, burned_mass,
                "reaction burned-fuel mass changed");
    expect_bits(result.energy_release_j, released_energy,
                "reaction energy release changed");
    expect_bits(cell.thermal_energy_j, old_cell.thermal_energy_j + released_energy,
                "reaction did not add heat after species mutation");
    expect(bits(cell.volume_m3) == bits(old_cell.volume_m3) &&
               bits(cell.momentum_x_kg_m_s) == bits(old_cell.momentum_x_kg_m_s) &&
               bits(cell.momentum_y_kg_m_s) == bits(old_cell.momentum_y_kg_m_s),
           "reaction altered volume or rescaled planar momentum");
}

void test_geometric_progress_and_no_progress_extinction() {
    const LegacyGasolineFuelParameters fuel = gasoline_fuel();
    LegacyGasCell cell{
        0.05, 200.0, 0.0005, 0.0, 0.0, {0.04, 0.46, 0.50},
    };
    LegacyFlameState flame;
    flame.active = true;
    flame.efficiency_01 = 0.8;
    flame.flame_speed_m_s = 20.0;
    flame.last_volume_m3 = cell.volume_m3;
    flame.global_mixture = cell.mixture;

    const double step_s = 1.25e-5;
    const double bore_m = 0.084;
    const double piston_area_m2 = 0.005;
    const double expected_travel = step_s * flame.flame_speed_m_s;
    const double expected_burned_volume =
        expected_travel * expected_travel * kLegacyPi * expected_travel;
    const double expected_swept =
        (expected_burned_volume / cell.volume_m3) * cell.amount_mol;
    const LegacyFlameAdvanceResult advanced = legacy_advance_gasoline_flame(
        flame, cell, step_s, bore_m, piston_area_m2, fuel);

    expect(advanced.disposition == LegacyFlameAdvanceDisposition::advanced &&
               flame.active,
           "propagating flame did not report geometric progress");
    expect_bits(flame.radial_travel_m, expected_travel, "radial flame travel changed");
    expect_bits(flame.axial_travel_m, expected_travel, "axial flame travel changed");
    expect_bits(advanced.burned_volume_m3, expected_burned_volume,
                "geometric burned volume changed");
    expect_bits(advanced.swept_amount_mol, expected_swept,
                "geometric swept amount changed");
    expect_bits(flame.lit_amount_mol, expected_swept,
                "flame progress did not accumulate unattenuated swept amount");
    expect_bits(flame.percentage_lit_01, expected_burned_volume / 0.0005,
                "flame progress fraction changed");

    LegacyGasCell saturated_cell{
        0.05, 200.0, 0.002, 0.0, 0.0, {0.04, 0.46, 0.50},
    };
    LegacyFlameState saturated;
    saturated.active = true;
    saturated.efficiency_01 = 0.8;
    saturated.flame_speed_m_s = 20.0;
    saturated.last_volume_m3 = 0.001;
    saturated.radial_travel_m = bore_m / 2.0;
    saturated.axial_travel_m = 0.001 / piston_area_m2;
    saturated.global_mixture = saturated_cell.mixture;
    const LegacyFlameState old_saturated = saturated;

    const LegacyFlameAdvanceResult extinguished = legacy_advance_gasoline_flame(
        saturated, saturated_cell, step_s, bore_m, piston_area_m2, fuel);
    expect(extinguished.disposition ==
                   LegacyFlameAdvanceDisposition::extinguished_no_geometric_progress &&
               !saturated.active,
           "saturated flame did not extinguish for no geometric progress");
    expect_bits(saturated.radial_travel_m, old_saturated.radial_travel_m,
                "no-progress extinction changed saturated radial travel");
    expect_bits(saturated.axial_travel_m,
                old_saturated.axial_travel_m *
                    (saturated_cell.volume_m3 / old_saturated.last_volume_m3),
                "no-progress extinction did not preserve expansion-scaled travel");
    expect_bits(saturated.last_volume_m3, saturated_cell.volume_m3,
                "no-progress extinction did not update last volume");
    expect(extinguished.reaction == LegacyCombustionReactionResult{},
           "no-progress extinction performed a reaction");
}

void test_intake_transfer_extinction_threshold() {
    LegacyFlameState flame;
    flame.active = true;
    flame.lit_amount_mol = 0.25;
    const LegacyFlameState before_threshold = flame;

    expect(!legacy_extinguish_flame_for_intake_transfer(
               flame, kLegacyIntakeFlameExtinctionAmountMol),
           "intake extinction used an inclusive threshold");
    expect(flame == before_threshold,
           "threshold-equal intake transfer mutated flame state");

    expect(legacy_extinguish_flame_for_intake_transfer(flame, -1.0000001e-9),
           "negative intake transfer above the absolute threshold did not extinguish");
    expect(!flame.active && flame.lit_amount_mol == 0.25,
           "intake extinction changed fields other than active state");
    expect(!legacy_extinguish_flame_for_intake_transfer(flame, 2.0e-9),
           "already-inactive flame emitted a second intake extinction");
}

void run_tests() {
    test_exact_pcg32_vectors_and_draw_width();
    test_oversized_stream_rejection_is_transactional();
    test_ignition_rejection_order_and_immutability();
    test_accepted_ignition_snapshot_and_two_draws();
    test_species_limited_reaction_and_no_momentum_rescale();
    test_geometric_progress_and_no_progress_extinction();
    test_intake_transfer_extinction_threshold();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "legacy_combustion_primitives_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
