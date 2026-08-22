#include "crankwave/authoring/parse.hpp"

#include "authoring/document_reader.hpp"
#include "authoring/parse_engine_detail.hpp"
#include "crankwave/authoring/atlas_bake_document.hpp"

#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace crankwave::authoring {
namespace {

using detail::DocumentReader;
using detail::pointer_index;
using detail::pointer_member;
using detail::read_enum;

constexpr std::uint32_t kKnownRunningStateMask = 31U;

[[nodiscard]] bool valid_relative_uri_syntax(std::string_view uri) {
    if (uri.empty() || uri.size() > 4096U || uri.front() == '/' ||
        uri.front() == '\\' || uri.find('\\') != std::string_view::npos ||
        uri.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0U;
    while (begin <= uri.size()) {
        const auto end = uri.find('/', begin);
        const auto component = uri.substr(
            begin, end == std::string_view::npos ? uri.size() - begin
                                                 : end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return true;
}

void require_positive_duration(DocumentReader &reader, const Quantity &quantity,
                               std::string_view path) {
    detail::require_positive(reader, quantity, path);
}

void require_nonnegative_duration(DocumentReader &reader,
                                  const Quantity &quantity,
                                  std::string_view path) {
    detail::require_nonnegative(reader, quantity, path);
}

void parse_audio(DocumentReader &reader, JsonValue value, std::string_view path,
                 AtlasBakeAudio &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"sample_rate", "routes", "output_bus"});
    reader.rational_rate(reader.required(value, "sample_rate", path),
                         pointer_member(path, "sample_rate"), output.sample_rate);
    detail::read_required_array(
        reader, value, "routes", path, output.routes,
        [&](JsonValue item, std::string_view item_path, SourceRouteRef &route) {
            reader.ref(item, item_path, route);
        });
    reader.ref(reader.required(value, "output_bus", path),
               pointer_member(path, "output_bus"), output.output_bus);

    if (output.routes.empty()) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "routes"),
                   "at least one ordered dry-source route is required");
    }
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < output.routes.size(); ++index) {
        const auto &route = output.routes[index].value;
        if (!route.empty() && !seen.insert(route).second) {
            reader.add(DiagnosticCode::duplicate_id,
                       pointer_index(pointer_member(path, "routes"), index),
                       "source-route references must be unique");
        }
    }
}

void parse_domain(DocumentReader &reader, JsonValue value, std::string_view path,
                  AtlasBakeDomain &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"minimum_rpm", "maximum_rpm", "load_coordinate",
                           "phase_cycle_revolutions", "running_state_mask"});
    reader.number(reader.required(value, "minimum_rpm", path),
                  pointer_member(path, "minimum_rpm"), output.minimum_rpm);
    reader.number(reader.required(value, "maximum_rpm", path),
                  pointer_member(path, "maximum_rpm"), output.maximum_rpm);
    read_enum(
        reader, reader.required(value, "load_coordinate", path),
        pointer_member(path, "load_coordinate"),
        {{"measured-intake-manifold-pressure-pa-abs",
          AtlasBakeLoadCoordinate::measured_intake_manifold_pressure_pa_abs}},
        output.load_coordinate);
    reader.number(reader.required(value, "phase_cycle_revolutions", path),
                  pointer_member(path, "phase_cycle_revolutions"),
                  output.phase_cycle_revolutions);
    reader.uint32(reader.required(value, "running_state_mask", path),
                  pointer_member(path, "running_state_mask"),
                  output.running_state_mask);

    if (!(output.minimum_rpm > 0.0)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "minimum_rpm"),
                   "minimum RPM must be positive");
    }
    if (!(output.maximum_rpm > output.minimum_rpm)) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum_rpm"),
                   "maximum RPM must exceed minimum RPM");
    }
    if (!(output.phase_cycle_revolutions > 0.0)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "phase_cycle_revolutions"),
                   "phase cycle must span a positive number of revolutions");
    }
    if ((output.running_state_mask & ~kKnownRunningStateMask) != 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "running_state_mask"),
                   "running-state mask contains an unknown state bit");
    }
}

