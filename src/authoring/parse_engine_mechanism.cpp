#include "authoring/parse_engine_detail.hpp"

#include <string>
#include <utility>

namespace crankwave::authoring::detail {

void parse_crankshaft(DocumentReader &reader, JsonValue value, std::string_view path,
                      CrankshaftDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "throw_radius", "mass", "flywheel_mass",
                           "moment_of_inertia", "friction_torque",
                           "tdc_reference_angle"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("crankshaft", output.id.value);
    read_quantity_member(reader, value, "throw_radius", path, QuantityDimension::length,
                         output.throw_radius, owner);
    read_quantity_member(reader, value, "mass", path, QuantityDimension::mass,
                         output.mass, owner);
    read_quantity_member(reader, value, "flywheel_mass", path, QuantityDimension::mass,
                         output.flywheel_mass, owner);
    read_quantity_member(reader, value, "moment_of_inertia", path,
                         QuantityDimension::moment_of_inertia, output.moment_of_inertia,
                         owner);
    const auto friction_torque = reader.optional(value, "friction_torque");
    if (friction_torque.valid() && !friction_torque.is_null()) {
        Quantity parsed;
        reader.quantity(friction_torque, pointer_member(path, "friction_torque"),
                        QuantityDimension::torque, parsed, owner);
        require_nonnegative(reader, parsed, pointer_member(path, "friction_torque"),
                            owner);
        output.friction_torque = std::move(parsed);
    }
    read_quantity_member(reader, value, "tdc_reference_angle", path,
                         QuantityDimension::angle, output.tdc_reference_angle, owner);
    require_positive(reader, output.throw_radius, pointer_member(path, "throw_radius"),
                     owner);
    require_nonnegative(reader, output.mass, pointer_member(path, "mass"), owner);
    require_nonnegative(reader, output.flywheel_mass,
                        pointer_member(path, "flywheel_mass"), owner);
    require_nonnegative(reader, output.moment_of_inertia,
                        pointer_member(path, "moment_of_inertia"), owner);
}

void parse_journal(DocumentReader &reader, JsonValue value, std::string_view path,
                   JournalDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "crankshaft") {
        reader.reject_unknown(value, path, {"id", "type", "crankshaft", "phase"});
    } else if (type == "master_rod") {
        reader.reject_unknown(
            value, path,
            {"id", "type", "master_cylinder", "throw_radius", "phase"});
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown journal attachment type '" + type + "'");
    }

    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("journal", output.id.value);
    if (type == "crankshaft") {
        CrankshaftJournalAttachment parsed;
        read_ref_member(reader, value, "crankshaft", path, parsed.crankshaft, owner);
        output.attachment = std::move(parsed);
    } else if (type == "master_rod") {
        MasterRodJournalAttachment parsed;
        read_ref_member(reader, value, "master_cylinder", path,
                        parsed.master_cylinder, owner);
        read_quantity_member(reader, value, "throw_radius", path,
                             QuantityDimension::length, parsed.throw_radius, owner);
        require_positive(reader, parsed.throw_radius,
                         pointer_member(path, "throw_radius"), owner);
        output.attachment = std::move(parsed);
    }
    read_quantity_member(reader, value, "phase", path, QuantityDimension::angle,
                         output.phase, owner);
}

void parse_connecting_rod(DocumentReader &reader, JsonValue value,
                          std::string_view path, ConnectingRodDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "length", "mass", "moment_of_inertia",
                           "center_of_mass_from_crank_pin"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("connecting_rod", output.id.value);
    read_quantity_member(reader, value, "length", path, QuantityDimension::length,
                         output.length, owner);
    read_quantity_member(reader, value, "mass", path, QuantityDimension::mass,
                         output.mass, owner);
    read_quantity_member(reader, value, "moment_of_inertia", path,
                         QuantityDimension::moment_of_inertia, output.moment_of_inertia,
                         owner);
    const auto center_of_mass = reader.optional(value, "center_of_mass_from_crank_pin");
    if (center_of_mass.valid() && !center_of_mass.is_null()) {
        Quantity parsed;
        reader.quantity(center_of_mass,
                        pointer_member(path, "center_of_mass_from_crank_pin"),
                        QuantityDimension::length, parsed, owner);
        require_nonnegative(reader, parsed,
                            pointer_member(path, "center_of_mass_from_crank_pin"),
                            owner);
        output.center_of_mass_from_crank_pin = std::move(parsed);
    }
    require_positive(reader, output.length, pointer_member(path, "length"), owner);
    require_nonnegative(reader, output.mass, pointer_member(path, "mass"), owner);
    require_nonnegative(reader, output.moment_of_inertia,
                        pointer_member(path, "moment_of_inertia"), owner);
}

