#include "identity/simulation_request_identity_writer.hpp"

#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace engine_sim_offline::identity::detail {
namespace {

template <class Range, class WriteElement>
[[nodiscard]] bool write_array(CanonicalJsonWriter &writer, const Range &values,
                               WriteElement write_element) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &value : values) {
        if (!write_element(writer, value)) {
            return false;
        }
    }
    return writer.end_array();
}

template <class Id>
[[nodiscard]] bool write_stable_id(CanonicalJsonWriter &writer, Id id) {
    if (!id.valid()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "simulation input contains an invalid stable ID");
    }
    return writer.uint32_value(id.value);
}

template <class Id>
[[nodiscard]] bool write_optional_stable_id(CanonicalJsonWriter &writer,
                                            const std::optional<Id> &id) {
    return id.has_value() ? write_stable_id(writer, *id) : writer.null_value();
}

template <class T, class WriteValue>
[[nodiscard]] bool
write_optional_resolved(CanonicalJsonWriter &writer,
                        const std::optional<contract::ResolvedValue<T>> &value,
                        WriteValue write_value) {
    return value.has_value() ? write_resolved(writer, *value, write_value)
                             : writer.null_value();
}

[[nodiscard]] bool write_string(CanonicalJsonWriter &writer, const std::string &value) {
    return writer.string_value(value);
}

[[nodiscard]] bool write_f64(CanonicalJsonWriter &writer, double value) {
    return writer.binary64_bits_value(value);
}

[[nodiscard]] bool write_u32(CanonicalJsonWriter &writer, std::uint32_t value) {
    return writer.uint32_value(value);
}

[[nodiscard]] bool write_u64(CanonicalJsonWriter &writer, std::uint64_t value) {
    return writer.uint64_hex_value(value);
}

[[nodiscard]] bool
write_starter_capability_type(CanonicalJsonWriter &writer,
                              contract::StarterCapabilityType value) {
    switch (value) {
    case contract::StarterCapabilityType::mechanically_disengaged:
        return writer.string_value("mechanically_disengaged");
    case contract::StarterCapabilityType::cranking:
        return writer.string_value("cranking");
    case contract::StarterCapabilityType::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "starter capability type is unspecified or unknown");
}

[[nodiscard]] bool write_sha256(CanonicalJsonWriter &writer,
                                const contract::Sha256Digest &value) {
    return writer.sha256_value(value);
}

[[nodiscard]] bool write_engine_cycle(CanonicalJsonWriter &writer,
                                      contract::EngineCycle value) {
    switch (value) {
    case contract::EngineCycle::four_stroke:
        return writer.string_value("four_stroke");
    case contract::EngineCycle::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "engine cycle is unspecified or unknown");
}

[[nodiscard]] bool write_ignition_kind(CanonicalJsonWriter &writer,
                                       contract::IgnitionKind value) {
    switch (value) {
    case contract::IgnitionKind::spark_ignition:
        return writer.string_value("spark_ignition");
    case contract::IgnitionKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "ignition kind is unspecified or unknown");
}

[[nodiscard]] bool write_cylinder_layout(CanonicalJsonWriter &writer,
                                         contract::CylinderLayoutKind value) {
    switch (value) {
    case contract::CylinderLayoutKind::inline_engine:
        return writer.string_value("inline_engine");
    case contract::CylinderLayoutKind::vee_engine:
        return writer.string_value("vee_engine");
    case contract::CylinderLayoutKind::flat_engine:
        return writer.string_value("flat_engine");
    case contract::CylinderLayoutKind::other:
        return writer.string_value("other");
    case contract::CylinderLayoutKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "cylinder layout is unspecified or unknown");
}

[[nodiscard]] bool write_port_kind(CanonicalJsonWriter &writer,
                                   contract::PortKind value) {
    switch (value) {
    case contract::PortKind::intake:
        return writer.string_value("intake");
    case contract::PortKind::exhaust:
        return writer.string_value("exhaust");
    case contract::PortKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "port kind is unspecified or unknown");
}

[[nodiscard]] bool write_gas_volume_kind(CanonicalJsonWriter &writer,
                                         contract::GasVolumeKind value) {
    switch (value) {
    case contract::GasVolumeKind::atmosphere:
        return writer.string_value("atmosphere");
    case contract::GasVolumeKind::intake_plenum:
        return writer.string_value("intake_plenum");
    case contract::GasVolumeKind::intake_runner:
        return writer.string_value("intake_runner");
    case contract::GasVolumeKind::cylinder:
        return writer.string_value("cylinder");
    case contract::GasVolumeKind::exhaust_primary:
        return writer.string_value("exhaust_primary");
    case contract::GasVolumeKind::exhaust_collector:
        return writer.string_value("exhaust_collector");
    case contract::GasVolumeKind::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "gas-volume kind is unspecified or unknown");
}

[[nodiscard]] bool write_source_route_kind(CanonicalJsonWriter &writer,
                                           contract::SourceRouteKind value) {
    switch (value) {
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
                       "source-route kind is unspecified or unknown");
}

[[nodiscard]] bool
write_restriction_calibration(CanonicalJsonWriter &writer,
                              contract::LegacyRestrictionCalibration value) {
    switch (value) {
    case contract::LegacyRestrictionCalibration::carb_at_1p5_inhg:
        return writer.string_value("carb_at_1p5_inhg");
    case contract::LegacyRestrictionCalibration::cfm_at_28_inh2o:
        return writer.string_value("cfm_at_28_inh2o");
    case contract::LegacyRestrictionCalibration::unspecified:
        break;
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "legacy restriction calibration is unspecified or unknown");
}

template <class Id>
[[nodiscard]] bool write_stable_id_array(CanonicalJsonWriter &writer,
                                         const std::vector<Id> &values) {
    return write_array(writer, values, [](CanonicalJsonWriter &output, Id value) {
        return write_stable_id(output, value);
    });
}

[[nodiscard]] bool write_bank(CanonicalJsonWriter &writer,
                              const contract::BankSpec &bank) {
    if (!(writer.begin_object() && writer.key("id") &&
          write_stable_id(writer, bank.id) && writer.key("semantic_id") &&
          write_resolved(writer, bank.semantic_id, write_string))) {
        return false;
    }
    if (bank.angle_rad.has_value() &&
        !(writer.key("angle_rad") &&
          write_resolved(writer, *bank.angle_rad, write_f64))) {
        return false;
    }
    return writer.end_object();
}

