#include "reference/p18_reference_catalog.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace engine_sim_offline::reference {
namespace {

[[nodiscard]] consteval std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(10 + value - 'a');
    }
    throw "P1.8 catalog digest must use lowercase hexadecimal";
}

template <std::size_t Size>
[[nodiscard]] consteval contract::Sha256Digest digest(const char (&hex)[Size]) {
    static_assert(Size == 65, "SHA-256 text must contain exactly 64 digits");
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (hex_nibble(hex[index * 2]) << 4U) | hex_nibble(hex[index * 2 + 1]));
    }
    return result;
}

[[nodiscard]] consteval P18ExpectedBinary64 binary64(double value) {
    return {std::bit_cast<std::uint64_t>(value)};
}

} // namespace

const P18ReferenceCatalogV1 &p18_reference_catalog_v1() noexcept {
    static const P18ReferenceCatalogV1 catalog = [] {
        P18ReferenceCatalogV1 result;

        result.expected_reference_inputs_schema_version = 1;
        result.expected_fixture_schema_version = 1;
        result.expected_fixture_id = "bmw-m52b28-p18-reference-capture-v1";
        result.expected_engine_id = "bmw-m52b28";
        result.expected_engine_profile_id = "bmw-m52b28-p18-reference";
        result.expected_presentation_calibration_id = "bmw-m52b28-p18-presentation-v1";

        const auto expected_kernel_coefficient_sha256 =
            digest("940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598");
        result.expected_lineage_files = {{
            {
                P18ReferenceLineageFile::manifest,
                "manifest.json",
                UINT64_C(21435),
                digest(
                    "52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07"),
            },
            {
                P18ReferenceLineageFile::parity_evidence,
                "reference-parity.bin",
                UINT64_C(38080608),
                digest(
                    "19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c"),
            },
            {
                P18ReferenceLineageFile::audit_input,
                "reference-audit.bin",
                UINT64_C(21760064),
                digest(
                    "93fbaef5fe887ba229d7acc28235d63c98f9205d2fe7e426a3e501473a2643a4"),
            },
            {
                P18ReferenceLineageFile::component_seed_input,
                "component-seeds.bin",
                UINT64_C(216),
                digest(
                    "ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f"),
            },
            {
                P18ReferenceLineageFile::renderer_algorithm_record,
                "P18_PRESENTATION_RENDERER.md",
                UINT64_C(20832),
                digest(
                    "0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008"),
            },
            {
                P18ReferenceLineageFile::configured_ir_input,
                "presentation/smooth_39.wav",
                UINT64_C(78602),
                digest(
                    "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"),
            },
            {
                P18ReferenceLineageFile::kernel_oracle_comparator,
                "presentation/smooth_39-192000hz-volume-0p001-f64le.bin",
                UINT64_C(240568),
                expected_kernel_coefficient_sha256,
            },
        }};

        result.expected_audit_reader = {"p18-reference-audit-reader-v1", 1};
        result.expected_excitation_adapter = {
            "p18-reference-audit-excitation-adapter-v1", 1};
        result.expected_audit_lane_semantic_id = "legacy_reference.exhaust_bus_pre_dsp";
        result.expected_excitation_seam = {"exhaust-excitation-block-v1", 1};

        result.expected_routes = {{
            {
                P18ReferenceRoute::exhaust_0,
                contract::RouteId{1},
                "exhaust.reference.0",
                contract::SourceRouteKind::exhaust_outlet,
            },
            {
                P18ReferenceRoute::exhaust_1,
                contract::RouteId{2},
                "exhaust.reference.1",
                contract::SourceRouteKind::exhaust_outlet,
            },
        }};
        result.expected_random_generator = {"p18_reference_pcg32_v1", 1};
        result.expected_seed_derivation = {
            "sha256_length_prefixed_capture_component_pcg32_v1", 1};
        result.expected_executed_seeds = {{
            {
                P18ReferenceExecutedRandomComponent::air_noise,
                P18ReferenceRoute::exhaust_0,
                UINT64_C(0x75bc579d4c90a640),
                UINT64_C(0x7e4ef6200e7c70c1),
            },
            {
                P18ReferenceExecutedRandomComponent::air_noise,
                P18ReferenceRoute::exhaust_1,
                UINT64_C(0x208e57f73615bd95),
                UINT64_C(0x786d92e584c43b78),
            },
            {
                P18ReferenceExecutedRandomComponent::jitter,
                P18ReferenceRoute::exhaust_0,
                UINT64_C(0x9e2b91cd0dc51cfc),
                UINT64_C(0x1ae6ee3019603abb),
            },
            {
                P18ReferenceExecutedRandomComponent::jitter,
                P18ReferenceRoute::exhaust_1,
                UINT64_C(0xdb7540a0c8b54d74),
                UINT64_C(0x41ddcdeb066bf214),
            },
        }};

        result.expected_capture = {
            {
                {UINT64_C(10000), 1},
                {UINT64_C(10000), 1},
                {UINT64_C(192000), 1},
                {UINT64_C(192000), 1},
                {UINT64_C(192000), 1},
            },
            UINT64_C(170000),
            0,
            UINT64_C(170000),
            UINT64_C(20000),
            UINT64_C(170000),
            UINT64_C(200),
            UINT64_C(3840),
            UINT64_C(3264000),
            UINT64_C(384000),
            UINT64_C(3264000),
            UINT64_C(2880000),
            UINT64_C(12648430),
        };

        result.expected_presentation.expected_schema_version = 1;
        result.expected_presentation.expected_algorithm_record_semantic_id =
            "bmw-m52b28-p18-presentation-renderer-v1";
        result.expected_presentation.expected_algorithm_record_evidence_source_id =
            "p18-presentation-renderer-record";
        result.expected_presentation.expected_methods = {{
            {
                P18ReferencePresentationMethod::reconstruction,
                {
                    "kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_"
                    "cutoff0p95_source_nyquist_unity_dc_binary64_v2",
                    1,
                },
            },
            {
                P18ReferencePresentationMethod::conditioning,
                {"p18-synthesizer-conditioning-v1", 1},
            },
            {
                P18ReferencePresentationMethod::impulse_response_conversion,
                {
                    "blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_"
                    "area_binary64_v3",
                    1,
                },
            },
            {
                P18ReferencePresentationMethod::convolution,
                {
                    "causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1",
                    1,
                },
            },
            {
                P18ReferencePresentationMethod::publication,
                {"p18-float32-stem-publication-v1", 1},
            },
            {
                P18ReferencePresentationMethod::audition_mix,
                {"p18-reference-audition-mix-v1", 1},
            },
        }};
        result.expected_presentation.expected_configured_ir_media = {
            contract::AudioAssetId{1},
            "smooth-39",
            "smooth-39-ir",
            contract::AudioSampleEncoding::pcm_s16le,
            contract::AudioChannelLayout::mono,
            {UINT64_C(44100), 1},
            UINT64_C(33705),
            UINT64_C(6907),
        };
        result.expected_presentation.expected_scalars = {
            binary64(0.5),  binary64(10000.0),   {UINT64_C(0x3f847ae140000000)},
            binary64(1.0),  binary64(2000.0),    {UINT64_C(0x3f50624dd2f1a9fc)},
            binary64(1.0),  binary64(0x1.0p-26), binary64(128.0),
            binary64(0.02), binary64(0.02),
        };
        result.expected_presentation.expected_audition_route_order = {
            P18ReferenceRoute::exhaust_0,
            P18ReferenceRoute::exhaust_1,
        };

        result.expected_audio = {{
            {
                P18ReferenceAudioArtifact::exhaust_0_dry,
                "exhaust.reference.0.dry",
                "audio/exhaust.reference.0.dry.wav",
                UINT64_C(11520058),
                digest(
                    "e5a96cb5d3b9f1732e741916706a99c6a7c5e1a912e3d751cb92846d33ce6eeb"),
                true,
            },
            {
                P18ReferenceAudioArtifact::exhaust_0_configured_ir,
                "exhaust.reference.0.configured_ir",
                "audio/exhaust.reference.0.configured-ir.wav",
                UINT64_C(11520058),
                digest(
                    "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32"),
                true,
            },
            {
                P18ReferenceAudioArtifact::exhaust_0_selected,
                "exhaust.reference.0.selected",
                "audio/exhaust.reference.0.selected.wav",
                UINT64_C(11520058),
                digest(
                    "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32"),
                false,
            },
            {
                P18ReferenceAudioArtifact::exhaust_1_dry,
                "exhaust.reference.1.dry",
                "audio/exhaust.reference.1.dry.wav",
                UINT64_C(11520058),
                digest(
                    "2ad2ed41097af30421047f3e4a6033086ec70b9731082833699caad1da9d81ea"),
                true,
            },
            {
                P18ReferenceAudioArtifact::exhaust_1_configured_ir,
                "exhaust.reference.1.configured_ir",
                "audio/exhaust.reference.1.configured-ir.wav",
                UINT64_C(11520058),
                digest(
                    "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d"),
                true,
            },
            {
                P18ReferenceAudioArtifact::exhaust_1_selected,
                "exhaust.reference.1.selected",
                "audio/exhaust.reference.1.selected.wav",
                UINT64_C(11520058),
                digest(
                    "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d"),
                false,
            },
            {
                P18ReferenceAudioArtifact::master_raw,
                "master.reference.raw",
                "audio/master.reference.raw.wav",
                UINT64_C(11520058),
                digest(
                    "2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146"),
                false,
            },
            {
                P18ReferenceAudioArtifact::master_audition,
                "master.reference.audition",
                "audio/master.reference.audition.wav",
                UINT64_C(8640302),
                digest(
                    "f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb"),
                false,
            },
        }};

        result.expected_mastering = {{
            {
                P18ReferenceMasteringPayload::raw_float32,
                digest(
                    "fe2475249df2f6a51b2c82c8251493216db1a1ec094a7a0c5577a11c430f410f"),
            },
            {
                P18ReferenceMasteringPayload::monitoring_float32,
                digest(
                    "0a2abe8ea8f166c1022efda26c57e5ad4eda5e7cb515100a5e6d16eb465318db"),
            },
            {
                P18ReferenceMasteringPayload::faded_float32,
                digest(
                    "af194389df2ba20ab9d1bc5e3f97735afbb6c76d4c215a7ac2e1d45ecd3a3633"),
            },
            {
                P18ReferenceMasteringPayload::s32le,
                digest(
                    "b0505bc9a81cfdcea0256ff6e5731ac2a1f58f90f43911bc84d799926151d924"),
            },
            {
                P18ReferenceMasteringPayload::pcm24le,
                digest(
                    "2153869958bb924e4eda277a37e95eab1abb7c29aa9fa389c1fa8f879e7bfdcf"),
            },
        }};
        result.expected_kernel = {
            UINT64_C(30071),
            expected_kernel_coefficient_sha256,
            digest("a1a12fc0224ecdf824e402562ed6b5fd31d915278693a8cfaaeea41a5cf957d2"),
        };
        result.expected_render = {
            UINT64_C(170000),     UINT64_C(850),    UINT64_C(100),     UINT64_C(750),
            UINT64_C(3264000),    UINT64_C(384000), UINT64_C(2880000), 0,
            UINT32_C(0x3f2ad253),
        };

        return result;
    }();
    return catalog;
}

} // namespace engine_sim_offline::reference
