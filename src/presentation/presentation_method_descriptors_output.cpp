#include "presentation/presentation_method_registry.hpp"

#include "presentation/presentation_method_descriptor_support.hpp"

namespace crankwave::presentation {
namespace {

#if defined(__wasm32__)
#define CRANKWAVE_AUDITION_METHOD_ID_LITERAL                                  \
    "ordered-n-route-rate-adjusted-leveler-tanh-quarter-sine-pcm24-wave-master-"       \
    "wasm32-binary128-v4"
#define CRANKWAVE_DURATION_EXTENDED_LABEL "wasm32-ieee754-binary128"
#else
#define CRANKWAVE_AUDITION_METHOD_ID_LITERAL                                  \
    "ordered-n-route-rate-adjusted-leveler-tanh-quarter-sine-pcm24-wave-master-v4"
#define CRANKWAVE_DURATION_EXTENDED_LABEL "x87-extended"
#endif

constexpr std::string_view kRouteStemPublicationMethodDescriptor =
    R"method(crankwave.presentation-method-configuration.v1
method=exhaust-route-wet-selection-float32-wave-publication-10000-or-20000-to-192000-20ms-clock-v7
version=7
operation=exhaust-route-dry-configured-transfer-selected-stem-publication
topology=one-or-more-distinct-ordered-active-exhaust-routes;three-stems-per-route
active_exhaust_input=one-finite-binary64-dry-sample-and-one-finite-binary64-configured-transfer-sample-per-route-per-source-frame
resolved_argument_1=per-route-wet_mix_01
resolved_argument_1_domain=finite-canonical-binary64-in-closed-interval-0..1;negative-zero-is-rejected
selected=wet-mix-01*configured-transfer+(binary64-1-wet-mix-01)*dry-in-written-order
selection_execution=every-processed-source-frame-including-pre-audible-frames
publication_inputs=dry,configured-transfer,selected-in-that-order-per-route
resolved_argument_2=calibration_gain_linear
resolved_argument_2_domain=finite-binary64-strictly-greater-than-positive-zero;both-signed-zeros-are-rejected
timeline_unit=complete-3840-source-frame-blocks-at-192000/1-hz
timeline_input_clock=exactly-one-of-10000/1-hz-or-20000/1-hz-fixed-for-session
timeline_input_mapping=each-complete-20ms-input-block-of-200-frames-at-10000/1-hz-or-400-frames-at-20000/1-hz-produces-exactly-3840-source-frames-at-192000/1-hz-and-returns-the-reconstruction-clock-to-zero-phase
timeline_domain=positive-total-block-count-and-pre-audible-block-count-strictly-less-than-total
timeline_arithmetic=total-and-pre-audible-block-count-times-3840-must-be-representable-as-u64
published_interval=complete-block-ordinals-pre-audible-block-count-through-total-block-count-minus-one
pre_audible_policy=process-selection-and-continuous-convolution-state-but-publish-no-samples
crop_state_policy=no-reconstruction-conditioning-convolution-or-random-state-is-reset-at-the-pre-audible-boundary
tail_policy=truncate-at-timeline-end-with-no-convolution-zero-extension-or-tail-flush
published_frame_mapping=one-output-frame-per-published-source-frame-in-input-order
published_stem_order=for-route-index-zero-through-r-minus-one-serially-dry,configured-transfer,selected
publication_round_1=float32(input)-using-nearest-ties-to-even
publication_round_1_validation=result-must-be-finite
calibration=binary64(publication-round-1)-times-calibration-gain-linear-in-written-order
publication_round_2=float32(calibration)-using-nearest-ties-to-even
publication_round_2_validation=result-must-be-finite
output=exactly-three-times-all-published-exhaust-route-count-finite-float32-mono-route-stem-streams
clipping=none
limiting=none
dither=none
normalization=none
float32_wave_count=exactly-three-times-r-one-per-published-stem
float32_wave_byte_order=little-endian
float32_wave_header=58-bytes-riff-wave-fmt18-fact4-data-in-that-order
float32_wave_format=ieee-float-format-tag-3;mono;192000-hz;byte-rate-768000;block-align-4;bits-per-sample-32;fmt-extension-size-0
float32_wave_fact=sample-count-equals-published-frame-count
float32_wave_data_size=published-frame-count-times-4-representable-as-u32
float32_wave_riff_size=50-plus-data-size-representable-as-u32
float32_wave_payload=published-float32-bit-patterns-in-frame-order-as-u32-little-endian
float32_wave_metadata=none
float32_wave_padding=none-because-data-size-is-divisible-by-4
serialization_partition=caller-chunking-does-not-affect-bytes
parallel_reduction=none
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
float32_execution=ieee754-binary32-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

constexpr std::string_view kOrderedRouteAuditionMethodDescriptor =
    R"method(crankwave.presentation-method-configuration.v1
)method"
    "method=" CRANKWAVE_AUDITION_METHOD_ID_LITERAL "\n"
    R"method(version=4
operation=ordered-n-route-serial-float32-rate-adjusted-peak-leveler-tanh-quarter-sine-fades-and-pcm24-master
route_selection=all-active-published-exhaust-source-routes-exactly-once-in-declared-vector-order
active_route_input=one-finite-binary64-selected-sample-and-one-finite-publication-calibrated-float32-selected-stem-sample-per-active-exhaust-route-per-frame
delivery_rate_hz=192000/1
master_dynamics_execution_rate_hz=192000/1
reference_coefficient_rate_hz=44100/1
resolved_argument_1=volume_linear
resolved_argument_1_domain=finite-binary64-that-rounds-nearest-ties-even-to-finite-positive-float32
resolved_argument_2=fade_in_duration_s
resolved_argument_2_domain=finite-canonical-nonnegative-binary64-resolving-to-an-exact-delivery-frame-index
resolved_argument_3=fade_out_duration_s
resolved_argument_3_domain=finite-canonical-nonnegative-binary64-resolving-to-an-exact-delivery-frame-index
duration_resolution=contract-resolve-frame-index-at-reduced-rate-192000/1
)method"
    "duration_resolution_arithmetic=" CRANKWAVE_DURATION_EXTENDED_LABEL
    R"method((duration-times-192000-divided-by-1)-then-std-round-long-double-half-away-from-zero-with-8-times-binary64-epsilon-times-max(1,absolute-frames)-tolerance
