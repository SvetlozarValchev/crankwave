#include "engine_sim_offline/authoring/parse.hpp"

#include "authoring/document_reader.hpp"
#include "authoring/parse_engine_detail.hpp"

#include <cmath>
#include <exception>
#include <new>
#include <numbers>
#include <optional>
#include <ranges>
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
                 PackageBakeAudio &output) {
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
    std::unordered_set<std::string> buses;
    for (std::size_t index = 0; index < output.buses.size(); ++index) {
        const auto &bus = output.buses[index].value;
        if (!bus.empty() && !buses.insert(bus).second) {
            reader.add(DiagnosticCode::duplicate_id,
                       pointer_index(pointer_member(path, "buses"), index),
                       "duplicate audio bus reference '" + bus + "'");
        }
    }
}

void parse_scenario_source(DocumentReader &reader, JsonValue value,
                           std::string_view path, PackageBakeScenarioSource &output) {
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
                     PackageBakeRpmRange &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"minimum", "maximum"});
    reader.quantity(reader.required(value, "minimum", path),
                    pointer_member(path, "minimum"), QuantityDimension::angular_speed,
                    output.minimum);
    reader.quantity(reader.required(value, "maximum", path),
                    pointer_member(path, "maximum"), QuantityDimension::angular_speed,
                    output.maximum);

    if (output.minimum.value <= 0.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(pointer_member(path, "minimum"), "value"),
                   "minimum running speed must be positive");
    }
    if (output.maximum.value <= 0.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(pointer_member(path, "maximum"), "value"),
                   "maximum running speed must be positive");
    }

    const auto radians_per_second = [](const Quantity &speed) -> std::optional<double> {
        if (speed.unit == "rad/s") {
            return speed.value;
        }
        if (speed.unit == "rpm") {
            return speed.value * (2.0 * std::numbers::pi / 60.0);
        }
        return std::nullopt;
    };
    const auto minimum = radians_per_second(output.minimum);
    const auto maximum = radians_per_second(output.maximum);
    if (minimum && maximum && *maximum <= *minimum) {
        reader.add(DiagnosticCode::inconsistent_value, pointer_member(path, "maximum"),
                   "maximum running speed must exceed minimum running speed");
    }
}

void parse_running_plane(DocumentReader &reader, JsonValue value, std::string_view path,
                         PackageBakeRunningPlane &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "load_coordinate", "direction", "scenario"});
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"),
              output.id);
    reader.number(reader.required(value, "load_coordinate", path),
                  pointer_member(path, "load_coordinate"), output.load_coordinate);
    if (output.load_coordinate < -1.0 || output.load_coordinate > 1.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "load_coordinate"),
                   "running-plane load coordinate must lie in [-1, 1]");
    }
    read_enum(reader, reader.required(value, "direction", path),
              pointer_member(path, "direction"),
              {{"rising", PackageBakeRunningDirection::rising},
               {"falling", PackageBakeRunningDirection::falling}},
              output.direction);
    reader.ref(reader.required(value, "scenario", path),
               pointer_member(path, "scenario"), output.scenario);
}

void parse_idle(DocumentReader &reader, JsonValue value, std::string_view path,
                PackageBakeIdle &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"scenario"});
    reader.ref(reader.required(value, "scenario", path),
               pointer_member(path, "scenario"), output.scenario);
}

void parse_running(DocumentReader &reader, JsonValue value, std::string_view path,
                   PackageBakeRunning &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"rpm_range", "planes", "idle"});
    parse_rpm_range(reader, reader.required(value, "rpm_range", path),
                    pointer_member(path, "rpm_range"), output.rpm_range);
    detail::read_required_array(reader, value, "planes", path, output.planes,
                                [&](JsonValue item, std::string_view item_path,
                                    PackageBakeRunningPlane &plane) {
                                    parse_running_plane(reader, item, item_path, plane);
                                });
    parse_idle(reader, reader.required(value, "idle", path),
               pointer_member(path, "idle"), output.idle);

    const auto planes_path = pointer_member(path, "planes");
    if (output.planes.size() < 3U) {
        reader.add(DiagnosticCode::out_of_range, planes_path,
                   "at least three running load planes are required");
    }

    std::unordered_set<std::string> plane_ids;
    for (std::size_t index = 0; index < output.planes.size(); ++index) {
        const auto plane_path = pointer_index(planes_path, index);
        const auto &plane = output.planes[index];
        if (!plane.id.value.empty() && !plane_ids.insert(plane.id.value).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(plane_path, "id"),
                       "duplicate running-plane ID '" + plane.id.value + "'");
        }
        if (index != 0U &&
            plane.load_coordinate <= output.planes[index - 1U].load_coordinate) {
            reader.add(DiagnosticCode::inconsistent_value,
                       pointer_member(plane_path, "load_coordinate"),
                       "running-plane load coordinates must be strictly ascending");
        }
    }
    if (!output.planes.empty() && output.planes.front().load_coordinate != -1.0) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(pointer_index(planes_path, 0U), "load_coordinate"),
                   "first running-plane load coordinate must be exactly -1");
    }
    if (!output.planes.empty() && output.planes.back().load_coordinate != 1.0) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(pointer_index(planes_path, output.planes.size() - 1U),
                                  "load_coordinate"),
                   "last running-plane load coordinate must be exactly 1");
    }
}

