#include "simulation/chen_flynn_per_cylinder_travel_cycle_mean_loss.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kPascalPerBar = 100000.0;
constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

constexpr std::string_view kMethodDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=chen-flynn-per-cylinder-piston-travel-cycle-mean-aggregate-loss-v1
version=1
operation=stable-per-cylinder-piston-travel-chen-flynn-work-reduction
correlation_coefficients=constant-fmep-bar,peak-pressure-coefficient,mean-piston-speed-coefficient-bar-s-per-m,mean-piston-speed-squared-coefficient-bar-s2-per-m2
correlation_coefficient_domain=finite-canonical-nonnegative-binary64;negative-zero-is-rejected;at-least-one-coefficient-is-positive
heterogeneous_application_authority=engine-sim-offline-greenfield-per-cylinder-work-application
heterogeneous_application_nonclaim=not-pristine-engine-sim-behavior-and-not-a-chen-flynn-literature-multicylinder-reduction
input_engine_speed_rpm=finite-positive-binary64
cylinder_input=nonempty-array-in-strictly-increasing-nonzero-stable-cylinder-id-order
cylinder_swept_displacement_m3=finite-positive-binary64-from-caller-no-nominal-stroke-reconstruction
cylinder_piston_axis_path_length_m_per_crank_revolution=finite-positive-binary64-from-caller-no-twice-stroke-reconstruction
cylinder_peak_pressure_pa_abs=finite-positive-binary64-absolute-pressure
cylinder_mean_piston_speed_step_1=path-rpm-m-per-minute=piston-axis-path-length-m-per-crank-revolution-times-engine-speed-rpm
cylinder_mean_piston_speed_step_2=mean-piston-speed-m-s=path-rpm-m-per-minute-divided-by-binary64-60
cylinder_mean_piston_speed_squared=mean-piston-speed-m-s-times-mean-piston-speed-m-s
pascal_per_bar=binary64-100000
cylinder_peak_pressure_bar_abs=peak-pressure-pa-abs-divided-by-binary64-100000
cylinder_peak_pressure_term_bar=peak-pressure-coefficient-times-peak-pressure-bar-abs
cylinder_mean_speed_term_bar=mean-piston-speed-coefficient-bar-s-per-m-times-mean-piston-speed-m-s
cylinder_mean_speed_squared_term_bar=mean-piston-speed-squared-coefficient-bar-s2-per-m2-times-mean-piston-speed-squared
cylinder_fmep_sum_order=((constant-fmep-bar-plus-peak-pressure-term-bar)-plus-mean-speed-term-bar)-plus-mean-speed-squared-term-bar
cylinder_loss_work_step_1=friction-mean-effective-pressure-pa=binary64-100000-times-cylinder-fmep-bar
cylinder_loss_work_step_2=positive-loss-work-j=friction-mean-effective-pressure-pa-times-cylinder-swept-displacement-m3
cylinder_diagnostic_products=swept-displacement-times-peak-pressure-pa-abs,swept-displacement-times-mean-piston-speed,and-swept-displacement-times-mean-piston-speed-squared
cylinder_reduction_order=caller-supplied-ascending-stable-cylinder-id-order
cylinder_reduction=separate-left-to-right-binary64-sums-for-swept-displacement,displacement-times-pressure,displacement-times-speed,displacement-times-speed-squared,and-positive-loss-work
cylinder_reduction_progress=each-sum-must-remain-finite-and-increase-strictly
aggregate_weighted_peak_pressure_pa_abs=displacement-pressure-sum-pa-m3-divided-by-total-swept-displacement-m3
aggregate_weighted_peak_pressure_bar_abs=aggregate-weighted-peak-pressure-pa-abs-divided-by-binary64-100000
aggregate_weighted_mean_piston_speed_m_s=displacement-speed-sum-m4-s-divided-by-total-swept-displacement-m3
aggregate_weighted_mean_squared_piston_speed_m2_s2=displacement-speed-squared-sum-m5-s2-divided-by-total-swept-displacement-m3
aggregate_fmep_denominator=binary64-100000-times-total-swept-displacement-m3
aggregate_friction_mean_effective_pressure_bar=positive-aggregate-loss-work-j-divided-by-aggregate-fmep-denominator
four_stroke_cycle_radians=binary64-4-times-std-numbers-pi-v-binary64
running_direction_cycle_mean_loss_torque_nm=negative-positive-aggregate-loss-work-j-divided-by-four-stroke-cycle-radians
result_order=total-swept-displacement-m3,displacement-weighted-mean-piston-speed-m-s,displacement-weighted-mean-squared-piston-speed-m2-s2,displacement-weighted-peak-pressure-pa-abs,displacement-weighted-peak-pressure-bar-abs,friction-mean-effective-pressure-bar,positive-aggregate-loss-work-j,running-direction-cycle-mean-loss-torque-nm
result_domain=all-finite;displacement-pressure-speed-speed-squared-fmep-work-positive;running-direction-loss-torque-negative
component_loss_claim=none-result-is-one-aggregate-loss
brake_or_net_claim=none-result-does-not-prove-term-completeness-or-shaft-output
instantaneous_waveform=none-cycle-mean-only
parallel_reduction=none-all-reductions-follow-declared-cylinder-order
failure=first-domain-or-derived-finiteness-positivity-or-precision-check-in-written-execution-order
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-numbers-pi-v-binary64-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

