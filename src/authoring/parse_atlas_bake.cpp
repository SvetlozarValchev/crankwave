#include "engine_sim_offline/authoring/parse.hpp"

#include "authoring/document_reader.hpp"
#include "authoring/parse_engine_detail.hpp"
#include "engine_sim_offline/authoring/atlas_bake_document.hpp"

#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine_sim_offline::authoring {
namespace {

using detail::DocumentReader;
using detail::pointer_index;
using detail::pointer_member;
using detail::read_enum;

void parse_audio(DocumentReader &reader, JsonValue value, std::string_view path,
                 AtlasBakeAudio &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"sample_rate", "buses"});
    reader.rational_rate(reader.required(value, "sample_rate", path),
                         pointer_member(path, "sample_rate"), output.sample_rate);
    detail::read_required_array(
        reader, value, "buses", path, output.buses,
        [&](JsonValue item, std::string_view item_path, AudioBusRef &bus) {
            reader.ref(item, item_path, bus);
        });
    if (output.buses.empty()) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "buses"),
                   "at least one ordered audio bus is required");
    }
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < output.buses.size(); ++index) {
        if (!output.buses[index].value.empty() &&
            !seen.insert(output.buses[index].value).second) {
            reader.add(DiagnosticCode::duplicate_id,
                       pointer_index(pointer_member(path, "buses"), index),
                       "audio bus references must be unique");
        }
    }
}

void parse_domain(DocumentReader &reader, JsonValue value, std::string_view path,
                  AtlasBakeDomain &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"minimum_rpm", "maximum_rpm",
                           "minimum_load_coordinate", "maximum_load_coordinate"});
    reader.number(reader.required(value, "minimum_rpm", path),
                  pointer_member(path, "minimum_rpm"), output.minimum_rpm);
    reader.number(reader.required(value, "maximum_rpm", path),
                  pointer_member(path, "maximum_rpm"), output.maximum_rpm);
    reader.number(reader.required(value, "minimum_load_coordinate", path),
                  pointer_member(path, "minimum_load_coordinate"),
                  output.minimum_load_coordinate);
    reader.number(reader.required(value, "maximum_load_coordinate", path),
                  pointer_member(path, "maximum_load_coordinate"),
                  output.maximum_load_coordinate);
    if (output.minimum_rpm <= 0.0) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "minimum_rpm"),
                   "minimum RPM must be positive");
    }
    if (output.maximum_rpm <= output.minimum_rpm) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum_rpm"),
                   "maximum RPM must exceed minimum RPM");
    }
    if (output.minimum_load_coordinate < -1.0 ||
        output.maximum_load_coordinate > 1.0) {
        reader.add(DiagnosticCode::out_of_range, path,
                   "load-coordinate domain must lie in [-1, 1]");
    }
    if (output.maximum_load_coordinate < output.minimum_load_coordinate) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum_load_coordinate"),
                   "maximum load coordinate must not be below minimum load coordinate");
    }
}

void parse_source(DocumentReader &reader, JsonValue value, std::string_view path,
                  AtlasBakeScenarioSource &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "uri"});
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"),
              output.id);
    reader.string(reader.required(value, "uri", path), pointer_member(path, "uri"),
                  output.uri);
    if (output.uri.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "uri"),
                   "scenario-source URI must not be empty");
    }
}

void parse_rpm_range(DocumentReader &reader, JsonValue value, std::string_view path,
                     AtlasBakeRpmRange &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"minimum", "maximum"});
    reader.number(reader.required(value, "minimum", path),
                  pointer_member(path, "minimum"), output.minimum);
    reader.number(reader.required(value, "maximum", path),
                  pointer_member(path, "maximum"), output.maximum);
    if (output.minimum <= 0.0) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "minimum"),
                   "minimum RPM must be positive");
    }
    if (output.maximum <= output.minimum) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum"),
                   "maximum RPM must exceed minimum RPM");
    }
}

void parse_normalized_slope_range(
    DocumentReader &reader, JsonValue value, std::string_view path,
    AtlasBakeNormalizedRpmSlopeRange &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"minimum_per_second", "maximum_per_second"});
    reader.number(reader.required(value, "minimum_per_second", path),
                  pointer_member(path, "minimum_per_second"),
                  output.minimum_per_second);
    reader.number(reader.required(value, "maximum_per_second", path),
                  pointer_member(path, "maximum_per_second"),
                  output.maximum_per_second);
    if (output.maximum_per_second < output.minimum_per_second) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum_per_second"),
                   "maximum normalized slope must not be below its minimum");
    }
}