[[nodiscard]] bool write_crankshaft(CanonicalJsonWriter &writer,
                                    const contract::CrankshaftSpec &crankshaft) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, crankshaft.id) && writer.key("semantic_id") &&
           write_resolved(writer, crankshaft.semantic_id, write_string) &&
           writer.end_object();
}

[[nodiscard]] bool write_intake(CanonicalJsonWriter &writer,
                                const contract::IntakeSpec &intake) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, intake.id) && writer.key("semantic_id") &&
           write_resolved(writer, intake.semantic_id, write_string) &&
           writer.end_object();
}

[[nodiscard]] bool write_cylinder(CanonicalJsonWriter &writer,
                                  const contract::CylinderSpec &cylinder) {
    if (!(writer.begin_object() && writer.key("id") &&
          write_stable_id(writer, cylinder.id) && writer.key("semantic_id") &&
          write_resolved(writer, cylinder.semantic_id, write_string) &&
          writer.key("bank_id") && write_stable_id(writer, cylinder.bank_id) &&
          writer.key("crankshaft_id") &&
          write_stable_id(writer, cylinder.crankshaft_id) && writer.key("intake_id") &&
          write_stable_id(writer, cylinder.intake_id) && writer.key("bore_m") &&
          write_resolved(writer, cylinder.bore_m, write_f64) &&
          writer.key("stroke_m") &&
          write_resolved(writer, cylinder.stroke_m, write_f64) &&
          writer.key("connecting_rod_length_m") &&
          write_resolved(writer, cylinder.connecting_rod_length_m, write_f64) &&
          writer.key("compression_ratio") &&
          write_resolved(writer, cylinder.compression_ratio, write_f64) &&
          writer.key("firing_tdc_offset_rad") &&
          write_resolved(writer, cylinder.firing_tdc_offset_rad, write_f64) &&
          writer.key("journal_phase_rad") &&
          write_resolved(writer, cylinder.journal_phase_rad, write_f64))) {
        return false;
    }
    if (cylinder.master_rod_attachment.has_value()) {
        const auto &attachment = *cylinder.master_rod_attachment;
        if (!(writer.key("master_rod_attachment") && writer.begin_object() &&
              writer.key("master_cylinder_id") &&
              write_stable_id(writer, attachment.master_cylinder_id) &&
              writer.key("throw_radius_m") &&
              write_resolved(writer, attachment.throw_radius_m, write_f64) &&
              writer.end_object())) {
            return false;
        }
    }
    if (cylinder.shared_ignition_wire_semantic_id.has_value() &&
        !(writer.key("shared_ignition_wire_semantic_id") &&
          write_resolved(writer, *cylinder.shared_ignition_wire_semantic_id,
                         write_string))) {
        return false;
    }
    return writer.end_object();
}

[[nodiscard]] bool write_port(CanonicalJsonWriter &writer,
                              const contract::PortSpec &port) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, port.id) && writer.key("semantic_id") &&
           write_resolved(writer, port.semantic_id, write_string) &&
           writer.key("cylinder_id") && write_stable_id(writer, port.cylinder_id) &&
           writer.key("kind") && write_resolved(writer, port.kind, write_port_kind) &&
           writer.end_object();
}

[[nodiscard]] bool write_gas_volume(CanonicalJsonWriter &writer,
                                    const contract::GasVolumeSpec &volume) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, volume.id) && writer.key("semantic_id") &&
           write_resolved(writer, volume.semantic_id, write_string) &&
           writer.key("kind") &&
           write_resolved(writer, volume.kind, write_gas_volume_kind) &&
           writer.end_object();
}

[[nodiscard]] bool write_flow_edge(CanonicalJsonWriter &writer,
                                   const contract::FlowEdgeSpec &edge) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, edge.id) && writer.key("semantic_id") &&
           write_resolved(writer, edge.semantic_id, write_string) &&
           writer.key("endpoint_0_volume_id") &&
           write_stable_id(writer, edge.endpoint_0_volume_id) &&
           writer.key("endpoint_1_volume_id") &&
           write_stable_id(writer, edge.endpoint_1_volume_id) && writer.end_object();
}

[[nodiscard]] bool write_route(CanonicalJsonWriter &writer,
                               const contract::RouteSpec &route) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, route.id) && writer.key("semantic_id") &&
           write_resolved(writer, route.semantic_id, write_string) &&
           writer.key("kind") &&
           write_resolved(writer, route.kind, write_source_route_kind) &&
           writer.key("source_volume_id") &&
           write_optional_stable_id(writer, route.source_volume_id) &&
           writer.key("default_parent_route_id") &&
           write_optional_stable_id(writer, route.default_parent_route_id) &&
           writer.key("emitter_anchor_id") &&
           write_optional_resolved(writer, route.emitter_anchor_id, write_string) &&
           writer.end_object();
}

