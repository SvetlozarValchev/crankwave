#include "simulation/chen_flynn_cycle_mean_loss.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kPascalPerBar = 100000.0;
constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

[[nodiscard]] ChenFlynnCycleMeanLossCalculation
fail(ChenFlynnCycleMeanLossErrorCode code,
     std::size_t element_index = kNoChenFlynnInputElement) noexcept {
    return ChenFlynnCycleMeanLossError{code, element_index};
}

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

} // namespace

ChenFlynnCycleMeanLossCalculation calculate_chen_flynn_cycle_mean_loss(
    const ChenFlynnCycleMeanLossPlan &plan,
    const ChenFlynnCycleMeanLossInput &input) noexcept {
    const std::array coefficients{
        plan.constant_fmep_bar,
        plan.peak_pressure_coefficient,
        plan.mean_piston_speed_coefficient_bar_s_per_m,
        plan.mean_piston_speed_squared_coefficient_bar_s2_per_m2,
    };
    bool has_positive_coefficient = false;
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        const double coefficient = coefficients[index];
        if (!finite(coefficient)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::nonfinite_plan_coefficient,
                        index);
        }
        if (coefficient < 0.0 || std::signbit(coefficient)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::negative_plan_coefficient,
                        index);
        }
        has_positive_coefficient = has_positive_coefficient || coefficient > 0.0;
    }
    if (!has_positive_coefficient) {
        return fail(ChenFlynnCycleMeanLossErrorCode::zero_loss_plan);
    }

    if (!finite(input.engine_speed_rpm)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::nonfinite_engine_speed);
    }
    if (input.engine_speed_rpm <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::nonpositive_engine_speed);
    }
    if (!finite(input.stroke_m)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::nonfinite_stroke);
    }
    if (input.stroke_m <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::nonpositive_stroke);
    }
    if (input.cylinders.empty()) {
        return fail(ChenFlynnCycleMeanLossErrorCode::empty_cylinder_input);
    }

    std::uint32_t previous_cylinder_id = 0;
    for (std::size_t index = 0; index < input.cylinders.size(); ++index) {
        const auto &cylinder = input.cylinders[index];
        if (cylinder.stable_cylinder_id == 0) {
            return fail(ChenFlynnCycleMeanLossErrorCode::invalid_cylinder_identity,
                        index);
        }
        if (index != 0 && cylinder.stable_cylinder_id <= previous_cylinder_id) {
            return fail(ChenFlynnCycleMeanLossErrorCode::unstable_cylinder_order,
                        index);
        }
        if (!finite(cylinder.displacement_m3)) {
            return fail(
                ChenFlynnCycleMeanLossErrorCode::nonfinite_cylinder_displacement,
                index);
        }
        if (cylinder.displacement_m3 <= 0.0) {
            return fail(
                ChenFlynnCycleMeanLossErrorCode::nonpositive_cylinder_displacement,
                index);
        }
        if (!finite(cylinder.peak_pressure_pa_abs)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::nonfinite_peak_pressure,
                        index);
        }
        if (cylinder.peak_pressure_pa_abs <= 0.0) {
            return fail(ChenFlynnCycleMeanLossErrorCode::nonpositive_peak_pressure,
                        index);
        }
        previous_cylinder_id = cylinder.stable_cylinder_id;
    }

    // Preserve the documented operation order instead of allowing an implementation
    // to silently replace it with a differently rounded algebraic form.
    const double twice_stroke_m = 2.0 * input.stroke_m;
    if (!finite(twice_stroke_m)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    const double stroke_rpm_m_per_minute = twice_stroke_m * input.engine_speed_rpm;
    if (!finite(stroke_rpm_m_per_minute)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    const double mean_piston_speed_m_s = stroke_rpm_m_per_minute / 60.0;
    if (!finite(mean_piston_speed_m_s)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    if (mean_piston_speed_m_s <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result);
    }

    double total_displacement_m3 = 0.0;
    double displacement_pressure_sum_pa_m3 = 0.0;
    for (std::size_t index = 0; index < input.cylinders.size(); ++index) {
        const auto &cylinder = input.cylinders[index];
        const double weighted_pressure_pa_m3 =
            cylinder.displacement_m3 * cylinder.peak_pressure_pa_abs;
        if (!finite(weighted_pressure_pa_m3)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow, index);
        }
        if (weighted_pressure_pa_m3 <= 0.0) {
            return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result,
                        index);
        }

        const double next_total_displacement_m3 =
            total_displacement_m3 + cylinder.displacement_m3;
        const double next_displacement_pressure_sum_pa_m3 =
            displacement_pressure_sum_pa_m3 + weighted_pressure_pa_m3;
        if (!finite(next_total_displacement_m3) ||
            !finite(next_displacement_pressure_sum_pa_m3)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow, index);
        }
        if (!(next_total_displacement_m3 > total_displacement_m3) ||
            !(next_displacement_pressure_sum_pa_m3 > displacement_pressure_sum_pa_m3)) {
            return fail(ChenFlynnCycleMeanLossErrorCode::derived_precision_loss, index);
        }
        total_displacement_m3 = next_total_displacement_m3;
        displacement_pressure_sum_pa_m3 = next_displacement_pressure_sum_pa_m3;
    }

    const double weighted_peak_pressure_pa_abs =
        displacement_pressure_sum_pa_m3 / total_displacement_m3;
    const double weighted_peak_pressure_bar_abs =
        weighted_peak_pressure_pa_abs / kPascalPerBar;
    if (!finite(weighted_peak_pressure_pa_abs) ||
        !finite(weighted_peak_pressure_bar_abs)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    if (weighted_peak_pressure_pa_abs <= 0.0 || weighted_peak_pressure_bar_abs <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result);
    }

    const double mean_piston_speed_squared_m2_s2 =
        mean_piston_speed_m_s * mean_piston_speed_m_s;
    if (!finite(mean_piston_speed_squared_m2_s2)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    const double peak_pressure_term_bar =
        plan.peak_pressure_coefficient * weighted_peak_pressure_bar_abs;
    const double mean_speed_term_bar =
        plan.mean_piston_speed_coefficient_bar_s_per_m * mean_piston_speed_m_s;
    const double mean_speed_squared_term_bar =
        plan.mean_piston_speed_squared_coefficient_bar_s2_per_m2 *
        mean_piston_speed_squared_m2_s2;
    if (!finite(peak_pressure_term_bar) || !finite(mean_speed_term_bar) ||
        !finite(mean_speed_squared_term_bar)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }

    const double constant_plus_pressure_bar =
        plan.constant_fmep_bar + peak_pressure_term_bar;
    const double through_mean_speed_bar =
        constant_plus_pressure_bar + mean_speed_term_bar;
    const double friction_mean_effective_pressure_bar =
        through_mean_speed_bar + mean_speed_squared_term_bar;
    if (!finite(constant_plus_pressure_bar) || !finite(through_mean_speed_bar) ||
        !finite(friction_mean_effective_pressure_bar)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    if (friction_mean_effective_pressure_bar <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result);
    }

    const double friction_mean_effective_pressure_pa =
        kPascalPerBar * friction_mean_effective_pressure_bar;
    const double positive_aggregate_loss_work_j =
        friction_mean_effective_pressure_pa * total_displacement_m3;
    if (!finite(friction_mean_effective_pressure_pa) ||
        !finite(positive_aggregate_loss_work_j)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    if (positive_aggregate_loss_work_j <= 0.0) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result);
    }

    const double running_direction_cycle_mean_loss_torque_nm =
        -positive_aggregate_loss_work_j / kFourStrokeCycleRadians;
    if (!finite(running_direction_cycle_mean_loss_torque_nm)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_overflow);
    }
    if (!(running_direction_cycle_mean_loss_torque_nm < 0.0)) {
        return fail(ChenFlynnCycleMeanLossErrorCode::derived_nonpositive_result);
    }

    return ChenFlynnCycleMeanLossResult{
        total_displacement_m3,
        mean_piston_speed_m_s,
        weighted_peak_pressure_pa_abs,
        weighted_peak_pressure_bar_abs,
        friction_mean_effective_pressure_bar,
        positive_aggregate_loss_work_j,
        running_direction_cycle_mean_loss_torque_nm,
    };
}

} // namespace engine_sim_offline::simulation
