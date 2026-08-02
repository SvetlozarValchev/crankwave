#include "simulation/chen_flynn_per_cylinder_travel_cycle_mean_loss.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::simulation;

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(const double actual, const double expected, const double tolerance,
                 const std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] std::uint64_t bits(const double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

void expect_bits(const double actual, const std::uint64_t expected,
                 const std::string_view message) {
    expect(bits(actual) == expected, message);
}

[[nodiscard]] std::uint8_t hex_nibble(const char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::runtime_error{"invalid pinned digest digit"};
}

[[nodiscard]] contract::Sha256Digest digest_from_hex(const std::string_view value) {
    expect(value.size() == 64U, "pinned method digest has the wrong length");
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (hex_nibble(value[index * 2U]) << 4U) |
            hex_nibble(value[index * 2U + 1U]));
    }
    return result;
}

[[nodiscard]] ChenFlynnCycleMeanLossPlan generic_plan() noexcept {
    return {0.4, 0.005, 0.09, 0.0009};
}

template <std::size_t Size>
[[nodiscard]] ChenFlynnPerCylinderPistonTravelCycleMeanLossInput
input(std::array<ChenFlynnPerCylinderPistonTravelInput, Size> &cylinders,
      const double rpm = 60.0) noexcept {
    return {rpm, cylinders};
}

[[nodiscard]] const ChenFlynnPerCylinderPistonTravelCycleMeanLossResult &
require_result(
    const ChenFlynnPerCylinderPistonTravelCycleMeanLossCalculation &calculation,
    const std::string_view message) {
    const auto *result =
        std::get_if<ChenFlynnPerCylinderPistonTravelCycleMeanLossResult>(
            &calculation);
    expect(result != nullptr, message);
    return *result;
}

void expect_error(
    const ChenFlynnCycleMeanLossPlan &plan,
    const ChenFlynnPerCylinderPistonTravelCycleMeanLossInput &value,
    const ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode expected_code,
    const std::size_t expected_index, const std::string_view message) {
    const auto calculation =
        calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(plan, value);
    const auto *error =
        std::get_if<ChenFlynnPerCylinderPistonTravelCycleMeanLossError>(
            &calculation);
    expect(error != nullptr && error->code == expected_code &&
               error->element_index == expected_index,
           message);
}

void test_method_identity_and_claim_boundary() {
    constexpr std::string_view kExpectedDigest =
        "6f2b6aeaff65da4c0cbc166a6d030f8085a594cf3c57427bb405c6eb6b27bcc1";
    const auto descriptor =
        chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_descriptor();
    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           "per-cylinder travel method descriptor is not canonical LF text");
    expect(descriptor.find("engine-sim-offline-greenfield-per-cylinder-work-application") !=
                   std::string_view::npos &&
               descriptor.find("not-pristine-engine-sim-behavior") !=
                   std::string_view::npos &&
               descriptor.find("not-a-chen-flynn-literature-multicylinder-reduction") !=
                   std::string_view::npos,
           "heterogeneous method descriptor lost its explicit non-authority claim");

    const auto expected_digest = digest_from_hex(kExpectedDigest);
    const auto actual_digest = contract::sha256(std::as_bytes(
        std::span<const char>{descriptor.data(), descriptor.size()}));
    expect(actual_digest == expected_digest,
           "per-cylinder travel method descriptor digest changed");
    const auto &identity =
        chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity();
    expect(identity.id ==
                   kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodId &&
               identity.version ==
                   kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodVersion &&
               identity.configuration_sha256 == expected_digest &&
               contract::validate(identity).ok() &&
               &identity ==
                   &chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity(),
           "per-cylinder travel method identity is not exact and stable");
}