void parse_capture(DocumentReader &reader, JsonValue value, std::string_view path,
                   AtlasBakeCapture &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"physics_rate", "samples_per_cycle", "cycles_per_cell",
         "guard_cycles_before", "guard_cycles_after", "preparation_duration",
         "residual_taper_fraction_per_edge", "maximum_concurrency",
         "scenario_template_uri"});
    reader.rational_rate(reader.required(value, "physics_rate", path),
                         pointer_member(path, "physics_rate"), output.physics_rate);
    reader.uint32(reader.required(value, "samples_per_cycle", path),
                  pointer_member(path, "samples_per_cycle"),
                  output.samples_per_cycle);
    reader.uint32(reader.required(value, "cycles_per_cell", path),
                  pointer_member(path, "cycles_per_cell"), output.cycles_per_cell);
    reader.uint32(reader.required(value, "guard_cycles_before", path),
                  pointer_member(path, "guard_cycles_before"),
                  output.guard_cycles_before);
    reader.uint32(reader.required(value, "guard_cycles_after", path),
                  pointer_member(path, "guard_cycles_after"),
                  output.guard_cycles_after);
    reader.quantity(reader.required(value, "preparation_duration", path),
                    pointer_member(path, "preparation_duration"),
                    QuantityDimension::duration, output.preparation_duration);
    reader.number(reader.required(value, "residual_taper_fraction_per_edge", path),
                  pointer_member(path, "residual_taper_fraction_per_edge"),
                  output.residual_taper_fraction_per_edge);
    reader.uint32(reader.required(value, "maximum_concurrency", path),
                  pointer_member(path, "maximum_concurrency"),
                  output.maximum_concurrency);
    reader.string(reader.required(value, "scenario_template_uri", path),
                  pointer_member(path, "scenario_template_uri"),
                  output.scenario_template_uri);

    if (output.samples_per_cycle == 0U ||
        (output.samples_per_cycle & (output.samples_per_cycle - 1U)) != 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "samples_per_cycle"),
                   "samples per phase cycle must be a positive power of two");
    }
    if (output.cycles_per_cell == 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "cycles_per_cell"),
                   "a held cell must contain at least one cycle");
    }
    require_positive_duration(reader, output.preparation_duration,
                              pointer_member(path, "preparation_duration"));
    if (!(output.residual_taper_fraction_per_edge > 0.0 &&
          output.residual_taper_fraction_per_edge < 0.5)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "residual_taper_fraction_per_edge"),
                   "residual taper per edge must lie strictly between zero and one half");
    }
    if (output.maximum_concurrency == 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "maximum_concurrency"),
                   "maximum capture concurrency must be positive");
    }
    if (!valid_relative_uri_syntax(output.scenario_template_uri)) {
        reader.add(DiagnosticCode::invalid_value,
                   pointer_member(path, "scenario_template_uri"),
                   "capture scenario-template URI must be a confined relative path");
    }
}

void parse_load_lane(DocumentReader &reader, JsonValue value,
                     std::string_view path, AtlasBakeLoadLane &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "requested_throttle_01"});
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"),
              output.id);
    reader.fraction(reader.required(value, "requested_throttle_01", path),
                    pointer_member(path, "requested_throttle_01"),
                    output.requested_throttle_01);
}

void parse_phase_alignment_reference(
    DocumentReader &reader, JsonValue value, std::string_view path,
    AtlasBakePhaseAlignmentReference &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"rpm", "load_lane"});
    reader.number(reader.required(value, "rpm", path), pointer_member(path, "rpm"),
                  output.rpm);
    reader.ref(reader.required(value, "load_lane", path),
               pointer_member(path, "load_lane"), output.load_lane);
    if (!(output.rpm > 0.0)) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "rpm"),
                   "phase-alignment reference RPM must be positive");
    }
}

