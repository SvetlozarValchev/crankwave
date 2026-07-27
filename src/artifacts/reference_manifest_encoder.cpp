#include "engine_sim_offline/artifacts/reference_manifest_encoder.hpp"

#include "reference_manifest_encoder_impl.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace engine_sim_offline::artifacts {
namespace detail {

namespace {

[[nodiscard]] bool write_build_identity(CanonicalJsonWriter &writer,
                                        const contract::BuildIdentity &build) {
    return writer.begin_object() && writer.key("project_revision") &&
           writer.string_value(build.project_revision) &&
           writer.key("source_tree_sha256") &&
           writer.sha256_value(build.source_tree_sha256) && writer.key("compiler_id") &&
           writer.string_value(build.compiler_id) && writer.key("compiler_version") &&
           writer.string_value(build.compiler_version) && writer.key("target_triple") &&
           writer.string_value(build.target_triple) &&
           writer.key("standard_library_id") &&
           writer.string_value(build.standard_library_id) &&
           writer.key("standard_library_version") &&
           writer.string_value(build.standard_library_version) &&
           writer.key("math_library_id") &&
           writer.string_value(build.math_library_id) &&
           writer.key("math_library_version") &&
           writer.string_value(build.math_library_version) && writer.end_object();
}

[[nodiscard]] bool
write_floating_point(CanonicalJsonWriter &writer,
                     const contract::FloatingPointIdentity &floating_point) {
    return writer.begin_object() && writer.key("format") &&
           writer.string_value(floating_point.format) && writer.key("rounding") &&
           writer.string_value(floating_point.rounding) &&
           writer.key("fma_contraction") &&
           writer.bool_value(floating_point.fma_contraction) &&
           writer.key("flush_to_zero") &&
           writer.bool_value(floating_point.flush_to_zero) &&
           writer.key("denormals_are_zero") &&
           writer.bool_value(floating_point.denormals_are_zero) && writer.end_object();
}

[[nodiscard]] bool write_determinism(CanonicalJsonWriter &writer,
                                     const contract::DeterminismEnvelope &determinism) {
    return writer.begin_object() && writer.key("build") &&
           write_build_identity(writer, determinism.build) &&
           writer.key("instruction_set_profile") &&
           writer.string_value(determinism.instruction_set_profile) &&
           writer.key("floating_point") &&
           write_floating_point(writer, determinism.floating_point) &&
           writer.key("deterministic_worker_count") &&
           writer.uint32_value(determinism.deterministic_worker_count) &&
           writer.key("deterministic_reduction_topology") &&
           writer.string_value(determinism.deterministic_reduction_topology) &&
           writer.end_object();
}

[[nodiscard]] std::string_view
random_component_kind(contract::RandomComponentKind kind) noexcept {
    switch (kind) {
    case contract::RandomComponentKind::combustion:
        return "combustion";
    case contract::RandomComponentKind::presentation_jitter:
        return "presentation_jitter";
    case contract::RandomComponentKind::presentation_air_noise:
        return "presentation_air_noise";
    case contract::RandomComponentKind::starter:
        return "starter";
    case contract::RandomComponentKind::unspecified:
        break;
    }
    return {};
}

template <class Id>
[[nodiscard]] bool write_optional_id(CanonicalJsonWriter &writer,
                                     const std::optional<Id> &id) {
    return id.has_value() ? writer.uint32_value(id->value) : writer.null_value();
}

[[nodiscard]] bool write_component_seed(CanonicalJsonWriter &writer,
                                        const contract::ComponentSeed &seed) {
    const auto kind = random_component_kind(seed.kind);
    if (kind.empty()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "random component kind is not encodable");
    }
    return writer.begin_object() && writer.key("kind") && writer.string_value(kind) &&
           writer.key("cylinder_id") && write_optional_id(writer, seed.cylinder_id) &&
           writer.key("route_id") && write_optional_id(writer, seed.route_id) &&
           writer.key("initial_state") && writer.uint64_hex_value(seed.initial_state) &&
           writer.key("stream") && writer.uint64_hex_value(seed.stream) &&
           writer.end_object();
}

[[nodiscard]] bool write_random_plan(CanonicalJsonWriter &writer,
                                     const contract::RandomPlan &randomness) {
    if (!(writer.begin_object() && writer.key("generator") &&
          write_method_identity(writer, randomness.generator) &&
          writer.key("public_seed") &&
          writer.uint64_hex_value(randomness.public_seed) && writer.key("derivation") &&
          write_method_identity(writer, randomness.derivation) &&
          writer.key("component_seeds") && writer.begin_array())) {
        return false;
    }
    for (const auto &seed : randomness.component_seeds) {
        if (!write_component_seed(writer, seed)) {
            return false;
        }
    }
    return writer.end_array() && writer.end_object();
}

[[nodiscard]] bool write_provenance(CanonicalJsonWriter &writer,
                                    const contract::ProvenanceBundleRef &provenance) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(provenance.id) && writer.key("sha256") &&
           writer.sha256_value(provenance.sha256) && writer.end_object();
}