void test_per_cylinder_work_and_weighted_diagnostics() {
    std::array cylinders{
        ChenFlynnPerCylinderPistonTravelInput{2U, 0.001, 2.0, 1.0e6},
        ChenFlynnPerCylinderPistonTravelInput{7U, 0.003, 6.0, 3.0e6},
    };
    const auto preserved_inputs = cylinders;
    const auto calculation =
        calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
            generic_plan(), input(cylinders));
    const auto &result = require_result(
        calculation, "valid heterogeneous piston travels were rejected");

    expect(cylinders == preserved_inputs,
           "calculation changed a cylinder identity or physical input");
    expect_near(result.total_swept_displacement_m3, 0.004, 0.0,
                "stable swept-displacement sum changed");
    expect_near(result.displacement_weighted_peak_pressure_pa_abs, 2.5e6, 0.0,
                "displacement-weighted peak pressure changed");
    expect_near(result.displacement_weighted_peak_pressure_bar_abs, 25.0, 0.0,
                "weighted peak pressure Pa-to-bar conversion changed");
    expect_near(result.displacement_weighted_mean_piston_speed_m_s, 5.0, 1.0e-15,
                "weighted piston speed changed");
    expect_near(result.displacement_weighted_mean_squared_piston_speed_m2_s2,
                28.0, 4.0e-15, "weighted squared piston speed changed");

    // At 60 rpm, each path length is numerically its mean speed. Applying the
    // correlation to each cylinder gives 0.6336 and 1.1224 bar respectively.
    expect_near(result.positive_aggregate_loss_work_j, 400.08, 2.0e-13,
                "stable per-cylinder loss-work sum changed");
    expect_near(result.friction_mean_effective_pressure_bar, 1.0002, 4.0e-16,
                "work-derived aggregate FMEP changed");
    expect_near(result.running_direction_cycle_mean_loss_torque_nm *
                    kFourStrokeCycleRadians,
                -result.positive_aggregate_loss_work_j, 2.0e-13,
                "aggregate work-to-torque identity changed");
}

void test_squared_speed_is_reduced_after_per_cylinder_evaluation() {
    std::array cylinders{
        ChenFlynnPerCylinderPistonTravelInput{2U, 0x1p-10, 0.125, 1.0e6},
        ChenFlynnPerCylinderPistonTravelInput{7U, 3.0 * 0x1p-10, 0.375, 3.0e6},
    };
    const ChenFlynnCycleMeanLossPlan squared_speed_only{0.0, 0.0, 0.0, 0.25};
    const auto calculation =
        calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
            squared_speed_only, input(cylinders, 960.0));
    const auto &result = require_result(
        calculation, "squared-speed-only heterogeneous plan was rejected");

    // Independent exact-binary vector: U={2,6} m/s and displacement weights
    // {1,3}. The mean of U^2 is 28 while the square of mean U is only 25.
    expect_bits(result.total_swept_displacement_m3,
                UINT64_C(0x3f70000000000000),
                "exact heterogeneous displacement vector changed");
    expect_bits(result.displacement_weighted_mean_piston_speed_m_s,
                UINT64_C(0x4014000000000000),
                "exact weighted mean speed vector changed");
    expect_bits(result.displacement_weighted_mean_squared_piston_speed_m2_s2,
                UINT64_C(0x403c000000000000),
                "exact weighted mean squared-speed vector changed");
    expect_bits(result.displacement_weighted_peak_pressure_pa_abs,
                UINT64_C(0x414312d000000000),
                "exact weighted pressure vector changed");
    expect_bits(result.displacement_weighted_peak_pressure_bar_abs,
                UINT64_C(0x4039000000000000),
                "exact weighted pressure-bar vector changed");
    expect(result.displacement_weighted_mean_squared_piston_speed_m2_s2 !=
               result.displacement_weighted_mean_piston_speed_m_s *
                   result.displacement_weighted_mean_piston_speed_m_s,
           "heterogeneous squared-speed term collapsed to square of mean speed");
    expect_bits(result.friction_mean_effective_pressure_bar,
                UINT64_C(0x401c000000000000),
                "exact per-cylinder squared-speed FMEP changed");
    expect_bits(result.positive_aggregate_loss_work_j,
                UINT64_C(0x40a55cc000000000),
                "exact per-cylinder squared-speed work changed");
    expect_bits(result.running_direction_cycle_mean_loss_torque_nm,
                UINT64_C(0xc06b33075cd544e2),
                "exact per-cylinder squared-speed torque changed");
}

