#include "simulation/legacy_gas_primitives.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using namespace crankwave::simulation;

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

[[nodiscard]] LegacyGasCell make_forward_source() noexcept {
    return legacy_initialize_gas_cell(180000.0, 0.002, 360.0,
                                      LegacyGasMixture{0.1, 0.7, 0.2});
}

[[nodiscard]] LegacyGasCell make_forward_sink() noexcept {
    return legacy_initialize_gas_cell(90000.0, 0.001, 300.0);
}

[[nodiscard]] LegacyFiniteGasTransferParameters
finite_parameters(double restriction_coefficient) noexcept {
    return LegacyFiniteGasTransferParameters{
        restriction_coefficient, 1.25e-5, 1.0, 0.0, 0.01, 0.002};
}

// Independent source-era directional-pressure oracle. Keeping the ordered z2/z3/z7
// expansion here catches an accidental replacement with pow(z, 3.5).
[[nodiscard]] double source_directional_pressure(const LegacyGasCell &cell,
                                                 double direction_x,
                                                 double direction_y) noexcept {
    if (cell.amount_mol == 0.0 || cell.thermal_energy_j == 0.0) {
        return 0.0;
    }
    constexpr double air_molar_mass = 0.02897;
    constexpr double gamma = 1.4;
    const double mass = air_molar_mass * cell.amount_mol;
    const double inverse_mass = 1.0 / mass;
    const double velocity = inverse_mass * (direction_x * cell.momentum_x_kg_m_s +
                                            direction_y * cell.momentum_y_kg_m_s);
    if (velocity <= 0.0) {
        return 0.0;
    }
    const double pressure = cell.thermal_energy_j / (0.5 * 5.0 * cell.volume_m3);
    const double density = (air_molar_mass * cell.amount_mol) / cell.volume_m3;
    const double sound_squared = pressure * gamma / density;
    const double mach_squared = velocity * velocity / sound_squared;
    const double z = 1.0 + ((gamma - 1.0) / 2.0) * mach_squared;
    const double z2 = z * z;
    const double z3 = z2 * z;
    const double z7 = z3 * z3 * z;
    return pressure * (std::sqrt(z7) - 1.0);
}

void test_initialization_reset_and_derived_goldens() {
    LegacyGasCell cell = legacy_initialize_gas_cell(101325.0, 0.002, 298.15);

    // Exact hex values were emitted by the frozen source GasSystem built with the
    // M3 numerical policy. In particular, reconstructing P from initialized U is
    // one ULP above the authored 101325 Pa input.
    expect_bits(cell.amount_mol, 0x1.4ed7158f7cfcfp-4, "initialized amount changed");
    expect_bits(cell.thermal_energy_j, 0x1.faa0000000001p+8,
                "initialized thermal energy changed");
    expect_bits(cell.volume_m3, 0x1.0624dd2f1a9fcp-9, "initialized volume changed");
    expect_bits(legacy_gas_pressure_pa(cell), 0x1.8bcd000000001p+16,
                "derived static pressure changed");
    expect_bits(legacy_gas_temperature_k(cell), 0x1.2a26666666666p+8,
                "derived temperature changed");
    expect_bits(legacy_gas_density_kg_m3(cell), 0x1.2f228ef5806efp+0,
                "derived density changed");
    expect_bits(legacy_gas_sonic_velocity_m_s(cell), 0x1.5a1e39a3a9d17p+8,
                "derived sonic velocity changed");
    expect_bits(legacy_gas_mass_kg(cell), 0.02897 * cell.amount_mol,
                "derived mass operation changed");
    expect_bits(legacy_gas_total_energy_j(cell), cell.thermal_energy_j,
                "zero-momentum total energy changed");
    expect(legacy_gas_velocity_x_m_s(cell) == 0.0 &&
               legacy_gas_velocity_y_m_s(cell) == 0.0 &&
               legacy_gas_bulk_kinetic_energy_j(cell) == 0.0 &&
               legacy_gas_directional_dynamic_pressure_pa(cell, 1.0, 0.0) == 0.0,
           "fresh cell did not have zero planar motion");

    cell.momentum_x_kg_m_s = 0.25;
    cell.momentum_y_kg_m_s = -0.125;
    const double expected_q = source_directional_pressure(cell, 0.6, -0.8);
    expect_bits(legacy_gas_directional_dynamic_pressure_pa(cell, 0.6, -0.8), expected_q,
                "directional pressure no longer uses ordered z2/z3/z7 evaluation");
    expect(legacy_gas_directional_dynamic_pressure_pa(cell, -0.6, 0.8) == 0.0,
           "nonpositive directional velocity produced dynamic pressure");

    const LegacyGasMixture reset_mixture{0.2, 0.5, 0.3};
    legacy_reset_gas_cell(cell, 101325.0, 298.15, reset_mixture);
    expect_bits(cell.amount_mol, 0x1.4ed7158f7cfcfp-4, "reset amount changed");
    expect_bits(cell.thermal_energy_j, 0x1.faa0000000001p+8,
                "reset thermal energy changed");
    expect_bits(cell.volume_m3, 0x1.0624dd2f1a9fcp-9, "reset did not retain volume");
    expect(cell.momentum_x_kg_m_s == 0.0 && cell.momentum_y_kg_m_s == 0.0 &&
               cell.mixture == reset_mixture,
           "reset did not replace mixture and clear momentum");
}