[[nodiscard]] bool write_execution(CanonicalJsonWriter &writer,
                                   const contract::ExecutionFacts &execution) {
    if (execution.wall_elapsed.count() > std::numeric_limits<std::int64_t>::max() ||
        execution.wall_elapsed.count() < std::numeric_limits<std::int64_t>::min()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "execution wall duration does not fit canonical int64");
    }
    return writer.begin_object() && writer.key("run_id") &&
           writer.string_value(execution.run_id) && writer.key("started_utc") &&
           writer.string_value(execution.started_utc) &&
           writer.key("wall_elapsed_ns") &&
           writer.int64_string_value(
               static_cast<std::int64_t>(execution.wall_elapsed.count())) &&
           writer.key("host_os") && writer.string_value(execution.host_os) &&
           writer.key("cpu_model") && writer.string_value(execution.cpu_model) &&
           writer.key("logical_cpu_count") &&
           writer.uint32_value(execution.logical_cpu_count) &&
           writer.key("observed_process_threads") &&
           writer.uint32_value(execution.observed_process_threads) &&
           writer.key("concurrent_render_jobs") &&
           writer.uint32_value(execution.concurrent_render_jobs) &&
           writer.key("peak_resident_bytes") &&
           (execution.peak_resident_bytes.has_value()
                ? writer.uint64_hex_value(*execution.peak_resident_bytes)
                : writer.null_value()) &&
           writer.end_object();
}

[[nodiscard]] bool write_content(CanonicalJsonWriter &writer,
                                 const contract::RenderManifestContent &content) {
    if (content.schema_version != 1U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "reference manifest content schema version is not v1");
    }
    const auto *reference =
        std::get_if<contract::ReferencePresentationInputsV1>(&content.inputs);
    if (reference == nullptr) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "simulation_v1 manifest encoding is not frozen");
    }
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(content.schema_version) && writer.key("inputs") &&
           writer.begin_object() && writer.key("kind") &&
           writer.string_value("reference_presentation_v1") && writer.key("value") &&
           write_reference_inputs(writer, *reference) && writer.end_object() &&
           writer.key("provenance") && write_provenance(writer, content.provenance) &&
           writer.key("determinism") &&
           write_determinism(writer, content.determinism) && writer.key("rates") &&
           write_render_rates(writer, content.rates) && writer.key("randomness") &&
           write_random_plan(writer, content.randomness) &&
           writer.key("output_contract") &&
           write_output_contract(writer, content.output_contract) &&
           writer.key("routes") && write_route_records(writer, content.routes) &&
           writer.key("output_buses") &&
           write_output_bus_records(writer, content.output_buses) &&
           writer.key("artifacts") &&
           write_artifact_records(writer, content.artifacts) && writer.end_object();
}

[[nodiscard]] RenderSinkError writer_error(const CanonicalJsonWriter &writer) {
    std::string detail_code = "reference-manifest-wire-unrepresentable";
    if (writer.error() == CanonicalJsonWriter::Error::size_limit) {
        detail_code = "reference-manifest-wire-size-exceeded";
    } else if (writer.error() == CanonicalJsonWriter::Error::invalid_utf8) {
        detail_code = "reference-manifest-wire-invalid-utf8";
    } else if (writer.error() == CanonicalJsonWriter::Error::non_finite_binary64) {
        detail_code = "reference-manifest-wire-nonfinite";
    }
    return {RenderSinkErrorKind::protocol_violation, std::move(detail_code),
            std::string(writer.error_message())};
}

} // namespace

