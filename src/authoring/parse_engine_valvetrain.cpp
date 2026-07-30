#include "authoring/parse_engine_detail.hpp"

#include <string>
#include <utility>

namespace engine_sim_offline::authoring::detail {

void parse_cam_lobe(DocumentReader &reader, JsonValue value, std::string_view path,
                    CamLobeDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "sampled") {
        reader.reject_unknown(
            value, path,
            {"id", "cylinder", "port_kind", "centerline", "type", "lift_curve"});
    } else if (type == "harmonic") {
        reader.reject_unknown(value, path,
                              {"id", "cylinder", "port_kind", "centerline", "type",
                               "duration_at_reference_lift", "reference_lift",
                               "maximum_lift", "gamma", "sample_count"});
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown cam-lobe type '" + type + "'");
    }
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("cam_lobe", output.id.value);
    read_ref_member(reader, value, "cylinder", path, output.cylinder, owner);
    read_enum(reader, reader.required(value, "port_kind", path, owner),
              pointer_member(path, "port_kind"),
              {{"intake", PortKind::intake}, {"exhaust", PortKind::exhaust}},
              output.port_kind, owner);
    read_quantity_member(reader, value, "centerline", path, QuantityDimension::angle,
                         output.centerline, owner);
    if (type == "sampled") {
        SampledCamLobe parsed;
        read_ref_member(reader, value, "lift_curve", path, parsed.lift_curve, owner);
        output.shape = std::move(parsed);
    } else if (type == "harmonic") {
        HarmonicCamLobe parsed;
        read_quantity_member(reader, value, "duration_at_reference_lift", path,
                             QuantityDimension::angle,
                             parsed.duration_at_reference_lift, owner);
        read_quantity_member(reader, value, "reference_lift", path,
                             QuantityDimension::length, parsed.reference_lift, owner);
        read_quantity_member(reader, value, "maximum_lift", path,
                             QuantityDimension::length, parsed.maximum_lift, owner);
        reader.nonnegative_number(reader.required(value, "gamma", path, owner),
                                  pointer_member(path, "gamma"), parsed.gamma, owner);
        reader.uint32(reader.required(value, "sample_count", path, owner),
                      pointer_member(path, "sample_count"), parsed.sample_count, owner);
        require_positive(reader, parsed.duration_at_reference_lift,
                         pointer_member(path, "duration_at_reference_lift"), owner);
        require_positive(reader, parsed.reference_lift,
                         pointer_member(path, "reference_lift"), owner);
        require_positive(reader, parsed.maximum_lift,
                         pointer_member(path, "maximum_lift"), owner);
        if (parsed.sample_count < 2U) {
            reader.add(DiagnosticCode::out_of_range,
                       pointer_member(path, "sample_count"),
                       "harmonic lobe requires at least two samples", owner);
        }
        output.shape = std::move(parsed);
    }
}

void parse_camshaft(DocumentReader &reader, JsonValue value, std::string_view path,
                    CamshaftDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "advance", "base_radius", "lobes"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("camshaft", output.id.value);
    read_quantity_member(reader, value, "advance", path, QuantityDimension::angle,
                         output.advance, owner);
    read_quantity_member(reader, value, "base_radius", path, QuantityDimension::length,
                         output.base_radius, owner);
    read_required_array(
        reader, value, "lobes", path, output.lobes,
        [&](JsonValue item, std::string_view item_path, CamLobeRef &reference) {
            reader.ref(item, item_path, reference, owner);
        },
        owner);
    require_positive(reader, output.base_radius, pointer_member(path, "base_radius"),
                     owner);
}

void parse_valvetrain(DocumentReader &reader, JsonValue value, std::string_view path,
                      ValvetrainDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "standard") {
        reader.reject_unknown(value, path,
                              {"id", "type", "intake_camshaft", "exhaust_camshaft"});
    } else if (type == "vtec") {
        reader.reject_unknown(value, path,
                              {"id", "type", "base_intake_camshaft",
                               "base_exhaust_camshaft", "alternate_intake_camshaft",
                               "alternate_exhaust_camshaft", "activation"});
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown valvetrain type '" + type + "'");
    }
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("valvetrain", output.id.value);
    if (type == "standard") {
        StandardValvetrain parsed;
        read_ref_member(reader, value, "intake_camshaft", path, parsed.intake_camshaft,
                        owner);
        read_ref_member(reader, value, "exhaust_camshaft", path,
                        parsed.exhaust_camshaft, owner);
        output.kind = std::move(parsed);
    } else if (type == "vtec") {
        VtecValvetrain parsed;
        read_ref_member(reader, value, "base_intake_camshaft", path,
                        parsed.base_intake_camshaft, owner);
        read_ref_member(reader, value, "base_exhaust_camshaft", path,
                        parsed.base_exhaust_camshaft, owner);
        read_ref_member(reader, value, "alternate_intake_camshaft", path,
                        parsed.alternate_intake_camshaft, owner);
        read_ref_member(reader, value, "alternate_exhaust_camshaft", path,
                        parsed.alternate_exhaust_camshaft, owner);
        const auto activation = reader.required(value, "activation", path, owner);
        const auto activation_path = pointer_member(path, "activation");
        if (reader.object(activation, activation_path, owner)) {
            reader.reject_unknown(activation, activation_path,
                                  {"minimum_engine_speed", "minimum_vehicle_speed",
                                   "minimum_manifold_vacuum", "minimum_throttle_01"},
                                  owner);
            read_quantity_member(reader, activation, "minimum_engine_speed",
                                 activation_path, QuantityDimension::angular_speed,
                                 parsed.activation.minimum_engine_speed, owner);
            read_quantity_member(reader, activation, "minimum_vehicle_speed",
                                 activation_path, QuantityDimension::speed,
                                 parsed.activation.minimum_vehicle_speed, owner);
            read_quantity_member(reader, activation, "minimum_manifold_vacuum",
                                 activation_path, QuantityDimension::pressure,
                                 parsed.activation.minimum_manifold_vacuum, owner);
            reader.fraction(reader.required(activation, "minimum_throttle_01",
                                            activation_path, owner),
                            pointer_member(activation_path, "minimum_throttle_01"),
                            parsed.activation.minimum_throttle_01, owner);
        }
        output.kind = std::move(parsed);
    }
}

void parse_head(DocumentReader &reader, JsonValue value, std::string_view path,
                HeadDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "chamber_volume", "valvetrain", "ports"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("head", output.id.value);
    read_quantity_member(reader, value, "chamber_volume", path,
                         QuantityDimension::volume, output.chamber_volume, owner);
    read_ref_member(reader, value, "valvetrain", path, output.valvetrain, owner);
    read_required_array(
        reader, value, "ports", path, output.ports,
        [&](JsonValue item, std::string_view item_path, PortRef &reference) {
            reader.ref(item, item_path, reference, owner);
        },
        owner);
    require_positive(reader, output.chamber_volume,
                     pointer_member(path, "chamber_volume"), owner);
}

} // namespace engine_sim_offline::authoring::detail