void test_typed_input_rejection() {
    std::array valid{
        ChenFlynnPerCylinderPistonTravelInput{1U, 0.001, 0.2, 1.0e6},
        ChenFlynnPerCylinderPistonTravelInput{3U, 0.002, 0.21, 2.0e6},
    };
    const auto valid_input = input(valid, 3000.0);
    auto plan = generic_plan();
    plan.constant_fmep_bar = -0.0;
    expect_error(
        plan, valid_input,
        ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
            negative_plan_coefficient,
        0U, "negative-zero coefficient was admitted");
    expect_error({}, valid_input,
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     zero_loss_plan,
                 kNoChenFlynnPerCylinderPistonTravelInputElement,
                 "zero loss plan was admitted");

    auto invalid_common = valid_input;
    invalid_common.engine_speed_rpm =
        std::numeric_limits<double>::infinity();
    expect_error(generic_plan(), invalid_common,
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonfinite_engine_speed,
                 kNoChenFlynnPerCylinderPistonTravelInputElement,
                 "nonfinite engine speed was admitted");
    invalid_common.engine_speed_rpm = 0.0;
    expect_error(generic_plan(), invalid_common,
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonpositive_engine_speed,
                 kNoChenFlynnPerCylinderPistonTravelInputElement,
                 "zero engine speed was admitted");
    expect_error(generic_plan(), {3000.0, {}},
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     empty_cylinder_input,
                 kNoChenFlynnPerCylinderPistonTravelInputElement,
                 "empty cylinder input was admitted");

    auto changed = valid;
    changed[0].stable_cylinder_id = 0U;
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     invalid_cylinder_identity,
                 0U, "zero cylinder identity was admitted");
    changed = valid;
    changed[1].stable_cylinder_id = 1U;
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     unstable_cylinder_order,
                 1U, "duplicate cylinder identity was admitted");
    changed = valid;
    changed[1].swept_displacement_m3 = 0.0;
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonpositive_swept_displacement,
                 1U, "zero swept displacement was admitted");
    changed = valid;
    changed[1].piston_axis_path_length_m_per_crank_revolution =
        std::numeric_limits<double>::quiet_NaN();
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonfinite_piston_axis_path_length,
                 1U, "nonfinite piston path was admitted");
    changed = valid;
    changed[1].piston_axis_path_length_m_per_crank_revolution = -0.1;
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonpositive_piston_axis_path_length,
                 1U, "negative piston path was admitted");
    changed = valid;
    changed[1].peak_pressure_pa_abs =
        std::numeric_limits<double>::infinity();
    expect_error(generic_plan(), input(changed, 3000.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     nonfinite_peak_pressure,
                 1U, "nonfinite peak pressure was admitted");
}

void test_overflow_and_stable_binary64_order() {
    std::array cylinders{
        ChenFlynnPerCylinderPistonTravelInput{1U, 0.0007, 0.18, 1.7e6},
        ChenFlynnPerCylinderPistonTravelInput{4U, 0.0011, 0.22, 2.9e6},
        ChenFlynnPerCylinderPistonTravelInput{9U, 0.0009, 0.20, 2.2e6},
    };
    const auto first_calculation =
        calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
            generic_plan(), input(cylinders, 3100.0));
    const auto second_calculation =
        calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
            generic_plan(), input(cylinders, 3100.0));
    const auto &first = require_result(first_calculation,
                                       "stable heterogeneous input was rejected");
    const auto &second = require_result(second_calculation,
                                        "repeated heterogeneous input was rejected");
    expect(bits(first.total_swept_displacement_m3) ==
                   bits(second.total_swept_displacement_m3) &&
               bits(first.displacement_weighted_mean_piston_speed_m_s) ==
                   bits(second.displacement_weighted_mean_piston_speed_m_s) &&
               bits(first.displacement_weighted_mean_squared_piston_speed_m2_s2) ==
                   bits(second.displacement_weighted_mean_squared_piston_speed_m2_s2) &&
               bits(first.displacement_weighted_peak_pressure_pa_abs) ==
                   bits(second.displacement_weighted_peak_pressure_pa_abs) &&
               bits(first.friction_mean_effective_pressure_bar) ==
                   bits(second.friction_mean_effective_pressure_bar) &&
               bits(first.positive_aggregate_loss_work_j) ==
                   bits(second.positive_aggregate_loss_work_j) &&
               bits(first.running_direction_cycle_mean_loss_torque_nm) ==
                   bits(second.running_direction_cycle_mean_loss_torque_nm),
           "stable cylinder order did not reproduce identical binary64 output");

    std::array overflow{
        ChenFlynnPerCylinderPistonTravelInput{
            1U, 0.001, std::numeric_limits<double>::max(), 1.0e6},
    };
    expect_error(generic_plan(), input(overflow, 2.0),
                 ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                     derived_overflow,
                 0U, "piston-speed overflow was admitted");
}

void run_tests() {
    test_method_identity_and_claim_boundary();
    test_per_cylinder_work_and_weighted_diagnostics();
    test_squared_speed_is_reduced_after_per_cylinder_evaluation();
    test_typed_input_rejection();
    test_overflow_and_stable_binary64_order();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Chen-Flynn per-cylinder piston-travel loss failure: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