void test_volume_work_and_amount_mix_operations() {
    LegacyGasCell cell = legacy_initialize_gas_cell(160000.0, 0.0012, 340.0,
                                                    LegacyGasMixture{0.1, 0.7, 0.2});

    const double old_volume = cell.volume_m3;
    const double old_energy = cell.thermal_energy_j;
    const double delta_volume = 0.00017;
    const double length = std::pow(old_volume + delta_volume, 1 / 3.0);
    const double area = length * length;
    const double delta_length = -delta_volume / area;
    const double old_pressure = old_energy / (0.5 * 5.0 * old_volume);
    const double work = delta_length * old_pressure * area;
    const double expected_volume = old_volume + delta_volume;
    const double expected_energy = old_energy + work;

    legacy_change_gas_volume(cell, delta_volume);
    expect_bits(cell.volume_m3, expected_volume,
                "volume work changed V addition order");
    expect_bits(cell.thermal_energy_j, expected_energy,
                "volume work was reassociated to -p*dV");

    const double set_volume = 0.0011;
    const double before_set_volume = cell.volume_m3;
    const double before_set_energy = cell.thermal_energy_j;
    const double set_delta = set_volume - before_set_volume;
    const double set_length = std::pow(before_set_volume + set_delta, 1 / 3.0);
    const double set_area = set_length * set_length;
    const double set_delta_length = -set_delta / set_area;
    const double set_pressure = before_set_energy / (0.5 * 5.0 * before_set_volume);
    const double set_work = set_delta_length * set_pressure * set_area;
    legacy_set_gas_volume(cell, set_volume);
    expect_bits(cell.volume_m3, before_set_volume + set_delta,
                "set-volume bypassed legacy delta construction");
    expect_bits(cell.thermal_energy_j, before_set_energy + set_work,
                "set-volume changed work order");

    const LegacyGasMixture incoming{0.4, 0.2, 0.4};
    const double old_amount = cell.amount_mol;
    const double old_add_energy = cell.thermal_energy_j;
    const LegacyGasMixture old_mix = cell.mixture;
    const double added_amount = 0.013;
    const double incoming_energy_per_mol = 7777.0;
    const double next_amount = old_amount + added_amount;
    const double expected_add_energy =
        old_add_energy + added_amount * incoming_energy_per_mol;
    const LegacyGasMixture expected_mix{
        (old_mix.fuel_fraction * old_amount + added_amount * incoming.fuel_fraction) /
            next_amount,
        (old_mix.inert_fraction * old_amount + added_amount * incoming.inert_fraction) /
            next_amount,
        (old_mix.oxygen_fraction * old_amount +
         added_amount * incoming.oxygen_fraction) /
            next_amount};

    legacy_add_gas_amount(cell, added_amount, incoming_energy_per_mol, incoming);
    expect_bits(cell.amount_mol, next_amount, "gas addition changed amount order");
    expect_bits(cell.thermal_energy_j, expected_add_energy,
                "gas addition changed thermal order");
    expect_bits(cell.mixture.fuel_fraction, expected_mix.fuel_fraction,
                "gas addition changed fuel mixing order");
    expect_bits(cell.mixture.inert_fraction, expected_mix.inert_fraction,
                "gas addition changed inert mixing order");
    expect_bits(cell.mixture.oxygen_fraction, expected_mix.oxygen_fraction,
                "gas addition changed oxygen mixing order");

    const double removed_amount = 0.007;
    const double removal_energy_per_mol = cell.thermal_energy_j / cell.amount_mol;
    const double expected_remove_energy =
        cell.thermal_energy_j - removal_energy_per_mol * removed_amount;
    const double expected_remove_amount = cell.amount_mol - removed_amount;
    legacy_remove_gas_amount(cell, removed_amount, removal_energy_per_mol);
    expect_bits(cell.thermal_energy_j, expected_remove_energy,
                "gas removal changed U-=e*dn order");
    expect_bits(cell.amount_mol, expected_remove_amount,
                "gas removal changed n-=dn order");
    expect(cell.mixture == expected_mix,
           "gas removal unexpectedly changed composition");

    LegacyGasCell floor_cell{0.001, 10.0, 0.001, 0.0, 0.0, {}};
    legacy_remove_gas_amount(floor_cell, 0.01, 2000.0);
    expect(floor_cell.amount_mol == 0.0 && floor_cell.thermal_energy_j < 0.0,
           "amount floor also invented a thermal floor");
    legacy_floor_negative_gas_thermal_energy(floor_cell);
    expect(floor_cell.thermal_energy_j == 0.0,
           "named negative thermal-energy floor failed");
}