void parse_phase_alignment(DocumentReader &reader, JsonValue value,
                           std::string_view path,
                           AtlasBakePhaseAlignment &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"reference"});
    parse_phase_alignment_reference(
        reader, reader.required(value, "reference", path),
        pointer_member(path, "reference"), output.reference);
}

void parse_transient_capture(DocumentReader &reader, JsonValue value,
                             std::string_view path,
                             AtlasBakeTransientCapture &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"motion", "cycles_per_cell",
                           "normalized_rpm_slope_per_second",
                           "seam_closure_fraction_per_edge"});
    read_enum(reader, reader.required(value, "motion", path),
              pointer_member(path, "motion"),
              {{"prescribed-exponential-speed",
                AtlasBakeTransientMotion::prescribed_exponential_speed}},
              output.motion);
    reader.uint32(reader.required(value, "cycles_per_cell", path),
                  pointer_member(path, "cycles_per_cell"), output.cycles_per_cell);
    reader.number(reader.required(value, "normalized_rpm_slope_per_second", path),
                  pointer_member(path, "normalized_rpm_slope_per_second"),
                  output.normalized_rpm_slope_per_second);
    reader.number(reader.required(value, "seam_closure_fraction_per_edge", path),
                  pointer_member(path, "seam_closure_fraction_per_edge"),
                  output.seam_closure_fraction_per_edge);
    if (output.cycles_per_cell == 0U) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "cycles_per_cell"),
                   "a transient cell must contain at least one cycle");
    }
    if (!(output.normalized_rpm_slope_per_second > 0.0)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "normalized_rpm_slope_per_second"),
                   "normalized transient RPM slope must be positive");
    }
    if (!(output.seam_closure_fraction_per_edge > 0.0 &&
          output.seam_closure_fraction_per_edge < 0.5)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "seam_closure_fraction_per_edge"),
                   "seam closure per edge must lie strictly between zero and one half");
    }
}

void parse_transient_envelope(DocumentReader &reader, JsonValue value,
                              std::string_view path,
                              AtlasBakeTransientEnvelope &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"maximum_gain", "attack_duration", "hold_duration",
                           "release_duration"});
    reader.nonnegative_number(reader.required(value, "maximum_gain", path),
                              pointer_member(path, "maximum_gain"),
                              output.maximum_gain);
    reader.quantity(reader.required(value, "attack_duration", path),
                    pointer_member(path, "attack_duration"),
                    QuantityDimension::duration, output.attack_duration);
    reader.quantity(reader.required(value, "hold_duration", path),
                    pointer_member(path, "hold_duration"),
                    QuantityDimension::duration, output.hold_duration);
    reader.quantity(reader.required(value, "release_duration", path),
                    pointer_member(path, "release_duration"),
                    QuantityDimension::duration, output.release_duration);
    require_positive_duration(reader, output.attack_duration,
                              pointer_member(path, "attack_duration"));
    require_nonnegative_duration(reader, output.hold_duration,
                                 pointer_member(path, "hold_duration"));
    require_positive_duration(reader, output.release_duration,
                              pointer_member(path, "release_duration"));
}