[[nodiscard]] bool
write_model_methods(CanonicalJsonWriter &writer,
                    const contract::EngineSpec::ModelMethods &methods) {
    return writer.begin_object() && writer.key("mechanism") &&
           write_resolved(writer, methods.mechanism, write_method_identity) &&
           writer.key("valvetrain") &&
           write_resolved(writer, methods.valvetrain, write_method_identity) &&
           writer.key("gas_exchange") &&
           write_resolved(writer, methods.gas_exchange, write_method_identity) &&
           writer.key("ignition") &&
           write_resolved(writer, methods.ignition, write_method_identity) &&
           writer.key("combustion") &&
           write_resolved(writer, methods.combustion, write_method_identity) &&
           writer.key("heat_transfer") &&
           write_resolved(writer, methods.heat_transfer, write_method_identity) &&
           writer.key("losses") &&
           write_resolved(writer, methods.losses, write_method_identity) &&
           writer.key("excitation") &&
           write_resolved(writer, methods.excitation, write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_restriction(CanonicalJsonWriter &writer,
                         const contract::LegacyRestriction &restriction);

[[nodiscard]] bool
write_legacy_cylinder_topology(CanonicalJsonWriter &writer,
                               const contract::LegacyCylinderTopology &topology) {
    return writer.begin_object() && writer.key("cylinder_id") &&
           write_stable_id(writer, topology.cylinder_id) &&
           writer.key("crankshaft_id") &&
           write_stable_id(writer, topology.crankshaft_id) && writer.key("intake_id") &&
           write_stable_id(writer, topology.intake_id) &&
           writer.key("intake_port_id") &&
           write_stable_id(writer, topology.intake_port_id) &&
           writer.key("exhaust_port_id") &&
           write_stable_id(writer, topology.exhaust_port_id) &&
           writer.key("intake_runner_volume_id") &&
           write_stable_id(writer, topology.intake_runner_volume_id) &&
           writer.key("chamber_volume_id") &&
           write_stable_id(writer, topology.chamber_volume_id) &&
           writer.key("exhaust_primary_volume_id") &&
           write_stable_id(writer, topology.exhaust_primary_volume_id) &&
           writer.key("plenum_to_runner_edge_id") &&
           write_stable_id(writer, topology.plenum_to_runner_edge_id) &&
           writer.key("intake_valve_edge_id") &&
           write_stable_id(writer, topology.intake_valve_edge_id) &&
           writer.key("exhaust_valve_edge_id") &&
           write_stable_id(writer, topology.exhaust_valve_edge_id) &&
           writer.key("primary_to_collector_edge_id") &&
           write_stable_id(writer, topology.primary_to_collector_edge_id) &&
           writer.key("blowby_edge_id") &&
           write_stable_id(writer, topology.blowby_edge_id) &&
           writer.key("exhaust_route_id") &&
           write_stable_id(writer, topology.exhaust_route_id) && writer.end_object();
}

[[nodiscard]] bool
write_legacy_cylinder_parameters(CanonicalJsonWriter &writer,
                                 const contract::LegacyCylinderAssembly &cylinder) {
    const auto &parameters = cylinder.parameters;
    const auto *direct =
        std::get_if<contract::LegacyDirectJournalKinematics>(&cylinder.kinematics);
    const auto *master =
        std::get_if<contract::LegacyMasterRodJournalKinematics>(&cylinder.kinematics);
    if (direct == nullptr && master == nullptr) {
        return false;
    }
    if (!(writer.begin_object() && writer.key("bore_m") &&
          write_resolved(writer, parameters.bore_m, write_f64))) {
        return false;
    }
    if (direct != nullptr &&
        !(writer.key("stroke_m") &&
          write_resolved(writer, direct->stroke_m, write_f64) &&
          writer.key("crank_radius_m") &&
          write_resolved(writer, direct->crank_radius_m, write_f64))) {
        return false;
    }
    if (!(writer.key("connecting_rod_length_m") &&
          write_resolved(writer, parameters.connecting_rod_length_m, write_f64) &&
          writer.key("connecting_rod_center_of_mass_from_crank_pin_m") &&
          write_resolved(writer,
                         parameters.connecting_rod_center_of_mass_from_crank_pin_m,
                         write_f64) &&
          writer.key("deck_height_m") &&
          write_resolved(writer, parameters.deck_height_m, write_f64) &&
          writer.key("piston_compression_height_m") &&
          write_resolved(writer, parameters.piston_compression_height_m, write_f64) &&
          writer.key("piston_wrist_pin_position_m") &&
          write_resolved(writer, parameters.piston_wrist_pin_position_m, write_f64) &&
          writer.key("piston_displacement_term_m3") &&
          write_resolved(writer, parameters.piston_displacement_term_m3, write_f64) &&
          writer.key("piston_mass_kg") &&
          write_resolved(writer, parameters.piston_mass_kg, write_f64) &&
          writer.key("connecting_rod_mass_kg") &&
          write_resolved(writer, parameters.connecting_rod_mass_kg, write_f64) &&
          writer.key("connecting_rod_inertia_kg_m2") &&
          write_resolved(writer, parameters.connecting_rod_inertia_kg_m2, write_f64))) {
        return false;
    }
    if (direct != nullptr &&
        !(writer.key("journal_angle_rad") &&
          write_resolved(writer, direct->journal_angle_rad, write_f64))) {
        return false;
    }
    return writer.key("ignition_wire_angle_rad") &&
           write_resolved(writer, parameters.ignition_wire_angle_rad, write_f64) &&
           writer.key("header_primary_length_m") &&
           write_resolved(writer, parameters.header_primary_length_m, write_f64) &&
           writer.key("piston_blowby") &&
           write_legacy_restriction(writer, parameters.piston_blowby) &&
           writer.end_object();
}

[[nodiscard]] bool write_master_rod_kinematics(
    CanonicalJsonWriter &writer,
    const contract::LegacyMasterRodJournalKinematics &kinematics) {
    return writer.begin_object() && writer.key("type") &&
           writer.string_value("master_rod") && writer.key("master_cylinder_id") &&
           write_stable_id(writer, kinematics.master_cylinder_id) &&
           writer.key("throw_radius_m") &&
           write_resolved(writer, kinematics.throw_radius_m, write_f64) &&
           writer.key("master_local_phase_rad") &&
           write_resolved(writer, kinematics.master_local_phase_rad, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_cylinder_assembly(CanonicalJsonWriter &writer,
                               const contract::LegacyCylinderAssembly &cylinder) {
    if (!(writer.begin_object() && writer.key("topology") &&
          write_legacy_cylinder_topology(writer, cylinder.topology) &&
          writer.key("parameters") &&
          write_legacy_cylinder_parameters(writer, cylinder))) {
        return false;
    }
    if (const auto *master = std::get_if<contract::LegacyMasterRodJournalKinematics>(
            &cylinder.kinematics)) {
        if (!(writer.key("kinematics") &&
              write_master_rod_kinematics(writer, *master))) {
            return false;
        }
    }
    return writer.end_object();
}

[[nodiscard]] bool write_legacy_crank(CanonicalJsonWriter &writer,
                                      const contract::LegacyCrankAssembly &crank) {
    return writer.begin_object() && writer.key("crankshaft_id") &&
           write_stable_id(writer, crank.crankshaft_id) &&
           writer.key("crank_tdc_reference_rad") &&
           write_resolved(writer, crank.crank_tdc_reference_rad, write_f64) &&
           writer.key("crankshaft_mass_kg") &&
           write_resolved(writer, crank.crankshaft_mass_kg, write_f64) &&
           writer.key("flywheel_mass_kg") &&
           write_resolved(writer, crank.flywheel_mass_kg, write_f64) &&
           writer.key("authored_crank_inertia_kg_m2") &&
           write_resolved(writer, crank.authored_crank_inertia_kg_m2, write_f64) &&
           writer.key("running_friction_torque_magnitude_nm") &&
           write_resolved(writer, crank.running_friction_torque_magnitude_nm,
                          write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_mechanism(CanonicalJsonWriter &writer,
                       const contract::LegacyMechanismProfile &mechanism) {
    return writer.begin_object() && writer.key("output_crankshaft_id") &&
           write_stable_id(writer, mechanism.output_crankshaft_id) &&
           writer.key("cranks") &&
           write_array(writer, mechanism.cranks,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyCrankAssembly &crank) {
                           return write_legacy_crank(output, crank);
                       }) &&
           writer.key("cylinders") &&
           write_array(writer, mechanism.cylinders,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyCylinderAssembly &cylinder) {
                           return write_legacy_cylinder_assembly(output, cylinder);
                       }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_restriction(CanonicalJsonWriter &writer,
                         const contract::LegacyRestriction &restriction) {
    return writer.begin_object() && writer.key("calibration") &&
           write_resolved(writer, restriction.calibration,
                          write_restriction_calibration) &&
           writer.key("source_rating") &&
           write_resolved(writer, restriction.source_rating, write_f64) &&
           writer.key("resolved_k") &&
           write_resolved(writer, restriction.resolved_k, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_intake_topology(CanonicalJsonWriter &writer,
                             const contract::LegacyIntakeTopology &topology) {
    return writer.begin_object() && writer.key("intake_id") &&
           write_stable_id(writer, topology.intake_id) &&
           writer.key("plenum_volume_id") &&
           write_stable_id(writer, topology.plenum_volume_id) &&
           writer.key("main_throttle_edge_id") &&
           write_stable_id(writer, topology.main_throttle_edge_id) &&
           writer.key("idle_bypass_edge_id") &&
           write_stable_id(writer, topology.idle_bypass_edge_id) && writer.end_object();
}

[[nodiscard]] bool write_legacy_intake(CanonicalJsonWriter &writer,
                                       const contract::LegacyIntakeParameters &intake) {
    return writer.begin_object() && writer.key("plenum_volume_m3") &&
           write_resolved(writer, intake.plenum_volume_m3, write_f64) &&
           writer.key("plenum_cross_section_area_m2") &&
           write_resolved(writer, intake.plenum_cross_section_area_m2, write_f64) &&
           writer.key("runner_length_m") &&
           write_resolved(writer, intake.runner_length_m, write_f64) &&
           writer.key("velocity_decay") &&
           write_resolved(writer, intake.velocity_decay, write_f64) &&
           writer.key("idle_throttle_plate_position_01") &&
           write_resolved(writer, intake.idle_throttle_plate_position_01, write_f64) &&
           writer.key("main_mixture_lambda") &&
           write_resolved(writer, intake.main_mixture_lambda, write_f64) &&
           writer.key("main_throttle") &&
           write_legacy_restriction(writer, intake.main_throttle) &&
           writer.key("idle_bypass") &&
           write_legacy_restriction(writer, intake.idle_bypass) &&
           writer.key("plenum_to_runner") &&
           write_legacy_restriction(writer, intake.plenum_to_runner) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_intake_profile(CanonicalJsonWriter &writer,
                            const contract::LegacyIntakeProfile &intake) {
    return writer.begin_object() && writer.key("topology") &&
           write_legacy_intake_topology(writer, intake.topology) &&
           writer.key("parameters") && write_legacy_intake(writer, intake.parameters) &&
           writer.end_object();
}

[[nodiscard]] bool
write_throttle_controller(CanonicalJsonWriter &writer,
                          const contract::ThrottleControllerV1 &controller) {
    return std::visit(
        [&](const auto &value) {
            if constexpr (requires { value.minimum_engine_speed_rad_s; }) {
                return writer.begin_object() && writer.key("kind") &&
                       writer.string_value("governor") && writer.key("value") &&
                       writer.begin_object() &&
                       writer.key("minimum_engine_speed_rad_s") &&
                       write_resolved(writer, value.minimum_engine_speed_rad_s,
                                      write_f64) &&
                       writer.key("maximum_engine_speed_rad_s") &&
                       write_resolved(writer, value.maximum_engine_speed_rad_s,
                                      write_f64) &&
                       writer.key("minimum_velocity_per_s") &&
                       write_resolved(writer, value.minimum_velocity_per_s,
                                      write_f64) &&
                       writer.key("maximum_velocity_per_s") &&
                       write_resolved(writer, value.maximum_velocity_per_s,
                                      write_f64) &&
                       writer.key("k_s") &&
                       write_resolved(writer, value.k_s, write_f64) &&
                       writer.key("k_d_per_s") &&
                       write_resolved(writer, value.k_d_per_s, write_f64) &&
                       writer.key("gamma") &&
                       write_resolved(writer, value.gamma, write_f64) &&
                       writer.end_object() && writer.end_object();
            } else {
                return writer.begin_object() && writer.key("kind") &&
                       writer.string_value("direct") && writer.key("value") &&
                       writer.begin_object() && writer.key("gamma") &&
                       write_resolved(writer, value.gamma, write_f64) &&
                       writer.end_object() && writer.end_object();
            }
        },
        controller);
}

[[nodiscard]] bool
write_legacy_valve_flow_point(CanonicalJsonWriter &writer,
                              const contract::LegacyValveFlowPoint &point) {
    return writer.begin_object() && writer.key("sample_id") &&
           write_resolved(writer, point.sample_id, write_string) &&
           writer.key("lift_m") && write_resolved(writer, point.lift_m, write_f64) &&
           writer.key("source_cfm_at_28_inh2o") &&
           write_resolved(writer, point.source_cfm_at_28_inh2o, write_f64) &&
           writer.key("resolved_k") &&
           write_resolved(writer, point.resolved_k, write_f64) && writer.end_object();
}

[[nodiscard]] bool write_legacy_head(CanonicalJsonWriter &writer,
                                     const contract::LegacyBankHeadProfile &head) {
    const auto write_flow_points = [](CanonicalJsonWriter &output, const auto &points) {
        return write_array(output, points,
                           [](CanonicalJsonWriter &array_writer,
                              const contract::LegacyValveFlowPoint &point) {
                               return write_legacy_valve_flow_point(array_writer,
                                                                    point);
                           });
    };
    return writer.begin_object() && writer.key("bank_id") &&
           write_stable_id(writer, head.bank_id) && writer.key("chamber_volume_m3") &&
           write_resolved(writer, head.chamber_volume_m3, write_f64) &&
           writer.key("intake_runner_base_volume_m3") &&
           write_resolved(writer, head.intake_runner_base_volume_m3, write_f64) &&
           writer.key("intake_runner_cross_section_area_m2") &&
           write_resolved(writer, head.intake_runner_cross_section_area_m2,
                          write_f64) &&
           writer.key("exhaust_runner_base_volume_m3") &&
           write_resolved(writer, head.exhaust_runner_base_volume_m3, write_f64) &&
           writer.key("exhaust_runner_cross_section_area_m2") &&
           write_resolved(writer, head.exhaust_runner_cross_section_area_m2,
                          write_f64) &&
           writer.key("intake_flow_triangle_radius_m") &&
           write_resolved(writer, head.intake_flow_triangle_radius_m, write_f64) &&
           writer.key("exhaust_flow_triangle_radius_m") &&
           write_resolved(writer, head.exhaust_flow_triangle_radius_m, write_f64) &&
           writer.key("intake_flow") && write_flow_points(writer, head.intake_flow) &&
           writer.key("exhaust_flow") && write_flow_points(writer, head.exhaust_flow) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_exhaust_topology(CanonicalJsonWriter &writer,
                              const contract::LegacyExhaustRouteTopology &topology) {
    return writer.begin_object() && writer.key("route_id") &&
           write_stable_id(writer, topology.route_id) &&
           writer.key("collector_volume_id") &&
           write_stable_id(writer, topology.collector_volume_id) &&
           writer.key("collector_outlet_edge_id") &&
           write_stable_id(writer, topology.collector_outlet_edge_id) &&
           writer.end_object();
}

[[nodiscard]] bool write_legacy_exhaust_parameters(
    CanonicalJsonWriter &writer,
    const contract::LegacyExhaustRouteParameters &parameters) {
    return writer.begin_object() && writer.key("collector_volume_m3") &&
           write_resolved(writer, parameters.collector_volume_m3, write_f64) &&
           writer.key("collector_cross_section_area_m2") &&
           write_resolved(writer, parameters.collector_cross_section_area_m2,
                          write_f64) &&
           writer.key("exhaust_system_length_m") &&
           write_resolved(writer, parameters.exhaust_system_length_m, write_f64) &&
           writer.key("primary_tube_length_m") &&
           write_resolved(writer, parameters.primary_tube_length_m, write_f64) &&
           writer.key("velocity_decay") &&
           write_resolved(writer, parameters.velocity_decay, write_f64) &&
           writer.key("audio_volume_linear") &&
           write_resolved(writer, parameters.audio_volume_linear, write_f64) &&
           writer.key("primary_to_collector") &&
           write_legacy_restriction(writer, parameters.primary_to_collector) &&
           writer.key("collector_outlet") &&
           write_legacy_restriction(writer, parameters.collector_outlet) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_exhaust_route(CanonicalJsonWriter &writer,
                           const contract::LegacyExhaustRouteProfile &route) {
    return writer.begin_object() && writer.key("topology") &&
           write_legacy_exhaust_topology(writer, route.topology) &&
           writer.key("parameters") &&
           write_legacy_exhaust_parameters(writer, route.parameters) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_gas_path(CanonicalJsonWriter &writer,
                      const contract::LegacyGasPathProfile &gas_path) {
    return writer.begin_object() && writer.key("intakes") &&
           write_array(writer, gas_path.intakes,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyIntakeProfile &intake) {
                           return write_legacy_intake_profile(output, intake);
                       }) &&
           writer.key("heads") &&
           write_array(writer, gas_path.heads,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyBankHeadProfile &head) {
                           return write_legacy_head(output, head);
                       }) &&
           writer.key("exhaust_routes") &&
           write_array(writer, gas_path.exhaust_routes,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyExhaustRouteProfile &route) {
                           return write_legacy_exhaust_route(output, route);
                       }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_harmonic_cam_shape(CanonicalJsonWriter &writer,
                                const contract::LegacyHarmonicCamShape &shape) {
    return writer.begin_object() && writer.key("maximum_lift_m") &&
           write_resolved(writer, shape.maximum_lift_m, write_f64) &&
           writer.key("duration_at_reference_lift_rad") &&
           write_resolved(writer, shape.duration_at_reference_lift_rad, write_f64) &&
           writer.key("exponent") &&
           write_resolved(writer, shape.exponent, write_f64) &&
           writer.key("construction_steps") &&
           write_resolved(writer, shape.construction_steps, write_u32) &&
           writer.key("advance_rad") &&
           write_resolved(writer, shape.advance_rad, write_f64) &&
           writer.key("base_radius_m") &&
           write_resolved(writer, shape.base_radius_m, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_sampled_cam_point(CanonicalJsonWriter &writer,
                               const contract::LegacySampledCamPoint &point) {
    return writer.begin_object() && writer.key("sample_id") &&
           write_resolved(writer, point.sample_id, write_string) &&
           writer.key("angle_rad") &&
           write_resolved(writer, point.angle_rad, write_f64) && writer.key("lift_m") &&
           write_resolved(writer, point.lift_m, write_f64) && writer.end_object();
}

[[nodiscard]] bool
write_legacy_sampled_cam_shape(CanonicalJsonWriter &writer,
                               const contract::LegacySampledCamShape &shape) {
    return writer.begin_object() && writer.key("kind") &&
           writer.string_value("sampled") && writer.key("triangle_radius_rad") &&
           write_resolved(writer, shape.triangle_radius_rad, write_f64) &&
           writer.key("samples") &&
           write_array(writer, shape.samples,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacySampledCamPoint &point) {
                           return write_legacy_sampled_cam_point(output, point);
                       }) &&
           writer.key("advance_rad") &&
           write_resolved(writer, shape.advance_rad, write_f64) &&
           writer.key("base_radius_m") &&
           write_resolved(writer, shape.base_radius_m, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_legacy_cam_shape(CanonicalJsonWriter &writer,
                                          const contract::LegacyCamShape &shape) {
    if (shape.valueless_by_exception()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "legacy cam-shape variant is valueless");
    }
    return std::visit(
        [&](const auto &resolved_shape) {
            using Shape = std::decay_t<decltype(resolved_shape)>;
            if constexpr (std::is_same_v<Shape, contract::LegacyHarmonicCamShape>) {
                return write_legacy_harmonic_cam_shape(writer, resolved_shape);
            } else {
                return write_legacy_sampled_cam_shape(writer, resolved_shape);
            }
        },
        shape);
}

[[nodiscard]] bool write_legacy_cam_lobe(CanonicalJsonWriter &writer,
                                         const contract::LegacyCamLobe &lobe) {
    return writer.begin_object() && writer.key("cylinder_id") &&
           write_stable_id(writer, lobe.cylinder_id) && writer.key("port_id") &&
           write_stable_id(writer, lobe.port_id) && writer.key("profile_index") &&
           writer.uint32_value(lobe.profile_index) && writer.key("crank_center_rad") &&
           write_resolved(writer, lobe.crank_center_rad, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_camshaft(CanonicalJsonWriter &writer,
                      const contract::LegacyCamshaftProfile &camshaft) {
    return writer.begin_object() && writer.key("profiles") &&
           write_array(
               writer, camshaft.profiles,
               [](CanonicalJsonWriter &output, const contract::LegacyCamShape &shape) {
                   return write_legacy_cam_shape(output, shape);
               }) &&
           writer.key("lobes") &&
           write_array(
               writer, camshaft.lobes,
               [](CanonicalJsonWriter &output, const contract::LegacyCamLobe &lobe) {
                   return write_legacy_cam_lobe(output, lobe);
               }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_vtec_activation(CanonicalJsonWriter &writer,
                             const contract::LegacyVtecActivationProfile &activation) {
    return writer.begin_object() && writer.key("minimum_engine_speed_rad_s") &&
           write_resolved(writer, activation.minimum_engine_speed_rad_s, write_f64) &&
           writer.key("minimum_mean_manifold_pressure_pa_abs") &&
           write_resolved(writer, activation.minimum_mean_manifold_pressure_pa_abs,
                          write_f64) &&
           writer.key("minimum_throttle_linkage_opening_01") &&
           write_resolved(writer, activation.minimum_throttle_linkage_opening_01,
                          write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_vtec_alternate(CanonicalJsonWriter &writer,
                            const contract::LegacyVtecAlternateCamProfile &alternate) {
    return writer.begin_object() && writer.key("intake") &&
           write_legacy_camshaft(writer, alternate.intake) && writer.key("exhaust") &&
           write_legacy_camshaft(writer, alternate.exhaust) &&
           writer.key("activation") &&
           write_legacy_vtec_activation(writer, alternate.activation) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_valvetrain(CanonicalJsonWriter &writer,
                        const contract::LegacyValvetrainProfile &valvetrain) {
    if (!(writer.begin_object() && writer.key("intake") &&
          write_legacy_camshaft(writer, valvetrain.intake) && writer.key("exhaust") &&
          write_legacy_camshaft(writer, valvetrain.exhaust))) {
        return false;
    }
    if (valvetrain.alternate.has_value() &&
        !(writer.key("alternate") &&
          write_legacy_vtec_alternate(writer, *valvetrain.alternate))) {
        return false;
    }
    return writer.end_object();
}

[[nodiscard]] bool write_legacy_timing_point(CanonicalJsonWriter &writer,
                                             const contract::LegacyTimingPoint &point) {
    return writer.begin_object() && writer.key("sample_id") &&
           write_resolved(writer, point.sample_id, write_string) &&
           writer.key("angular_speed_rad_s") &&
           write_resolved(writer, point.angular_speed_rad_s, write_f64) &&
           writer.key("timing_advance_rad") &&
           write_resolved(writer, point.timing_advance_rad, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_ignition(CanonicalJsonWriter &writer,
                      const contract::LegacyIgnitionProfile &ignition) {
    const auto write_cylinder_ids =
        [](CanonicalJsonWriter &output,
           const std::vector<contract::CylinderId> &cylinders) {
            return write_stable_id_array(output, cylinders);
        };
    return writer.begin_object() && writer.key("firing_order") &&
           write_resolved(writer, ignition.firing_order, write_cylinder_ids) &&
           writer.key("timing_curve_triangle_radius_rad_s") &&
           write_resolved(writer, ignition.timing_curve_triangle_radius_rad_s,
                          write_f64) &&
           writer.key("timing_curve") &&
           write_array(writer, ignition.timing_curve,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyTimingPoint &point) {
                           return write_legacy_timing_point(output, point);
                       }) &&
           writer.key("limiter_speed_rpm") &&
           write_resolved(writer, ignition.limiter_speed_rpm, write_f64) &&
           writer.key("limiter_hold_s") &&
           write_resolved(writer, ignition.limiter_hold_s, write_f64) &&
           writer.key("declared_redline_rpm") &&
           write_resolved(writer, ignition.declared_redline_rpm, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_flame_speed_point(CanonicalJsonWriter &writer,
                               const contract::LegacyFlameSpeedPoint &point) {
    return writer.begin_object() && writer.key("sample_id") &&
           write_resolved(writer, point.sample_id, write_string) &&
           writer.key("turbulence") &&
           write_resolved(writer, point.turbulence, write_f64) &&
           writer.key("flame_speed_ratio") &&
           write_resolved(writer, point.flame_speed_ratio, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_legacy_fuel(CanonicalJsonWriter &writer,
                                     const contract::LegacyFuelProfile &fuel) {
    return writer.begin_object() && writer.key("fuel_id") &&
           write_resolved(writer, fuel.fuel_id, write_string) &&
           writer.key("molecular_mass_kg_per_mol") &&
           write_resolved(writer, fuel.molecular_mass_kg_per_mol, write_f64) &&
           writer.key("energy_density_j_per_kg") &&
           write_resolved(writer, fuel.energy_density_j_per_kg, write_f64) &&
           writer.key("molecular_afr") &&
           write_resolved(writer, fuel.molecular_afr, write_f64) &&
           writer.key("maximum_burning_efficiency_01") &&
           write_resolved(writer, fuel.maximum_burning_efficiency_01, write_f64) &&
           writer.key("burning_efficiency_randomness_01") &&
           write_resolved(writer, fuel.burning_efficiency_randomness_01, write_f64) &&
           writer.key("low_efficiency_attenuation_01") &&
           write_resolved(writer, fuel.low_efficiency_attenuation_01, write_f64) &&
           writer.key("maximum_turbulence_effect") &&
           write_resolved(writer, fuel.maximum_turbulence_effect, write_f64) &&
           writer.key("maximum_dilution_effect") &&
           write_resolved(writer, fuel.maximum_dilution_effect, write_f64) &&
           writer.key("lbv_multiplier") &&
           write_resolved(writer, fuel.lbv_multiplier, write_f64) &&
           writer.key("turbulence_to_flame_speed_ratio_triangle_radius") &&
           write_resolved(writer, fuel.turbulence_to_flame_speed_ratio_triangle_radius,
                          write_f64) &&
           writer.key("turbulence_to_flame_speed_ratio") &&
           write_array(writer, fuel.turbulence_to_flame_speed_ratio,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyFlameSpeedPoint &point) {
                           return write_legacy_flame_speed_point(output, point);
                       }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_pressure_gains(CanonicalJsonWriter &writer,
                            const contract::LegacyExcitationPressureGains &gains) {
    return writer.begin_object() && writer.key("gauge_static") &&
           write_resolved(writer, gains.gauge_static, write_f64) &&
           writer.key("dynamic_forward") &&
           write_resolved(writer, gains.dynamic_forward, write_f64) &&
           writer.key("dynamic_reverse") &&
           write_resolved(writer, gains.dynamic_reverse, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_legacy_excitation_cylinder_path(
    CanonicalJsonWriter &writer, const contract::LegacyExcitationCylinderPath &path) {
    return writer.begin_object() && writer.key("cylinder_id") &&
           write_stable_id(writer, path.cylinder_id) && writer.key("route_id") &&
           write_stable_id(writer, path.route_id) &&
           writer.key("header_primary_length_m") &&
           write_resolved(writer, path.header_primary_length_m, write_f64) &&
           writer.key("sound_attenuation_linear") &&
           write_resolved(writer, path.sound_attenuation_linear, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_excitation_route(CanonicalJsonWriter &writer,
                              const contract::LegacyExcitationRoute &route) {
    return writer.begin_object() && writer.key("route_id") &&
           write_stable_id(writer, route.route_id) &&
           writer.key("exhaust_system_length_m") &&
           write_resolved(writer, route.exhaust_system_length_m, write_f64) &&
           writer.key("audio_volume_linear") &&
           write_resolved(writer, route.audio_volume_linear, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_legacy_excitation(CanonicalJsonWriter &writer,
                        const contract::LegacyReferenceExcitationProfile &excitation) {
    const auto write_cylinder_ids =
        [](CanonicalJsonWriter &output,
           const std::vector<contract::CylinderId> &cylinders) {
            return write_stable_id_array(output, cylinders);
        };
    return writer.begin_object() && writer.key("reference_atmosphere_pa_abs") &&
           write_resolved(writer, excitation.reference_atmosphere_pa_abs, write_f64) &&
           writer.key("legacy_propagation_speed_m_s") &&
           write_resolved(writer, excitation.legacy_propagation_speed_m_s, write_f64) &&
           writer.key("excitation_scale") &&
           write_resolved(writer, excitation.excitation_scale, write_f64) &&
           writer.key("filtered_speed_threshold_rpm") &&
           write_resolved(writer, excitation.filtered_speed_threshold_rpm, write_f64) &&
           writer.key("filtered_speed_exponent") &&
           write_resolved(writer, excitation.filtered_speed_exponent, write_u32) &&
           writer.key("pressure_gains") &&
           write_legacy_pressure_gains(writer, excitation.pressure_gains) &&
           writer.key("cylinder_count_divisor") &&
           write_resolved(writer, excitation.cylinder_count_divisor, write_f64) &&
           writer.key("inverse_length_exponent") &&
           write_resolved(writer, excitation.inverse_length_exponent, write_f64) &&
           writer.key("cylinder_accumulation_order") &&
           write_resolved(writer, excitation.cylinder_accumulation_order,
                          write_cylinder_ids) &&
           writer.key("cylinder_paths") &&
           write_array(writer, excitation.cylinder_paths,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyExcitationCylinderPath &path) {
                           return write_legacy_excitation_cylinder_path(output, path);
                       }) &&
           writer.key("routes") &&
           write_array(writer, excitation.routes,
                       [](CanonicalJsonWriter &output,
                          const contract::LegacyExcitationRoute &route) {
                           return write_legacy_excitation_route(output, route);
                       }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_low_order_engine_core(CanonicalJsonWriter &writer,
                            const contract::LowOrderEngineCoreV1 &core) {
    return writer.begin_object() && writer.key("mechanism") &&
           write_legacy_mechanism(writer, core.mechanism) &&
           writer.key("throttle_controller") &&
           write_throttle_controller(writer, core.throttle_controller) &&
           writer.key("gas_path") && write_legacy_gas_path(writer, core.gas_path) &&
           writer.key("valvetrain") &&
           write_legacy_valvetrain(writer, core.valvetrain) && writer.key("ignition") &&
           write_legacy_ignition(writer, core.ignition) && writer.key("fuel") &&
           write_legacy_fuel(writer, core.fuel) && writer.key("excitation") &&
           write_legacy_excitation(writer, core.excitation) && writer.end_object();
}

[[nodiscard]] bool write_chen_flynn_aggregate_loss(
    CanonicalJsonWriter &writer,
    const contract::ChenFlynnCycleMeanAggregateLossV1 &loss) {
    return writer.begin_object() && writer.key("constant_fmep_bar") &&
           write_resolved(writer, loss.constant_fmep_bar, write_f64) &&
           writer.key("peak_pressure_coefficient") &&
           write_resolved(writer, loss.peak_pressure_coefficient, write_f64) &&
           writer.key("mean_piston_speed_coefficient_bar_s_per_m") &&
           write_resolved(writer, loss.mean_piston_speed_coefficient_bar_s_per_m,
                          write_f64) &&
           writer.key("mean_piston_speed_squared_coefficient_bar_s2_per_m2") &&
           write_resolved(writer,
                          loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2,
                          write_f64) &&
           writer.key("required_oil_temperature_k") &&
           write_resolved(writer, loss.required_oil_temperature_k, write_f64) &&
           writer.key("included_terms") &&
           write_resolved(writer, loss.included_terms, write_u64) &&
           writer.end_object();
}

[[nodiscard]] bool write_accessory_configuration(
    CanonicalJsonWriter &writer,
    const contract::AccessoryConfigurationIdentityV1 &configuration) {
    return writer.begin_object() && writer.key("configuration_id") &&
           write_resolved(writer, configuration.configuration_id, write_string) &&
           writer.key("content_sha256") &&
           write_resolved(writer, configuration.content_sha256, write_sha256) &&
           writer.end_object();
}

[[nodiscard]] bool
write_starter_capability(CanonicalJsonWriter &writer,
                         const contract::StarterCapabilityV1 &starter) {
    return writer.begin_object() && writer.key("type") &&
           write_resolved(writer, starter.type, write_starter_capability_type) &&
           writer.key("maximum_torque_nm") &&
           write_resolved(writer, starter.maximum_torque_nm, write_f64) &&
           writer.key("target_speed_rad_s") &&
           write_resolved(writer, starter.target_speed_rad_s, write_f64) &&
           writer.key("included_terms") &&
           write_resolved(writer, starter.included_terms, write_u64) &&
           writer.end_object();
}

[[nodiscard]] bool write_physics_profile_alternative(
    CanonicalJsonWriter &writer,
    const contract::LowOrderOperatingPointV1Profile &profile) {
    return writer.begin_object() && writer.key("kind") &&
           writer.string_value("low_order_operating_point_v1") && writer.key("value") &&
           writer.begin_object() && writer.key("core") &&
           write_low_order_engine_core(writer, profile.core) &&
           writer.key("aggregate_loss") &&
           write_chen_flynn_aggregate_loss(writer, profile.aggregate_loss) &&
           writer.key("accessory_configuration") &&
           write_accessory_configuration(writer, profile.accessory_configuration) &&
           writer.key("starter") && write_starter_capability(writer, profile.starter) &&
           writer.key("cycle_quadrature") &&
           write_resolved(writer, profile.cycle_quadrature, write_method_identity) &&
           writer.end_object() && writer.end_object();
}

[[nodiscard]] bool
write_physics_profile(CanonicalJsonWriter &writer,
                      const contract::ExecutablePhysicsProfile &profile) {
    if (profile.valueless_by_exception()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "physics-profile variant is valueless");
    }
    return std::visit(
        [&](const auto &typed_profile) {
            return write_physics_profile_alternative(writer, typed_profile);
        },
        profile);
}

[[nodiscard]] bool write_availability(CanonicalJsonWriter &writer,
                                      contract::Availability availability) {
    switch (availability) {
    case contract::Availability::available:
        return writer.string_value("available");
    case contract::Availability::unavailable:
        return writer.string_value("unavailable");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "torque availability is unknown");
}

[[nodiscard]] bool write_completeness(CanonicalJsonWriter &writer,
                                      contract::Completeness completeness) {
    switch (completeness) {
    case contract::Completeness::complete:
        return writer.string_value("complete");
    case contract::Completeness::incomplete:
        return writer.string_value("incomplete");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "torque completeness is unknown");
}

[[nodiscard]] bool
write_net_torque_form_capability(CanonicalJsonWriter &writer,
                                 const contract::NetTorqueFormCapability &capability) {
    return writer.begin_object() && writer.key("availability") &&
           write_availability(writer, capability.availability) &&
           writer.key("completeness") &&
           write_completeness(writer, capability.completeness) &&
           writer.key("included_terms") &&
           writer.uint64_hex_value(capability.included_terms) &&
           writer.key("omitted_terms") &&
           writer.uint64_hex_value(capability.omitted_terms) && writer.end_object();
}

[[nodiscard]] bool
write_torque_capability(CanonicalJsonWriter &writer,
                        const contract::TorqueCapability &capability) {
    return writer.begin_object() && writer.key("instantaneous_net_shaft") &&
           write_net_torque_form_capability(writer,
                                            capability.instantaneous_net_shaft) &&
           writer.key("cycle_mean_net_shaft") &&
           write_net_torque_form_capability(writer, capability.cycle_mean_net_shaft) &&
           writer.key("equivalent_inertia_available") &&
           writer.bool_value(capability.equivalent_inertia_available) &&
           writer.end_object();
}

} // namespace

bool write_engine_spec(CanonicalJsonWriter &writer,
                       const contract::EngineSpec &engine) {
    if (engine.schema_version != 1U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "simulation engine schema version is not v1");
    }
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(engine.schema_version) && writer.key("id") &&
           write_stable_id(writer, engine.id) && writer.key("engine_id") &&
           write_resolved(writer, engine.engine_id, write_string) &&
           writer.key("profile_id") &&
           write_resolved(writer, engine.profile_id, write_string) &&
           writer.key("display_name") &&
           write_resolved(writer, engine.display_name, write_string) &&
           writer.key("cycle") &&
           write_resolved(writer, engine.cycle, write_engine_cycle) &&
           writer.key("ignition") &&
           write_resolved(writer, engine.ignition, write_ignition_kind) &&
           writer.key("cylinder_layout") &&
           write_resolved(writer, engine.cylinder_layout, write_cylinder_layout) &&
           writer.key("total_displacement_m3") &&
           write_resolved(writer, engine.total_displacement_m3, write_f64) &&
           writer.key("crankshafts") &&
           write_array(writer, engine.crankshafts,
                       [](CanonicalJsonWriter &output,
                          const contract::CrankshaftSpec &crankshaft) {
                           return write_crankshaft(output, crankshaft);
                       }) &&
           writer.key("output_crankshaft_id") &&
           write_stable_id(writer, engine.output_crankshaft_id) &&
           writer.key("banks") &&
           write_array(writer, engine.banks,
                       [](CanonicalJsonWriter &output, const contract::BankSpec &bank) {
                           return write_bank(output, bank);
                       }) &&
           writer.key("intakes") &&
           write_array(
               writer, engine.intakes,
               [](CanonicalJsonWriter &output, const contract::IntakeSpec &intake) {
                   return write_intake(output, intake);
               }) &&
           writer.key("cylinders") &&
           write_array(
               writer, engine.cylinders,
               [](CanonicalJsonWriter &output, const contract::CylinderSpec &cylinder) {
                   return write_cylinder(output, cylinder);
               }) &&
           writer.key("ports") &&
           write_array(writer, engine.ports,
                       [](CanonicalJsonWriter &output, const contract::PortSpec &port) {
                           return write_port(output, port);
                       }) &&
           writer.key("gas_volumes") &&
           write_array(
               writer, engine.gas_volumes,
               [](CanonicalJsonWriter &output, const contract::GasVolumeSpec &volume) {
                   return write_gas_volume(output, volume);
               }) &&
           writer.key("flow_edges") &&
           write_array(
               writer, engine.flow_edges,
               [](CanonicalJsonWriter &output, const contract::FlowEdgeSpec &edge) {
                   return write_flow_edge(output, edge);
               }) &&
           writer.key("routes") &&
           write_array(
               writer, engine.routes,
               [](CanonicalJsonWriter &output, const contract::RouteSpec &route) {
                   return write_route(output, route);
               }) &&
           writer.key("methods") && write_model_methods(writer, engine.methods) &&
           writer.key("physics_profile") &&
           write_physics_profile(writer, engine.physics_profile) &&
           writer.key("torque_capability") &&
           write_resolved(writer, engine.torque_capability, write_torque_capability) &&
           writer.key("provenance_schema_id") &&
           writer.string_value(engine.provenance_schema_id) && writer.end_object();
}

} // namespace engine_sim_offline::identity::detail
