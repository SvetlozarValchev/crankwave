#include "presentation/presentation_method_registry.hpp"

#include "presentation/presentation_method_descriptor_support.hpp"

namespace engine_sim_offline::presentation {
namespace {

constexpr std::string_view kCalibratedPressurePublicationMethodDescriptor =
    R"method(engine-sim-offline.presentation-method-configuration.v1
method=calibrated-two-outlet-pressure-float32-wave-publication-v1
version=1
operation=publish-two-physical-outlet-pressure-stems-and-their-coherent-raw-master
topology=exactly-two-distinct-exhaust-outlet-routes
route_order=ascending-stable-nonzero-route-id
route_input=one-finite-binary64-radiated-pressure-pa-sample-per-route-per-acoustic-frame
acoustic_rate_hz=192000/1
delivery_rate_hz=192000/1
resampling=none
resolved_calibration=engine-physical-exhaust-acoustic-assembly-pa_per_full_scale
resolved_calibration_domain=finite-canonical-positive-binary64
route_calibration=binary64-pressure-pa-divided-by-pa-per-full-scale-in-written-order
route_publication=float32(route-calibration)-using-nearest-ties-to-even
route_publication_validation=result-must-be-finite
raw_master_reduction=float32(route-0-publication-plus-route-1-publication)-in-route-order
raw_master_validation=result-must-be-finite
published_interval=scenario-audible-interval-after-continuous-pre-audible-acoustic-execution
crop_state_policy=no-acoustic-state-is-reset-at-the-audible-boundary
tail_policy=truncate-at-scenario-timeline-end-with-no-network-tail-flush
published_frame_mapping=one-output-frame-per-audible-acoustic-frame-in-input-order
output_order=route-0-pressure,route-1-pressure,master-engine-raw
output=exactly-three-finite-float32-mono-streams
clipping=none
limiting=none
dither=none
normalization=none
float32_wave_count=exactly-three-one-per-output-stream
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

constexpr std::string_view kCoherentTwoOutletAuditionMethodDescriptor =
    R"method(engine-sim-offline.presentation-method-configuration.v1
method=coherent-two-outlet-quarter-sine-pcm24-wave-audition-v1
version=1
operation=monitor-fade-and-quantize-the-coherent-two-outlet-raw-master
input=one-finite-float32-calibrated-coherent-two-outlet-raw-master-sample-per-frame
delivery_rate_hz=192000/1
resolved_argument_1=monitoring-gain-linear
resolved_argument_1_domain=finite-binary64-that-rounds-nearest-ties-even-to-finite-positive-float32
resolved_argument_2=fade-in-duration-s
resolved_argument_2_domain=finite-canonical-nonnegative-binary64-resolving-to-an-exact-delivery-frame-index
resolved_argument_3=fade-out-duration-s
resolved_argument_3_domain=finite-canonical-nonnegative-binary64-resolving-to-an-exact-delivery-frame-index
duration_resolution=contract-resolve-frame-index-at-reduced-rate-192000/1
duration_resolution_arithmetic=x87-extended(duration-times-192000-divided-by-1)-then-std-round-long-double-half-away-from-zero-with-8-times-binary64-epsilon-times-max(1,absolute-frames)-tolerance
duration_resolution_bound=resolved-frame-index-less-than-or-equal-to-2^53-minus-1-and-tolerance-strictly-less-than-0.25
audible_frame_count=positive-resolved-integer
fade_fit=fade-in-frame-count-plus-fade-out-frame-count-less-than-or-equal-to-audible-frame-count
monitoring_gain_compile=float32(monitoring-gain-linear)-using-nearest-ties-to-even
monitor=float32(raw-master-times-compiled-monitoring-gain)-in-written-order
monitor_validation=result-must-be-finite
fade_pi_binary64_bits=0x400921fb54442d18
quarter_sine_ratio=binary64(k)/binary64(fade-frame-count)
quarter_sine_angle=(ratio-times-pi)/binary64-2-in-written-order
quarter_sine_gain=std-sin(quarter-sine-angle)
fade_in=if-frame-index-less-than-fade-in-frame-count-then-quarter-sine(frame-index,fade-in-frame-count)
fade_out_start=audible-frame-count-minus-fade-out-frame-count
unity=if-not-fade-in-and-frame-index-less-than-or-equal-to-fade-out-start-then-binary64-1
fade_out=otherwise-quarter-sine(audible-frame-count-minus-frame-index,fade-out-frame-count)
fade_geometry=frame-index-is-zero-based-and-strictly-less-than-audible-frame-count
faded=float32(binary64(monitor)-times-fade-gain)-using-nearest-ties-to-even
faded_validation=result-must-be-finite
quantizer_positive_saturation=if-faded-greater-than-or-equal-to-float32-1-then-s32-2147483647
quantizer_negative_saturation=if-faded-less-than-or-equal-to-float32-negative-1-then-s32-negative-2147483648
quantizer_interior_scale=float32(faded-times-float32-2^31)
quantizer_interior_round=absolute-value-to-floor-and-fraction;round-nearest-with-exact-half-to-even;restore-sign
pcm24_code=floor(s32/256)-including-toward-negative-infinity-correction-for-negative-remainder
pcm24_range=integer-negative-8388608-through-positive-8388607
pcm24_serialization=low-24-bits-in-little-endian-byte-order
frame_execution=serial-ascending-frame-order
clipping=pcm24-saturation-only
limiting=none
dither=none
normalization=none
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
transcendentals=std-sin-binary64-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(
    detail::canonical_lf_descriptor(kCalibratedPressurePublicationMethodDescriptor));
static_assert(
    detail::canonical_lf_descriptor(kCoherentTwoOutletAuditionMethodDescriptor));

} // namespace

std::string_view calibrated_pressure_publication_method_descriptor() noexcept {
    return kCalibratedPressurePublicationMethodDescriptor;
}

std::string_view coherent_two_outlet_audition_method_descriptor() noexcept {
    return kCoherentTwoOutletAuditionMethodDescriptor;
}

} // namespace engine_sim_offline::presentation
