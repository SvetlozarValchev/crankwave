#include "reference/p18_reference_manifest_content.hpp"

#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_method_identity.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {
namespace {

using contract::ReferencePayloadIdentity;

[[nodiscard]] ReferencePayloadIdentity
observed_payload(const P18VerifiedReferenceLineage &lineage,
                 P18ReferenceLineageFile file) {
    const auto &observed = lineage.at(file);
    if (observed.file != file || observed.byte_count == 0 ||
        observed.payload_sha256.is_zero()) {
        throw std::logic_error{
            "verified P1.8 lineage returned an incomplete observed payload"};
    }
    return {observed.byte_count, observed.payload_sha256};
}

template <class Value>
[[nodiscard]] contract::ResolvedValue<Value>
resolved(Value value, const P18ReferenceProvenance &provenance,
         std::string_view parameter_path) {
    return {
        std::move(value),
        std::string{provenance.resolution_id(parameter_path)},
    };
}

[[nodiscard]] double binary64(P18ExpectedBinary64 expected) noexcept {
    return std::bit_cast<double>(expected.expected_ieee754_bits);
}

[[nodiscard]] const P18ExpectedRoute &
expected_route(const P18ReferenceCatalogV1 &catalog, P18ReferenceRoute route) {
    const auto index = static_cast<std::size_t>(route);
    if (index >= catalog.expected_routes.size() ||
        catalog.expected_routes[index].route != route) {
        throw std::logic_error{"P1.8 route catalog order changed"};
    }
    return catalog.expected_routes[index];
}

[[nodiscard]] contract::MethodIdentity method(P18ReferenceMethod selected) {
    return p18_reference_method_identity(selected).contract_identity();
}

[[nodiscard]] std::uint64_t
checked_rate_projection(std::uint64_t input_frames,
                        const contract::RationalRateHz &input_rate,
                        const contract::RationalRateHz &output_rate) {
    if (input_rate.numerator == 0 || input_rate.denominator == 0 ||
        output_rate.numerator == 0 || output_rate.denominator == 0) {
        throw std::logic_error{"P1.8 frame projection received an invalid rate"};
    }

    // Cancel the rational factors before multiplication. The reference clocks all
    // reduce exactly, but retaining checked arithmetic prevents a future policy edit
    // from silently truncating or overflowing an observed frame count.
    std::array<std::uint64_t, 3> numerator{
        input_frames,
        input_rate.denominator,
        output_rate.numerator,
    };
    std::array<std::uint64_t, 2> denominator{
        input_rate.numerator,
        output_rate.denominator,
    };
    for (auto &lower : denominator) {
        for (auto &upper : numerator) {
            const auto divisor = std::gcd(upper, lower);
            upper /= divisor;
            lower /= divisor;
        }
        if (lower != 1) {
            throw std::logic_error{
                "P1.8 frame projection is not an integral frame count"};
        }
    }

    std::uint64_t result = 1;
    for (const auto factor : numerator) {
        if (factor != 0 &&
            result > std::numeric_limits<std::uint64_t>::max() / factor) {
            throw std::overflow_error{"P1.8 frame projection overflowed u64"};
        }
        result *= factor;
    }
    return result;
}

[[nodiscard]] contract::ReferenceCaptureWindow
make_capture(const P18LoadedReferenceFixture &fixture,
             const P18ReferenceCatalogV1 &catalog) {
    const auto &policy = catalog.expected_capture;
    const auto record_count = static_cast<std::uint64_t>(fixture.audit.frames.size());
    if (static_cast<std::size_t>(record_count) != fixture.audit.frames.size() ||
        record_count != policy.expected_record_count ||
        policy.expected_consumed_start_record >
            policy.expected_consumed_end_record_exclusive ||
        policy.expected_consumed_end_record_exclusive > record_count ||
        policy.expected_audible_start_record >
            policy.expected_audible_end_record_exclusive ||
        policy.expected_audible_end_record_exclusive > record_count) {
        throw std::logic_error{"decoded P1.8 audit has an invalid observed extent"};
    }

    const auto total_source_frames = checked_rate_projection(
        policy.expected_record_count, policy.expected_rates.capture,
        policy.expected_rates.source_processing);
    const auto audible_source_start = checked_rate_projection(
        policy.expected_audible_start_record, policy.expected_rates.capture,
        policy.expected_rates.source_processing);
    const auto audible_source_end = checked_rate_projection(
        policy.expected_audible_end_record_exclusive, policy.expected_rates.capture,
        policy.expected_rates.source_processing);
    const auto delivery_frames = checked_rate_projection(
        policy.expected_audible_end_record_exclusive -
            policy.expected_audible_start_record,
        policy.expected_rates.capture, policy.expected_rates.delivery);
    if (total_source_frames != policy.expected_total_source_frame_count ||
        audible_source_start != policy.expected_audible_source_start_frame ||
        audible_source_end != policy.expected_audible_source_end_frame_exclusive ||
        delivery_frames != policy.expected_delivery_frame_count) {
        throw std::logic_error{
            "P1.8 authored capture windows disagree with their integer clocks"};
    }

    return {
        policy.expected_rates,
        record_count,
        policy.expected_consumed_start_record,
        policy.expected_consumed_end_record_exclusive,
        policy.expected_audible_start_record,
        policy.expected_audible_end_record_exclusive,
        policy.expected_physics_frames_per_block,
        policy.expected_total_source_frame_count,
        policy.expected_audible_source_start_frame,
        policy.expected_audible_source_end_frame_exclusive,
        policy.expected_delivery_frame_count,
        policy.expected_public_seed,
    };
}

[[nodiscard]] contract::PresentationCalibration
make_presentation(const P18VerifiedReferenceLineage &lineage,
                  const P18ReferenceProvenance &provenance,
                  const P18ReferenceCatalogV1 &catalog) {
    const auto &policy = catalog.expected_presentation;
    const auto &ir_policy = policy.expected_configured_ir_media;
    const auto &scalars = policy.expected_scalars;
    const auto algorithm_record =
        observed_payload(lineage, P18ReferenceLineageFile::renderer_algorithm_record);
    const auto configured_ir =
        observed_payload(lineage, P18ReferenceLineageFile::configured_ir_input);

    contract::PresentationCalibration presentation;
    presentation.schema_version = policy.expected_schema_version;
    presentation.calibration_id =
        std::string{catalog.expected_presentation_calibration_id};
    presentation.engine_profile_id =
        resolved(std::string{catalog.expected_engine_profile_id}, provenance,
                 "presentation.engine_profile_id");
    presentation.methods = {
        resolved(method(P18ReferenceMethod::reconstruction), provenance,
                 "presentation.methods.reconstruction"),
        resolved(method(P18ReferenceMethod::conditioning), provenance,
                 "presentation.methods.conditioning"),
        resolved(method(P18ReferenceMethod::impulse_response_conversion), provenance,
                 "presentation.methods.impulse_response_conversion"),
        resolved(method(P18ReferenceMethod::convolution), provenance,
                 "presentation.methods.convolution"),
        resolved(method(P18ReferenceMethod::publication), provenance,
                 "presentation.methods.publication"),
        resolved(method(P18ReferenceMethod::audition_mix), provenance,
                 "presentation.methods.audition_mix"),
    };
    presentation.algorithm_record = {
        resolved(std::string{policy.expected_algorithm_record_semantic_id}, provenance,
                 "presentation.algorithm_record.semantic_id"),
        resolved(std::string{policy.expected_algorithm_record_evidence_source_id},
                 provenance, "presentation.algorithm_record.evidence_source_id"),
        resolved(algorithm_record.payload_sha256, provenance,
                 "presentation.algorithm_record.content_sha256"),
    };
    presentation.conditioning = {
        resolved(binary64(scalars.expected_jitter_scale), provenance,
                 "presentation.conditioning.jitter_scale"),
        resolved(binary64(scalars.expected_jitter_modulation_cutoff_hz), provenance,
                 "presentation.conditioning.jitter_modulation_cutoff_hz"),
        resolved(binary64(scalars.expected_derivative_mix_01), provenance,
                 "presentation.conditioning.derivative_mix_01"),
        resolved(binary64(scalars.expected_air_noise_mix_01), provenance,
                 "presentation.conditioning.air_noise_mix_01"),
        resolved(binary64(scalars.expected_air_noise_cutoff_hz), provenance,
                 "presentation.conditioning.air_noise_cutoff_hz"),
    };
    presentation.assets.push_back({
        ir_policy.expected_asset_id,
        resolved(std::string{ir_policy.expected_semantic_id}, provenance,
                 "presentation.assets.smooth-39.semantic_id"),
        resolved(std::string{ir_policy.expected_evidence_source_id}, provenance,
                 "presentation.assets.smooth-39.evidence_source_id"),
        resolved(configured_ir.payload_sha256, provenance,
                 "presentation.assets.smooth-39.content_sha256"),
        resolved(
            contract::AudioMediaContract{
                ir_policy.expected_encoding,
                ir_policy.expected_channel_layout,
                ir_policy.expected_sample_rate,
                ir_policy.expected_frame_count,
            },
            provenance, "presentation.assets.smooth-39.media"),
    });

    for (const auto &route : catalog.expected_routes) {
        const std::string parameter_prefix =
            "presentation.routes." + std::string{route.expected_semantic_id};
        presentation.routes.push_back({
            route.expected_route_id,
            ir_policy.expected_asset_id,
            resolved(binary64(scalars.expected_impulse_response_gain_linear),
                     provenance, parameter_prefix + ".impulse_response_gain_linear"),
            resolved(binary64(scalars.expected_wet_mix_01), provenance,
                     parameter_prefix + ".wet_mix_01"),
        });
    }
    presentation.publication.calibration_gain_linear =
        resolved(binary64(scalars.expected_publication_calibration_gain_linear),
                 provenance, "presentation.publication.calibration_gain_linear");

    std::vector<contract::RouteId> audition_routes;
    audition_routes.reserve(policy.expected_audition_route_order.size());
    for (const auto route : policy.expected_audition_route_order) {
        audition_routes.push_back(expected_route(catalog, route).expected_route_id);
    }
    presentation.audition = {
        resolved(std::move(audition_routes), provenance,
                 "presentation.audition.selected_routes"),
        resolved(binary64(scalars.expected_audition_monitoring_gain_linear), provenance,
                 "presentation.audition.monitoring_gain_linear"),
        resolved(binary64(scalars.expected_audition_fade_in_duration_s), provenance,
                 "presentation.audition.fade_in_duration_s"),
        resolved(binary64(scalars.expected_audition_fade_out_duration_s), provenance,
                 "presentation.audition.fade_out_duration_s"),
    };
    presentation.provenance_schema_id = provenance.ledger().schema_id;
    return presentation;
}

[[nodiscard]] contract::ReferencePresentationInputsV1
make_inputs(const P18LoadedReferenceFixture &fixture,
            const P18ReferenceProvenance &provenance,
            const P18ReferenceCatalogV1 &catalog) {
    const auto &lineage = fixture.verified_lineage;
    contract::ReferencePresentationInputsV1 inputs;
    inputs.schema_version = catalog.expected_reference_inputs_schema_version;
    inputs.fixture = {
        std::string{catalog.expected_fixture_id},
        catalog.expected_fixture_schema_version,
        observed_payload(lineage, P18ReferenceLineageFile::manifest),
        observed_payload(lineage, P18ReferenceLineageFile::parity_evidence),
        observed_payload(lineage, P18ReferenceLineageFile::audit_input),
        observed_payload(lineage, P18ReferenceLineageFile::component_seed_input),
        observed_payload(lineage, P18ReferenceLineageFile::renderer_algorithm_record),
        observed_payload(lineage, P18ReferenceLineageFile::configured_ir_input),
        observed_payload(lineage, P18ReferenceLineageFile::kernel_oracle_comparator),
    };
    inputs.audit_reader = method(P18ReferenceMethod::audit_reader);
    inputs.excitation_adapter = method(P18ReferenceMethod::excitation_adapter);
    inputs.audit_lane_semantic_id =
        std::string{catalog.expected_audit_lane_semantic_id};
    inputs.excitation_seam = method(P18ReferenceMethod::excitation_seam);
    inputs.engine.engine_id = std::string{catalog.expected_engine_id};
    inputs.engine.engine_profile_id = std::string{catalog.expected_engine_profile_id};
    for (const auto &route : catalog.expected_routes) {
        inputs.engine.routes.push_back({
            route.expected_route_id,
            std::string{route.expected_semantic_id},
            route.expected_source_matrix_classification,
        });
    }
    inputs.capture = make_capture(fixture, catalog);
    inputs.presentation = make_presentation(lineage, provenance, catalog);
    return inputs;
}

[[nodiscard]] contract::RandomPlan
make_random_plan(const P18LoadedReferenceFixture &fixture,
                 const contract::ReferenceCaptureWindow &capture,
                 const P18ReferenceCatalogV1 &catalog) {
    const auto route_seeds = fixture.component_seeds.route_seeds();
    if (route_seeds.size() != catalog.expected_routes.size()) {
        throw std::logic_error{"P1.8 decoded route-seed shape changed"};
    }

    contract::RandomPlan result;
    result.generator = method(P18ReferenceMethod::random_generator);
    result.public_seed = capture.public_seed;
    result.derivation = method(P18ReferenceMethod::seed_derivation);
    result.component_seeds.reserve(route_seeds.size() * 2);

    // The decoded fixture owns values. Manifest order is the contract's canonical
    // air route 0/1 followed by jitter route 0/1 ordering.
    for (std::size_t index = 0; index < route_seeds.size(); ++index) {
        const auto &route = catalog.expected_routes[index];
        const auto &seed = route_seeds[index].air_noise;
        result.component_seeds.push_back({
            contract::RandomComponentKind::presentation_air_noise,
            std::nullopt,
            route.expected_route_id,
            seed.initial_state,
            seed.stream,
        });
    }
    for (std::size_t index = 0; index < route_seeds.size(); ++index) {
        const auto &route = catalog.expected_routes[index];
        const auto &seed = route_seeds[index].jitter;
        result.component_seeds.push_back({
            contract::RandomComponentKind::presentation_jitter,
            std::nullopt,
            route.expected_route_id,
            seed.initial_state,
            seed.stream,
        });
    }
    return result;
}

void append_routes_and_buses(contract::RenderManifestContent &content,
                             const contract::SourceMatrixContract &source_matrix,
                             const P18ReferenceCatalogV1 &catalog) {
    if (source_matrix.required_source_routes.size() != catalog.expected_routes.size()) {
        throw std::logic_error{"P1.8 source-matrix route shape changed"};
    }
    content.routes.reserve(source_matrix.required_source_routes.size());
    for (std::size_t index = 0; index < source_matrix.required_source_routes.size();
         ++index) {
        const auto &required = source_matrix.required_source_routes[index];
        const auto &route = catalog.expected_routes[index];
        if (required.semantic_id != route.expected_semantic_id ||
            required.kind != route.expected_source_matrix_classification) {
            throw std::logic_error{
                "P1.8 source-matrix route differs from authored context"};
        }
        content.routes.push_back({
            route.expected_route_id,
            required.semantic_id,
            required.kind,
            required.disposition,
            required.disposition_reason,
            required.artifact_roles,
        });
    }

    content.output_buses.reserve(source_matrix.required_output_buses.size());
    for (const auto &required : source_matrix.required_output_buses) {
        content.output_buses.push_back(
            {required.semantic_id, required.kind, required.artifact_roles});
    }
}

void append_observed_artifacts(contract::RenderManifestContent &content,
                               const P18SealedPresentationEvidence &evidence,
                               const contract::SourceMatrixContract &source_matrix,
                               const P18ReferenceCatalogV1 &catalog) {
    const auto &artifacts = evidence.artifacts();
    if (artifacts.size() != kP18ReferenceAudioArtifactCount ||
        source_matrix.required_artifacts.size() != kP18ReferenceAudioArtifactCount ||
        catalog.expected_audio.size() != kP18ReferenceAudioArtifactCount) {
        throw std::logic_error{
            "P1.8 manifest requires exactly eight sealed presentation artifacts"};
    }

    content.artifacts.reserve(kP18ReferenceAudioArtifactCount);
    for (std::size_t index = 0; index < kP18ReferenceAudioArtifactCount; ++index) {
        const auto artifact = static_cast<P18ReferenceAudioArtifact>(index);
        const auto &observed = artifacts[index];
        if (observed.byte_count == 0 || observed.payload_sha256.is_zero()) {
            throw std::logic_error{
                "P1.8 manifest requires all eight actual sealed artifact records"};
        }
        const auto &required = source_matrix.required_artifacts[index];
        const auto &expected = catalog.expected_audio[index];
        if (expected.audio != artifact || expected.expected_role != required.role ||
            expected.expected_diagnostic != required.diagnostic) {
            throw std::logic_error{
                "P1.8 frozen artifact comparator differs from the source matrix"};
        }
        if (observed.role != required.role || observed.kind != required.kind ||
            observed.audio != required.audio ||
            observed.diagnostic != required.diagnostic) {
            throw std::logic_error{
                "P1.8 sealed artifact differs from the source-matrix requirement"};
        }
        if (observed.role != expected.expected_role ||
            observed.relative_path != expected.expected_relative_path ||
            observed.diagnostic != expected.expected_diagnostic ||
            observed.byte_count != expected.expected_byte_count ||
            observed.payload_sha256 != expected.expected_sha256) {
            throw std::logic_error{
                "P1.8 sealed artifact differs from the frozen reference comparator"};
        }
        content.artifacts.push_back(observed);
    }
}

[[nodiscard]] std::logic_error
validation_error(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return std::logic_error{
            "constructed P1.8 reference manifest content failed validation"};
    }
    const auto &first = report.issues.front();
    return std::logic_error{
        "constructed P1.8 reference manifest content is invalid at " + first.path +
        ": " + first.message};
}

} // namespace