void parse_transient_policy(DocumentReader &reader, JsonValue value,
                            std::string_view path,
                            AtlasBakeTransientPolicy &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"detection_window", "onset_delta_01", "full_delta_01",
         "rearm_delta_01", "refractory_duration", "rising", "falling"});
    reader.quantity(reader.required(value, "detection_window", path),
                    pointer_member(path, "detection_window"),
                    QuantityDimension::duration, output.detection_window);
    reader.fraction(reader.required(value, "onset_delta_01", path),
                    pointer_member(path, "onset_delta_01"), output.onset_delta_01);
    reader.fraction(reader.required(value, "full_delta_01", path),
                    pointer_member(path, "full_delta_01"), output.full_delta_01);
    reader.fraction(reader.required(value, "rearm_delta_01", path),
                    pointer_member(path, "rearm_delta_01"), output.rearm_delta_01);
    reader.quantity(reader.required(value, "refractory_duration", path),
                    pointer_member(path, "refractory_duration"),
                    QuantityDimension::duration, output.refractory_duration);
    parse_transient_envelope(reader, reader.required(value, "rising", path),
                             pointer_member(path, "rising"), output.rising);
    parse_transient_envelope(reader, reader.required(value, "falling", path),
                             pointer_member(path, "falling"), output.falling);

    require_positive_duration(reader, output.detection_window,
                              pointer_member(path, "detection_window"));
    require_positive_duration(reader, output.refractory_duration,
                              pointer_member(path, "refractory_duration"));
    if (!(output.rearm_delta_01 < output.onset_delta_01 &&
          output.onset_delta_01 < output.full_delta_01)) {
        reader.add(DiagnosticCode::inconsistent_value, path,
                   "transient thresholds must satisfy rearm < onset < full");
    }
}

void parse_lifecycle_capture(DocumentReader &reader, JsonValue value,
                             std::string_view path,
                             AtlasBakeLifecycleCapture &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "event", "scenario_uri"});
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"),
              output.id);
    read_enum(reader, reader.required(value, "event", path),
              pointer_member(path, "event"),
              {{"startup", AtlasBakeLifecycleEvent::startup},
               {"shutdown", AtlasBakeLifecycleEvent::shutdown},
               {"limiter", AtlasBakeLifecycleEvent::limiter}},
              output.event);
    reader.string(reader.required(value, "scenario_uri", path),
                  pointer_member(path, "scenario_uri"), output.scenario_uri);
    if (!valid_relative_uri_syntax(output.scenario_uri)) {
        reader.add(DiagnosticCode::invalid_value,
                   pointer_member(path, "scenario_uri"),
                   "lifecycle scenario URI must be a confined relative path");
    }
}

void validate_grid(DocumentReader &reader, const AtlasBakeDocument &document) {
    if (document.rpm_anchors.size() < 2U) {
        reader.add(DiagnosticCode::out_of_range, "/rpm_anchors",
                   "at least two RPM anchors are required");
    }
    for (std::size_t index = 0; index < document.rpm_anchors.size(); ++index) {
        const auto rpm = document.rpm_anchors[index];
        const auto path = pointer_index("/rpm_anchors", index);
        if (!(rpm > 0.0)) {
            reader.add(DiagnosticCode::out_of_range, path,
                       "RPM anchor must be positive");
        }
        if (rpm < document.domain.minimum_rpm ||
            rpm > document.domain.maximum_rpm) {
            reader.add(DiagnosticCode::out_of_range, path,
                       "RPM anchor lies outside the authored domain");
        }
        if (index != 0U) {
            if (rpm == document.rpm_anchors[index - 1U]) {
                reader.add(DiagnosticCode::duplicate_id, path,
                           "RPM anchors must be unique");
            } else if (rpm < document.rpm_anchors[index - 1U]) {
                reader.add(DiagnosticCode::inconsistent_value, path,
                           "RPM anchors must be strictly increasing");
            }
        }
    }

    if (document.load_lanes.empty()) {
        reader.add(DiagnosticCode::out_of_range, "/load_lanes",
                   "at least one load lane is required");
    }
    std::unordered_set<std::string> lane_ids;
    for (std::size_t index = 0; index < document.load_lanes.size(); ++index) {
        const auto &lane = document.load_lanes[index];
        const auto path = pointer_index("/load_lanes", index);
        if (!lane.id.value.empty() && !lane_ids.insert(lane.id.value).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(path, "id"),
                       "load-lane IDs must be unique");
        }
        if (index != 0U &&
            !(lane.requested_throttle_01 >
              document.load_lanes[index - 1U].requested_throttle_01)) {
            reader.add(DiagnosticCode::inconsistent_value,
                       pointer_member(path, "requested_throttle_01"),
                       "load lanes must be ordered by strictly increasing requested throttle");
        }
    }

    bool rpm_resolves = false;
    for (const auto rpm : document.rpm_anchors) {
        if (rpm == document.phase_alignment.reference.rpm) {
            rpm_resolves = true;
            break;
        }
    }
    if (!rpm_resolves) {
        reader.add(DiagnosticCode::dangling_reference,
                   "/phase_alignment/reference/rpm",
                   "phase-alignment reference RPM is not an authored anchor");
    }
    const auto &lane_reference = document.phase_alignment.reference.load_lane.value;
    if (!lane_reference.empty() && !lane_ids.contains(lane_reference)) {
        reader.add(DiagnosticCode::dangling_reference,
                   "/phase_alignment/reference/load_lane",
                   "phase-alignment load-lane reference does not resolve");
    }
}

