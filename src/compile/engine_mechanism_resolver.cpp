#include "compile/engine_resolver_internal.hpp"

namespace engine_sim_offline::compile::detail::engine_resolution {

void resolve_mechanism(const ModelContext &context, ResolutionEmitter &emitter,
                       contract::LowOrderEngineCoreV1 &core) {
    const auto &source = context.document.engine;
    const auto crank_base = profile_path("mechanism.crank");
    core.mechanism.crank = {
        emitter.authored(legacy_si_value(context.crankshaft->tdc_reference_angle),
                         crank_base + ".crank_tdc_reference_rad"),
        emitter.authored(legacy_si_value(context.crankshaft->mass),
                         crank_base + ".crankshaft_mass_kg"),
        emitter.authored(legacy_si_value(context.crankshaft->flywheel_mass),
                         crank_base + ".flywheel_mass_kg"),
        emitter.authored(legacy_si_value(context.crankshaft->moment_of_inertia),
                         crank_base + ".authored_crank_inertia_kg_m2"),
        emitter.authored(legacy_si_value(*context.crankshaft->friction_torque),
                         crank_base + ".running_friction_torque_magnitude_nm"),
    };

    for (const auto &cylinder : source.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto base = profile_path("mechanism.cylinders." + semantic);
        const auto &journal = *context.journals.at(cylinder.journal.value);
        const auto &rod = *context.rods.at(cylinder.connecting_rod.value);
        const auto &piston = *context.pistons.at(cylinder.piston.value);
        const auto &bank = *context.banks.at(cylinder.bank.value);
        const auto route_semantic =
            context.route_for_exhaust.at(cylinder.exhaust.value);
        const double crank_radius_m = legacy_si_value(context.crankshaft->throw_radius);
        const double raw_journal_phase_rad = legacy_si_value(journal.phase);
        contract::ResolvedValue<double> effective_journal_phase;
        if (source.layout != authoring::CylinderLayout::inline_engine) {
            const auto raw_phase_path =
                "engine.cylinders." + semantic + ".journal_phase_rad";
            const auto bank_angle_path = "engine.banks." + bank.id.value + ".angle_rad";
            effective_journal_phase = emitter.derived(
                raw_journal_phase_rad - legacy_si_value(bank.angle),
                base + ".journal_angle_rad",
                derived_method_identity("cylinder-axis-relative-journal-phase-v1"),
                {raw_phase_path, bank_angle_path});
        } else {
            effective_journal_phase =
                emitter.authored(raw_journal_phase_rad, base + ".journal_angle_rad");
        }
        core.mechanism.cylinders.push_back({
            {
                cylinder_id(context, semantic),
                port_id(context,
                        port_semantic_id(semantic, authoring::PortKind::intake)),
                port_id(context,
                        port_semantic_id(semantic, authoring::PortKind::exhaust)),
                volume_id(context, volume_semantic_id(semantic, "intake-runner")),
                volume_id(context, volume_semantic_id(semantic, "chamber")),
                volume_id(context, volume_semantic_id(semantic, "exhaust-primary")),
                edge_id(context, flow_semantic_id(semantic, "plenum-to-runner")),
                edge_id(context, flow_semantic_id(semantic, "intake-valve")),
                edge_id(context, flow_semantic_id(semantic, "exhaust-valve")),
                edge_id(context, flow_semantic_id(semantic, "primary-to-collector")),
                edge_id(context, flow_semantic_id(semantic, "blowby")),
                route_id(context, route_semantic),
            },
            {
                emitter.authored(legacy_si_value(bank.bore), base + ".bore_m"),
                emitter.derived(2.0 * crank_radius_m, base + ".stroke_m",
                                derived_method_identity("twice-crank-throw-stroke-v1"),
                                {base + ".crank_radius_m"}),
                emitter.authored(crank_radius_m, base + ".crank_radius_m"),
                emitter.authored(legacy_si_value(rod.length),
                                 base + ".connecting_rod_length_m"),
                emitter.authored(legacy_si_value(bank.deck_height),
                                 base + ".deck_height_m"),
                emitter.authored(legacy_si_value(piston.compression_height),
                                 base + ".piston_compression_height_m"),
                emitter.authored(legacy_si_value(context.head->chamber_volume),
                                 base + ".head_chamber_volume_m3"),
                emitter.authored(legacy_si_value(piston.displacement_volume),
                                 base + ".piston_displacement_term_m3"),
                emitter.authored(legacy_si_value(piston.mass),
                                 base + ".piston_mass_kg"),
                emitter.authored(legacy_si_value(rod.mass),
                                 base + ".connecting_rod_mass_kg"),
                emitter.authored(legacy_si_value(rod.moment_of_inertia),
                                 base + ".connecting_rod_inertia_kg_m2"),
                std::move(effective_journal_phase),
                emitter.authored(
                    context.firing_angle_for_wire_rad.at(cylinder.ignition_wire.value),
                    base + ".ignition_wire_angle_rad"),
                emitter.authored(
                    legacy_si_value(cylinder.exhaust_header_primary_length),
                    base + ".header_primary_length_m"),
            },
        });
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