const contract::RenderManifestContent &
P18ReferenceManifestContent::content() const noexcept {
    return content_;
}

const P18ReferenceProvenance &P18ReferenceManifestContent::provenance() const noexcept {
    return provenance_;
}

P18ReferenceManifestContent make_p18_reference_manifest_content(
    const P18LoadedReferenceFixture &fixture,
    const P18SealedPresentationEvidence &evidence,
    const determinism::RendererDeterminismEnvelope &renderer_identity) {
    if (!renderer_identity.production_observation()) {
        throw std::logic_error{
            "P1.8 manifest requires the live zero-argument renderer observation"};
    }
    const auto &catalog = p18_reference_catalog_v1();
    auto provenance = make_p18_reference_provenance(fixture.verified_lineage);
    auto inputs = make_inputs(fixture, provenance, catalog);
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();

    contract::RenderManifestContent content;
    content.schema_version = 2;
    content.inputs = inputs;
    content.provenance = provenance.ledger().bundle;
    content.determinism = renderer_identity.manifest_identity();
    content.rates = inputs.capture.rates;
    content.randomness = make_random_plan(fixture, inputs.capture, catalog);
    content.output_contract = contract::resolve_output_contract(source_matrix);
    append_routes_and_buses(content, source_matrix, catalog);
    append_observed_artifacts(content, evidence, source_matrix, catalog);

    const auto report = contract::validate(content, provenance.ledger(), source_matrix);
    if (!report.ok()) {
        throw validation_error(report);
    }
    return P18ReferenceManifestContent{std::move(provenance), std::move(content)};
}

} // namespace engine_sim_offline::reference