void test_restriction_calibration_and_rate_goldens() {
    const double one_source_scfm_mol_s = 0.002641 * 453.59237 / 60.0;
    const double carb_drop_pa = 1.5 * 3386.3886666666713;
    const double water_drop_pa = 28.0 * (3386.3886666666713 * 0.0734824);

    expect_bits(legacy_restriction_coefficient(500.0 * one_source_scfm_mol_s, 101325.0,
                                               carb_drop_pa, 298.15),
                0x1.04eb9d8a8a841p-6, "k_carb(500) source calibration changed");
    expect_bits(legacy_restriction_coefficient(one_source_scfm_mol_s, 101325.0,
                                               water_drop_pa, 298.15),
                0x1.cd2666c670c63p-16, "k_28inH2O(1) source calibration changed");

    const double zero_k =
        legacy_restriction_molar_rate(0.0, 120000.0, 100000.0, 330.0, 300.0);
    expect(zero_k == 0.0 && !std::signbit(zero_k),
           "K==0 did not return immediate positive zero");
    const double equal =
        legacy_restriction_molar_rate(1.0, 101325.0, 101325.0, 298.15, 298.15);
    expect(equal == 0.0 && std::signbit(equal),
           "equal pressure lost the endpoint-1 negative-zero branch");
    expect_bits(legacy_restriction_molar_rate(0.01, 120000.0, 100000.0, 330.0, 300.0),
                0x1.7f9edd9a3a11p+3, "subsonic restriction rate changed");
    expect_bits(legacy_restriction_molar_rate(0.01, 250000.0, 50000.0, 400.0, 300.0),
                0x1.daef14fef96b7p+4, "choked restriction rate changed");
    expect_bits(legacy_restriction_molar_rate(0.01, 100000.0, 120000.0, 300.0, 330.0),
                -0x1.7f9edd9a3a11p+3,
                "reverse restriction rate changed sign or upstream selection");
}

