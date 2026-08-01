#include "compile/engine_resolver_internal.hpp"

namespace engine_sim_offline::compile::detail::engine_resolution {

void resolve_mechanism(const ModelContext &context, ResolutionEmitter &emitter,
                       contract::LowOrderEngineCoreV1 &core) {
    const auto &source = context.document.engine;
    core.mechanism.output_crankshaft_id =
        crankshaft_id(context, context.output_crankshaft->id.value);
    core.mechanism.cranks.reserve(source.crankshafts.size());
    for (const auto &crankshaft : source.crankshafts) {
        const auto crank_base = profile_path("mechanism.cranks." + crankshaft.id.value);
        core.mechanism.cranks.push_back({
            crankshaft_id(context, crankshaft.id.value),
            emitter.authored(legacy_si_value(crankshaft.tdc_reference_angle),
                             crank_base + ".crank_tdc_reference_rad"),
            emitter.authored(legacy_si_value(crankshaft.mass),
                             crank_base + ".crankshaft_mass_kg"),
            emitter.authored(legacy_si_value(crankshaft.flywheel_mass),
                             crank_base + ".flywheel_mass_kg"),
            emitter.authored(legacy_si_value(crankshaft.moment_of_inertia),
                             crank_base + ".authored_crank_inertia_kg_m2"),
            emitter.authored(legacy_si_value(*crankshaft.friction_torque),
                             crank_base + ".running_friction_torque_magnitude_nm"),
        });
    }

    for (const auto &cylinder : source.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto base = profile_path("mechanism.cylinders." + semantic);
        const auto &journal = *context.journals.at(cylinder.journal.value);
        const auto &rod = *context.rods.at(cylinder.connecting_rod.value);
        const auto &piston = *context.pistons.at(cylinder.piston.value);
        const auto &bank = *context.banks.at(cylinder.bank.value);
        const auto &crankshaft = crankshaft_for_cylinder(context, semantic);
        const auto route_semantic =
            context.route_for_exhaust.at(cylinder.exhaust.value);
        const double crank_radius_m = legacy_si_value(crankshaft.throw_radius);
        const double raw_journal_phase_rad = legacy_si_value(journal.phase);
        const auto *master_attachment =
            std::get_if<authoring::MasterRodJournalAttachment>(&journal.attachment);
        std::optional<contract::ResolvedValue<double>> direct_journal_phase;
        std::optional<contract::ResolvedValue<double>> master_local_phase;
        if (master_attachment != nullptr) {
            master_local_phase = emitter.authored(
                raw_journal_phase_rad, base + ".kinematics.master_local_phase_rad");
        } else if (source.layout != authoring::CylinderLayout::inline_engine) {
            const auto raw_phase_path =
                "engine.cylinders." + semantic + ".journal_phase_rad";
            const auto bank_angle_path = "engine.banks." + bank.id.value + ".angle_rad";
            direct_journal_phase = emitter.derived(
                raw_journal_phase_rad - legacy_si_value(bank.angle),
                base + ".journal_angle_rad",
                derived_method_identity("cylinder-axis-relative-journal-phase-v1"),
                {raw_phase_path, bank_angle_path});
        } else {
            direct_journal_phase =
                emitter.authored(raw_journal_phase_rad, base + ".journal_angle_rad");
        }
        auto bore_m = emitter.authored(legacy_si_value(bank.bore), base + ".bore_m");
        std::optional<contract::ResolvedValue<double>> direct_stroke_m;
        std::optional<contract::ResolvedValue<double>> direct_crank_radius_m;
        if (master_attachment == nullptr) {
            // Keep direct resolution emission in its historical order. Resolution
            // identifiers are part of the canonical request identity.
            direct_stroke_m =
                emitter.derived(2.0 * crank_radius_m, base + ".stroke_m",
                                derived_method_identity("twice-crank-throw-stroke-v1"),
                                {base + ".crank_radius_m"});
            direct_crank_radius_m =
                emitter.authored(crank_radius_m, base + ".crank_radius_m");
        }
        auto connecting_rod_length_m = emitter.authored(
            legacy_si_value(rod.length), base + ".connecting_rod_length_m");
        auto deck_height_m = emitter.authored(legacy_si_value(bank.deck_height),
                                              base + ".deck_height_m");
        auto piston_compression_height_m =
            emitter.authored(legacy_si_value(piston.compression_height),
                             base + ".piston_compression_height_m");
        auto piston_displacement_term_m3 =
            emitter.authored(legacy_si_value(piston.displacement_volume),
                             base + ".piston_displacement_term_m3");
        auto piston_mass_kg =
            emitter.authored(legacy_si_value(piston.mass), base + ".piston_mass_kg");
        auto connecting_rod_mass_kg = emitter.authored(
            legacy_si_value(rod.mass), base + ".connecting_rod_mass_kg");
        auto connecting_rod_inertia_kg_m2 =
            emitter.authored(legacy_si_value(rod.moment_of_inertia),
                             base + ".connecting_rod_inertia_kg_m2");

        auto ignition_wire_angle_rad = emitter.authored(
            context.firing_angle_for_wire_rad.at(cylinder.ignition_wire.value),
            base + ".ignition_wire_angle_rad");
        auto header_primary_length_m =
            emitter.authored(legacy_si_value(cylinder.exhaust_header_primary_length),
                             base + ".header_primary_length_m");
        auto piston_blowby =
            resolve_restriction(*piston.blowby, base + ".piston_blowby", emitter);

        contract::LegacyCylinderKinematics kinematics;
        if (master_attachment != nullptr) {
            kinematics = contract::LegacyMasterRodJournalKinematics{
                cylinder_id(context, master_attachment->master_cylinder.value),
                emitter.authored(legacy_si_value(master_attachment->throw_radius),
                                 base + ".kinematics.throw_radius_m"),
                std::move(*master_local_phase),
            };
        } else {
            kinematics = contract::LegacyDirectJournalKinematics{
                std::move(*direct_stroke_m),
                std::move(*direct_crank_radius_m),
                std::move(*direct_journal_phase),
            };
        }
        core.mechanism.cylinders.push_back({
            {
                cylinder_id(context, semantic),
                crankshaft_id(context, crankshaft.id.value),
                intake_id(context, cylinder.intake.value),
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
                std::move(bore_m),
                std::move(connecting_rod_length_m),
                std::move(deck_height_m),
                std::move(piston_compression_height_m),
                std::move(piston_displacement_term_m3),
                std::move(piston_mass_kg),
                std::move(connecting_rod_mass_kg),
                std::move(connecting_rod_inertia_kg_m2),
                std::move(ignition_wire_angle_rad),
                std::move(header_primary_length_m),
                std::move(piston_blowby),
            },
            std::move(kinematics),
        });
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
