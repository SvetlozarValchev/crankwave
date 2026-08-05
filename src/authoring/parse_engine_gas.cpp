#include "authoring/parse_engine_detail.hpp"

#include <string>
#include <utility>

namespace engine_sim_offline::authoring::detail {

void parse_intake(DocumentReader &reader, JsonValue value, std::string_view path,
                  IntakeDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "plenum_volume", "plenum_cross_section_area",
                           "runner_length", "main_restriction",
                           "idle_bypass_restriction", "runner_restriction",
                           "idle_throttle_position_01", "runner_velocity_decay_01",
                           "main_mixture_lambda"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("intake", output.id.value);
    read_quantity_member(reader, value, "plenum_volume", path,
                         QuantityDimension::volume, output.plenum_volume, owner);
    read_quantity_member(reader, value, "plenum_cross_section_area", path,
                         QuantityDimension::area, output.plenum_cross_section_area,
                         owner);
    read_quantity_member(reader, value, "runner_length", path,
                         QuantityDimension::length, output.runner_length, owner);
    parse_flow_restriction(
        reader, reader.required(value, "main_restriction", path, owner),
        pointer_member(path, "main_restriction"), output.main_restriction, owner);
    parse_flow_restriction(
        reader, reader.required(value, "idle_bypass_restriction", path, owner),
        pointer_member(path, "idle_bypass_restriction"), output.idle_bypass_restriction,
        owner);
    parse_flow_restriction(
        reader, reader.required(value, "runner_restriction", path, owner),
        pointer_member(path, "runner_restriction"), output.runner_restriction, owner);
    reader.fraction(reader.required(value, "idle_throttle_position_01", path, owner),
                    pointer_member(path, "idle_throttle_position_01"),
                    output.idle_throttle_position_01, owner);
    reader.fraction(reader.required(value, "runner_velocity_decay_01", path, owner),
                    pointer_member(path, "runner_velocity_decay_01"),
                    output.runner_velocity_decay_01, owner);
    reader.nonnegative_number(
        reader.required(value, "main_mixture_lambda", path, owner),
        pointer_member(path, "main_mixture_lambda"), output.main_mixture_lambda, owner);
    require_positive(reader, output.plenum_volume,
                     pointer_member(path, "plenum_volume"), owner);
    require_positive(reader, output.plenum_cross_section_area,
                     pointer_member(path, "plenum_cross_section_area"), owner);
    require_nonnegative(reader, output.runner_length,
                        pointer_member(path, "runner_length"), owner);
    if (output.main_mixture_lambda <= 0.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "main_mixture_lambda"),
                   "main mixture lambda must be positive", owner);
    }
}

void parse_exhaust(DocumentReader &reader, JsonValue value, std::string_view path,
                   ExhaustDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "collector_cross_section_area", "collector_length",
                           "collector_volume", "primary_tube_length",
                           "outlet_restriction", "primary_restriction",
                           "velocity_decay_01"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("exhaust", output.id.value);
    read_quantity_member(reader, value, "collector_cross_section_area", path,
                         QuantityDimension::area, output.collector_cross_section_area,
                         owner);
    const auto collector_length = reader.optional(value, "collector_length");
    if (collector_length.valid() && !collector_length.is_null()) {
        Quantity parsed;
        reader.quantity(collector_length, pointer_member(path, "collector_length"),
                        QuantityDimension::length, parsed, owner);
        require_positive(reader, parsed, pointer_member(path, "collector_length"),
                         owner);
        output.collector_length = std::move(parsed);
    }
    const auto collector_volume = reader.optional(value, "collector_volume");
    if (collector_volume.valid() && !collector_volume.is_null()) {
        Quantity parsed;
        reader.quantity(collector_volume, pointer_member(path, "collector_volume"),
                        QuantityDimension::volume, parsed, owner);
        require_positive(reader, parsed, pointer_member(path, "collector_volume"),
                         owner);
        output.collector_volume = std::move(parsed);
    }
    if (!output.collector_length && !output.collector_volume) {
        reader.add(DiagnosticCode::missing_value,
                   pointer_member(path, "collector_length"),
                   "exhaust requires collector_length or collector_volume", owner);
    }
    read_quantity_member(reader, value, "primary_tube_length", path,
                         QuantityDimension::length, output.primary_tube_length, owner);
    parse_flow_restriction(
        reader, reader.required(value, "outlet_restriction", path, owner),
        pointer_member(path, "outlet_restriction"), output.outlet_restriction, owner);
    parse_flow_restriction(
        reader, reader.required(value, "primary_restriction", path, owner),
        pointer_member(path, "primary_restriction"), output.primary_restriction, owner);
    reader.fraction(reader.required(value, "velocity_decay_01", path, owner),
                    pointer_member(path, "velocity_decay_01"), output.velocity_decay_01,
                    owner);
    require_positive(reader, output.collector_cross_section_area,
                     pointer_member(path, "collector_cross_section_area"), owner);
    require_nonnegative(reader, output.primary_tube_length,
                        pointer_member(path, "primary_tube_length"), owner);
}

void parse_port(DocumentReader &reader, JsonValue value, std::string_view path,
                PortDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "head", "kind", "runner_volume",
                           "runner_cross_section_area", "flow_curve"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("port", output.id.value);
    read_ref_member(reader, value, "head", path, output.head, owner);
    read_enum(reader, reader.required(value, "kind", path, owner),
              pointer_member(path, "kind"),
              {{"intake", PortKind::intake}, {"exhaust", PortKind::exhaust}},
              output.kind, owner);
    read_quantity_member(reader, value, "runner_volume", path,
                         QuantityDimension::volume, output.runner_volume, owner);
    read_quantity_member(reader, value, "runner_cross_section_area", path,
                         QuantityDimension::area, output.runner_cross_section_area,
                         owner);
    read_ref_member(reader, value, "flow_curve", path, output.flow_curve, owner);
    require_positive(reader, output.runner_volume,
                     pointer_member(path, "runner_volume"), owner);
    require_positive(reader, output.runner_cross_section_area,
                     pointer_member(path, "runner_cross_section_area"), owner);
}

void parse_source_route(DocumentReader &reader, JsonValue value, std::string_view path,
                        SourceRouteDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "exhaust") {
        reader.reject_unknown(value, path, {"id", "type", "exhaust"});
    } else if (type == "mechanical") {
        reader.reject_unknown(value, path, {"id", "type", "component"});
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown source-route type '" + type + "'");
    }
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("source_route", output.id.value);
    if (type == "exhaust") {
        ExhaustRouteSource parsed;
        read_ref_member(reader, value, "exhaust", path, parsed.exhaust, owner);
        output.source = std::move(parsed);
    } else if (type == "mechanical") {
        MechanicalRouteSource parsed;
        reader.string(reader.required(value, "component", path, owner),
                      pointer_member(path, "component"), parsed.component, owner);
        output.source = std::move(parsed);
    }
}

} // namespace engine_sim_offline::authoring::detail