[[nodiscard]] consteval bool
canonical_lf_descriptor(const std::string_view descriptor) noexcept {
    if (descriptor.empty() || descriptor.back() != '\n') {
        return false;
    }
    for (const char character : descriptor) {
        if (character == '\r' || character == '\0') {
            return false;
        }
    }
    return true;
}

static_assert(canonical_lf_descriptor(kMethodDescriptor));

[[nodiscard]] ChenFlynnPerCylinderPistonTravelCycleMeanLossCalculation
fail(const ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode code,
     const std::size_t element_index =
         kNoChenFlynnPerCylinderPistonTravelInputElement) noexcept {
    return ChenFlynnPerCylinderPistonTravelCycleMeanLossError{code, element_index};
}

[[nodiscard]] bool finite(const double value) noexcept {
    return std::isfinite(value);
}

} // namespace

std::string_view
chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_descriptor()
    noexcept {
    return kMethodDescriptor;
}

const contract::MethodIdentity &
chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{
            kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodId},
        kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodVersion,
        contract::sha256(std::as_bytes(std::span<const char>{
            kMethodDescriptor.data(), kMethodDescriptor.size()})),
    };
    return identity;
}

ChenFlynnPerCylinderPistonTravelCycleMeanLossCalculation
calculate_chen_flynn_per_cylinder_piston_travel_cycle_mean_loss(
    const ChenFlynnCycleMeanLossPlan &plan,
    const ChenFlynnPerCylinderPistonTravelCycleMeanLossInput &input) noexcept {
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
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonfinite_plan_coefficient,
                        index);
        }
        if (coefficient < 0.0 || std::signbit(coefficient)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            negative_plan_coefficient,
                        index);
        }
        has_positive_coefficient = has_positive_coefficient || coefficient > 0.0;
    }
    if (!has_positive_coefficient) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        zero_loss_plan);
    }

    if (!finite(input.engine_speed_rpm)) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        nonfinite_engine_speed);
    }
    if (input.engine_speed_rpm <= 0.0) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        nonpositive_engine_speed);
    }
    if (input.cylinders.empty()) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        empty_cylinder_input);
    }

    std::uint32_t previous_cylinder_id = 0U;
    for (std::size_t index = 0; index < input.cylinders.size(); ++index) {
        const auto &cylinder = input.cylinders[index];
        if (cylinder.stable_cylinder_id == 0U) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            invalid_cylinder_identity,
                        index);
        }
        if (index != 0U && cylinder.stable_cylinder_id <= previous_cylinder_id) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            unstable_cylinder_order,
                        index);
        }
        if (!finite(cylinder.swept_displacement_m3)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonfinite_swept_displacement,
                        index);
        }
        if (cylinder.swept_displacement_m3 <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonpositive_swept_displacement,
                        index);
        }
        if (!finite(cylinder.piston_axis_path_length_m_per_crank_revolution)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonfinite_piston_axis_path_length,
                        index);
        }
        if (cylinder.piston_axis_path_length_m_per_crank_revolution <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonpositive_piston_axis_path_length,
                        index);
        }
        if (!finite(cylinder.peak_pressure_pa_abs)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonfinite_peak_pressure,
                        index);
        }
        if (cylinder.peak_pressure_pa_abs <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            nonpositive_peak_pressure,
                        index);
        }
        previous_cylinder_id = cylinder.stable_cylinder_id;
    }

    double total_swept_displacement_m3 = 0.0;
    double displacement_pressure_sum_pa_m3 = 0.0;
    double displacement_speed_sum_m4_s = 0.0;
    double displacement_speed_squared_sum_m5_s2 = 0.0;
    double positive_aggregate_loss_work_j = 0.0;

    for (std::size_t index = 0; index < input.cylinders.size(); ++index) {
        const auto &cylinder = input.cylinders[index];

        const double path_rpm_m_per_minute =
            cylinder.piston_axis_path_length_m_per_crank_revolution *
            input.engine_speed_rpm;
        const double mean_piston_speed_m_s = path_rpm_m_per_minute / 60.0;
        const double mean_piston_speed_squared_m2_s2 =
            mean_piston_speed_m_s * mean_piston_speed_m_s;
        const double peak_pressure_bar_abs =
            cylinder.peak_pressure_pa_abs / kPascalPerBar;
        if (!finite(path_rpm_m_per_minute) || !finite(mean_piston_speed_m_s) ||
            !finite(mean_piston_speed_squared_m2_s2) ||
            !finite(peak_pressure_bar_abs)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_overflow,
                        index);
        }
        if (mean_piston_speed_m_s <= 0.0 ||
            mean_piston_speed_squared_m2_s2 <= 0.0 ||
            peak_pressure_bar_abs <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_nonpositive_result,
                        index);
        }

        const double peak_pressure_term_bar =
            plan.peak_pressure_coefficient * peak_pressure_bar_abs;
        const double mean_speed_term_bar =
            plan.mean_piston_speed_coefficient_bar_s_per_m *
            mean_piston_speed_m_s;
        const double mean_speed_squared_term_bar =
            plan.mean_piston_speed_squared_coefficient_bar_s2_per_m2 *
            mean_piston_speed_squared_m2_s2;
        const double constant_plus_pressure_bar =
            plan.constant_fmep_bar + peak_pressure_term_bar;
        const double through_mean_speed_bar =
            constant_plus_pressure_bar + mean_speed_term_bar;
        const double cylinder_fmep_bar =
            through_mean_speed_bar + mean_speed_squared_term_bar;
        if (!finite(peak_pressure_term_bar) || !finite(mean_speed_term_bar) ||
            !finite(mean_speed_squared_term_bar) ||
            !finite(constant_plus_pressure_bar) ||
            !finite(through_mean_speed_bar) || !finite(cylinder_fmep_bar)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_overflow,
                        index);
        }
        if (cylinder_fmep_bar <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_nonpositive_result,
                        index);
        }

        const double cylinder_fmep_pa = kPascalPerBar * cylinder_fmep_bar;
        const double cylinder_loss_work_j =
            cylinder_fmep_pa * cylinder.swept_displacement_m3;
        const double weighted_pressure_pa_m3 =
            cylinder.swept_displacement_m3 * cylinder.peak_pressure_pa_abs;
        const double weighted_speed_m4_s =
            cylinder.swept_displacement_m3 * mean_piston_speed_m_s;
        const double weighted_speed_squared_m5_s2 =
            cylinder.swept_displacement_m3 *
            mean_piston_speed_squared_m2_s2;
        if (!finite(cylinder_fmep_pa) || !finite(cylinder_loss_work_j) ||
            !finite(weighted_pressure_pa_m3) || !finite(weighted_speed_m4_s) ||
            !finite(weighted_speed_squared_m5_s2)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_overflow,
                        index);
        }
        if (cylinder_fmep_pa <= 0.0 || cylinder_loss_work_j <= 0.0 ||
            weighted_pressure_pa_m3 <= 0.0 || weighted_speed_m4_s <= 0.0 ||
            weighted_speed_squared_m5_s2 <= 0.0) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_nonpositive_result,
                        index);
        }

        const double next_total_swept_displacement_m3 =
            total_swept_displacement_m3 + cylinder.swept_displacement_m3;
        const double next_displacement_pressure_sum_pa_m3 =
            displacement_pressure_sum_pa_m3 + weighted_pressure_pa_m3;
        const double next_displacement_speed_sum_m4_s =
            displacement_speed_sum_m4_s + weighted_speed_m4_s;
        const double next_displacement_speed_squared_sum_m5_s2 =
            displacement_speed_squared_sum_m5_s2 +
            weighted_speed_squared_m5_s2;
        const double next_positive_aggregate_loss_work_j =
            positive_aggregate_loss_work_j + cylinder_loss_work_j;
        if (!finite(next_total_swept_displacement_m3) ||
            !finite(next_displacement_pressure_sum_pa_m3) ||
            !finite(next_displacement_speed_sum_m4_s) ||
            !finite(next_displacement_speed_squared_sum_m5_s2) ||
            !finite(next_positive_aggregate_loss_work_j)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_overflow,
                        index);
        }
        if (!(next_total_swept_displacement_m3 >
              total_swept_displacement_m3) ||
            !(next_displacement_pressure_sum_pa_m3 >
              displacement_pressure_sum_pa_m3) ||
            !(next_displacement_speed_sum_m4_s > displacement_speed_sum_m4_s) ||
            !(next_displacement_speed_squared_sum_m5_s2 >
              displacement_speed_squared_sum_m5_s2) ||
            !(next_positive_aggregate_loss_work_j >
              positive_aggregate_loss_work_j)) {
            return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                            derived_precision_loss,
                        index);
        }

        total_swept_displacement_m3 = next_total_swept_displacement_m3;
        displacement_pressure_sum_pa_m3 =
            next_displacement_pressure_sum_pa_m3;
        displacement_speed_sum_m4_s = next_displacement_speed_sum_m4_s;
        displacement_speed_squared_sum_m5_s2 =
            next_displacement_speed_squared_sum_m5_s2;
        positive_aggregate_loss_work_j = next_positive_aggregate_loss_work_j;
    }

    const double displacement_weighted_peak_pressure_pa_abs =
        displacement_pressure_sum_pa_m3 / total_swept_displacement_m3;
    const double displacement_weighted_peak_pressure_bar_abs =
        displacement_weighted_peak_pressure_pa_abs / kPascalPerBar;
    const double displacement_weighted_mean_piston_speed_m_s =
        displacement_speed_sum_m4_s / total_swept_displacement_m3;
    const double displacement_weighted_mean_squared_piston_speed_m2_s2 =
        displacement_speed_squared_sum_m5_s2 / total_swept_displacement_m3;
    const double aggregate_fmep_denominator =
        kPascalPerBar * total_swept_displacement_m3;
    const double friction_mean_effective_pressure_bar =
        positive_aggregate_loss_work_j / aggregate_fmep_denominator;
    if (!finite(displacement_weighted_peak_pressure_pa_abs) ||
        !finite(displacement_weighted_peak_pressure_bar_abs) ||
        !finite(displacement_weighted_mean_piston_speed_m_s) ||
        !finite(displacement_weighted_mean_squared_piston_speed_m2_s2) ||
        !finite(aggregate_fmep_denominator) ||
        !finite(friction_mean_effective_pressure_bar)) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        derived_overflow);
    }
    if (displacement_weighted_peak_pressure_pa_abs <= 0.0 ||
        displacement_weighted_peak_pressure_bar_abs <= 0.0 ||
        displacement_weighted_mean_piston_speed_m_s <= 0.0 ||
        displacement_weighted_mean_squared_piston_speed_m2_s2 <= 0.0 ||
        aggregate_fmep_denominator <= 0.0 ||
        friction_mean_effective_pressure_bar <= 0.0) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        derived_nonpositive_result);
    }

    const double running_direction_cycle_mean_loss_torque_nm =
        -positive_aggregate_loss_work_j / kFourStrokeCycleRadians;
    if (!finite(running_direction_cycle_mean_loss_torque_nm)) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        derived_overflow);
    }
    if (!(running_direction_cycle_mean_loss_torque_nm < 0.0)) {
        return fail(ChenFlynnPerCylinderPistonTravelCycleMeanLossErrorCode::
                        derived_nonpositive_result);
    }

    return ChenFlynnPerCylinderPistonTravelCycleMeanLossResult{
        total_swept_displacement_m3,
        displacement_weighted_mean_piston_speed_m_s,
        displacement_weighted_mean_squared_piston_speed_m2_s2,
        displacement_weighted_peak_pressure_pa_abs,
        displacement_weighted_peak_pressure_bar_abs,
        friction_mean_effective_pressure_bar,
        positive_aggregate_loss_work_j,
        running_direction_cycle_mean_loss_torque_nm,
    };
}

} // namespace engine_sim_offline::simulation
