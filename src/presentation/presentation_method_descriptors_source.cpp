#include "presentation/presentation_method_registry.hpp"

#include "presentation/presentation_method_descriptor_support.hpp"

namespace engine_sim_offline::presentation {
namespace {

constexpr std::string_view kCausalReconstructionMethodDescriptor =
    R"method(engine-sim-offline.presentation-method-configuration.v1
method=causal-kaiser-sinc-257tap-4096phase-10000-or-20000-to-192000-binary64-v2
version=2
operation=continuous-ordered-route-causal-bandlimited-reconstruction
input=finite-binary64-frame-major-ordered-route-matrix-at-session-selected-10000/1-hz-or-20000/1-hz
output=finite-binary64-frame-major-ordered-route-matrix-at-192000/1-hz
route_count=nonzero-session-owned-size-t-fixed-for-session
input_rate_hz=exactly-one-of-10000/1-or-20000/1-fixed-for-session
input_method_block_duration=exactly-1/50-second
input_method_block_frame_count=200-at-10000/1-hz-or-400-at-20000/1-hz
input_block_frame_count=caller-partitioned-nonempty-integer-1..input-method-block-frame-count
block_partition=not-arithmetic-or-state-affecting
table_shape=4097-phases-by-257-taps-phase-major-binary64
table_phase_interval_count=4096
table_half_width=128
table_pi_binary64_bits=0x400921fb54442d18
table_kaiser_beta_binary64_bits=0x4028000000000000
table_bandwidth_binary64_bits=0x3fee666666666666
table_sinc_zero_guard_binary64_bits=0x3cd203af9ee75616
bessel_i0_quarter_square=binary64-0.25-times-value-times-value-in-written-order
bessel_i0_initial_term=binary64-1
bessel_i0_initial_sum=binary64-1
bessel_i0_loop=k-ascending-1-through-40-inclusive
bessel_i0_term=term-times-(quarter-square-divided-by-binary64(k-times-k))-in-written-order
bessel_i0_sum=sum-plus-term
window_inverse_i0_beta=binary64-1-divided-by-bessel_i0(binary64-12)
window_normalized=(binary64(tap)-binary64(128))/binary64(128)
window_radicand=max(binary64-positive-zero,binary64-1-minus-normalized-times-normalized)
window_value=bessel_i0(binary64-12-times-sqrt(radicand))-times-window-inverse-i0-beta-in-written-order
window_endpoint_override=tap-0-and-tap-256-are-positive-binary64-zero
table_phase_order=phase-ascending-0-through-4095
table_tap_order=tap-ascending-0-through-256
table_phase_fraction=binary64(phase)/binary64(4096)
table_tap_offset=binary64(tap)-binary64(128)
table_distance=tap-offset-minus-phase-fraction
sinc_radians=pi-times-value
sinc=if-absolute-value-less-than-binary64-1e-15-then-binary64-1-else-sin(sinc-radians)/sinc-radians
unnormalized_coefficient=binary64-0.95-times-sinc(binary64-0.95-times-distance)-times-window-in-written-order
row_sum=positive-binary64-zero-then-serial-tap-ascending-addition
row_normalization=each-coefficient-divided-by-complete-row-sum-in-tap-ascending-order
phase_wrap_row_coefficient_0=positive-binary64-zero
phase_wrap_row_coefficients_1_through_256=phase-0-coefficients-0-through-255
route_state=one-independent-257-sample-binary64-ring-history-per-ordered-route
route_state_initialization=all-positive-binary64-zero
oldest_history_frame_initial=unsigned-zero
distance_to_next_output_initial=unsigned-zero
phase_resolution_input=unsigned-source-interval-offset-in-range-0..191999
phase_resolution_loop=12-bits-by-repeated-remainder-doubling-and-subtraction
phase_resolution_branch=if-remainder-greater-than-or-equal-to-192000-minus-remainder-then-remainder-minus-equals-192000-minus-remainder-and-set-phase-low-bit-else-remainder-times-equals-2
phase_mix=binary64(final-remainder)/binary64(192000)
outputs_per_input=1+(192000-distance-to-next-output-1)/session-input-rate-numerator-using-u64-integer-division
output_offset=distance-to-next-output-then-plus-session-input-rate-numerator-after-each-output
output_phase_rows=resolved-phase-and-resolved-phase-plus-one
coefficient_interpolation=row0+(row1-row0)*phase-mix-in-written-order
output_loop_order=input-ascending-then-generated-output-ascending-then-tap-ascending-then-route-ascending
output_accumulator=per-route-positive-binary64-zero
output_accumulation=sample-plus-history-at-ring-index-times-interpolated-coefficient-in-written-order
input_commit=write-current-ordered-route-input-at-oldest-history-frame-only-after-all-outputs-for-that-input
history_advance=oldest-history-frame-plus-one-modulo-257
clock_commit=distance-to-next-output=final-output-offset-minus-192000
finite_checks=all-input-table-intermediates-table-output-and-reconstruction-output-must-remain-finite
parallel_reduction=none-all-arithmetic-in-declared-serial-loop-order
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-abs-std-sin-std-sqrt-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

constexpr std::string_view kRouteConditioningMethodDescriptor =
    R"method(engine-sim-offline.presentation-method-configuration.v1
method=route-jitter-dc-derivative-air-noise-binary64-v1
version=1
operation=continuous-route-owned-jitter-dc-derivative-and-air-noise-conditioning
input=one-finite-binary64-reconstructed-route-sample-per-accepted-frame
output=one-finite-binary64-conditioned-route-sample-per-accepted-frame
source_rate_hz=192000/1
time_step_binary64_bits=0x3ed5d867c3ece2a5
resolved_argument_1=jitter_scale
resolved_argument_1_domain=finite-canonical-nonnegative-binary64
resolved_argument_2=jitter_modulation_cutoff_hz
resolved_argument_2_domain=finite-binary64-strictly-greater-than-zero-and-less-than-96000
resolved_argument_3=derivative_mix_01
resolved_argument_3_domain=finite-canonical-binary64-in-closed-interval-0..1
resolved_argument_4=air_noise_mix_01
resolved_argument_4_domain=finite-canonical-binary64-in-closed-interval-0..1
resolved_argument_5=air_noise_cutoff_hz
resolved_argument_5_domain=finite-binary64-strictly-greater-than-zero-and-less-than-96000
canonical_zero=positive-binary64-zero;negative-zero-is-rejected
random_generator_method=pcg32_xsh_rr_64_32_binary64_v1
random_generator_version=1
random_generator_configuration_sha256=a48383d2716a059b0b60aabf4c6febb0a81edbad622da4634823bf22421eaf0c
random_stream_ownership=one-independent-jitter-stream-and-one-independent-air-noise-stream-per-route
accepted_frame_draw_cadence=jitter-unsigned-uniform-first-using-two-u32-draws-then-air-signed-uniform-using-two-u32-draws
zero_calibration_draw_cadence=unchanged-all-four-u32-draws-are-consumed
jitter_history_shape=41-binary64-samples
jitter_history_initialization=all-positive-zero
jitter_write_offset_initial=unsigned-zero
jitter_history_commit=store-current-input-at-write-offset-then-increment-modulo-41-before-delay-read
jitter_maximum_offset_binary64_bits=0x4044000000000000
jitter_mean_offset_binary64_bits=0x4034000000000000
random_excitation_scale_binary64_bits=0x3ff6a09e667f3bcd
jitter_random_offset=unsigned-uniform-times-binary64-40
jitter_rate_normalized_offset=binary64-20+(random-offset-binary64-20)*random-excitation-scale-in-written-order
jitter_modulation_input=rate-normalized-offset-times-jitter-scale
jitter_filtered_offset=fourth-order-low-pass(jitter-modulation-input,jitter-modulation-cutoff-hz)
jitter_clamp=minimum-binary64-positive-zero-maximum-binary64-40
jitter_lower_offset=size_t(floor(clamped-offset))
jitter_upper_offset=size_t(ceil(clamped-offset))
jitter_fraction=clamped-offset-minus-binary64(lower-offset)
jitter_history_indices=(post-increment-write-offset+delay-offset)-modulo-41
jitter_interpolation=lower+(upper-lower)*fraction-in-written-order
low_pass_pi_binary64_bits=0x400921fb54442eea
low_pass_f=tan(pi-times-cutoff-hz-divided-by-sample-rate-hz-in-written-order)
low_pass_powers=f2=f-times-f;f3=f2-times-f;f4=f2-times-f2
low_pass_m=negative-2-times-cos(binary64-5-times-pi-divided-by-binary64-8)
low_pass_n=negative-2-times-cos(binary64-7-times-pi-divided-by-binary64-8)
low_pass_a0=1+(m+n)*f+(2+n*m)*f2+(m+n)*f3+f4-in-written-order
low_pass_a1=(-4-2*(n+m)*f+2*(m+n)*f3+4*f4)/a0-in-written-order
low_pass_a2=(6-2*(2+m*n)*f2+6*f4)/a0-in-written-order
low_pass_a3=(-4+2*(m+n)*f-2*(m+n)*f3+4*f4)/a0-in-written-order
low_pass_a4=(1-(n+m)*f+(2+m*n)*f2-(m+n)*f3+f4)/a0-in-written-order
low_pass_numerator_scale=f4/a0
low_pass_state=prior-four-inputs-and-prior-four-outputs-all-positive-binary64-zero
low_pass_numerator=numerator-scale*(input+4*input1+6*input2+4*input3+input4)-in-written-order
low_pass_feedback=-a1*output1-a2*output2-a3*output3-a4*output4-in-written-order
low_pass_output=numerator+feedback
low_pass_commit=prior-input-3=prior-input-2;prior-input-2=prior-input-1;prior-input-1=prior-input-0;prior-input-0=current-input;then-same-assignment-order-for-prior-outputs-with-prior-output-0=current-output
dc_time_constant_binary64_bits=0x3f904c26be3b05a1
dc_alpha=time-step/(dc-time-constant+time-step)-in-written-order
dc_state_initial=positive-binary64-zero
dc_state=alpha-times-jittered+(binary64-1-minus-alpha)-times-prior-dc-state-in-written-order
dc_removed=jittered-dc-state
derivative_previous_initial=positive-binary64-zero
derivative=(jittered-previous-jittered)/time-step-in-written-order
derivative_commit=previous-jittered=jittered
air_excitation=signed-uniform-times-random-excitation-scale
filtered_air_noise=fourth-order-low-pass(air-excitation,air-noise-cutoff-hz)
noise_mix=air-noise-mix-01*filtered-air-noise+(1-air-noise-mix-01)-in-written-order
conditioned=derivative*derivative-mix-01+dc-removed*noise-mix*(1-derivative-mix-01)-in-written-order
subnormal_cleanup=conditioned-subnormal-becomes-positive-binary64-zero;normal-and-signed-zero-bit-patterns-are-preserved
state_continuity=all-route-state-and-random-state-continue-across-caller-block-boundaries
finite_checks=constructor-rejects-nonfinite-configuration-and-coefficients;process-rejects-nonfinite-input;each-low-pass-rejects-nonfinite-input-and-output;dc-removal-rejects-nonfinite-input-state-and-output;derivative-rejects-nonfinite-input-and-output;conditioned-cleanup-rejects-nonfinite-final-mixture
parallel_reduction=none-all-arithmetic-in-declared-serial-loop-order
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-tan-std-cos-std-floor-std-ceil-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(detail::canonical_lf_descriptor(kCausalReconstructionMethodDescriptor));
static_assert(detail::canonical_lf_descriptor(kRouteConditioningMethodDescriptor));

} // namespace

std::string_view causal_reconstruction_method_descriptor() noexcept {
    return kCausalReconstructionMethodDescriptor;
}

std::string_view route_conditioning_method_descriptor() noexcept {
    return kRouteConditioningMethodDescriptor;
}

} // namespace engine_sim_offline::presentation