void test_forward_reverse_and_clamped_finite_transfer_goldens() {
    LegacyGasCell source = make_forward_source();
    LegacyGasCell sink = make_forward_sink();
    const auto forward = legacy_transfer_gas(source, sink, finite_parameters(0.01));

    expect(forward.source_endpoint == LegacyGasEndpoint::endpoint_0,
           "forward flow selected the wrong source endpoint");
    expect_bits(forward.signed_amount_mol, 0x1.2747bd0d09d68p-12,
                "forward transfer amount changed");
    expect_bits(source.amount_mol, 0x1.eb7b6cbb4aa74p-4,
                "forward source amount changed");
    expect_bits(source.thermal_energy_j, 0x1.c0f245f91254p+9,
                "forward source thermal reconcile changed");
    expect_bits(legacy_gas_pressure_pa(source), 0x1.5ebd46aa9651ap+17,
                "forward source pressure changed");
    expect_bits(legacy_gas_velocity_x_m_s(source), 0x1.681c2c53e9066p-4,
                "forward source jet momentum changed");
    expect_bits(legacy_gas_directional_dynamic_pressure_pa(source, 1.0, 0.0),
                0x1.b8378de87d4ccp-8, "forward source directional pressure changed");
    expect_bits(sink.amount_mol, 0x1.29e36228b5178p-5, "forward sink amount changed");
    expect_bits(sink.thermal_energy_j, 0x1.c6365515edc74p+7,
                "forward sink staged thermal reconcile changed");
    expect_bits(legacy_gas_velocity_x_m_s(sink), 0x1.7356966f892bfp+0,
                "forward sink jet momentum changed");
    expect_bits(legacy_gas_directional_dynamic_pressure_pa(sink, 1.0, 0.0),
                0x1.1bb72e5018ce6p+0, "forward sink directional pressure changed");
    expect_bits(sink.mixture.fuel_fraction, 0x1.96039473c155fp-11,
                "forward sink fuel mixing changed");
    expect_bits(sink.mixture.inert_fraction, 0x1.fecf7d50a92fp-1,
                "forward sink inert mixing changed");
    expect_bits(sink.mixture.oxygen_fraction, 0x1.96039473c155fp-10,
                "forward sink oxygen mixing changed");

    LegacyGasCell endpoint_0 = legacy_initialize_gas_cell(
        90000.0, 0.002, 360.0, LegacyGasMixture{0.1, 0.7, 0.2});
    LegacyGasCell endpoint_1 = legacy_initialize_gas_cell(180000.0, 0.001, 300.0);
    const auto reverse =
        legacy_transfer_gas(endpoint_0, endpoint_1, finite_parameters(0.01));
    expect(reverse.source_endpoint == LegacyGasEndpoint::endpoint_1,
           "reverse flow selected the wrong source endpoint");
    expect_bits(reverse.signed_amount_mol, -0x1.437699c469b97p-12,
                "reverse transfer amount changed");
    expect_bits(endpoint_0.thermal_energy_j, 0x1.c3ec70f92587cp+8,
                "reverse endpoint-0 thermal state changed");
    expect_bits(legacy_gas_velocity_x_m_s(endpoint_0), -0x1.656ef28eba77ap-3,
                "reverse sink did not receive signed reverse jet momentum");
    expect_bits(legacy_gas_directional_dynamic_pressure_pa(endpoint_0, -1.0, 0.0),
                0x1.b4f1e6ee2c9a8p-7,
                "reverse endpoint-0 directional pressure changed");
    expect_bits(endpoint_1.thermal_energy_j, 0x1.c0136885e4225p+8,
                "reverse endpoint-1 thermal state changed");
    expect_bits(legacy_gas_velocity_x_m_s(endpoint_1), -0x1.77d7cdb08d2d6p-1,
                "reverse source did not receive same-signed jet momentum");

    LegacyGasCell clamped_source = make_forward_source();
    LegacyGasCell clamped_sink = make_forward_sink();
    const double source_amount_before = clamped_source.amount_mol;
    const double expected_clamped_amount = 0.9 * source_amount_before;
    const auto clamped =
        legacy_transfer_gas(clamped_source, clamped_sink, finite_parameters(1.0e9));
    expect_bits(clamped.signed_amount_mol, expected_clamped_amount,
                "finite transfer did not apply the sole 90% source clamp");
    expect_bits(clamped.signed_amount_mol, 0x1.bb5f3c05e885cp-4,
                "90%-clamped amount source golden changed");
    expect_bits(clamped_source.amount_mol, 0x1.8a1bc393795a8p-7,
                "90%-clamped source remainder changed");
    expect(clamped_source.thermal_energy_j == 0.0,
           "clamped transfer omitted the ordered source thermal floor");
    expect_bits(legacy_gas_velocity_x_m_s(clamped_source), 0x1.abde6abe4eb2ap+11,
                "clamped source jet impulse changed");
    expect_bits(clamped_sink.mixture.fuel_fraction, 0x1.3333333333334p-4,
                "clamped sink mixture changed");
    expect_bits(legacy_gas_directional_dynamic_pressure_pa(clamped_sink, 1.0, 0.0),
                0x1.77220f2825382p+17, "clamped sink jet directional pressure changed");
}

