#pragma once

#include "contract_test_support.hpp"
#include "reference/p18_reference_catalog.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::contract::test {

inline const reference::P18ExpectedLineageFile &
reference_lineage_file(const reference::P18ReferenceCatalogV1 &catalog,
                       reference::P18ReferenceLineageFile file) {
    for (const auto &candidate : catalog.expected_lineage_files) {
        if (candidate.file == file) {
            return candidate;
        }
    }
    throw std::logic_error("P1.8 catalog is missing a reference lineage file");
}

inline const reference::P18ExpectedRoute &
reference_route(const reference::P18ReferenceCatalogV1 &catalog,
                reference::P18ReferenceRoute route) {
    for (const auto &candidate : catalog.expected_routes) {
        if (candidate.route == route) {
            return candidate;
        }
    }
    throw std::logic_error("P1.8 catalog is missing a reference route");
}

inline const reference::P18ExpectedPresentationMethod &
reference_presentation_method(const reference::P18ExpectedPresentation &presentation,
                              reference::P18ReferencePresentationMethod method) {
    for (const auto &candidate : presentation.expected_methods) {
        if (candidate.method == method) {
            return candidate;
        }
    }
    throw std::logic_error("P1.8 catalog is missing a presentation method");
}

inline ReferencePayloadIdentity
reference_payload(const reference::P18ExpectedLineageFile &file) {
    return {file.expected_byte_count, file.expected_sha256};
}

inline MethodIdentity
reference_method(const reference::P18ExpectedSemanticMethod &expected,
                 std::uint8_t synthetic_configuration_digest_byte) {
    return {
        std::string(expected.expected_id),
        expected.expected_version,
        digest(synthetic_configuration_digest_byte),
    };
}

inline double reference_binary64(reference::P18ExpectedBinary64 expected) noexcept {
    return std::bit_cast<double>(expected.expected_ieee754_bits);
}

inline std::string
reference_fixture_path(const reference::P18ExpectedLineageFile &file) {
    return "reference/fixtures/bmw-m52b28-p18/" +
           std::string(file.expected_relative_path);
}

