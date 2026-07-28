#include "simulation/legacy_fixed_crank_torque_accounting.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline;

constexpr contract::Sha256Digest kLegacyLowOrderV1ConfigurationSha256{{
    0x43, 0x54, 0x41, 0x89, 0x0e, 0x0a, 0x5f, 0x8d, 0x01, 0xe8, 0x19,
    0x95, 0xf6, 0x4f, 0x33, 0xd4, 0xc5, 0x54, 0x14, 0x4f, 0x5b, 0x14,
    0x36, 0x89, 0x5e, 0x68, 0x16, 0xf6, 0xdb, 0x85, 0xe3, 0x4c,
}};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] contract::EngineSpec make_engine() {
    const auto included =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    contract::EngineSpec engine;
    engine.methods.losses.value = {
        "legacy_low_order_v1",
        1,
        kLegacyLowOrderV1ConfigurationSha256,
    };
    engine.torque_capability.value = {
        {
            contract::Availability::available,
            contract::Completeness::incomplete,
            included,
            contract::known_torque_term_mask() & ~included,
        },
        {
            contract::Availability::unavailable,
            contract::Completeness::incomplete,
            0,
            0,
        },
        false,
    };
    return engine;
}

[[nodiscard]] contract::LegacyFixedCrankLossV1 make_profile(double magnitude_nm) {
    const auto included =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    contract::LegacyFixedCrankLossV1 profile;
    profile.fixed_crank_friction_magnitude_nm.value = magnitude_nm;
    profile.included_terms.value = included;
    profile.omitted_terms.value = contract::known_torque_term_mask() & ~included;
    return profile;
}

[[nodiscard]] contract::TorqueTelemetry require_telemetry(
    const simulation::LegacyFixedCrankTorqueAccountingEvaluation &evaluation) {
    const auto *telemetry = std::get_if<contract::TorqueTelemetry>(&evaluation);
    expect(telemetry != nullptr,
           "valid legacy fixed crank accounting evaluation was rejected");
    return *telemetry;
}

void expect_error(
    const simulation::LegacyFixedCrankTorqueAccountingEvaluation &evaluation,
    simulation::LegacyFixedCrankTorqueAccountingErrorCode expected,
    std::string_view message) {
    const auto *error =
        std::get_if<simulation::LegacyFixedCrankTorqueAccountingError>(&evaluation);
    expect(error != nullptr && error->code == expected, message);
}

void test_compile_domain_and_inventory() {
    auto engine = make_engine();
    auto profile = make_profile(13.5);
    auto compiled =
        simulation::compile_legacy_fixed_crank_torque_accounting(engine, profile);
    const auto *plan =
        std::get_if<simulation::LegacyFixedCrankTorqueAccountingPlan>(&compiled);
    expect(plan != nullptr && plan->magnitude_nm == 13.5,
           "valid fixed crank accounting plan did not compile exactly");

    profile.fixed_crank_friction_magnitude_nm.value = -1.0;
    compiled =
        simulation::compile_legacy_fixed_crank_torque_accounting(engine, profile);
    expect(std::holds_alternative<contract::ValidationReport>(compiled),
           "negative fixed crank-loss magnitude was admitted");

    profile = make_profile(13.5);
    profile.omitted_terms.value = 0;
    compiled =
        simulation::compile_legacy_fixed_crank_torque_accounting(engine, profile);
    expect(std::holds_alternative<contract::ValidationReport>(compiled),
           "incomplete M3 loss inventory was admitted");

    profile = make_profile(13.5);
    engine.torque_capability.value.cycle_mean_net_shaft.availability =
        contract::Availability::available;
    compiled =
        simulation::compile_legacy_fixed_crank_torque_accounting(engine, profile);
    expect(std::holds_alternative<contract::ValidationReport>(compiled),
           "wrong temporal torque capability was admitted");

    engine = make_engine();
    engine.methods.losses.value.configuration_sha256.bytes[0] ^= 0xffU;
    compiled =
        simulation::compile_legacy_fixed_crank_torque_accounting(engine, profile);
    expect(std::holds_alternative<contract::ValidationReport>(compiled),
           "wrong M3 loss method configuration was admitted");
}