void test_environment_transfer_goldens_and_signs() {
    const LegacyGasEnvironment environment{101325.0, 298.15, {}};
    const LegacyEnvironmentGasTransferParameters parameters{1.0e9, 1.25e-5,
                                                            environment};

    LegacyGasCell outflow = legacy_initialize_gas_cell(180000.0, 0.002, 350.0,
                                                       LegacyGasMixture{0.1, 0.7, 0.2});
    const auto out = legacy_transfer_gas_environment(outflow, parameters);
    expect_bits(out.pressure_equilibrium_amount_mol, 0x1.baf322da812b5p-5,
                "outflow equilibrium amount changed");
    expect_bits(out.signed_amount_mol, out.pressure_equilibrium_amount_mol,
                "environment outflow was not equilibrium-clipped");
    expect_bits(outflow.amount_mol, 0x1.1d3c6a0736deap-4,
                "environment outflow amount changed");
    expect_bits(outflow.thermal_energy_j, 0x1.faap+8,
                "environment outflow thermal energy changed");
    expect_bits(legacy_gas_pressure_pa(outflow), 0x1.8bcdp+16,
                "environment outflow did not reach static equilibrium");
    expect(outflow.mixture == LegacyGasMixture{0.1, 0.7, 0.2},
           "environment outflow changed mixture fractions");

    LegacyGasCell inflow = legacy_initialize_gas_cell(60000.0, 0.002, 350.0,
                                                      LegacyGasMixture{0.1, 0.7, 0.2});
    const auto in = legacy_transfer_gas_environment(inflow, parameters);
    expect_bits(in.pressure_equilibrium_amount_mol, -0x1.112060cb1202p-5,
                "inflow equilibrium amount changed");
    expect_bits(in.signed_amount_mol, in.pressure_equilibrium_amount_mol,
                "environment inflow was not equilibrium-clipped");
    expect_bits(inflow.amount_mol, 0x1.317784370627cp-4,
                "environment inflow amount changed");
    expect_bits(inflow.thermal_energy_j, 0x1.faap+8,
                "environment inflow legacy bulk-energy sign changed");
    expect_bits(legacy_gas_pressure_pa(inflow), 0x1.8bcdp+16,
                "environment inflow did not reach static equilibrium");
    expect_bits(inflow.mixture.fuel_fraction, 0x1.c4f7152bc8226p-5,
                "environment inflow fuel mixing changed");
    expect_bits(inflow.mixture.inert_fraction, 0x1.ab11ac07ca799p-1,
                "environment inflow inert mixing changed");
    expect_bits(inflow.mixture.oxygen_fraction, 0x1.c4f7152bc8226p-4,
                "environment inflow oxygen mixing changed");

    LegacyGasCell equal = legacy_initialize_gas_cell(101325.0, 0.002, 350.0,
                                                     LegacyGasMixture{0.1, 0.7, 0.2});
    const auto equality = legacy_transfer_gas_environment(
        equal, LegacyEnvironmentGasTransferParameters{0.01, 1.25e-5, environment});
    expect(equality.pressure_equilibrium_amount_mol == 0.0 &&
               std::signbit(equality.pressure_equilibrium_amount_mol) &&
               equality.signed_amount_mol == 0.0 &&
               std::signbit(equality.signed_amount_mol),
           "environment equality lost negative-zero sign behavior");
}