void parse_piston(DocumentReader &reader, JsonValue value, std::string_view path,
                  PistonDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "mass", "compression_height", "wrist_pin_position",
                           "displacement_volume", "blowby"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("piston", output.id.value);
    read_quantity_member(reader, value, "mass", path, QuantityDimension::mass,
                         output.mass, owner);
    read_quantity_member(reader, value, "compression_height", path,
                         QuantityDimension::length, output.compression_height, owner);
    const auto wrist_pin = reader.optional(value, "wrist_pin_position");
    if (wrist_pin.valid() && !wrist_pin.is_null()) {
        Quantity parsed;
        reader.quantity(wrist_pin, pointer_member(path, "wrist_pin_position"),
                        QuantityDimension::length, parsed, owner);
        require_nonnegative(reader, parsed, pointer_member(path, "wrist_pin_position"),
                            owner);
        output.wrist_pin_position = std::move(parsed);
    }
    read_quantity_member(reader, value, "displacement_volume", path,
                         QuantityDimension::volume, output.displacement_volume, owner);
    const auto blowby = reader.optional(value, "blowby");
    if (blowby.valid() && !blowby.is_null()) {
        FlowRestriction parsed;
        parse_flow_restriction(reader, blowby, pointer_member(path, "blowby"), parsed,
                               owner);
        output.blowby = std::move(parsed);
    }
    require_nonnegative(reader, output.mass, pointer_member(path, "mass"), owner);
    require_nonnegative(reader, output.compression_height,
                        pointer_member(path, "compression_height"), owner);
}

void parse_bank(DocumentReader &reader, JsonValue value, std::string_view path,
                BankDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "angle", "bore", "deck_height", "head"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("bank", output.id.value);
    read_quantity_member(reader, value, "angle", path, QuantityDimension::angle,
                         output.angle, owner);
    read_quantity_member(reader, value, "bore", path, QuantityDimension::length,
                         output.bore, owner);
    read_quantity_member(reader, value, "deck_height", path, QuantityDimension::length,
                         output.deck_height, owner);
    read_ref_member(reader, value, "head", path, output.head, owner);
    require_positive(reader, output.bore, pointer_member(path, "bore"), owner);
    require_positive(reader, output.deck_height, pointer_member(path, "deck_height"),
                     owner);
}

void parse_cylinder(DocumentReader &reader, JsonValue value, std::string_view path,
                    CylinderDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"id", "bank", "journal", "connecting_rod",
         "piston", "intake", "exhaust", "ignition_wire", "intake_port", "exhaust_port",
         "exhaust_header_primary_length"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("cylinder", output.id.value);
    read_ref_member(reader, value, "bank", path, output.bank, owner);
    read_ref_member(reader, value, "journal", path, output.journal, owner);
    read_ref_member(reader, value, "connecting_rod", path, output.connecting_rod,
                    owner);
    read_ref_member(reader, value, "piston", path, output.piston, owner);
    read_ref_member(reader, value, "intake", path, output.intake, owner);
    read_ref_member(reader, value, "exhaust", path, output.exhaust, owner);
    read_ref_member(reader, value, "ignition_wire", path, output.ignition_wire, owner);
    read_ref_member(reader, value, "intake_port", path, output.intake_port, owner);
    read_ref_member(reader, value, "exhaust_port", path, output.exhaust_port, owner);
    read_quantity_member(reader, value, "exhaust_header_primary_length", path,
                         QuantityDimension::length,
                         output.exhaust_header_primary_length, owner);
    require_nonnegative(reader, output.exhaust_header_primary_length,
                        pointer_member(path, "exhaust_header_primary_length"), owner);
}

} // namespace crankwave::authoring::detail
