#pragma once

#include "contract_test_support.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract::test {

inline Sha256Digest reference_digest(std::string_view text) {
    if (text.size() != 64) {
        throw std::runtime_error("reference SHA-256 fixture must contain 64 digits");
    }

    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(10 + value - 'a');
        }
        throw std::runtime_error("reference SHA-256 fixture is not lowercase hex");
    };

    Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2]) << 4U) | nibble(text[index * 2 + 1]));
    }
    return result;
}

inline ReferencePayloadIdentity reference_payload(std::uint64_t byte_count,
                                                  std::string_view sha256) {
    return {byte_count, reference_digest(sha256)};
}

inline PresentationCalibration make_reference_presentation(InputBuilder &builder) {
    const auto algorithm_digest = reference_digest(
        "0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008");
    const auto ir_digest = reference_digest(
        "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc");
    builder.provenance.evidence.push_back({
        "p18-presentation-renderer-record",
        "reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md",
        std::nullopt,
        algorithm_digest,
        RightsDisposition::permitted,
    });
    builder.provenance.evidence.push_back({
        "smooth-39-ir",
        "reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav",
        std::nullopt,
        ir_digest,
        RightsDisposition::local_evaluation_only,
    });

    const auto resolved_method = [&](std::string id, std::uint8_t digest_byte,
                                     std::string path) {
        return builder.resolved(method(std::move(id), digest_byte),
                                "presentation.methods." + std::move(path));
    };

    PresentationCalibration presentation;
    presentation.schema_version = 1;
    presentation.calibration_id = "bmw-m52b28-p18-presentation-v1";
    presentation.engine_profile_id = builder.resolved(
        std::string{"bmw-m52b28-p18-reference"}, "presentation.engine_profile_id");
    presentation.methods = {
        resolved_method("kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_"
                        "cutoff0p95_source_nyquist_unity_dc_binary64_v2",
                        80, "reconstruction"),
        resolved_method("p18-synthesizer-conditioning-v1", 81, "conditioning"),
        resolved_method(
            "blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_"
            "binary64_v3",
            82, "impulse_response_conversion"),
        resolved_method("causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1",
                        83, "convolution"),
        resolved_method("p18-float32-stem-publication-v1", 84, "publication"),
        resolved_method("p18-reference-audition-mix-v1", 85, "audition_mix"),
    };
    presentation.algorithm_record = {
        builder.resolved(std::string{"bmw-m52b28-p18-presentation-renderer-v1"},
                         "presentation.algorithm_record.semantic_id"),
        builder.resolved(std::string{"p18-presentation-renderer-record"},
                         "presentation.algorithm_record.evidence_source_id"),
        builder.resolved(algorithm_digest,
                         "presentation.algorithm_record.content_sha256"),
    };
    presentation.conditioning = {
        builder.resolved(0.5, "presentation.conditioning.jitter_scale"),
        builder.resolved(10000.0,
                         "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(std::bit_cast<double>(UINT64_C(0x3f847ae140000000)),
                         "presentation.conditioning.derivative_mix_01"),
        builder.resolved(1.0, "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(2000.0, "presentation.conditioning.air_noise_cutoff_hz"),
    };
    presentation.assets.push_back({
        AudioAssetId{1},
        builder.resolved(std::string{"smooth-39"},
                         "presentation.assets.smooth-39.semantic_id"),
        builder.resolved(std::string{"smooth-39-ir"},
                         "presentation.assets.smooth-39.evidence_source_id"),
        builder.resolved(ir_digest, "presentation.assets.smooth-39.content_sha256"),
        builder.resolved(
            AudioMediaContract{
                AudioSampleEncoding::pcm_s16le,
                AudioChannelLayout::mono,
                {44100, 1},
                33705,
            },
            "presentation.assets.smooth-39.media"),
    });
    const auto ir_gain = std::bit_cast<double>(UINT64_C(0x3f50624dd2f1a9fc));
    presentation.routes = {
        {
            RouteId{1},
            AudioAssetId{1},
            builder.resolved(ir_gain, "presentation.routes.exhaust.reference.0."
                                      "impulse_response_gain_linear"),
            builder.resolved(1.0, "presentation.routes.exhaust.reference.0.wet_mix_01"),
        },
        {
            RouteId{2},
            AudioAssetId{1},
            builder.resolved(ir_gain, "presentation.routes.exhaust.reference.1."
                                      "impulse_response_gain_linear"),
            builder.resolved(1.0, "presentation.routes.exhaust.reference.1.wet_mix_01"),
        },
    };
    presentation.publication.calibration_gain_linear =
        builder.resolved(0x1.0p-26, "presentation.publication.calibration_gain_linear");
    presentation.audition = {
        builder.resolved(std::vector<RouteId>{RouteId{1}, RouteId{2}},
                         "presentation.audition.selected_routes"),
        builder.resolved(128.0, "presentation.audition.monitoring_gain_linear"),
        builder.resolved(0.02, "presentation.audition.fade_in_duration_s"),
        builder.resolved(0.02, "presentation.audition.fade_out_duration_s"),
    };
    presentation.provenance_schema_id = builder.provenance.schema_id;
    return presentation;
}

inline ReferencePresentationInputsV1 make_reference_inputs(InputBuilder &builder) {
    ReferencePresentationInputsV1 inputs;
    inputs.schema_version = 1;
    inputs.fixture = {
        "bmw-m52b28-p18-reference-capture-v1",
        1,
        reference_payload(
            UINT64_C(21435),
            "52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07"),
        reference_payload(
            UINT64_C(38080608),
            "19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c"),
        reference_payload(
            UINT64_C(21760064),
            "93fbaef5fe887ba229d7acc28235d63c98f9205d2fe7e426a3e501473a2643a4"),
        reference_payload(
            UINT64_C(216),
            "ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f"),
        reference_payload(
            UINT64_C(20832),
            "0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008"),
        reference_payload(
            UINT64_C(78602),
            "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"),
        reference_payload(
            UINT64_C(240568),
            "940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598"),
    };
    inputs.audit_reader = method("p18-reference-audit-reader-v1", 86);
    inputs.excitation_adapter = method("p18-reference-audit-excitation-adapter-v1", 87);
    inputs.audit_lane_semantic_id = "legacy_reference.exhaust_bus_pre_dsp";
    inputs.excitation_seam = method("exhaust-excitation-block-v1", 88);
    inputs.engine = {
        "bmw-m52b28",
        "bmw-m52b28-p18-reference",
        {
            {RouteId{1}, "exhaust.reference.0", SourceRouteKind::exhaust_outlet},
            {RouteId{2}, "exhaust.reference.1", SourceRouteKind::exhaust_outlet},
        },
    };
    inputs.capture = {
        {
            {10000, 1},
            {10000, 1},
            {192000, 1},
            {192000, 1},
            {192000, 1},
        },
        UINT64_C(170000),
        0,
        UINT64_C(170000),
        UINT64_C(20000),
        UINT64_C(170000),
        UINT64_C(200),
        UINT64_C(3264000),
        UINT64_C(384000),
        UINT64_C(3264000),
        UINT64_C(2880000),
        UINT64_C(12648430),
    };
    inputs.presentation = make_reference_presentation(builder);
    return inputs;
}

inline const ReferencePresentationInputsV1 &
reference_inputs(const RenderManifestContent &content) {
    return std::get<ReferencePresentationInputsV1>(content.inputs);
}

inline ReferencePresentationInputsV1 &reference_inputs(RenderManifestContent &content) {
    return std::get<ReferencePresentationInputsV1>(content.inputs);
}

struct ReferenceManifestFixture {
    InputBuilder builder;
    RenderManifestContent content;

    ReferenceManifestFixture() {
        const auto &source_matrix = bmw_m52b28_reference_source_matrix_v1();
        const auto inputs = make_reference_inputs(builder);

        content.schema_version = 1;
        content.inputs = inputs;
        content.provenance = builder.provenance.bundle;
        content.determinism = {
            BuildIdentity{
                "reference-manifest-contract-test",
                digest(90),
                "gcc",
                "test",
                "x86_64-linux-gnu",
                "libstdcxx",
                "test",
                "glibc-libm",
                "test",
            },
            "x86-64-v3",
            FloatingPointIdentity{
                "ieee754_binary64",
                "nearest_ties_to_even",
                false,
                false,
                false,
            },
            1,
            "serial-stable-order",
        };
        content.rates = inputs.capture.rates;
        content.randomness = {
            method("p18_reference_pcg32_v1", 91),
            UINT64_C(12648430),
            method("sha256_length_prefixed_capture_component_pcg32_v1", 92),
            {
                {
                    RandomComponentKind::presentation_air_noise,
                    std::nullopt,
                    RouteId{1},
                    UINT64_C(0x75bc579d4c90a640),
                    UINT64_C(0x7e4ef6200e7c70c1),
                },
                {
                    RandomComponentKind::presentation_air_noise,
                    std::nullopt,
                    RouteId{2},
                    UINT64_C(0x208e57f73615bd95),
                    UINT64_C(0x786d92e584c43b78),
                },
                {
                    RandomComponentKind::presentation_jitter,
                    std::nullopt,
                    RouteId{1},
                    UINT64_C(0x9e2b91cd0dc51cfc),
                    UINT64_C(0x1ae6ee3019603abb),
                },
                {
                    RandomComponentKind::presentation_jitter,
                    std::nullopt,
                    RouteId{2},
                    UINT64_C(0xdb7540a0c8b54d74),
                    UINT64_C(0x41ddcdeb066bf214),
                },
            },
        };
        content.output_contract = resolve_output_contract(source_matrix);
        content.routes = {
            {
                RouteId{1},
                "exhaust.reference.0",
                SourceRouteKind::exhaust_outlet,
                RouteDisposition::rendered,
                "",
                {
                    "exhaust.reference.0.dry",
                    "exhaust.reference.0.configured_ir",
                    "exhaust.reference.0.selected",
                },
            },
            {
                RouteId{2},
                "exhaust.reference.1",
                SourceRouteKind::exhaust_outlet,
                RouteDisposition::rendered,
                "",
                {
                    "exhaust.reference.1.dry",
                    "exhaust.reference.1.configured_ir",
                    "exhaust.reference.1.selected",
                },
            },
        };
        content.output_buses = {
            {
                "master.reference.raw",
                OutputBusKind::master_reference_raw,
                {"master.reference.raw"},
            },
            {
                "master.reference.audition",
                OutputBusKind::master_reference_audition,
                {"master.reference.audition"},
            },
        };

        constexpr std::array<std::string_view, 8> paths{
            "audio/exhaust.reference.0.dry.wav",
            "audio/exhaust.reference.0.configured-ir.wav",
            "audio/exhaust.reference.0.selected.wav",
            "audio/exhaust.reference.1.dry.wav",
            "audio/exhaust.reference.1.configured-ir.wav",
            "audio/exhaust.reference.1.selected.wav",
            "audio/master.reference.raw.wav",
            "audio/master.reference.audition.wav",
        };
        constexpr std::array<std::uint64_t, 8> byte_counts{
            UINT64_C(11520058), UINT64_C(11520058), UINT64_C(11520058),
            UINT64_C(11520058), UINT64_C(11520058), UINT64_C(11520058),
            UINT64_C(11520058), UINT64_C(8640302),
        };
        constexpr std::array<std::string_view, 8> hashes{
            "e5a96cb5d3b9f1732e741916706a99c6a7c5e1a912e3d751cb92846d33ce6eeb",
            "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32",
            "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32",
            "2ad2ed41097af30421047f3e4a6033086ec70b9731082833699caad1da9d81ea",
            "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d",
            "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d",
            "2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146",
            "f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb",
        };
        for (std::size_t index = 0; index < source_matrix.required_artifacts.size();
             ++index) {
            const auto &requirement = source_matrix.required_artifacts[index];
            content.artifacts.push_back({
                requirement.role,
                requirement.kind,
                std::string(paths[index]),
                requirement.audio,
                byte_counts[index],
                reference_digest(hashes[index]),
                requirement.diagnostic,
            });
        }
    }
};

} // namespace engine_sim_offline::contract::test