void test_direction_operation_order_and_telemetry() {
    const simulation::LegacyFixedCrankTorqueAccountingPlan plan{13.5};
    const auto forward = require_telemetry(
        simulation::evaluate_legacy_fixed_crank_torque_accounting(plan, 100.0, 20.0));

    const auto indicated =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas);
    const auto crank = contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto included = indicated | crank;
    const auto omitted = contract::known_torque_term_mask() & ~included;
    expect(
        forward.instantaneous_indicated_gas ==
                contract::TorqueValueNm{
                    20.0,
                    contract::Availability::available,
                    contract::Completeness::complete,
                    contract::QuantityUnavailableReason::none,
                    indicated,
                    0,
                } &&
            forward.friction_pump_and_accessory ==
                contract::TorqueValueNm{
                    -13.5,
                    contract::Availability::available,
                    contract::Completeness::incomplete,
                    contract::QuantityUnavailableReason::none,
                    crank,
                    contract::friction_pump_and_accessory_torque_term_mask() & ~crank,
                } &&
            forward.instantaneous_net_shaft ==
                contract::TorqueValueNm{
                    6.5,
                    contract::Availability::available,
                    contract::Completeness::incomplete,
                    contract::QuantityUnavailableReason::none,
                    included,
                    omitted,
                } &&
            forward.instantaneous_power_w.value == 650.0,
        "positive-speed M3 torque telemetry or operation order changed");

    const auto reverse = require_telemetry(
        simulation::evaluate_legacy_fixed_crank_torque_accounting(plan, -100.0, 20.0));
    expect(reverse.friction_pump_and_accessory.value_nm == 13.5 &&
               reverse.instantaneous_net_shaft.value_nm == 33.5 &&
               reverse.instantaneous_power_w.value == -3350.0,
           "negative-speed M3 loss direction or torque sum changed");

    const auto stopped = require_telemetry(
        simulation::evaluate_legacy_fixed_crank_torque_accounting(plan, 0.0, 20.0));
    expect(stopped.friction_pump_and_accessory.value_nm == 0.0 &&
               !std::signbit(stopped.friction_pump_and_accessory.value_nm) &&
               stopped.instantaneous_net_shaft.value_nm == 20.0,
           "zero-speed M3 loss did not preserve positive zero");
}

void test_signed_zero_and_fail_closed() {
    const simulation::LegacyFixedCrankTorqueAccountingPlan negative_zero_plan{-0.0};
    const auto forward =
        require_telemetry(simulation::evaluate_legacy_fixed_crank_torque_accounting(
            negative_zero_plan, 1.0, 2.0));
    expect(std::bit_cast<std::uint64_t>(forward.friction_pump_and_accessory.value_nm) ==
               std::bit_cast<std::uint64_t>(0.0),
           "legacy negative-zero magnitude changed positive-speed sign behavior");

    expect_error(
        simulation::evaluate_legacy_fixed_crank_torque_accounting(
            {1.0}, std::numeric_limits<double>::quiet_NaN(), 2.0),
        simulation::LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_angular_speed,
        "nonfinite angular speed was admitted");
    expect_error(simulation::evaluate_legacy_fixed_crank_torque_accounting(
                     {1.0}, 1.0, std::numeric_limits<double>::infinity()),
                 simulation::LegacyFixedCrankTorqueAccountingErrorCode::
                     nonfinite_indicated_gas_torque,
                 "nonfinite indicated torque was admitted");
    expect_error(
        simulation::evaluate_legacy_fixed_crank_torque_accounting(
            {std::numeric_limits<double>::max()}, -1.0,
            std::numeric_limits<double>::max()),
        simulation::LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_torque_sum,
        "overflowed indicated-plus-loss torque was published");
}

void run_tests() {
    test_compile_domain_and_inventory();
    test_direction_operation_order_and_telemetry();
    test_signed_zero_and_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Legacy fixed crank torque-accounting failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
