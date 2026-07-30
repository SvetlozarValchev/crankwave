#include "presentation/presentation_method_registry.hpp"

#include "presentation/presentation_method_descriptor_support.hpp"

namespace engine_sim_offline::presentation {
namespace {

#if defined(__wasm32__)
#define ENGINE_SIM_OFFLINE_STATIC_IR_METHOD_ID_LITERAL                         \
    "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-"       \
    "wasm32-binary128-v1"
#define ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL "wasm32-binary128-extended"
#define ENGINE_SIM_OFFLINE_STATIC_IR_OUTPUT_EXTENDED_LABEL "wasm32-binary128"
#define ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_EXECUTION                        \
    "wasm32-ieee754-binary128-radix2-113-significand-bits-min-exponent-"      \
    "minus16381-max-exponent-16384-storage-16-bytes-nearest-ties-to-even"
#else
#define ENGINE_SIM_OFFLINE_STATIC_IR_METHOD_ID_LITERAL                         \
    "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-binary64-v1"
#define ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL "x87-extended"
#define ENGINE_SIM_OFFLINE_STATIC_IR_OUTPUT_EXTENDED_LABEL "x87"
#define ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_EXECUTION                        \
    "x87-radix2-64-significand-bits-min-exponent-minus16381-max-exponent-"     \
    "16384-storage-16-bytes-nearest-ties-to-even-masked-exceptions"
#endif

constexpr std::string_view kStaticIrConversionMethodDescriptor =
    "engine-sim-offline.presentation-method-configuration.v1\n"
    "method=" ENGINE_SIM_OFFLINE_STATIC_IR_METHOD_ID_LITERAL "\n"
    "version=1\n"
    "operation=strict-riff-wave-pcm16-decode-to-static-binary64-ir\n"
    "raw_input=nonempty-complete-content-addressed-byte-sequence-of-at-most-"
    "1048576-bytes\n"
    "riff_header=minimum-12-bytes;riff-fourcc-at-0;declared-u32le-size-plus-8-"
    "must-equal-complete-input-size;wave-fourcc-at-8\n"
    "chunk_traversal=from-byte-12-in-file-order-as-fourcc-plus-u32le-size-plus-"
    "payload-plus-one-pad-byte-for-odd-payload-size\n"
    "chunk_bounds=complete-header-payload-and-required-pad-must-fit;pad-byte-"
    "value-is-ignored\n"
    "recognized_chunks=exactly-one-fmt-space-and-exactly-one-data;duplicate-"
    "recognized-chunks-are-rejected;unknown-chunks-are-skipped\n"
    "fmt_chunk=exactly-16-payload-bytes;pcm-format-tag-1;mono;sample-rate-44100;"
    "byte-rate-88200;block-align-2;bits-per-sample-16\n"
    "data_chunk=even-payload-size-and-at-most-33705-frames\n"
    "sample_decode=u16-little-endian-in-data-order;values-0..32767-unchanged;"
    "values-32768..65535-minus-65536;then-convert-to-signed-int16\n"
    "input_sample=signed-int16-numeric-value\n"
    "input_rate_hz=44100/1\n"
    "input_frame_count=1..33705\n"
    "meaningful_support_argument=integer-in-range-1..input_frame_count\n"
    "meaningful_support_detection=one-plus-last-index-whose-int32-absolute-"
    "pcm16-magnitude-is-strictly-greater-than-100\n"
    "meaningful_support_validation=detected-count-must-equal-argument-after-"
    "scanning-complete-input\n"
    "configured_gain=caller-supplied-finite-binary64-with-value-greater-than-or-"
    "equal-to-positive-zero;negative-zero-is-rejected\n"
    "source_rate_integer=44100\n"
    "target_rate_integer=192000\n"
    "target_count=(support_count*192000+22050)/44100-using-u64-integer-division\n"
    "target_sample_rate_hz=192000/1\n"
    "pi_binary64_bits=0x400921fb54442d18\n"
    "pcm16_positive_divisor_binary64_bits=0x40dfffc000000000\n"
    "blackman_a0_binary64_bits=0x3fdae147ae147ae1\n"
    "blackman_a1_binary64_bits=0x3fe0000000000000\n"
    "blackman_a2_binary64_bits=0x3fb47ae147ae147b\n"
    "sinc_zero_guard_binary64_bits=0x3d719799812dea11\n"
    "minimum_retained_weight_source_binary64_bits=0x3e45798ee2308c3a\n"
    "weight_table_shape=4097-phases-by-24-taps-phase-major-binary64\n"
    "weight_table_phase_order=phase-0-through-4096-inclusive\n"
    "weight_table_tap_order=tap-0-through-23-inclusive\n"
    "phase_fraction=binary64(phase)/binary64(4096)\n"
    "tap_offset=tap-11-with-offset-range-minus11-through-plus12\n"
    "distance=binary64(tap_offset)-phase_fraction\n"
    "normalized_distance=distance/binary64(12)\n"
    "window_support=absolute-normalized-distance-strictly-less-than-1\n"
    "window_inside=0.42+0.5*cos(pi*normalized_distance)+0.08*cos(2*pi*"
    "normalized_distance)-in-written-binary64-order\n"
    "window_outside=positive-binary64-zero\n"
    "cutoff=min(binary64(1),binary64(192000)/binary64(44100))-equals-1\n"
    "sinc_radians=pi-times-argument\n"
    "sinc=if-absolute-argument-less-than-binary64-1e-12-then-1-else-sin("
    "sinc-radians)/sinc-radians\n"
    "table_weight=cutoff*sinc(cutoff*distance)*window-in-written-binary64-order\n"
    "source_clock_initial=center-u64-0,fraction-u64-0\n"
    "source_clock_per_target=use-current-position-then-fraction-plus-equals-"
    "44100;center-plus-equals-fraction-div-192000;fraction-mod-equals-192000\n"
    "phase_selection=scaled=fraction*4096;phase0=scaled/192000;phase1=min("
    "4096,phase0+1);mix=binary64(scaled%192000)/binary64(192000)\n"
    "phase_interpolation=weight0+(weight1-weight0)*mix-in-written-binary64-order\n"
    "tap_source_index=signed64(center)+signed64(tap)-11\n"
    "tap_boundary=skip-index-less-than-0-or-index-greater-than-or-equal-to-"
    "meaningful-support\n"
    "pass1_order=target-ascending-then-tap-ascending\n"
    "pass1_accumulator=per-source-" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
    "-initial-positive-zero\n"
    "pass1_update=source_weight_sum-plus-binary64-interpolated-weight\n"
    "retained_test=source_weight_sum-strictly-greater-than-binary64-1e-8-"
    "promoted-to-" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL "\n"
    "fallback_order=source-ascending-before-normal-matrix-pass\n"
    "fallback_target=(source_index*192000+22050)/44100-using-u64-integer-"
    "division\n"
    "fallback_update=target-" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
    "-accumulator-plus-signed-int16-value\n"
    "pass2_order=target-ascending-then-tap-ascending\n"
    "pass2_skip=out-of-support-or-source-weight-sum-less-than-or-equal-to-"
    "retained-threshold\n"
    "pass2_contribution=(" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
    "(sample)*" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
    "(weight))/source-weight-sum-in-written-order\n"
    "pass2_update=target-" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
    "-accumulator-plus-contribution\n"
    "coefficient_scale=binary64(configured_gain/binary64(32767))-then-promote-"
    "to-" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL "\n"
    "output_order=target-ascending\n"
    "output_conversion=" ENGINE_SIM_OFFLINE_STATIC_IR_OUTPUT_EXTENDED_LABEL
    "-target-accumulator-times-" ENGINE_SIM_OFFLINE_STATIC_IR_OUTPUT_EXTENDED_LABEL
    "-coefficient-scale-then-round-to-binary64\n"
    "rate_area_scale=not-applied\n"
    "clipping=none\n"
    "finite_checks=table,interpolation,accumulators,contributions,scale,and-"
    "output-must-remain-finite\n"
    "binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-"
    "no-daz\n"
    "extended_execution=" ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_EXECUTION "\n"
    "transcendentals=std-sin-and-std-cos-binary64-under-render-determinism-"
    "envelope\n"
    "external_numeric_authority=renderer-build-source-standard-library-math-"
    "runtime-and-thread-numeric-environment-identities\n";

constexpr std::string_view kFixedOverlapSaveConvolutionMethodDescriptor =
    "engine-sim-offline.presentation-method-configuration.v1\n"
    "method=fixed-causal-overlap-save-radix2-dit-fft-65536-binary64-v1\n"
    "version=1\n"
    "operation=continuous-causal-real-binary64-overlap-save-convolution\n"
    "kernel_input=1-through-30071-inclusive-finite-real-binary64-coefficients-"
    "in-tap-order\n"
    "kernel_capacity=exactly-30071-coefficients\n"
    "kernel_right_zero_padding=if-input-count-is-less-than-30071-append-positive-"
    "binary64-zero-through-exactly-30071-coefficients-before-spectrum-"
    "population\n"
    "kernel_time_origin=coefficient-0-at-zero-delay\n"
    "kernel_spectrum_storage=65536-complex-binary64-bins\n"
    "kernel_spectrum_initialization=all-positive-zero-real-and-imaginary\n"
    "kernel_spectrum_population=coefficient-indices-0-through-30070-as-real-"
    "with-positive-zero-imaginary;remaining-bins-stay-zero\n"
    "kernel_spectrum_transform=one-fixed-forward-fft-before-stream-processing\n"
    "fft_length=65536\n"
    "fft_bit_count=16\n"
    "fft_forward_root_count=32768\n"
    "fft_pi_binary64_bits=0x400921fb54442d18\n"
    "fft_inverse_scale_binary64_bits=0x3ef0000000000000\n"
    "bit_reversal_table=index-ascending;reverse-exactly-16-low-bits-by-"
    "remaining-low-bit-then-right-shift\n"
    "root_table=index-ascending-0-through-32767\n"
    "root_angle=(-binary64(2)*pi*binary64(index))/binary64(65536)-in-written-"
    "binary64-order\n"
    "forward_root=complex(std-cos(root-angle),std-sin(root-angle))\n"
    "fft_input_validation=exactly-65536-finite-complex-binary64-values-before-"
    "mutation\n"
    "fft_permutation=index-ascending;swap-index-with-bit-reversed-index-only-if-"
    "index-less-than-reversed\n"
    "fft_topology=iterative-radix2-decimation-in-time\n"
    "fft_stage_order=width-2-through-65536-by-doubling\n"
    "fft_stage_parameters=half-width=width/2;root-step=65536/width\n"
    "fft_loop_order=stage-ascending-then-base-ascending-by-width-then-offset-"
    "ascending-0-through-half-width-minus-1\n"
    "fft_root_index=offset*root-step\n"
    "fft_forward_root=precomputed-forward-root\n"
    "fft_inverse_root=complex-conjugate-of-precomputed-forward-root\n"
    "fft_butterfly=odd=upper*root;even=lower;lower=even+odd;upper=even-odd-in-"
    "written-order\n"
    "fft_inverse_postscale=index-ascending-complex-value-times-binary64-2^-16\n"
    "fft_output_validation=all-complex-values-finite-after-transform\n"
    "stream_history_frames=30070\n"
    "stream_history_initialization=positive-binary64-zero\n"
    "block_frame_count=caller-partitioned-nonempty-integer-1..9600\n"
    "block_partition=ordered-execution-input-and-output-affecting\n"
    "block_input=finite-real-binary64\n"
    "block_output_extent=exactly-input-frame-count\n"
    "block_transform_initialization=all-65536-complex-values-positive-zero\n"
    "block_transform_layout=history-frames-at-0..30069-then-current-input-at-"
    "30070..30070+block-count-1-then-zero-padding\n"
    "next_history_order=old-history-with-first-block-count-frames-discarded-"
    "then-complete-current-input\n"
    "block_execution=forward-fft-then-binwise-kernel-spectrum-multiply-then-"
    "inverse-fft\n"
    "spectrum_multiply_order=bin-ascending-0-through-65535-using-complex-"
    "binary64-multiply-assign\n"
    "overlap_save_discard=transformed-indices-0-through-30069\n"
    "block_output=real-part-of-indices-30070-through-30070+block-count-1\n"
    "imaginary_residual=discarded\n"
    "state_commit=validate-all-output-real-parts-then-copy-complete-output-then-"
    "swap-in-next-history\n"
    "input_output_aliasing=supported-because-output-write-follows-input-and-"
    "next-history-consumption\n"
    "tail=no-implicit-zero-extension-no-tail-flush-one-output-frame-per-"
    "submitted-input-frame\n"
    "fallback=none-no-direct-fir-path\n"
    "parallel_reduction=none-all-arithmetic-in-declared-serial-loop-order\n"
    "binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-"
    "no-daz\n"
    "complex_arithmetic=std-complex-binary64-operators-under-render-"
    "determinism-envelope\n"
    "transcendentals=std-cos-and-std-sin-binary64-under-render-determinism-"
    "envelope\n"
    "external_numeric_authority=renderer-build-source-standard-library-math-"
    "runtime-and-thread-numeric-environment-identities\n";

static_assert(detail::canonical_lf_descriptor(kStaticIrConversionMethodDescriptor));
static_assert(
    detail::canonical_lf_descriptor(kFixedOverlapSaveConvolutionMethodDescriptor));

} // namespace

std::string_view static_ir_conversion_method_descriptor() noexcept {
    return kStaticIrConversionMethodDescriptor;
}

std::string_view fixed_overlap_save_convolution_method_descriptor() noexcept {
    return kFixedOverlapSaveConvolutionMethodDescriptor;
}

} // namespace engine_sim_offline::presentation

#undef ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_EXECUTION
#undef ENGINE_SIM_OFFLINE_STATIC_IR_EXTENDED_LABEL
#undef ENGINE_SIM_OFFLINE_STATIC_IR_METHOD_ID_LITERAL
#undef ENGINE_SIM_OFFLINE_STATIC_IR_OUTPUT_EXTENDED_LABEL