inline PresentationCalibration make_reference_presentation(InputBuilder &builder) {
    const auto &catalog = reference::p18_reference_catalog_v1();
    const auto &expected = catalog.expected_presentation;
    const auto &algorithm_record = reference_lineage_file(
        catalog, reference::P18ReferenceLineageFile::renderer_algorithm_record);
    const auto &configured_ir = reference_lineage_file(
        catalog, reference::P18ReferenceLineageFile::configured_ir_input);
    const auto &configured_ir_media = expected.expected_configured_ir_media;
    const auto &scalars = expected.expected_scalars;
    builder.provenance.evidence.push_back({
        std::string(expected.expected_algorithm_record_evidence_source_id),
        reference_fixture_path(algorithm_record),
        std::nullopt,
        algorithm_record.expected_sha256,
        RightsDisposition::permitted,
    });
    builder.provenance.evidence.push_back({
        std::string(configured_ir_media.expected_evidence_source_id),
        reference_fixture_path(configured_ir),
        std::nullopt,
        configured_ir.expected_sha256,
        RightsDisposition::local_evaluation_only,
    });

    const auto resolved_method = [&](reference::P18ReferencePresentationMethod method,
                                     std::uint8_t digest_byte, std::string path) {
        return builder.resolved(
            reference_method(
                reference_presentation_method(expected, method).expected_semantic,
                digest_byte),
            "presentation.methods." + std::move(path));
    };

    PresentationCalibration presentation;
    presentation.schema_version = expected.expected_schema_version;
    presentation.calibration_id = catalog.expected_presentation_calibration_id;
    presentation.engine_profile_id =
        builder.resolved(std::string(catalog.expected_engine_profile_id),
                         "presentation.engine_profile_id");
    presentation.methods = {
        resolved_method(reference::P18ReferencePresentationMethod::reconstruction, 80,
                        "reconstruction"),
        resolved_method(reference::P18ReferencePresentationMethod::conditioning, 81,
                        "conditioning"),
        resolved_method(
            reference::P18ReferencePresentationMethod::impulse_response_conversion, 82,
            "impulse_response_conversion"),
        resolved_method(reference::P18ReferencePresentationMethod::convolution, 83,
                        "convolution"),
        resolved_method(reference::P18ReferencePresentationMethod::publication, 84,
                        "publication"),
        resolved_method(reference::P18ReferencePresentationMethod::audition_mix, 85,
                        "audition_mix"),
    };
    presentation.algorithm_record = {
        builder.resolved(std::string(expected.expected_algorithm_record_semantic_id),
                         "presentation.algorithm_record.semantic_id"),
        builder.resolved(
            std::string(expected.expected_algorithm_record_evidence_source_id),
            "presentation.algorithm_record.evidence_source_id"),
        builder.resolved(algorithm_record.expected_sha256,
                         "presentation.algorithm_record.content_sha256"),
    };
    presentation.conditioning = {
        builder.resolved(reference_binary64(scalars.expected_jitter_scale),
                         "presentation.conditioning.jitter_scale"),
        builder.resolved(
            reference_binary64(scalars.expected_jitter_modulation_cutoff_hz),
            "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(reference_binary64(scalars.expected_derivative_mix_01),
                         "presentation.conditioning.derivative_mix_01"),
        builder.resolved(reference_binary64(scalars.expected_air_noise_mix_01),
                         "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(reference_binary64(scalars.expected_air_noise_cutoff_hz),
                         "presentation.conditioning.air_noise_cutoff_hz"),
    };
    presentation.assets.push_back({
        configured_ir_media.expected_asset_id,
        builder.resolved(std::string(configured_ir_media.expected_semantic_id),
                         "presentation.assets.smooth-39.semantic_id"),
        builder.resolved(std::string(configured_ir_media.expected_evidence_source_id),
                         "presentation.assets.smooth-39.evidence_source_id"),
        builder.resolved(configured_ir.expected_sha256,
                         "presentation.assets.smooth-39.content_sha256"),
        builder.resolved(
            AudioMediaContract{
                configured_ir_media.expected_encoding,
                configured_ir_media.expected_channel_layout,
                configured_ir_media.expected_sample_rate,
                configured_ir_media.expected_frame_count,
            },
            "presentation.assets.smooth-39.media"),
    });
    for (const auto &route : catalog.expected_routes) {
        const auto path =
            "presentation.routes." + std::string(route.expected_semantic_id);
        presentation.routes.push_back({
            route.expected_route_id,
            configured_ir_media.expected_asset_id,
            builder.resolved(
                reference_binary64(scalars.expected_impulse_response_gain_linear),
                path + ".impulse_response_gain_linear"),
            builder.resolved(reference_binary64(scalars.expected_wet_mix_01),
                             path + ".wet_mix_01"),
        });
    }
    presentation.publication.calibration_gain_linear = builder.resolved(
        reference_binary64(scalars.expected_publication_calibration_gain_linear),
        "presentation.publication.calibration_gain_linear");
    std::vector<RouteId> audition_routes;
    audition_routes.reserve(expected.expected_audition_route_order.size());
    for (const auto route : expected.expected_audition_route_order) {
        audition_routes.push_back(reference_route(catalog, route).expected_route_id);
    }
    presentation.audition = {
        builder.resolved(std::move(audition_routes),
                         "presentation.audition.selected_routes"),
        builder.resolved(
            reference_binary64(scalars.expected_audition_monitoring_gain_linear),
            "presentation.audition.monitoring_gain_linear"),
        builder.resolved(
            reference_binary64(scalars.expected_audition_fade_in_duration_s),
            "presentation.audition.fade_in_duration_s"),
        builder.resolved(
            reference_binary64(scalars.expected_audition_fade_out_duration_s),
            "presentation.audition.fade_out_duration_s"),
    };
    presentation.provenance_schema_id = builder.provenance.schema_id;
    return presentation;
}