void validate_lifecycle_captures(DocumentReader &reader,
                                 const AtlasBakeDocument &document) {
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < document.lifecycle_captures.size(); ++index) {
        const auto &capture = document.lifecycle_captures[index];
        if (!capture.id.value.empty() && !ids.insert(capture.id.value).second) {
            reader.add(
                DiagnosticCode::duplicate_id,
                pointer_member(pointer_index("/lifecycle_captures", index), "id"),
                "lifecycle-capture IDs must be unique");
        }
    }
}

void parse_root(DocumentReader &reader, JsonValue value,
                AtlasBakeDocument &output) {
    if (!reader.object(value, "")) {
        return;
    }
    reader.reject_unknown(
        value, "",
        {"schema", "id", "engine", "public_seed", "audio", "domain",
         "capture", "rpm_anchors", "load_lanes", "phase_alignment",
         "transient_capture", "transient_policy", "lifecycle_captures"});
    reader.string(reader.required(value, "schema", ""), "/schema", output.schema);
    if (!output.schema.empty() && output.schema != kAtlasBakeSchema) {
        reader.add(DiagnosticCode::unsupported_schema, "/schema",
                   "expected schema 'crankwave/atlas-bake'");
    }
    reader.id(reader.required(value, "id", ""), "/id", output.id);
    reader.ref(reader.required(value, "engine", ""), "/engine", output.engine);
    reader.uint64(reader.required(value, "public_seed", ""), "/public_seed",
                  output.public_seed);
    parse_audio(reader, reader.required(value, "audio", ""), "/audio",
                output.audio);
    parse_domain(reader, reader.required(value, "domain", ""), "/domain",
                 output.domain);
    parse_capture(reader, reader.required(value, "capture", ""), "/capture",
                  output.capture);
    detail::read_required_array(
        reader, value, "rpm_anchors", "", output.rpm_anchors,
        [&](JsonValue item, std::string_view path, double &rpm) {
            reader.number(item, path, rpm);
        });
    detail::read_required_array(
        reader, value, "load_lanes", "", output.load_lanes,
        [&](JsonValue item, std::string_view path, AtlasBakeLoadLane &lane) {
            parse_load_lane(reader, item, path, lane);
        });
    parse_phase_alignment(reader, reader.required(value, "phase_alignment", ""),
                          "/phase_alignment", output.phase_alignment);
    parse_transient_capture(
        reader, reader.required(value, "transient_capture", ""),
        "/transient_capture", output.transient_capture);
    parse_transient_policy(reader,
                           reader.required(value, "transient_policy", ""),
                           "/transient_policy", output.transient_policy);
    detail::read_required_array(
        reader, value, "lifecycle_captures", "", output.lifecycle_captures,
        [&](JsonValue item, std::string_view path,
            AtlasBakeLifecycleCapture &capture) {
            parse_lifecycle_capture(reader, item, path, capture);
        });

    validate_grid(reader, output);
    validate_lifecycle_captures(reader, output);
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

} // namespace crankwave::authoring
