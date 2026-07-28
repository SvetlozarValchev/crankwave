#include "simulation/chen_flynn_cycle_mean_loss.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::simulation;

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] std::uint64_t bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] ChenFlynnCycleMeanLossPlan generic_plan() noexcept {
    return {
        0.4,
        0.005,
        0.09,
        0.0009,
    };
}

template <std::size_t Size>
[[nodiscard]] ChenFlynnCycleMeanLossInput
input(std::array<ChenFlynnCylinderPeakPressureInput, Size> &cylinders,
      double rpm = 3000.0, double stroke_m = 0.1) noexcept {
    return {rpm, stroke_m, cylinders};
}

[[nodiscard]] const ChenFlynnCycleMeanLossResult &
require_result(const ChenFlynnCycleMeanLossCalculation &calculation,
               std::string_view message) {
    const auto *result = std::get_if<ChenFlynnCycleMeanLossResult>(&calculation);
    expect(result != nullptr, message);
    return *result;
}

void expect_error(const ChenFlynnCycleMeanLossPlan &plan,
                  const ChenFlynnCycleMeanLossInput &value,
                  ChenFlynnCycleMeanLossErrorCode expected_code,
                  std::size_t expected_index, std::string_view message) {
    const auto calculation = calculate_chen_flynn_cycle_mean_loss(plan, value);
    const auto *error = std::get_if<ChenFlynnCycleMeanLossError>(&calculation);
    expect(error != nullptr && error->code == expected_code &&
               error->element_index == expected_index,
           message);
}

void test_each_analytic_coefficient_and_mean_speed() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{1, 0.002, 2.0e6},
    };
    const auto value = input(cylinders);

    const std::array plans{
        ChenFlynnCycleMeanLossPlan{0.4, 0.0, 0.0, 0.0},
        ChenFlynnCycleMeanLossPlan{0.0, 0.005, 0.0, 0.0},
        ChenFlynnCycleMeanLossPlan{0.0, 0.0, 0.09, 0.0},
        ChenFlynnCycleMeanLossPlan{0.0, 0.0, 0.0, 0.0009},
        generic_plan(),
    };
    constexpr std::array expected_fmep_bar{
        0.4, 0.1, 0.9, 0.09, 1.49,
    };

    for (std::size_t index = 0; index < plans.size(); ++index) {
        const auto calculation =
            calculate_chen_flynn_cycle_mean_loss(plans[index], value);
        const auto &result =
            require_result(calculation, "analytic coefficient plan was rejected");
        expect_near(result.mean_piston_speed_m_s, 10.0, 0.0,
                    "mean piston speed formula changed");
        expect_near(result.friction_mean_effective_pressure_bar,
                    expected_fmep_bar[index], 2e-15,
                    "analytic Chen-Flynn coefficient term changed");
    }
}

void test_unequal_displacement_weighting_and_generic_formula() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{2, 0.001, 1.0e6},
        ChenFlynnCylinderPeakPressureInput{7, 0.003, 3.0e6},
    };
    const auto calculation =
        calculate_chen_flynn_cycle_mean_loss(generic_plan(), input(cylinders));
    const auto &result =
        require_result(calculation, "unequal cylinder displacements were rejected");

    expect_near(result.total_displacement_m3, 0.004, 0.0,
                "total cylinder displacement changed");
    expect_near(result.displacement_weighted_peak_pressure_pa_abs, 2.5e6, 0.0,
                "displacement-weighted absolute pmax changed");
    expect_near(result.displacement_weighted_peak_pressure_bar_abs, 25.0, 0.0,
                "weighted pmax Pa-to-bar conversion changed");
    expect_near(result.friction_mean_effective_pressure_bar, 1.515, 3e-15,
                "generic Chen-Flynn formula changed");
}

void test_unit_conversion_work_and_torque_identity() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{1, 0.001, 1.0e6},
        ChenFlynnCylinderPeakPressureInput{4, 0.003, 3.0e6},
    };
    const auto calculation =
        calculate_chen_flynn_cycle_mean_loss(generic_plan(), input(cylinders));
    const auto &result =
        require_result(calculation, "unit-identity input was rejected");

    const double expected_work_j = 100000.0 *
                                   result.friction_mean_effective_pressure_bar *
                                   result.total_displacement_m3;
    expect_near(result.positive_aggregate_loss_work_j, 606.0, 2e-13,
                "aggregate loss work unit conversion changed");
    expect_near(result.positive_aggregate_loss_work_j, expected_work_j, 0.0,
                "FMEP-to-work identity changed");
    expect(result.positive_aggregate_loss_work_j > 0.0,
           "aggregate loss work is not positive");
    expect(result.running_direction_cycle_mean_loss_torque_nm < 0.0,
           "running-direction loss torque is not negative");
    expect_near(result.running_direction_cycle_mean_loss_torque_nm *
                    kFourStrokeCycleRadians,
                -result.positive_aggregate_loss_work_j, 2e-13,
                "four-stroke loss-work/mean-torque identity changed");
}