duration_resolution_bound=resolved-frame-index-less-than-or-equal-to-2^53-minus-1-and-tolerance-strictly-less-than-0.25
audible_frame_count=positive-resolved-integer
fade_fit=fade-in-frame-count-plus-fade-out-frame-count-less-than-or-equal-to-audible-frame-count
volume_compile=float32(volume-linear)-using-nearest-ties-to-even
raw_mix=initialize-with-first-active-publication-calibrated-selected-stem-then-for-each-remaining-active-route-assign-float32(raw-mix-plus-publication-calibrated-selected-stem)-serially-in-declared-route-order-with-no-leading-or-placeholder-zero
raw_mix_validation=result-must-be-finite
leveler_mix=initialize-with-float32(first-active-selected-binary64-sample)-then-for-each-remaining-active-route-assign-float32(leveler-mix-plus-float32(selected-binary64-sample))-serially-in-declared-route-order-with-no-leading-or-placeholder-zero
leveler_mix_validation=result-must-be-finite
peak_retention_float32_bits=0x3f7ffef2
gain_retention_float32_bits=0x3f7fe1df
gain_target_blend_float32_bits=0x39f10800
peak_state_initial_float32=30000
gain_state_initial_float32=1
master_dynamics_state_initialization=initialize-peak-state-and-gain-state-at-session-frame-zero-before-processing-that-frame
master_dynamics_pre_audible_policy=process-and-update-state-for-every-frame-of-every-pre-audible-block
master_dynamics_crop_state_policy=do-not-reset-state-at-the-pre-audible-to-audible-crop-boundary
master_dynamics_session_lifetime=retain-and-update-state-continuously-for-the-entire-presentation-audio-session-including-finite-or-open-ended-execution
peak_update=float32(peak-retention-times-prior-peak);then-if-absolute-leveler-mix-is-greater-replace-with-that-absolute-value
target_gain=float32(22000/peak-state)-clamped-to-float32-interval-[0.00001,1.3]
gain_update=float32(float32(gain-retention-times-prior-gain)+float32(gain-target-blend-times-target-gain))
leveled=float32(leveler-mix-times-updated-gain)
volume_applied=float32(leveled-times-compiled-volume)
audition_normalized=float32(tanh(float32(volume-applied/32767)))
audition_bound=clamp-audition-normalized-to-plus-or-minus-nextafter(float32-1,float32-0)
audition_validation=every-state-and-output-must-be-finite
fade_pi_binary64_bits=0x400921fb54442d18
quarter_sine_ratio=binary64(k)/binary64(fade-frame-count)
quarter_sine_angle=(ratio-times-pi)/binary64-2-in-written-order
quarter_sine_gain=std-sin(quarter-sine-angle)
fade_in=if-frame-index-less-than-fade-in-frame-count-then-quarter-sine(frame-index,fade-in-frame-count)
fade_out_start=audible-frame-count-minus-fade-out-frame-count
unity=if-not-fade-in-and-frame-index-less-than-or-equal-to-fade-out-start-then-binary64-1
fade_out=otherwise-quarter-sine(audible-frame-count-minus-frame-index,fade-out-frame-count)
fade_geometry=frame-index-is-zero-based-and-strictly-less-than-audible-frame-count
faded=float32(binary64(audition-normalized)-times-fade-gain)-using-nearest-ties-to-even
faded_validation=result-must-be-finite
quantizer_positive_saturation=if-faded-greater-than-or-equal-to-float32-1-then-s32-2147483647
quantizer_negative_saturation=if-faded-less-than-or-equal-to-float32-negative-1-then-s32-negative-2147483648
quantizer_interior_scale=float32(faded-times-float32-2^31)
quantizer_interior_round=absolute-value-to-floor-and-fraction;round-nearest-with-exact-half-to-even;restore-sign
pcm24_code=floor(s32/256)-including-toward-negative-infinity-correction-for-negative-remainder
pcm24_range=integer-negative-8388608-through-positive-8388607
pcm24_serialization=low-24-bits-in-little-endian-byte-order
frame_execution=serial-ascending-frame-order
clipping=stateful-tanh-soft-clip-before-fade-followed-by-pcm24-saturation-guard;integrated-artifact-path-counts-saturated-samples;successful-publication-requires-zero-saturated-samples
limiting=rate-adjusted-stateful-peak-leveler-with-fixed-target-and-gain-bounds
dither=none
normalization=stateful-peak-leveler
raw_master_output=finite-publication-calibrated-raw-mix-float32-samples-before-leveling-volume-or-soft-clipping
raw_master_wave=classic-58-byte-ieee-float32-mono-192000-hz-fmt18-fact4-data-container-identical-to-publication-wave-layout
audition_metadata_input=job-owned-comment-title-software-byte-strings
audition_metadata_domain=each-nonempty-without-embedded-nul-and-at-most-4096-bytes
audition_wave_byte_order=little-endian
audition_wave_header=riff-wave-then-wave-format-extensible-fmt-with-payload-size-40-and-complete-chunk-size-48-then-list-info-then-data
audition_wave_format=format-tag-0xfffe;mono;192000-hz;byte-rate-576000;block-align-3;container-bits-24;extension-size-22;valid-bits-24;channel-mask-4
audition_wave_subformat_guid=01000000-0000-1000-8000-00aa00389b71-in-wave-little-endian-byte-layout
audition_wave_info_order=icmt-comment,inam-title,isft-software
audition_wave_info_payload=verbatim-metadata-bytes-then-nul
audition_wave_info_subchunk=fourcc-then-u32le-value-byte-count-plus-one-then-verbatim-value-bytes-then-nul
audition_wave_info_padding=append-one-zero-byte-when-info-payload-size-is-odd
audition_wave_list_payload=four-byte-info-type-plus-the-three-complete-info-subchunks
audition_wave_data_size=audible-frame-count-times-3-representable-as-u32
audition_wave_payload=pcm24-code-low-24-bits-in-frame-order
audition_wave_data_padding=append-one-zero-byte-when-data-size-is-odd
audition_wave_riff_size=complete-file-byte-count-minus-8-representable-as-u32
audition_wave_trailing_chunks=none
serialization_partition=caller-chunking-does-not-affect-bytes
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
float32_execution=ieee754-binary32-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-tanh-for-master-dynamics-and-std-sin-for-fades-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(detail::canonical_lf_descriptor(kRouteStemPublicationMethodDescriptor));
static_assert(detail::canonical_lf_descriptor(kOrderedRouteAuditionMethodDescriptor));

} // namespace

std::string_view route_stem_publication_method_descriptor() noexcept {
    return kRouteStemPublicationMethodDescriptor;
}

std::string_view ordered_route_audition_method_descriptor() noexcept {
    return kOrderedRouteAuditionMethodDescriptor;
}

} // namespace crankwave::presentation

#undef CRANKWAVE_AUDITION_METHOD_ID_LITERAL
#undef CRANKWAVE_DURATION_EXTENDED_LABEL
