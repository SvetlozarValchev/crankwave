#include "manifest_encoder_impl.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace engine_sim_offline::artifacts {
namespace detail {

namespace {

[[nodiscard]] bool write_build_identity(CanonicalJsonWriter &writer,
                                        const contract::BuildIdentity &build) {
    if (build.standard_library_id != "libstdcxx" ||
        build.math_library_id != "glibc-libm" ||
        build.compiler_runtime_id != "libgcc-s") {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "manifest runtime providers are outside the canonical "
                           "profile");
    }
    return writer.begin_object() && writer.key("git_commit_id") &&
           writer.string_value(build.git_commit_id) &&
           writer.key("source_closure_sha256") &&
           writer.sha256_value(build.source_closure_sha256) &&
           writer.key("compiler_id") && writer.string_value(build.compiler_id) &&
           writer.key("compiler_version") &&
           writer.string_value(build.compiler_version) && writer.key("target_triple") &&
           writer.string_value(build.target_triple) &&
           writer.key("standard_library_id") &&
           writer.string_value(build.standard_library_id) &&
           writer.key("standard_library_identity") &&
           writer.string_value(build.standard_library_identity) &&
           writer.key("math_library_id") &&
           writer.string_value(build.math_library_id) &&
           writer.key("math_library_identity") &&
           writer.string_value(build.math_library_identity) &&
           writer.key("compiler_runtime_id") &&
           writer.string_value(build.compiler_runtime_id) &&
           writer.key("compiler_runtime_identity") &&
           writer.string_value(build.compiler_runtime_identity) && writer.end_object();
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
    const auto &floating_point = determinism.floating_point;
    if (determinism.numeric_policy_id != "x86-64-v1-binary64-x87-extended-strict-v1" ||
        determinism.instruction_set_profile != "x86-64-v1" ||
        floating_point.format != "ieee754_binary64" ||
        floating_point.rounding != "nearest_ties_to_even" ||
        floating_point.fma_contraction || floating_point.flush_to_zero ||
        floating_point.denormals_are_zero ||
        determinism.deterministic_worker_count != 1 ||
        determinism.deterministic_reduction_topology != "serial-stable-order") {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "manifest determinism policy is outside the canonical "
                           "profile");
    }
    return writer.begin_object() && writer.key("build") &&
           write_build_identity(writer, determinism.build) &&
           writer.key("numeric_policy_id") &&
           writer.string_value(determinism.numeric_policy_id) &&
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

} // namespace

bool write_completed_manifest_content(CanonicalJsonWriter &writer,
                                      const contract::RenderManifestContent &content,
                                      ManifestInputsWriter write_inputs) {
    if (content.schema_version != 10U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "manifest content schema version is not v10");
    }
    if (write_inputs == nullptr) {
        return writer.fail(CanonicalJsonWriter::Error::invalid_state,
                           "manifest inputs writer is missing");
    }
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(content.schema_version) && writer.key("inputs") &&
           write_inputs(writer, content.inputs) && writer.key("provenance") &&
           write_provenance_bundle_ref(writer, content.provenance) &&
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

bool write_execution_facts(CanonicalJsonWriter &writer,
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

RenderSinkError manifest_writer_error(const CanonicalJsonWriter &writer,
                                      std::string_view detail_code_domain) {
    std::string detail_code = std::string{detail_code_domain} + "-wire-unrepresentable";
    if (writer.error() == CanonicalJsonWriter::Error::size_limit) {
        detail_code = std::string{detail_code_domain} + "-wire-size-exceeded";
    } else if (writer.error() == CanonicalJsonWriter::Error::invalid_utf8) {
        detail_code = std::string{detail_code_domain} + "-wire-invalid-utf8";
    } else if (writer.error() == CanonicalJsonWriter::Error::non_finite_binary64) {
        detail_code = std::string{detail_code_domain} + "-wire-nonfinite";
    }
    return {RenderSinkErrorKind::protocol_violation, std::move(detail_code),
            std::string(writer.error_message())};
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

} // namespace engine_sim_offline::artifacts