void test_plan_and_common_input_rejection() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{1, 0.002, 2.0e6},
    };
    auto value = input(cylinders);
    auto plan = generic_plan();

    plan.constant_fmep_bar = std::numeric_limits<double>::quiet_NaN();
    expect_error(plan, value,
                 ChenFlynnCycleMeanLossErrorCode::nonfinite_plan_coefficient, 0,
                 "nonfinite plan coefficient was admitted");
    plan = generic_plan();
    plan.peak_pressure_coefficient = -0.001;
    expect_error(plan, value,
                 ChenFlynnCycleMeanLossErrorCode::negative_plan_coefficient, 1,
                 "negative plan coefficient was admitted");
    expect_error({}, value, ChenFlynnCycleMeanLossErrorCode::zero_loss_plan,
                 kNoChenFlynnInputElement, "zero-loss plan was admitted");

    value.engine_speed_rpm = std::numeric_limits<double>::infinity();
    expect_error(generic_plan(), value,
                 ChenFlynnCycleMeanLossErrorCode::nonfinite_engine_speed,
                 kNoChenFlynnInputElement, "nonfinite engine speed was admitted");
    value.engine_speed_rpm = 0.0;
    expect_error(generic_plan(), value,
                 ChenFlynnCycleMeanLossErrorCode::nonpositive_engine_speed,
                 kNoChenFlynnInputElement, "zero engine speed was admitted");
    value = input(cylinders);
    value.stroke_m = std::numeric_limits<double>::quiet_NaN();
    expect_error(generic_plan(), value,
                 ChenFlynnCycleMeanLossErrorCode::nonfinite_stroke,
                 kNoChenFlynnInputElement, "nonfinite stroke was admitted");
    value.stroke_m = -0.1;
    expect_error(generic_plan(), value,
                 ChenFlynnCycleMeanLossErrorCode::nonpositive_stroke,
                 kNoChenFlynnInputElement, "negative stroke was admitted");

    const ChenFlynnCycleMeanLossInput empty{3000.0, 0.1, {}};
    expect_error(generic_plan(), empty,
                 ChenFlynnCycleMeanLossErrorCode::empty_cylinder_input,
                 kNoChenFlynnInputElement, "empty cylinder input was admitted");
}

void test_cylinder_input_rejection_and_stable_order() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{1, 0.001, 1.0e6},
        ChenFlynnCylinderPeakPressureInput{3, 0.001, 2.0e6},
    };

    auto changed = cylinders;
    changed[0].stable_cylinder_id = 0;
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::invalid_cylinder_identity, 0,
                 "zero stable cylinder identity was admitted");
    changed = cylinders;
    changed[1].stable_cylinder_id = 1;
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::unstable_cylinder_order, 1,
                 "duplicate cylinder identity was admitted");
    changed = cylinders;
    changed[0].stable_cylinder_id = 4;
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::unstable_cylinder_order, 1,
                 "descending cylinder order was admitted");

    changed = cylinders;
    changed[1].displacement_m3 = std::numeric_limits<double>::infinity();
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::nonfinite_cylinder_displacement, 1,
                 "nonfinite cylinder displacement was admitted");
    changed = cylinders;
    changed[1].displacement_m3 = 0.0;
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::nonpositive_cylinder_displacement, 1,
                 "zero cylinder displacement was admitted");
    changed = cylinders;
    changed[1].peak_pressure_pa_abs = std::numeric_limits<double>::quiet_NaN();
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::nonfinite_peak_pressure, 1,
                 "nonfinite peak pressure was admitted");
    changed = cylinders;
    changed[1].peak_pressure_pa_abs = -1.0;
    expect_error(generic_plan(), input(changed),
                 ChenFlynnCycleMeanLossErrorCode::nonpositive_peak_pressure, 1,
                 "negative absolute peak pressure was admitted");
}

