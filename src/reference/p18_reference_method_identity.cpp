#include "reference/p18_reference_method_identity.hpp"

#include "reference/p18_reference_catalog.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace engine_sim_offline::reference {
namespace {

[[nodiscard]] consteval std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(10 + value - 'a');
    }
    throw "P1.8 method digest must use lowercase hexadecimal";
}

template <std::size_t Size>
[[nodiscard]] consteval contract::Sha256Digest pinned_digest(const char (&hex)[Size]) {
    static_assert(Size == 65, "SHA-256 text must contain exactly 64 digits");
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (hex_nibble(hex[index * 2]) << 4U) | hex_nibble(hex[index * 2 + 1]));
    }
    return result;
}

constexpr std::array<contract::Sha256Digest, kP18ReferenceMethodCount>
    kPinnedConfigurationDigests{
        pinned_digest(
            "3577c0c36a817c0c1c6ba944e0662dbf65cc1c8cedf6a530accff8baf76d7a7e"),
        pinned_digest(
            "08cd0ee63b6cc97c097d6a91577d65292a70d4a66302a1d45cdc23c3a47521e3"),
        pinned_digest(
            "f508eddbd4e031547bb6e9a4f49f7962c1f7bafdc4d0f740f309e2a370956208"),
        pinned_digest(
            "303578ab8b93ec4bde570e555df0034b30461f8680e97d04744bc49d5941f236"),
        pinned_digest(
            "f4fd3fd4005e607c79a1f6571166de539c849ab43708899a8eeb7d196ef1c7b3"),
        pinned_digest(
            "96e801125575b0a6235671c43a7da437739f969695ebc700a3753b5f5c9387ac"),
        pinned_digest(
            "8abbacdc9202a01a05daa306ea42a3fa3dd405a5484000e7e0de5d5673dfc2bc"),
        pinned_digest(
            "9758d764d28a60bfcd571fa6af128e06f37c1036b8cc1d65b93c4b2e7cef9554"),
        pinned_digest(
            "9879bf8f2186105ec105af5f9b47bd877abb072d598371309c8b025076b28b50"),
        pinned_digest(
            "6ab31f524e245a642f6fb9c6fe6dd18b85eaf5c1cd00daf63d1199c18fa188bf"),
        pinned_digest(
            "6e332228f8e64f30a821e9961e8a16c8d55da2a0a14a3a4118ee40054b2c4105"),
    };

constexpr std::string_view kAuditReaderDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18-reference-audit-reader-v1
wire_magic=ESOAUD01
wire_version=1
byte_order=little-endian
header_bytes=64
record_bytes=128
cylinder_count=6
bus_count=2
sample_rate_hz=10000
record_count=170000
record_interval=[0,170000)
record_layout=sample_index:u64,step_end:u64,values:f64[14]
record_continuity=sample_index==record_index;step_end==record_index+1
retained_values=values[12],values[13]
retained_unit=engine_sim_source_unit
floating_point=ieee754-binary64-finite-only
trailing_bytes=forbidden
)CONFIG";

constexpr std::string_view kExcitationAdapterDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18-reference-audit-excitation-adapter-v1
source_lane=legacy_reference.exhaust_bus_pre_dsp
source_shape=ordered-bus0-bus1-binary64
destination_shape=ExhaustExcitationFrame.route_values_engine_sim_source_unit[2]
mapping=bus0->route1;bus1->route2
arithmetic=unscaled-identity-copy
block_input_frames=200
sample_rate=10000/1
unit=engine_sim_source_unit
)CONFIG";

constexpr std::string_view kExcitationSeamDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=exhaust-excitation-block-v1
ownership=callback-scoped-borrowed-immutable-view
accepted_storage=contiguous-sized-lvalue-range-only
first_frame_index=u64-global-contiguous
sample_rate=10000/1
route_ids=1,2
frame_layout=frame-major-binary64-pair
block_input_frames=200
unit=engine_sim_source_unit
)CONFIG";

constexpr std::string_view kRandomGeneratorDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18_reference_pcg32_v1
state_bits=64
output_bits=32
multiplier=6364136223846793005
stream_max=9223372036854775807
increment=(stream<<1)|1
seeding=state=0;step;state+=initial_state;step
output=pcg-xsh-rr-old-state
rotation=old_state>>59
xorshifted=((old_state>>18)^old_state)>>27
uniform_binary64=((next_u32>>5)<<26|(next_u32>>6))*2^-53
uniform_signed_binary64=2*uniform_binary64-1
)CONFIG";

