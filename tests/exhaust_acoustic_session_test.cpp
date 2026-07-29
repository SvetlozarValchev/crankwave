#include "acoustics/exhaust_acoustic_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::acoustics;
using namespace engine_sim_offline::contract;

constexpr double kGasConstant = 8.31446261815324;
constexpr double kMolarMass = 0.02897;
constexpr double kGamma = 1.4;
constexpr double kSubstepDurationS = (1.0 / 10'000.0) / 8.0;
constexpr Sha256Digest kM5ExhaustAcousticNetworkSha256{{
    0x1c, 0x2e, 0x31, 0x48, 0x46, 0xd8, 0xf8, 0x61, 0x44, 0xe7, 0xae,
    0x70, 0x18, 0xa4, 0xad, 0xaa, 0xb4, 0x69, 0xfe, 0x5f, 0xe8, 0xcc,
    0xdf, 0xe0, 0x4d, 0x57, 0xae, 0x73, 0xbc, 0xbc, 0x31, 0xac,
}};

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

template <class Exception, class Function>
void expect_throw(Function &&function, const char *message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error{message};
}

template <class T> ResolvedValue<T> resolved(T value) {
    return {std::move(value), "test-resolution"};
}

MethodIdentity method(std::string id) {
    return {std::move(id), 1U, kM5ExhaustAcousticNetworkSha256};
}

ExhaustAcousticAssembly make_assembly() {
    ExhaustAcousticAssembly assembly;
    assembly.assembly_id = resolved(std::string{"declared-test-cell-twin-open-pipe"});
    assembly.methods = {
        resolved(method("ideal-pseudo-gas-source-properties")),
        resolved(method("causal-bandlimited-rational-resampling")),
        resolved(method("uniform-cylindrical-digital-waveguide")),
        resolved(method("ideal-compact-pressure-junction")),
        resolved(method("causal-unflanged-pipe-reflection")),
        resolved(method("compact-monopole-free-field-radiation")),
    };
    assembly.source_interval_rate = resolved(RationalRateHz{80'000, 1});
    assembly.acoustic_rate = resolved(RationalRateHz{192'000, 1});
    assembly.universal_gas_constant_j_per_mol_k = resolved(kGasConstant);
    assembly.source_molar_mass_kg_per_mol = resolved(kMolarMass);
    assembly.source_heat_capacity_ratio = resolved(kGamma);
    assembly.pa_per_full_scale = resolved(256.0);

    for (std::uint32_t cylinder = 1; cylinder <= 6; ++cylinder) {
        assembly.ducts.push_back({
            AcousticDuctId{cylinder},
            resolved(std::string{"primary-"} + std::to_string(cylinder)),
            resolved(AcousticDuctKind::primary),
            resolved(0.300),
            resolved(0.042),
            resolved(800.0),
            resolved(0.10),
        });
        assembly.primary_bindings.push_back({
            CylinderId{cylinder},
            PortId{100U + cylinder},
            AcousticDuctId{cylinder},
            AcousticJunctionId{cylinder <= 3U ? 201U : 202U},
        });
    }
    assembly.ducts.push_back({
        AcousticDuctId{7},
        resolved(std::string{"downstream-front"}),
        resolved(AcousticDuctKind::downstream),
        resolved(1.500),
        resolved(0.046),
        resolved(600.0),
        resolved(0.10),
    });
    assembly.ducts.push_back({
        AcousticDuctId{8},
        resolved(std::string{"downstream-rear"}),
        resolved(AcousticDuctKind::downstream),
        resolved(1.500),
        resolved(0.046),
        resolved(600.0),
        resolved(0.10),
    });
    assembly.junctions = {
        {
            AcousticJunctionId{201},
            resolved(std::string{"junction-front"}),
            {AcousticDuctId{1}, AcousticDuctId{2}, AcousticDuctId{3}},
            AcousticDuctId{7},
        },
        {
            AcousticJunctionId{202},
            resolved(std::string{"junction-rear"}),
            {AcousticDuctId{4}, AcousticDuctId{5}, AcousticDuctId{6}},
            AcousticDuctId{8},
        },
    };
    assembly.outlets = {
        {RouteId{301}, AcousticDuctId{7}, resolved(1.0)},
        {RouteId{302}, AcousticDuctId{8}, resolved(1.0)},
    };
    return assembly;
}

struct OwnedCapture {
    std::vector<PortIdentity> ports;
    std::vector<ExhaustPortSubstepCaptureSample> samples;
    std::uint64_t first_interval = 0;
    std::uint32_t interval_count = 0;

    [[nodiscard]] ExhaustPortSubstepCaptureView view() const {
        return ExhaustPortSubstepCaptureView::borrow_for_callback(
            CaptureClock{{80'000, 1},
                         first_interval,
                         first_interval + 1U,
                         SamplePhase::post_step},
            interval_count, ports, samples);
    }
};

OwnedCapture make_capture(std::uint64_t first_interval, std::uint32_t interval_count,
                          std::array<std::uint32_t, 6> lane_cylinders = {1, 2, 3, 4, 5,
                                                                         6},
                          std::uint32_t active_cylinder_mask = 0x3fU) {
    OwnedCapture capture;
    capture.first_interval = first_interval;
    capture.interval_count = interval_count;
    for (const auto cylinder : lane_cylinders) {
        capture.ports.push_back(
            {PortId{100U + cylinder}, CylinderId{cylinder}, PortKind::exhaust});
    }
    capture.samples.resize(static_cast<std::size_t>(interval_count) * 6U);

    constexpr double chamber_pressure_pa = 120'000.0;
    constexpr double chamber_temperature_k = 600.0;
    constexpr double primary_pressure_pa = 100'000.0;
    constexpr double primary_temperature_k = 500.0;
    const double specific_gas_constant = kGasConstant / kMolarMass;
    const double density =
        chamber_pressure_pa / (specific_gas_constant * chamber_temperature_k);
    const double sound_speed =
        std::sqrt(kGamma * specific_gas_constant * chamber_temperature_k);
    const auto validity = capture_validity_mask(CaptureValidity::thermodynamic_state) |
                          capture_validity_mask(CaptureValidity::gas_exchange);

    for (std::size_t interval = 0; interval < interval_count; ++interval) {
        const auto absolute_interval = first_interval + interval;
        for (std::size_t lane = 0; lane < capture.ports.size(); ++lane) {
            const auto cylinder = capture.ports[lane].cylinder_id.value;
            const bool cylinder_active =
                (active_cylinder_mask & (1U << (cylinder - 1U))) != 0U;
            const bool pulse =
                cylinder_active && ((absolute_interval + 13U * cylinder) % 79U == 0U);
            const double amount_mol = pulse ? 2.0e-7 : 0.0;
            capture.samples[interval * 6U + lane] = {
                absolute_interval / 8U,
                static_cast<std::uint8_t>(absolute_interval % 8U),
                validity,
                chamber_pressure_pa,
                chamber_temperature_k,
                primary_pressure_pa,
                primary_temperature_k,
                amount_mol,
                (amount_mol * kMolarMass) / kSubstepDurationS,
                ExhaustTransferUpstream::chamber,
                density,
                sound_speed,
                pulse ? 0.001 : 0.0,
                pulse ? 2.0e-5 : 0.0,
                Availability::unavailable,
                0.0,
            };
        }
    }
    expect(validate(capture.view()).ok(),
           "session test generated an invalid source capture");
    return capture;
}

void append(std::vector<double> &destination, const std::vector<double> &source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

void expect_finite(const ExhaustAcousticPressureBlock &block) {
    for (const auto &outlet : block.outlets) {
        expect(std::ranges::all_of(outlet.pressure_pa,
                                   [](double value) { return std::isfinite(value); }),
               "session published a non-finite radiated pressure");
    }
}

long double energy(const std::vector<double> &samples) {
    long double result = 0.0L;
    for (const auto sample : samples) {
        result += static_cast<long double>(sample) * sample;
    }
    return result;
}

void test_constructor_pins_environment_methods_and_exact_topology() {
    const ExhaustAcousticEnvironment environment{101'325.0, 298.15};
    ExhaustAcousticSession session{make_assembly(), environment};
    expect(session.assembly_id() == "declared-test-cell-twin-open-pipe" &&
               session.environment() == environment &&
               session.pa_per_full_scale() == 256.0,
           "session did not retain its exact assembly/environment identity");

    expect_throw<std::invalid_argument>(
        [&] {
            ExhaustAcousticSession invalid{
                make_assembly(), {101'325.0, std::numeric_limits<double>::quiet_NaN()}};
        },
        "session admitted an invalid ambient environment");
    auto unsupported = make_assembly();
    unsupported.methods.waveguide.value.id = "another-waveguide";
    expect_throw<std::invalid_argument>(
        [&] { ExhaustAcousticSession invalid{unsupported, environment}; },
        "session admitted an unsupported acoustic implementation path");
    auto wrong_configuration = make_assembly();
    wrong_configuration.methods.waveguide.value.configuration_sha256.bytes[0] ^= 1U;
    expect_throw<std::invalid_argument>(
        [&] { ExhaustAcousticSession invalid{wrong_configuration, environment}; },
        "session admitted a method with the wrong frozen configuration digest");
    auto wrong_partition = make_assembly();
    wrong_partition.junctions[1].primary_duct_ids[0] = AcousticDuctId{3};
    expect_throw<std::invalid_argument>(
        [&] { ExhaustAcousticSession invalid{wrong_partition, environment}; },
        "session admitted overlapping front/rear primary ownership");
}

void test_exact_clock_zero_source_passivity_and_route_identity() {
    ExhaustAcousticSession session{make_assembly(), {101'325.0, 298.15}};
    const auto source = make_capture(0U, 1'600U, {1, 2, 3, 4, 5, 6}, 0U);
    const auto output = session.process(source.view());

    expect(output.rate == RationalRateHz{192'000, 1} &&
               output.first_frame_index == 0U && output.frame_count() == 3'840U,
           "session changed the exact 80-to-192 kHz block clock");
    expect(output.outlets[0].route_id == RouteId{301} &&
               output.outlets[1].route_id == RouteId{302},
           "session lost resolved outlet route identity");
    expect(std::ranges::all_of(output.outlets[0].pressure_pa,
                               [](double sample) { return sample == 0.0; }) &&
               std::ranges::all_of(output.outlets[1].pressure_pa,
                                   [](double sample) { return sample == 0.0; }),
           "zero-state passive network generated pressure without a source");
    expect(session.source_intervals_consumed() == 1'600U &&
               session.acoustic_frames_produced() == 3'840U,
           "session state clocks disagreed with the published block");

    const auto discontinuous = make_capture(3'200U, 8U);
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(session.process(discontinuous.view())); },
        "session admitted a discontinuous source clock");
    expect(session.source_intervals_consumed() == 1'600U &&
               session.acoustic_frames_produced() == 3'840U,
           "rejected source clock mutated session state");
}

void test_partition_equality_identity_mapping_and_finite_pressure() {
    const auto assembly = make_assembly();
    const ExhaustAcousticEnvironment environment{101'325.0, 298.15};
    ExhaustAcousticSession contiguous{assembly, environment};
    ExhaustAcousticSession partitioned{assembly, environment};
    ExhaustAcousticSession reordered{assembly, environment};

    const auto whole = make_capture(0U, 1'600U);
    const auto expected = contiguous.process(whole.view());
    expect_finite(expected);

    std::array<std::vector<double>, 2> partitioned_pressure;
    std::uint64_t source_offset = 0U;
    std::uint64_t expected_acoustic_offset = 0U;
    for (const auto count : {400U, 800U, 400U}) {
        const auto block = make_capture(source_offset, count);
        const auto output = partitioned.process(block.view());
        expect(output.first_frame_index == expected_acoustic_offset,
               "partitioned block published the wrong acoustic frame origin");
        expect_finite(output);
        append(partitioned_pressure[0], output.outlets[0].pressure_pa);
        append(partitioned_pressure[1], output.outlets[1].pressure_pa);
        source_offset += count;
        expected_acoustic_offset += output.frame_count();
    }
    expect(partitioned_pressure[0] == expected.outlets[0].pressure_pa &&
               partitioned_pressure[1] == expected.outlets[1].pressure_pa,
           "session state changed across capture-block partitions");

    const auto reordered_source = make_capture(0U, 1'600U, {6, 2, 4, 1, 5, 3});
    const auto reordered_output = reordered.process(reordered_source.view());
    expect(reordered_output == expected,
           "source lane order changed identity-routed acoustic output");
    expect(energy(expected.outlets[0].pressure_pa) > 0.0L &&
               energy(expected.outlets[1].pressure_pa) > 0.0L,
           "active six-cylinder capture did not reach both radiating outlets");
}

void test_front_route_isolation_and_invalid_capture_is_transactional() {
    const auto assembly = make_assembly();
    const ExhaustAcousticEnvironment environment{101'325.0, 298.15};
    ExhaustAcousticSession isolated{assembly, environment};
    const auto front_only = make_capture(0U, 1'600U, {1, 2, 3, 4, 5, 6}, 0x01U);
    const auto isolated_output = isolated.process(front_only.view());
    expect(energy(isolated_output.outlets[0].pressure_pa) > 0.0L,
           "front-bound cylinder did not reach the front outlet");
    expect(std::ranges::all_of(isolated_output.outlets[1].pressure_pa,
                               [](double sample) { return sample == 0.0; }),
           "front-bound cylinder leaked into the independent rear network");

    ExhaustAcousticSession candidate{assembly, environment};
    ExhaustAcousticSession reference{assembly, environment};
    const auto first = make_capture(0U, 400U);
    expect(candidate.process(first.view()) == reference.process(first.view()),
           "identical sessions diverged before transactional rejection test");
    auto invalid = make_capture(400U, 400U);
    invalid.samples[17].upstream_density_kg_m3 =
        std::numeric_limits<double>::infinity();
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(candidate.process(invalid.view())); },
        "session admitted non-finite captured source data");
    const auto second = make_capture(400U, 400U);
    expect(candidate.process(second.view()) == reference.process(second.view()),
           "rejected capture block mutated continuous acoustic state");

    auto wrong_identity = make_capture(800U, 8U);
    wrong_identity.ports[0] = {PortId{999}, CylinderId{1}, PortKind::exhaust};
    expect(validate(wrong_identity.view()).ok(),
           "wrong-assembly identity fixture was not independently valid capture");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(candidate.process(wrong_identity.view())); },
        "session admitted a capture port absent from its assembly");
}

void run_tests() {
    test_constructor_pins_environment_methods_and_exact_topology();
    test_exact_clock_zero_source_passivity_and_route_identity();
    test_partition_equality_identity_mapping_and_finite_pressure();
    test_front_route_isolation_and_invalid_capture_is_transactional();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Exhaust acoustic session test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