void parse_handoff(DocumentReader &reader, JsonValue value, std::string_view path,
                   AtlasBakeHandoffEnvelope &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"transition_frames", "maximum_rpm_error",
         "maximum_normalized_rpm_slope_error_per_second", "maximum_load_error",
         "maximum_crank_phase_error_revolutions"});
    reader.uint32(reader.required(value, "transition_frames", path),
                  pointer_member(path, "transition_frames"),
                  output.transition_frames);
    reader.nonnegative_number(reader.required(value, "maximum_rpm_error", path),
                              pointer_member(path, "maximum_rpm_error"),
                              output.maximum_rpm_error);
    reader.nonnegative_number(
        reader.required(value,
                        "maximum_normalized_rpm_slope_error_per_second", path),
        pointer_member(path,
                       "maximum_normalized_rpm_slope_error_per_second"),
        output.maximum_normalized_rpm_slope_error_per_second);
    reader.nonnegative_number(reader.required(value, "maximum_load_error", path),
                              pointer_member(path, "maximum_load_error"),
                              output.maximum_load_error);
    reader.nonnegative_number(
        reader.required(value, "maximum_crank_phase_error_revolutions", path),
        pointer_member(path, "maximum_crank_phase_error_revolutions"),
        output.maximum_crank_phase_error_revolutions);
    if (output.transition_frames == 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "transition_frames"),
                   "handoff transition must contain at least one frame");
    }
    if (output.maximum_load_error > 2.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "maximum_load_error"),
                   "maximum load error must not exceed the full signed-load span");
    }
    if (output.maximum_crank_phase_error_revolutions > 0.5) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path,
                                  "maximum_crank_phase_error_revolutions"),
                   "maximum crank phase error must not exceed half a revolution");
    }
}

void parse_moving_segment(DocumentReader &reader, JsonValue value,
                          std::string_view path,
                          AtlasBakeMovingSegment &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "direction", "load_coordinate", "state_mask",
                           "normalized_rpm_slope", "captured_rpm", "usable_rpm", "scenario",
                           "handoff"});
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"),
              output.id);
    read_enum(reader, reader.required(value, "direction", path),
              pointer_member(path, "direction"),
              {{"rising", AtlasBakeMovingDirection::rising},
               {"falling", AtlasBakeMovingDirection::falling}},
              output.direction);
    reader.number(reader.required(value, "load_coordinate", path),
                  pointer_member(path, "load_coordinate"), output.load_coordinate);
    reader.uint32(reader.required(value, "state_mask", path),
                  pointer_member(path, "state_mask"), output.state_mask);
    parse_normalized_slope_range(
        reader, reader.required(value, "normalized_rpm_slope", path),
        pointer_member(path, "normalized_rpm_slope"),
        output.normalized_rpm_slope);
    parse_rpm_range(reader, reader.required(value, "captured_rpm", path),
                    pointer_member(path, "captured_rpm"), output.captured_rpm);
    parse_rpm_range(reader, reader.required(value, "usable_rpm", path),
                    pointer_member(path, "usable_rpm"), output.usable_rpm);
    reader.ref(reader.required(value, "scenario", path),
               pointer_member(path, "scenario"), output.scenario);
    parse_handoff(reader, reader.required(value, "handoff", path),
                  pointer_member(path, "handoff"), output.handoff);

    if (output.load_coordinate < -1.0 || output.load_coordinate > 1.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "load_coordinate"),
                   "moving-segment load coordinate must lie in [-1, 1]");
    }
    if (output.direction == AtlasBakeMovingDirection::rising &&
        output.normalized_rpm_slope.minimum_per_second <= 0.0) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(pointer_member(path, "normalized_rpm_slope"),
                                  "minimum_per_second"),
                   "a rising segment requires strictly positive normalized slope");
    }
    if (output.direction == AtlasBakeMovingDirection::falling &&
        output.normalized_rpm_slope.maximum_per_second >= 0.0) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(pointer_member(path, "normalized_rpm_slope"),
                                  "maximum_per_second"),
                   "a falling segment requires strictly negative normalized slope");
    }
    if (output.usable_rpm.minimum <= output.captured_rpm.minimum ||
        output.usable_rpm.maximum >= output.captured_rpm.maximum) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "usable_rpm"),
                   "usable RPM range must be a strict interior of captured RPM");
    }
}

void require_empty_array(DocumentReader &reader, JsonValue object,
                         std::string_view key) {
    const auto path = pointer_member("", key);
    const auto value = reader.required(object, key, "");
    if (reader.array(value, path) && value.size() != 0U) {
        reader.add(DiagnosticCode::unsupported_capability, path,
                   "this performance class is outside the moving-segment slice");
    }
}