constexpr std::string_view kSeedDerivationDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=sha256_length_prefixed_capture_component_pcg32_v1
text_encoding=utf-8
text_framing=u64be-byte-length+bytes
map_order=key-lexicographic
capture_key_domain=engine-sim-offline-capture-random-key-v1
capture_key_fields=capture_id,public_seed,seed_derivation
capture_id=baked.loaded_acceleration
public_seed_decimal=12648430
capture_seed_derivation=sha256_length_prefixed_capture_component_pcg32_v1
capture_random_key_sha256=272121adec1fe448dd149b05990e477a816a7e6191352854d7a1dafd92183f5d
component_domain=engine-sim-offline-capture-component-seed-v1
component_fields=capture_random_key_sha256,component_domain,component_index
component_inventory=combustion[0..5],synth_air_noise[0..1],synth_jitter[0..1],starter[0]
initial_state=u64be(digest[0:8])
stream=u64be(digest[8:16])&0x7fffffffffffffff
)CONFIG";

constexpr std::string_view kReconstructionDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_cutoff0p95_source_nyquist_unity_dc_binary64_v2
input_rate=10000/1
output_rate=192000/1
routes=2-shared-clock-independent-history
method_block=200-input-frames->3840-output-frames
kernel=kaiser-windowed-sinc
taps=257
half_width=128
phase_intervals=4096
phase_rows=4097-shifted-wrap-row
kaiser_beta=12
bessel_i0_terms=40
cutoff_source_nyquist=0.95
row_normalization=unity-dc-binary64
phase_interpolation=adjacent-row-linear-binary64
clock=integer-rational-causal-current-input-committed-after-interval
initial_state=zero
)CONFIG";

constexpr std::string_view kConditioningDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18-synthesizer-conditioning-v1
sample_rate=192000/1
state=continuous-per-route-zero-initialized
jitter_history_frames=41
jitter_random_offset=[0,40)
jitter_mean_offset=20
jitter_amount=0.5
noise_excitation_scale_bits=0x3ff6a09e667f3bcd
jitter_modulation_lowpass=4th-order-butterworth-10000hz-binary64
jitter_delay=clamp[0,40]+linear-interpolation
dc_removal=time_constant=1/(20*3.14159265359)
derivative=backward-first-difference
derivative_mix_bits=0x3f847ae140000000
air_noise=pcg32-signed*noise_excitation_scale
air_noise_lowpass=4th-order-butterworth-2000hz-binary64
air_noise_mix=1
cleanup=flush-conditioned-binary64-subnormal-to-positive-zero
)CONFIG";

constexpr std::string_view kImpulseResponseConversionDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_binary64_v3
input=mono-pcm16le-44100hz-33705-frames
meaningful_support=last-abs-pcm16>100;6907-frames
output=mono-binary64-192000hz-30071-coefficients
target_count=round-half-up(support_frames*192000/44100)
kernel=blackman-windowed-sinc
taps=24
half_width=12
phase_intervals=4096
phase_rows=4097
phase_interpolation=adjacent-row-linear-binary64
cutoff=min(1,target_rate/source_rate)
normalization=per-source-column-discrete-area
minimum_retained_weight=extended(binary64(1e-8))
fallback=nearest-target-before-normal-accumulation
accumulator=x87-extended-64-significand-bits-required
pcm16_divisor=32767
configured_gain_bits=0x3f50624dd2f1a9fc
)CONFIG";

constexpr std::string_view kConvolutionDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1
kernel_coefficients=30071
history_frames=30070
maximum_block_frames=9600
executed_block_frames=3840
fft=complex-binary64-radix2-dit
fft_length=65536
fft_bits=16
bit_reversal=precomputed
roots=precomputed-cos-sin-negative-angle
inverse=conjugate-roots-then-scale-1/65536
kernel_spectrum=zero-pad-then-forward-fft
convolution=overlap-save-discard-history-prefix
routes=2-independent-continuous-histories-shared-immutable-kernel
tail=not-published
)CONFIG";

constexpr std::string_view kPublicationDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18-float32-stem-publication-v1
input=binary64-dry-configured_ir-selected-per-route
route_order=exhaust.reference.0,exhaust.reference.1
stem_order=dry,configured_ir,selected
conversion=binary64->float32;promote-to-binary64;multiply-2^-26;float32
calibration_gain=0x1p-26
wet_mix=1
wave_format=RIFF-WAVE-IEEE-float32le-mono
sample_rate=192000/1
frames=2880000
warmup=384000-source-frames-not-published
published_stems=6
diagnostic_stems=dry,configured_ir
non_diagnostic_stems=selected
)CONFIG";

constexpr std::string_view kAuditionMixDescriptor =
    R"CONFIG(engine-sim-offline.p18-method-configuration.v1
method=p18-reference-audition-mix-v1
inputs=exhaust.reference.0.selected,exhaust.reference.1.selected
sum=float32(route0+route1)
monitor=float32(raw*float32(128))
frames=2880000
fade_frames=3840
fade_in=sin((frame/3840)*pi/2)
fade_out=sin(((2880000-frame)/3840)*pi/2)
fade_multiply=float32(binary64(monitor)*binary64(gain))
quantize=float32*2^31->signed32-nearest-ties-even-saturating
pcm24=floor(signed32/256)
wave_format=RIFF-WAVEFORMATEXTENSIBLE-pcm24le-mono
sample_rate=192000/1
prefix_bytes=302
payload_bytes=8640000
limiter=none
compressor=none
)CONFIG";