void validate_scenario_source_references(DocumentReader &reader,
                                         const PackageBakeDocument &document) {
    std::unordered_map<std::string, std::size_t> source_indices;
    std::unordered_set<std::string> source_uris;
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        const auto source_path = pointer_index("/scenario_sources", index);
        if (!source.id.value.empty() &&
            !source_indices.emplace(source.id.value, index).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(source_path, "id"),
                       "duplicate scenario-source ID '" + source.id.value + "'");
        }
        if (!source.uri.empty() && !source_uris.insert(source.uri).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(source_path, "uri"),
                       "duplicate scenario-source URI '" + source.uri + "'");
        }
    }

    std::unordered_set<std::string> referenced_sources;
    const auto validate_reference = [&](const PackageBakeScenarioSourceRef &reference,
                                        const std::string &path) {
        if (reference.value.empty()) {
            return;
        }
        if (!source_indices.contains(reference.value)) {
            reader.add(DiagnosticCode::dangling_reference, path,
                       "scenario-source reference '" + reference.value +
                           "' does not resolve");
            return;
        }
        if (!referenced_sources.insert(reference.value).second) {
            reader.add(DiagnosticCode::duplicate_id, path,
                       "scenario-source reference '" + reference.value +
                           "' is used more than once");
        }
    };

    for (std::size_t index = 0; index < document.running.planes.size(); ++index) {
        validate_reference(
            document.running.planes[index].scenario,
            pointer_member(pointer_index("/running/planes", index), "scenario"));
    }
    validate_reference(document.running.idle.scenario, "/running/idle/scenario");

    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        if (!source.id.value.empty() && !referenced_sources.contains(source.id.value)) {
            reader.add(DiagnosticCode::disconnected_object,
                       pointer_member(pointer_index("/scenario_sources", index), "id"),
                       "scenario source '" + source.id.value +
                           "' is not referenced by running audio");
        }
    }
}

void parse_package_bake_root(DocumentReader &reader, JsonValue value,
                             PackageBakeDocument &output) {
    if (!reader.object(value, "")) {
        return;
    }
    reader.reject_unknown(value, "",
                          {"schema", "id", "engine", "public_seed", "audio",
                           "scenario_sources", "running", "events"});
    reader.string(reader.required(value, "schema", ""), "/schema", output.schema);
    if (!output.schema.empty() && output.schema != "engine-sim-offline/package-bake") {
        reader.add(DiagnosticCode::unsupported_schema, "/schema",
                   "expected schema 'engine-sim-offline/package-bake'");
    }
    reader.id(reader.required(value, "id", ""), "/id", output.id);
    reader.ref(reader.required(value, "engine", ""), "/engine", output.engine);
    reader.uint64(reader.required(value, "public_seed", ""), "/public_seed",
                  output.public_seed);
    parse_audio(reader, reader.required(value, "audio", ""), "/audio", output.audio);
    detail::read_required_array(
        reader, value, "scenario_sources", "", output.scenario_sources,
        [&](JsonValue item, std::string_view item_path,
            PackageBakeScenarioSource &source) {
            parse_scenario_source(reader, item, item_path, source);
        });
    if (output.scenario_sources.empty()) {
        reader.add(DiagnosticCode::out_of_range, "/scenario_sources",
                   "at least one scenario source is required");
    }
    parse_running(reader, reader.required(value, "running", ""), "/running",
                  output.running);

    const auto events = reader.required(value, "events", "");
    if (reader.array(events, "/events") && events.size() != 0U) {
        reader.add(DiagnosticCode::unsupported_capability, "/events",
                   "lifecycle events are not admitted by the current package-bake "
                   "contract");
    }

    validate_scenario_source_references(reader, output);
}

[[nodiscard]] DiagnosticReport resource_diagnostic(std::string message) {
    Diagnostic diagnostic;
    diagnostic.code = DiagnosticCode::resource_limit;
    diagnostic.message = std::move(message);
    return DiagnosticReport{{std::move(diagnostic)}};
}

} // namespace

PackageBakeDocumentParseResult
parse_package_bake_document(std::string_view json,
                            AuthoringParseLimits limits) noexcept {
    try {
        auto json_result = parse_json(json, limits.json);
        if (const auto *error = std::get_if<JsonParseError>(&json_result)) {
            return detail::syntax_diagnostic(*error);
        }
        auto document = std::get<JsonDocument>(std::move(json_result));
        DocumentReader reader{std::move(limits)};
        PackageBakeDocument output;
        parse_package_bake_root(reader, document.root(), output);
        if (!reader.ok()) {
            return std::move(reader).finish();
        }
        return output;
    } catch (const std::bad_alloc &) {
        return resource_diagnostic(
            "allocation failed while parsing package-bake document");
    } catch (const std::exception &exception) {
        return detail::internal_diagnostic("unexpected package-bake parser failure: " +
                                           std::string{exception.what()});
    } catch (...) {
        return detail::internal_diagnostic(
            "unexpected non-standard package-bake parser failure");
    }
}

} // namespace engine_sim_offline::authoring