inline ReferencePresentationInputsV1 make_reference_inputs(InputBuilder &builder) {
    const auto &catalog = reference::p18_reference_catalog_v1();
    const auto &capture = catalog.expected_capture;
    const auto lineage_payload = [&](reference::P18ReferenceLineageFile file) {
        return reference_payload(reference_lineage_file(catalog, file));
    };

    ReferencePresentationInputsV1 inputs;
    inputs.schema_version = catalog.expected_reference_inputs_schema_version;
    inputs.fixture = {
        std::string(catalog.expected_fixture_id),
        catalog.expected_fixture_schema_version,
        lineage_payload(reference::P18ReferenceLineageFile::manifest),
        lineage_payload(reference::P18ReferenceLineageFile::parity_evidence),
        lineage_payload(reference::P18ReferenceLineageFile::audit_input),
        lineage_payload(reference::P18ReferenceLineageFile::component_seed_input),
        lineage_payload(reference::P18ReferenceLineageFile::renderer_algorithm_record),
        lineage_payload(reference::P18ReferenceLineageFile::configured_ir_input),
        lineage_payload(reference::P18ReferenceLineageFile::kernel_oracle_comparator),
    };
    inputs.audit_reader = reference_method(catalog.expected_audit_reader, 86);
    inputs.excitation_adapter =
        reference_method(catalog.expected_excitation_adapter, 87);
    inputs.audit_lane_semantic_id = catalog.expected_audit_lane_semantic_id;
    inputs.excitation_seam = reference_method(catalog.expected_excitation_seam, 88);
    inputs.engine.engine_id = catalog.expected_engine_id;
    inputs.engine.engine_profile_id = catalog.expected_engine_profile_id;
    for (const auto &route : catalog.expected_routes) {
        inputs.engine.routes.push_back({
            route.expected_route_id,
            std::string(route.expected_semantic_id),
            route.expected_source_matrix_classification,
        });
    }
    inputs.capture = {
        capture.expected_rates,
        capture.expected_record_count,
        capture.expected_consumed_start_record,
        capture.expected_consumed_end_record_exclusive,
        capture.expected_audible_start_record,
        capture.expected_audible_end_record_exclusive,
        capture.expected_physics_frames_per_block,
        capture.expected_total_source_frame_count,
        capture.expected_audible_source_start_frame,
        capture.expected_audible_source_end_frame_exclusive,
        capture.expected_delivery_frame_count,
        capture.expected_public_seed,
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
        const auto &catalog = reference::p18_reference_catalog_v1();
        const auto &source_matrix = bmw_m52b28_reference_source_matrix_v1();
        const auto inputs = make_reference_inputs(builder);

        content.schema_version = 2;
        content.inputs = inputs;
        content.provenance = builder.provenance.bundle;
        content.determinism = {
            BuildIdentity{
                "89abcdef0123456789abcdef0123456789abcdef",
                digest(90),
                "GNU",
                "13.3.0",
                "x86_64-linux-gnu",
                "libstdcxx",
                test_standard_library_identity(),
                "glibc-libm",
                test_math_library_identity(),
                "libgcc-s",
                test_compiler_runtime_identity(),
            },
            "x86-64-v1-binary64-x87-extended-strict-v1",
            "x86-64-v1",
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
        content.randomness.generator =
            reference_method(catalog.expected_random_generator, 91);
        content.randomness.public_seed = catalog.expected_capture.expected_public_seed;
        content.randomness.derivation =
            reference_method(catalog.expected_seed_derivation, 92);
        for (const auto &seed : catalog.expected_executed_seeds) {
            const auto component = [&] {
                switch (seed.component) {
                case reference::P18ReferenceExecutedRandomComponent::air_noise:
                    return RandomComponentKind::presentation_air_noise;
                case reference::P18ReferenceExecutedRandomComponent::jitter:
                    return RandomComponentKind::presentation_jitter;
                }
                throw std::logic_error(
                    "P1.8 catalog contains an unknown random component");
            }();
            content.randomness.component_seeds.push_back({
                component,
                std::nullopt,
                reference_route(catalog, seed.route).expected_route_id,
                seed.expected_initial_state,
                seed.expected_stream,
            });
        }
        content.output_contract = resolve_output_contract(source_matrix);
        if (source_matrix.required_source_routes.size() !=
            catalog.expected_routes.size()) {
            throw std::logic_error(
                "P1.8 catalog route count disagrees with the source matrix");
        }
        for (std::size_t index = 0; index < catalog.expected_routes.size(); ++index) {
            const auto &expected = catalog.expected_routes[index];
            const auto &requirement = source_matrix.required_source_routes[index];
            if (expected.expected_semantic_id != requirement.semantic_id ||
                expected.expected_source_matrix_classification != requirement.kind) {
                throw std::logic_error(
                    "P1.8 catalog route disagrees with the source matrix");
            }
            content.routes.push_back({
                expected.expected_route_id,
                requirement.semantic_id,
                requirement.kind,
                requirement.disposition,
                requirement.disposition_reason,
                requirement.artifact_roles,
            });
        }
        for (const auto &bus : source_matrix.required_output_buses) {
            content.output_buses.push_back({
                bus.semantic_id,
                bus.kind,
                bus.artifact_roles,
            });
        }

        if (source_matrix.required_artifacts.size() != catalog.expected_audio.size()) {
            throw std::logic_error(
                "P1.8 catalog artifact count disagrees with the source matrix");
        }
        for (std::size_t index = 0; index < source_matrix.required_artifacts.size();
             ++index) {
            const auto &requirement = source_matrix.required_artifacts[index];
            const auto &expected = catalog.expected_audio[index];
            if (expected.expected_role != requirement.role ||
                expected.expected_diagnostic != requirement.diagnostic) {
                throw std::logic_error(
                    "P1.8 catalog artifact disagrees with the source matrix");
            }
            content.artifacts.push_back({
                requirement.role,
                requirement.kind,
                std::string(expected.expected_relative_path),
                requirement.audio,
                expected.expected_byte_count,
                expected.expected_sha256,
                requirement.diagnostic,
            });
        }
    }
};

} // namespace engine_sim_offline::contract::test