bool write_rational_rate(CanonicalJsonWriter &writer,
                         const contract::RationalRateHz &rate) {
    return writer.begin_object() && writer.key("numerator") &&
           writer.uint64_hex_value(rate.numerator) && writer.key("denominator") &&
           writer.uint64_hex_value(rate.denominator) && writer.end_object();
}

bool write_render_rates(CanonicalJsonWriter &writer,
                        const contract::RenderRates &rates) {
    return writer.begin_object() && writer.key("physics") &&
           write_rational_rate(writer, rates.physics) && writer.key("capture") &&
           write_rational_rate(writer, rates.capture) &&
           writer.key("source_processing") &&
           write_rational_rate(writer, rates.source_processing) &&
           writer.key("acoustic") && write_rational_rate(writer, rates.acoustic) &&
           writer.key("delivery") && write_rational_rate(writer, rates.delivery) &&
           writer.end_object();
}

bool write_method_identity(CanonicalJsonWriter &writer,
                           const contract::MethodIdentity &method) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(method.id) && writer.key("version") &&
           writer.uint32_value(method.version) && writer.key("configuration_sha256") &&
           writer.sha256_value(method.configuration_sha256) && writer.end_object();
}

bool write_audio_contract(CanonicalJsonWriter &writer,
                          const contract::AudioContract &audio) {
    return writer.begin_object() && writer.key("sample_rate") &&
           write_rational_rate(writer, audio.sample_rate) &&
           writer.key("frame_count") && writer.uint64_hex_value(audio.frame_count) &&
           writer.key("channel_layout_id") &&
           writer.string_value(audio.channel_layout_id) &&
           writer.key("sample_encoding_id") &&
           writer.string_value(audio.sample_encoding_id) && writer.end_object();
}

} // namespace detail

ManifestEncodingResult
encode_reference_manifest_v1(const contract::RenderManifest &manifest) {
    if (!std::holds_alternative<contract::ReferencePresentationInputsV1>(
            manifest.content.inputs)) {
        return RenderSinkError{
            RenderSinkErrorKind::protocol_violation,
            "reference-manifest-input-kind-unsupported",
            "simulation manifest encoding remains unavailable until simulation_v1 "
            "is frozen in M3"};
    }
    if (!manifest.execution.has_value()) {
        return RenderSinkError{RenderSinkErrorKind::protocol_violation,
                               "reference-manifest-execution-missing",
                               "a completed reference manifest requires execution "
                               "facts"};
    }

    try {
        detail::CanonicalJsonWriter writer;
        std::vector<std::byte> bytes;
        const bool encoded = writer.begin_object() && writer.key("wire_schema") &&
                             writer.string_value(kReferenceManifestWireSchemaV1) &&
                             writer.key("content") &&
                             detail::write_content(writer, manifest.content) &&
                             writer.key("execution") &&
                             detail::write_execution(writer, *manifest.execution) &&
                             writer.end_object() && writer.finish(bytes);
        if (!encoded) {
            return detail::writer_error(writer);
        }
        return ManifestEncoding{std::move(bytes)};
    } catch (const std::bad_alloc &) {
        return RenderSinkError{RenderSinkErrorKind::publication_failure,
                               "reference-manifest-encoding-allocation-failed",
                               "canonical reference manifest encoding ran out of "
                               "memory"};
    } catch (const std::exception &error) {
        return RenderSinkError{RenderSinkErrorKind::publication_failure,
                               "reference-manifest-encoding-threw",
                               std::string("canonical reference manifest encoding "
                                           "threw: ") +
                                   error.what()};
    } catch (...) {
        return RenderSinkError{RenderSinkErrorKind::publication_failure,
                               "reference-manifest-encoding-threw",
                               "canonical reference manifest encoding threw a "
                               "non-standard exception"};
    }
}

} // namespace engine_sim_offline::artifacts