void test_derived_overflow_and_underflow_fail_closed() {
    std::array normal{
        ChenFlynnCylinderPeakPressureInput{1, 0.002, 2.0e6},
    };
    auto value = input(normal);
    value.stroke_m = std::numeric_limits<double>::max();
    expect_error(generic_plan(), value,
                 ChenFlynnCycleMeanLossErrorCode::derived_overflow,
                 kNoChenFlynnInputElement, "mean-piston-speed overflow was admitted");

    std::array weighted_product_overflow{
        ChenFlynnCylinderPeakPressureInput{1, std::numeric_limits<double>::max() / 4.0,
                                           10.0},
    };
    expect_error(generic_plan(), input(weighted_product_overflow),
                 ChenFlynnCycleMeanLossErrorCode::derived_overflow, 0,
                 "weighted-pressure overflow was admitted");

    auto coefficient_overflow = generic_plan();
    coefficient_overflow.peak_pressure_coefficient = std::numeric_limits<double>::max();
    expect_error(coefficient_overflow, input(normal),
                 ChenFlynnCycleMeanLossErrorCode::derived_overflow,
                 kNoChenFlynnInputElement, "FMEP overflow was admitted");

    std::array work_overflow{
        ChenFlynnCylinderPeakPressureInput{1, std::numeric_limits<double>::max() / 2.0,
                                           1.0},
    };
    expect_error(ChenFlynnCycleMeanLossPlan{0.4, 0.0, 0.0, 0.0}, input(work_overflow),
                 ChenFlynnCycleMeanLossErrorCode::derived_overflow,
                 kNoChenFlynnInputElement, "loss-work overflow was admitted");

    std::array work_underflow{
        ChenFlynnCylinderPeakPressureInput{1, std::numeric_limits<double>::denorm_min(),
                                           1.0},
    };
    expect_error(ChenFlynnCycleMeanLossPlan{std::numeric_limits<double>::denorm_min(),
                                            0.0, 0.0, 0.0},
                 input(work_underflow),
                 ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result,
                 kNoChenFlynnInputElement, "underflowed zero loss work was published");
}

void test_stable_input_order_has_repeatable_binary64_result() {
    std::array cylinders{
        ChenFlynnCylinderPeakPressureInput{1, 0.0007, 1.7e6},
        ChenFlynnCylinderPeakPressureInput{4, 0.0011, 2.9e6},
        ChenFlynnCylinderPeakPressureInput{9, 0.0009, 2.2e6},
    };
    const auto first_calculation =
        calculate_chen_flynn_cycle_mean_loss(generic_plan(), input(cylinders));
    const auto second_calculation =
        calculate_chen_flynn_cycle_mean_loss(generic_plan(), input(cylinders));
    const auto &first =
        require_result(first_calculation, "stable ordered input was rejected");
    const auto &second =
        require_result(second_calculation, "repeated stable input was rejected");

    expect(bits(first.total_displacement_m3) == bits(second.total_displacement_m3) &&
               bits(first.displacement_weighted_peak_pressure_pa_abs) ==
                   bits(second.displacement_weighted_peak_pressure_pa_abs) &&
               bits(first.friction_mean_effective_pressure_bar) ==
                   bits(second.friction_mean_effective_pressure_bar) &&
               bits(first.positive_aggregate_loss_work_j) ==
                   bits(second.positive_aggregate_loss_work_j) &&
               bits(first.running_direction_cycle_mean_loss_torque_nm) ==
                   bits(second.running_direction_cycle_mean_loss_torque_nm),
           "stable ordered input did not reproduce identical binary64 results");

    std::array reversed{
        cylinders[2],
        cylinders[1],
        cylinders[0],
    };
    expect_error(generic_plan(), input(reversed),
                 ChenFlynnCycleMeanLossErrorCode::unstable_cylinder_order, 1,
                 "unstable cylinder input order was silently reduced");
}

void run_tests() {
    test_each_analytic_coefficient_and_mean_speed();
    test_unequal_displacement_weighting_and_generic_formula();
    test_unit_conversion_work_and_torque_identity();
    test_plan_and_common_input_rejection();
    test_cylinder_input_rejection_and_stable_order();
    test_derived_overflow_and_underflow_fail_closed();
    test_stable_input_order_has_repeatable_binary64_result();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Chen-Flynn cycle-mean loss failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
