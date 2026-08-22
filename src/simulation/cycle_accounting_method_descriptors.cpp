#include "simulation/cycle_accounting_method_registry.hpp"

namespace crankwave::simulation {
namespace {

[[nodiscard]] consteval bool
canonical_lf_descriptor(std::string_view descriptor) noexcept {
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

constexpr std::string_view kFourStrokePiecewiseLinearCycleQuadratureDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=four-stroke-piecewise-linear-cycle-quadrature-v1
version=1
operation=indexed-four-stroke-piecewise-linear-torque-angle-quadrature
plan_input=finite-binary64-cycle-reference-theta-rad-and-finite-positive-binary64-total-displacement-m3
cycle_radians=binary64-4-times-std-numbers-pi-v-binary64
sample_semantics=strictly-increasing-post-step-sample-index-time-s-and-unwrapped-theta-rad
sample_lanes=finite-binary64-indicated-gas-torque-nm,friction-pump-and-accessory-torque-nm,starter-torque-nm
summed_torque=(indicated-gas-plus-friction-pump-and-accessory)-plus-starter-in-written-order
first_cycle_index=floor((first-theta-minus-cycle-reference-theta)/cycle-radians)-in-written-order
first_cycle_index_domain=integer-range-negative-9007199254740991-through-positive-9007199254740990
boundary_theta=cycle-reference-theta-plus-binary64(signed-cycle-index)-times-cycle-radians-in-written-order
first_sample_on_boundary=begin-complete-cycle-at-that-sample
first_sample_off_boundary=discard-initial-partial-cycle-until-next-boundary
boundary_progression=checked-signed-cycle-index-increment-and-independent-boundary-derivation
segment_crossing=at-most-one-represented-boundary-per-submitted-segment
exact_right_boundary_evidence=left-index-equals-right-index-equals-current-sample-index;fraction-positive-zero
bracketed_boundary_fraction=(boundary-theta-minus-left-theta)/(right-theta-minus-left-theta)-in-written-order
boundary_scalar_interpolation=if-left-index-equals-right-index-return-right-value;otherwise-left-value-plus-fraction-times-(right-value-minus-left-value)-in-written-order
boundary_interpolated_lanes=time-and-all-three-torque-lanes
segment_work=accumulator-plus-((left-torque-plus-right-torque)-times-delta-theta)-times-binary64-0.5-in-written-order
work_lane_order=indicated-gas,friction-pump-and-accessory,starter,summed-torque
boundary_segment_order=if-active-integrate-previous-to-boundary-then-finish-prior-full-cycle;begin-next-cycle;integrate-boundary-to-current-if-nonempty
crossing_observation=return-every-represented-boundary-including-the-end-of-the-discarded-initial-partial-cycle
completed_cycle_ordinal=unsigned-u64-zero-based-incremented-after-return-value-construction
completed_cycle_work=separate-three-lane-work-and-independently-accumulated-summed-torque-work
cycle_mean_summed_torque_nm=summed-torque-work-j/cycle-radians
summed_torque_mean_effective_pressure_pa=summed-torque-work-j/total-displacement-m3
cycle_mean_summed_power_w=summed-torque-work-j/(end-time-s-minus-start-time-s)
physical_discontinuities=caller-must-split-at-exact-event-times
net_torque_claim=none-supplied-lanes-are-not-proven-complete-by-this-method
parallel_reduction=none-all-updates-follow-submitted-sample-and-declared-lane-order
compile_rejection=invalid-plan-no-session-is-produced
session_terminal_failure=nonfinite-sample,nonmonotonic-sample,multiple-boundaries-in-segment,nonfinite-result,or-moved-from;subsequent-advance-returns-stored-error
move_semantics=nonself-source-session-becomes-terminal-moved-from;destination-retains-complete-state;self-move-assignment-is-no-op
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-floor-and-std-numbers-pi-v-binary64-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

constexpr std::string_view kChenFlynnCycleMeanAggregateLossDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=chen-flynn-cycle-mean-aggregate-loss-v1
version=1
operation=cycle-mean-chen-flynn-aggregate-loss-from-displacement-weighted-absolute-peak-pressure
plan_coefficient_order=constant-fmep-bar,peak-pressure-coefficient,mean-piston-speed-coefficient-bar-s-per-m,mean-piston-speed-squared-coefficient-bar-s2-per-m2
plan_coefficient_domain=finite-canonical-nonnegative-binary64;negative-zero-is-rejected;at-least-one-coefficient-is-positive
input_engine_speed_rpm=finite-positive-binary64
input_stroke_m=finite-positive-binary64
cylinder_input=nonempty-array-in-strictly-increasing-nonzero-stable-cylinder-id-order
cylinder_displacement_m3=finite-positive-binary64
cylinder_peak_pressure_pa_abs=finite-positive-binary64-absolute-pressure
mean_piston_speed_step_1=twice-stroke-m=binary64-2-times-stroke-m
mean_piston_speed_step_2=stroke-rpm-m-per-minute=twice-stroke-m-times-engine-speed-rpm
mean_piston_speed_step_3=mean-piston-speed-m-s=stroke-rpm-m-per-minute-divided-by-binary64-60
cylinder_weighted_pressure=displacement-m3-times-peak-pressure-pa-abs
cylinder_weighted_pressure_domain=finite-positive-binary64
cylinder_reduction_order=caller-supplied-ascending-stable-cylinder-id-order
cylinder_reduction=separate-left-to-right-binary64-sums-for-total-displacement-m3-and-displacement-times-pressure-pa-m3
cylinder_reduction_progress=each-sum-must-remain-finite-and-increase-strictly
weighted_peak_pressure_pa_abs=displacement-pressure-sum-pa-m3/total-displacement-m3
pascal_per_bar=binary64-100000
weighted_peak_pressure_bar_abs=weighted-peak-pressure-pa-abs/binary64-100000
mean_piston_speed_squared=mean-piston-speed-m-s-times-mean-piston-speed-m-s
peak_pressure_term_bar=peak-pressure-coefficient-times-weighted-peak-pressure-bar-abs
mean_speed_term_bar=mean-piston-speed-coefficient-bar-s-per-m-times-mean-piston-speed-m-s
mean_speed_squared_term_bar=mean-piston-speed-squared-coefficient-bar-s2-per-m2-times-mean-piston-speed-squared
fmep_sum_order=((constant-fmep-bar-plus-peak-pressure-term-bar)-plus-mean-speed-term-bar)-plus-mean-speed-squared-term-bar
friction_mean_effective_pressure_pa=binary64-100000-times-friction-mean-effective-pressure-bar
positive_aggregate_loss_work_j=friction-mean-effective-pressure-pa-times-total-displacement-m3
four_stroke_cycle_radians=binary64-4-times-std-numbers-pi-v-binary64
running_direction_cycle_mean_loss_torque_nm=negative-positive-aggregate-loss-work-j-divided-by-four-stroke-cycle-radians
result_order=total-displacement-m3,mean-piston-speed-m-s,weighted-peak-pressure-pa-abs,weighted-peak-pressure-bar-abs,friction-mean-effective-pressure-bar,positive-aggregate-loss-work-j,running-direction-cycle-mean-loss-torque-nm
result_domain=all-finite;pressure-speed-fmep-work-positive;running-direction-loss-torque-negative
component_loss_claim=none-result-is-one-aggregate-loss
brake_or_net_claim=none-result-does-not-prove-term-completeness-or-shaft-output
instantaneous_waveform=none-cycle-mean-only
parallel_reduction=none-all-reductions-follow-declared-cylinder-order
failure=first-domain-or-derived-finiteness-positivity-or-precision-check-in-written-execution-order
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-numbers-pi-v-binary64-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(
    canonical_lf_descriptor(kFourStrokePiecewiseLinearCycleQuadratureDescriptor));
static_assert(canonical_lf_descriptor(kChenFlynnCycleMeanAggregateLossDescriptor));

} // namespace

std::string_view
four_stroke_piecewise_linear_cycle_quadrature_method_descriptor() noexcept {
    return kFourStrokePiecewiseLinearCycleQuadratureDescriptor;
}

std::string_view chen_flynn_cycle_mean_aggregate_loss_method_descriptor() noexcept {
    return kChenFlynnCycleMeanAggregateLossDescriptor;
}

} // namespace crankwave::simulation