void test_sonic_bound_self_impulse_and_decay() {
    LegacyGasCell supersonic = legacy_initialize_gas_cell(101325.0, 0.002, 298.15);
    const double mass = 0.02897 * supersonic.amount_mol;
    supersonic.momentum_x_kg_m_s = mass * 1000.0;
    supersonic.momentum_y_kg_m_s = mass * -500.0;

    const double velocity_x = supersonic.momentum_x_kg_m_s / mass;
    const double velocity_y = supersonic.momentum_y_kg_m_s / mass;
    const double velocity_squared = velocity_x * velocity_x + velocity_y * velocity_y;
    const double pressure =
        supersonic.thermal_energy_j / (0.5 * 5.0 * supersonic.volume_m3);
    const double density = (0.02897 * supersonic.amount_mol) / supersonic.volume_m3;
    const double sonic_velocity = std::sqrt(pressure * 1.4 / density);
    const double sonic_squared = sonic_velocity * sonic_velocity;
    const double scale_squared = sonic_squared / velocity_squared;
    const double scale = std::sqrt(scale_squared);
    const double expected_momentum_x = supersonic.momentum_x_kg_m_s * scale;
    const double expected_momentum_y = supersonic.momentum_y_kg_m_s * scale;
    const double expected_energy =
        supersonic.thermal_energy_j + 0.5 * mass * (velocity_squared - sonic_squared);

    legacy_limit_gas_to_sonic_velocity(supersonic);
    expect_bits(supersonic.momentum_x_kg_m_s, expected_momentum_x,
                "sonic bound changed x-momentum scaling");
    expect_bits(supersonic.momentum_y_kg_m_s, expected_momentum_y,
                "sonic bound changed y-momentum scaling");
    expect_bits(supersonic.thermal_energy_j, expected_energy,
                "sonic bound changed bulk-to-thermal reconciliation");

    LegacyGasCell source = make_forward_source();
    LegacyGasCell moving = make_forward_sink();
    const auto transferred =
        legacy_transfer_gas(source, moving, finite_parameters(0.01));
    expect(transferred.signed_amount_mol > 0.0, "self-impulse setup produced no flow");
    legacy_apply_gas_self_impulse(moving, LegacyGasCellGeometry{0.1, 0.2, 1.0, 0.0},
                                  1.25e-5, 0.5);
    expect_bits(moving.thermal_energy_j, 0x1.c63655194cd28p+7,
                "self-impulse x/y thermal reconcile changed");
    expect_bits(legacy_gas_pressure_pa(moving), 0x1.62da727bc4047p+16,
                "self-impulse pressure changed");
    expect_bits(legacy_gas_velocity_x_m_s(moving), 0x1.7352474b51a06p+0,
                "self-impulse momentum changed");

    legacy_apply_gas_velocity_decay(moving, 1.25e-5, 0.01);
    expect_bits(moving.thermal_energy_j, 0x1.c636557615ccep+7,
                "linear decay thermalization changed");
    expect_bits(legacy_gas_pressure_pa(moving), 0x1.62da72c441081p+16,
                "linear decay pressure changed");
    expect_bits(legacy_gas_velocity_x_m_s(moving), 0x1.72db9a9ef5ccap+0,
                "linear decay momentum changed");
}

void run_tests() {
    test_initialization_reset_and_derived_goldens();
    test_volume_work_and_amount_mix_operations();
    test_restriction_calibration_and_rate_goldens();
    test_forward_reverse_and_clamped_finite_transfer_goldens();
    test_environment_transfer_goldens_and_signs();
    test_sonic_bound_self_impulse_and_decay();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Legacy gas primitives test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
