#include "manifest_encoder_impl.hpp"

namespace engine_sim_offline::artifacts::detail {
namespace {

bool write_source_route_kind(CanonicalJsonWriter &writer,
                             contract::SourceRouteKind kind) {
    switch (kind) {
    case contract::SourceRouteKind::exhaust_outlet:
        return writer.string_value("exhaust_outlet");
    case contract::SourceRouteKind::intake_inlet:
        return writer.string_value("intake_inlet");
    case contract::SourceRouteKind::mechanical_engine:
        return writer.string_value("mechanical_engine");
    case contract::SourceRouteKind::mechanical_starter:
        return writer.string_value("mechanical_starter");
    case contract::SourceRouteKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "source route kind is unspecified or unknown");
}

bool write_distribution_intent(CanonicalJsonWriter &writer,
                               contract::DistributionIntent intent) {
    switch (intent) {
    case contract::DistributionIntent::local_evaluation:
        return writer.string_value("local_evaluation");
    case contract::DistributionIntent::distributable:
        return writer.string_value("distributable");
    case contract::DistributionIntent::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "distribution intent is unspecified or unknown");
}

bool write_output_bus_kind(CanonicalJsonWriter &writer, contract::OutputBusKind kind) {
    switch (kind) {
    case contract::OutputBusKind::master_engine_raw:
        return writer.string_value("master_engine_raw");
    case contract::OutputBusKind::master_engine_audition:
        return writer.string_value("master_engine_audition");
    case contract::OutputBusKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "output bus kind is unspecified or unknown");
}

bool write_artifact_kind(CanonicalJsonWriter &writer, contract::ArtifactKind kind) {
    switch (kind) {
    case contract::ArtifactKind::audio:
        return writer.string_value("audio");
    case contract::ArtifactKind::telemetry:
        return writer.string_value("telemetry");
    case contract::ArtifactKind::routing_report:
        return writer.string_value("routing_report");
    case contract::ArtifactKind::validation_report:
        return writer.string_value("validation_report");
    case contract::ArtifactKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "artifact kind is unspecified or unknown");
}

bool write_route_disposition(CanonicalJsonWriter &writer,
                             contract::RouteDisposition disposition) {
    switch (disposition) {
    case contract::RouteDisposition::rendered:
        return writer.string_value("rendered");
    case contract::RouteDisposition::not_applicable:
        return writer.string_value("not_applicable");
    case contract::RouteDisposition::declared_silent:
        return writer.string_value("declared_silent");
    case contract::RouteDisposition::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "route disposition is unspecified or unknown");
}

bool write_omission_kind(CanonicalJsonWriter &writer, contract::OmissionKind kind) {
    switch (kind) {
    case contract::OmissionKind::source_route:
        return writer.string_value("source_route");
    case contract::OmissionKind::external_system:
        return writer.string_value("external_system");
    case contract::OmissionKind::presentation_scene:
        return writer.string_value("presentation_scene");
    case contract::OmissionKind::scenario_behavior:
        return writer.string_value("scenario_behavior");
    case contract::OmissionKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "omission kind is unspecified or unknown");
}

bool write_strings(CanonicalJsonWriter &writer,
                   const std::vector<std::string> &values) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &value : values) {
        if (!writer.string_value(value)) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_optional_audio(CanonicalJsonWriter &writer,
                          const std::optional<contract::AudioContract> &audio) {
    return audio.has_value() ? write_audio_contract(writer, *audio)
                             : writer.null_value();
}

bool write_source_route_requirement(CanonicalJsonWriter &writer,
                                    const contract::SourceRouteRequirement &route) {
    return writer.begin_object() && writer.key("semantic_id") &&
           writer.string_value(route.semantic_id) && writer.key("kind") &&
           write_source_route_kind(writer, route.kind) && writer.key("disposition") &&
           write_route_disposition(writer, route.disposition) &&
           writer.key("disposition_reason") &&
           writer.string_value(route.disposition_reason) &&
           writer.key("artifact_roles") &&
           write_strings(writer, route.artifact_roles) && writer.end_object();
}

bool write_output_bus_requirement(CanonicalJsonWriter &writer,
                                  const contract::OutputBusRequirement &bus) {
    return writer.begin_object() && writer.key("semantic_id") &&
           writer.string_value(bus.semantic_id) && writer.key("kind") &&
           write_output_bus_kind(writer, bus.kind) && writer.key("artifact_roles") &&
           write_strings(writer, bus.artifact_roles) && writer.end_object();
}

bool write_artifact_requirement(CanonicalJsonWriter &writer,
                                const contract::ArtifactRequirement &artifact) {
    return writer.begin_object() && writer.key("role") &&
           writer.string_value(artifact.role) && writer.key("kind") &&
           write_artifact_kind(writer, artifact.kind) && writer.key("audio") &&
           write_optional_audio(writer, artifact.audio) && writer.key("diagnostic") &&
           writer.bool_value(artifact.diagnostic) && writer.end_object();
}

bool write_declared_omission(CanonicalJsonWriter &writer,
                             const contract::DeclaredOmission &omission) {
    return writer.begin_object() && writer.key("semantic_id") &&
           writer.string_value(omission.semantic_id) && writer.key("kind") &&
           write_omission_kind(writer, omission.kind) && writer.key("rationale") &&
           writer.string_value(omission.rationale) && writer.end_object();
}

} // namespace

bool write_output_contract(CanonicalJsonWriter &writer,
                           const contract::OutputContract &output) {
    if (!writer.begin_object() || !writer.key("source_matrix_id") ||
        !writer.string_value(output.source_matrix_id) ||
        !writer.key("source_matrix_sha256") ||
        !writer.sha256_value(output.source_matrix_sha256) ||
        !writer.key("distribution") ||
        !write_distribution_intent(writer, output.distribution) ||
        !writer.key("required_source_routes") || !writer.begin_array()) {
        return false;
    }
    for (const auto &route : output.required_source_routes) {
        if (!write_source_route_requirement(writer, route)) {
            return false;
        }
    }
    if (!writer.end_array() || !writer.key("required_output_buses") ||
        !writer.begin_array()) {
        return false;
    }
    for (const auto &bus : output.required_output_buses) {
        if (!write_output_bus_requirement(writer, bus)) {
            return false;
        }
    }
    if (!writer.end_array() || !writer.key("required_artifacts") ||
        !writer.begin_array()) {
        return false;
    }
    for (const auto &artifact : output.required_artifacts) {
        if (!write_artifact_requirement(writer, artifact)) {
            return false;
        }
    }
    if (!writer.end_array() || !writer.key("declared_omissions") ||
        !writer.begin_array()) {
        return false;
    }
    for (const auto &omission : output.declared_omissions) {
        if (!write_declared_omission(writer, omission)) {
            return false;
        }
    }
    return writer.end_array() && writer.end_object();
}

bool write_route_records(CanonicalJsonWriter &writer,
                         std::span<const contract::RouteRecord> routes) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &route : routes) {
        if (!writer.begin_object() || !writer.key("route_id") ||
            !writer.uint32_value(route.route_id.value) || !writer.key("semantic_id") ||
            !writer.string_value(route.semantic_id) || !writer.key("kind") ||
            !write_source_route_kind(writer, route.kind) ||
            !writer.key("disposition") ||
            !write_route_disposition(writer, route.disposition) ||
            !writer.key("disposition_reason") ||
            !writer.string_value(route.disposition_reason) ||
            !writer.key("artifact_roles") ||
            !write_strings(writer, route.artifact_roles) || !writer.end_object()) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_output_bus_records(CanonicalJsonWriter &writer,
                              std::span<const contract::OutputBusRecord> buses) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &bus : buses) {
        if (!writer.begin_object() || !writer.key("semantic_id") ||
            !writer.string_value(bus.semantic_id) || !writer.key("kind") ||
            !write_output_bus_kind(writer, bus.kind) || !writer.key("artifact_roles") ||
            !write_strings(writer, bus.artifact_roles) || !writer.end_object()) {
            return false;
        }
    }
    return writer.end_array();
}

bool write_artifact_records(CanonicalJsonWriter &writer,
                            std::span<const contract::ArtifactRecord> artifacts) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &artifact : artifacts) {
        if (!writer.begin_object() || !writer.key("role") ||
            !writer.string_value(artifact.role) || !writer.key("kind") ||
            !write_artifact_kind(writer, artifact.kind) ||
            !writer.key("relative_path") ||
            !writer.string_value(artifact.relative_path) || !writer.key("audio") ||
            !write_optional_audio(writer, artifact.audio) ||
            !writer.key("byte_count") ||
            !writer.uint64_hex_value(artifact.byte_count) ||
            !writer.key("payload_sha256") ||
            !writer.sha256_value(artifact.payload_sha256) ||
            !writer.key("diagnostic") || !writer.bool_value(artifact.diagnostic) ||
            !writer.end_object()) {
            return false;
        }
    }
    return writer.end_array();
}

} // namespace engine_sim_offline::artifacts::detail