void validate_references(DocumentReader &reader, const AtlasBakeDocument &document) {
    std::unordered_map<std::string, std::size_t> source_indices;
    std::unordered_set<std::string> source_uris;
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        const auto path = pointer_index("/scenario_sources", index);
        if (!source.id.value.empty() &&
            !source_indices.emplace(source.id.value, index).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(path, "id"),
                       "scenario-source IDs must be unique");
        }
        if (!source.uri.empty() && !source_uris.insert(source.uri).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(path, "uri"),
                       "scenario-source URIs must be unique");
        }
    }

    std::unordered_set<std::string> referenced;
    std::unordered_set<std::string> segment_ids;
    for (std::size_t index = 0; index < document.moving_segments.size(); ++index) {
        const auto &segment = document.moving_segments[index];
        const auto path = pointer_index("/moving_segments", index);
        if (!segment.id.value.empty() &&
            !segment_ids.insert(segment.id.value).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(path, "id"),
                       "moving-segment IDs must be unique");
        }
        if (segment.load_coordinate < document.domain.minimum_load_coordinate ||
            segment.load_coordinate > document.domain.maximum_load_coordinate) {
            reader.add(DiagnosticCode::out_of_range,
                       pointer_member(path, "load_coordinate"),
                       "moving-segment load coordinate lies outside the atlas domain");
        }
        if (segment.usable_rpm.minimum < document.domain.minimum_rpm ||
            segment.usable_rpm.maximum > document.domain.maximum_rpm) {
            reader.add(DiagnosticCode::out_of_range,
                       pointer_member(path, "usable_rpm"),
                       "usable RPM range lies outside the atlas domain");
        }
        const auto &reference = segment.scenario.value;
        if (!reference.empty() && !source_indices.contains(reference)) {
            reader.add(DiagnosticCode::dangling_reference,
                       pointer_member(path, "scenario"),
                       "moving-segment scenario reference does not resolve");
        } else if (!reference.empty() && !referenced.insert(reference).second) {
            reader.add(DiagnosticCode::duplicate_id,
                       pointer_member(path, "scenario"),
                       "each moving segment must own an independent source scenario");
        }
    }
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        if (!source.id.value.empty() && !referenced.contains(source.id.value)) {
            reader.add(DiagnosticCode::disconnected_object,
                       pointer_member(pointer_index("/scenario_sources", index),
                                      "id"),
                       "scenario source is not owned by a moving segment");
        }
    }
}

void parse_root(DocumentReader &reader, JsonValue value, AtlasBakeDocument &output) {
    if (!reader.object(value, "")) {
        return;
    }
    reader.reject_unknown(
        value, "",
        {"schema", "id", "engine", "public_seed", "audio", "domain",
         "scenario_sources", "moving_segments", "stationary_tiles",
         "transient_performances", "lifecycle_performances"});
    reader.string(reader.required(value, "schema", ""), "/schema", output.schema);
    if (!output.schema.empty() && output.schema != kAtlasBakeSchema) {
        reader.add(DiagnosticCode::unsupported_schema, "/schema",
                   "expected schema 'engine-sim-offline/atlas-bake'");
    }
    reader.id(reader.required(value, "id", ""), "/id", output.id);
    reader.ref(reader.required(value, "engine", ""), "/engine", output.engine);
    reader.uint64(reader.required(value, "public_seed", ""), "/public_seed",
                  output.public_seed);
    parse_audio(reader, reader.required(value, "audio", ""), "/audio", output.audio);
    parse_domain(reader, reader.required(value, "domain", ""), "/domain",
                 output.domain);
    detail::read_required_array(
        reader, value, "scenario_sources", "", output.scenario_sources,
        [&](JsonValue item, std::string_view path, AtlasBakeScenarioSource &source) {
            parse_source(reader, item, path, source);
        });
    detail::read_required_array(
        reader, value, "moving_segments", "", output.moving_segments,
        [&](JsonValue item, std::string_view path, AtlasBakeMovingSegment &segment) {
            parse_moving_segment(reader, item, path, segment);
        });
    if (output.scenario_sources.empty()) {
        reader.add(DiagnosticCode::out_of_range, "/scenario_sources",
                   "at least one scenario source is required");
    }
    if (output.moving_segments.empty()) {
        reader.add(DiagnosticCode::out_of_range, "/moving_segments",
                   "at least one moving segment is required");
    }
    require_empty_array(reader, value, "stationary_tiles");
    require_empty_array(reader, value, "transient_performances");
    require_empty_array(reader, value, "lifecycle_performances");
    validate_references(reader, output);
}

[[nodiscard]] DiagnosticReport resource_diagnostic(std::string message) {
    Diagnostic diagnostic;
    diagnostic.code = DiagnosticCode::resource_limit;
    diagnostic.message = std::move(message);
    return DiagnosticReport{{std::move(diagnostic)}};
}

} // namespace

AtlasBakeDocumentParseResult
parse_atlas_bake_document(std::string_view json,
                          AuthoringParseLimits limits) noexcept {
    try {
        auto json_result = parse_json(json, limits.json);
        if (const auto *error = std::get_if<JsonParseError>(&json_result)) {
            return detail::syntax_diagnostic(*error);
        }
        auto document = std::get<JsonDocument>(std::move(json_result));
        DocumentReader reader{std::move(limits)};
        AtlasBakeDocument output;
        parse_root(reader, document.root(), output);
        if (!reader.ok()) {
            return std::move(reader).finish();
        }
        return output;
    } catch (const std::bad_alloc &) {
        return resource_diagnostic(
            "allocation failed while parsing atlas-bake document");
    } catch (const std::exception &exception) {
        return detail::internal_diagnostic(
            "unexpected atlas-bake parser failure: " +
            std::string{exception.what()});
    } catch (...) {
        return detail::internal_diagnostic(
            "unexpected non-standard atlas-bake parser failure");
    }
}

} // namespace engine_sim_offline::authoring