[[nodiscard]] contract::Sha256Digest digest(std::string_view descriptor) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] P18ReferenceMethodIdentity
make_identity(P18ReferenceMethod method, const P18ExpectedSemanticMethod &semantic,
              std::string_view descriptor,
              contract::Sha256Digest pinned_configuration_digest) {
    if (!contract::is_valid_semantic_id(semantic.expected_id) ||
        semantic.expected_version == 0 || descriptor.empty() ||
        descriptor.back() != '\n' ||
        digest(descriptor) != pinned_configuration_digest ||
        pinned_configuration_digest.is_zero()) {
        throw std::logic_error{"invalid P1.8 method identity definition"};
    }
    return {
        method,
        semantic.expected_id,
        semantic.expected_version,
        pinned_configuration_digest,
        descriptor,
    };
}

[[nodiscard]] const P18ExpectedSemanticMethod &
presentation_method(const P18ReferenceCatalogV1 &catalog,
                    P18ReferencePresentationMethod method) {
    const auto index = static_cast<std::size_t>(method);
    if (index >= catalog.expected_presentation.expected_methods.size() ||
        catalog.expected_presentation.expected_methods[index].method != method) {
        throw std::logic_error{"P1.8 presentation method catalog order changed"};
    }
    return catalog.expected_presentation.expected_methods[index].expected_semantic;
}

} // namespace

contract::MethodIdentity P18ReferenceMethodIdentity::contract_identity() const {
    return {std::string{id}, version, configuration_sha256};
}

const std::array<P18ReferenceMethodIdentity, kP18ReferenceMethodCount> &
p18_reference_method_identities_v1() {
    static const auto identities = [] {
        const auto &catalog = p18_reference_catalog_v1();
        return std::array<P18ReferenceMethodIdentity, kP18ReferenceMethodCount>{
            make_identity(P18ReferenceMethod::audit_reader,
                          catalog.expected_audit_reader, kAuditReaderDescriptor,
                          kPinnedConfigurationDigests[0]),
            make_identity(P18ReferenceMethod::excitation_adapter,
                          catalog.expected_excitation_adapter,
                          kExcitationAdapterDescriptor, kPinnedConfigurationDigests[1]),
            make_identity(P18ReferenceMethod::excitation_seam,
                          catalog.expected_excitation_seam, kExcitationSeamDescriptor,
                          kPinnedConfigurationDigests[2]),
            make_identity(P18ReferenceMethod::random_generator,
                          catalog.expected_random_generator, kRandomGeneratorDescriptor,
                          kPinnedConfigurationDigests[3]),
            make_identity(P18ReferenceMethod::seed_derivation,
                          catalog.expected_seed_derivation, kSeedDerivationDescriptor,
                          kPinnedConfigurationDigests[4]),
            make_identity(P18ReferenceMethod::reconstruction,
                          presentation_method(
                              catalog, P18ReferencePresentationMethod::reconstruction),
                          kReconstructionDescriptor, kPinnedConfigurationDigests[5]),
            make_identity(P18ReferenceMethod::conditioning,
                          presentation_method(
                              catalog, P18ReferencePresentationMethod::conditioning),
                          kConditioningDescriptor, kPinnedConfigurationDigests[6]),
            make_identity(
                P18ReferenceMethod::impulse_response_conversion,
                presentation_method(
                    catalog,
                    P18ReferencePresentationMethod::impulse_response_conversion),
                kImpulseResponseConversionDescriptor, kPinnedConfigurationDigests[7]),
            make_identity(P18ReferenceMethod::convolution,
                          presentation_method(
                              catalog, P18ReferencePresentationMethod::convolution),
                          kConvolutionDescriptor, kPinnedConfigurationDigests[8]),
            make_identity(P18ReferenceMethod::publication,
                          presentation_method(
                              catalog, P18ReferencePresentationMethod::publication),
                          kPublicationDescriptor, kPinnedConfigurationDigests[9]),
            make_identity(P18ReferenceMethod::audition_mix,
                          presentation_method(
                              catalog, P18ReferencePresentationMethod::audition_mix),
                          kAuditionMixDescriptor, kPinnedConfigurationDigests[10]),
        };
    }();
    return identities;
}

const P18ReferenceMethodIdentity &
p18_reference_method_identity(P18ReferenceMethod method) {
    const auto &identities = p18_reference_method_identities_v1();
    const auto index = static_cast<std::size_t>(method);
    if (index >= identities.size() || identities[index].method != method) {
        throw std::out_of_range{"unknown P1.8 reference method"};
    }
    return identities[index];
}

} // namespace engine_sim_offline::reference
