#include "compile/engine_resolver_internal.hpp"

#include "simulation/legacy_mechanics_primitives.hpp"

#include <algorithm>
#include <numbers>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {

void resolve_public_topology(const ModelContext &context, ResolutionEmitter &emitter,
                             contract::EngineSpec &engine) {
    const auto &source = context.document.engine;

    std::vector<const authoring::BankDefinition *> ordered_banks;
    ordered_banks.reserve(source.banks.size());
    for (const auto &bank : source.banks) {
        ordered_banks.push_back(&bank);
    }
    std::ranges::sort(ordered_banks, [&](const auto *left, const auto *right) {
        return bank_id(context, left->id.value).value <
               bank_id(context, right->id.value).value;
    });
    for (const auto *bank : ordered_banks) {
        const auto base = "engine.banks." + bank->id.value;
        contract::BankSpec resolved_bank{
            bank_id(context, bank->id.value),
            emitter.authored(bank->id.value, base + ".semantic_id"),
            std::nullopt,
        };
        if (source.layout == authoring::CylinderLayout::v_engine) {
            resolved_bank.angle_rad =
                emitter.authored(legacy_si_value(bank->angle), base + ".angle_rad");
        }
        engine.banks.push_back(std::move(resolved_bank));
    }

    for (const auto &cylinder : source.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto base = "engine.cylinders." + semantic;
        const auto &journal = *context.journals.at(cylinder.journal.value);
        const auto &rod = *context.rods.at(cylinder.connecting_rod.value);
        const auto &piston = *context.pistons.at(cylinder.piston.value);
        const auto &bank = *context.banks.at(cylinder.bank.value);
        const double bore_m = legacy_si_value(bank.bore);
        const double crank_radius_m = legacy_si_value(context.crankshaft->throw_radius);
        const double stroke_m = 2.0 * crank_radius_m;
        const auto geometry = simulation::derive_legacy_cylinder_geometry(
            bore_m, crank_radius_m, legacy_si_value(rod.length),
            legacy_si_value(bank.deck_height),
            legacy_si_value(piston.compression_height),
            legacy_si_value(context.head->chamber_volume),
            legacy_si_value(piston.displacement_volume));
        const auto profile_base = profile_path("mechanism.cylinders." + semantic);
        engine.cylinders.push_back({
            cylinder_id(context, semantic),
            emitter.authored(semantic, base + ".semantic_id"),
            bank_id(context, cylinder.bank.value),
            emitter.authored(bore_m, base + ".bore_m"),
            emitter.derived(stroke_m, base + ".stroke_m",
                            derived_method_identity("twice-crank-throw-stroke-v1"),
                            {profile_base + ".crank_radius_m"}),
            emitter.authored(legacy_si_value(rod.length),
                             base + ".connecting_rod_length_m"),
            emitter.derived(
                geometry.compression_ratio, base + ".compression_ratio",
                derived_method_identity("legacy-slider-crank-compression-ratio-v1"),
                {
                    profile_base + ".bore_m",
                    profile_base + ".crank_radius_m",
                    profile_base + ".connecting_rod_length_m",
                    profile_base + ".deck_height_m",
                    profile_base + ".piston_compression_height_m",
                    profile_base + ".head_chamber_volume_m3",
                    profile_base + ".piston_displacement_term_m3",
                }),
            emitter.authored(
                context.firing_angle_for_wire_rad.at(cylinder.ignition_wire.value),
                base + ".firing_tdc_offset_rad"),
            emitter.authored(legacy_si_value(journal.phase),
                             base + ".journal_phase_rad"),
        });
    }

    std::vector<const contract::CylinderSpec *> stable_cylinders;
    stable_cylinders.reserve(engine.cylinders.size());
    for (const auto &cylinder : engine.cylinders) {
        stable_cylinders.push_back(&cylinder);
    }
    std::ranges::sort(stable_cylinders, {},
                      [](const auto *cylinder) { return cylinder->id.value; });
    double total_displacement_m3 = 0.0;
    std::vector<std::string> dependency_storage;
    dependency_storage.reserve(stable_cylinders.size() * 2U);
    for (const auto *cylinder : stable_cylinders) {
        total_displacement_m3 += std::numbers::pi * cylinder->bore_m.value *
                                 cylinder->bore_m.value * cylinder->stroke_m.value /
                                 4.0;
        dependency_storage.push_back("engine.cylinders." + cylinder->semantic_id.value +
                                     ".bore_m");
        dependency_storage.push_back("engine.cylinders." + cylinder->semantic_id.value +
                                     ".stroke_m");
    }
    std::vector<std::string_view> dependency_views;
    dependency_views.reserve(dependency_storage.size());
    for (const auto &dependency : dependency_storage) {
        dependency_views.push_back(dependency);
    }
    engine.total_displacement_m3 = emitter.derived(
        total_displacement_m3, "engine.total_displacement_m3",
        derived_method_identity("geometric-cylinder-displacement-std-pi-v1"),
        dependency_views);

    for (const auto &cylinder : source.cylinders) {
        const auto cylinder_runtime_id = cylinder_id(context, cylinder.id.value);
        for (const auto kind :
             {authoring::PortKind::intake, authoring::PortKind::exhaust}) {
            const auto semantic = port_semantic_id(cylinder.id.value, kind);
            const auto base = "engine.ports." + semantic;
            const auto cylinder_path =
                "engine.cylinders." + cylinder.id.value + ".semantic_id";
            engine.ports.push_back({
                port_id(context, semantic),
                emitter.derived(semantic, base + ".semantic_id",
                                derived_method_identity("engine-cylinder-port-id-v1"),
                                {cylinder_path}),
                cylinder_runtime_id,
                emitter.derived(kind == authoring::PortKind::intake
                                    ? contract::PortKind::intake
                                    : contract::PortKind::exhaust,
                                base + ".kind",
                                derived_method_identity("engine-cylinder-port-kind-v1"),
                                {base + ".semantic_id"}),
            });
        }
    }

    const auto add_volume = [&](std::string semantic, contract::GasVolumeKind kind,
                                std::string_view dependency) {
        const auto base = "engine.gas_volumes." + semantic;
        engine.gas_volumes.push_back({
            volume_id(context, semantic),
            emitter.derived(semantic, base + ".semantic_id",
                            derived_method_identity("engine-gas-volume-id-v1"),
                            {dependency}),
            emitter.derived(kind, base + ".kind",
                            derived_method_identity("engine-gas-volume-kind-v1"),
                            {base + ".semantic_id"}),
        });
    };
    add_volume("volume.atmosphere", contract::GasVolumeKind::atmosphere,
               "engine.profile_id");
    add_volume("volume.intake.plenum", contract::GasVolumeKind::intake_plenum,
               "engine.profile_id");
    for (const auto &cylinder : source.cylinders) {
        const auto dependency =
            "engine.cylinders." + cylinder.id.value + ".semantic_id";
        add_volume(volume_semantic_id(cylinder.id.value, "intake-runner"),
                   contract::GasVolumeKind::intake_runner, dependency);
        add_volume(volume_semantic_id(cylinder.id.value, "chamber"),
                   contract::GasVolumeKind::cylinder, dependency);
        add_volume(volume_semantic_id(cylinder.id.value, "exhaust-primary"),
                   contract::GasVolumeKind::exhaust_primary, dependency);
    }
    for (const auto &route : ordered_routes(context)) {
        add_volume(collector_semantic_id(route.exhaust->id.value),
                   contract::GasVolumeKind::exhaust_collector,
                   "engine.routes." + route.route->id.value + ".semantic_id");
    }

    const auto add_edge = [&](std::string semantic, std::string endpoint_0,
                              std::string endpoint_1, std::string_view dependency) {
        engine.flow_edges.push_back({
            edge_id(context, semantic),
            emitter.derived(semantic, "engine.flow_edges." + semantic + ".semantic_id",
                            derived_method_identity("engine-flow-edge-id-v1"),
                            {dependency}),
            volume_id(context, endpoint_0),
            volume_id(context, endpoint_1),
        });
    };
    add_edge("flow.intake.main-throttle", "volume.atmosphere", "volume.intake.plenum",
             "engine.profile_id");
    add_edge("flow.intake.idle-bypass", "volume.atmosphere", "volume.intake.plenum",
             "engine.profile_id");
    for (const auto &cylinder : source.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto dependency = "engine.cylinders." + semantic + ".semantic_id";
        const auto runner = volume_semantic_id(semantic, "intake-runner");
        const auto chamber = volume_semantic_id(semantic, "chamber");
        const auto primary = volume_semantic_id(semantic, "exhaust-primary");
        const auto collector = collector_semantic_id(cylinder.exhaust.value);
        add_edge(flow_semantic_id(semantic, "plenum-to-runner"), "volume.intake.plenum",
                 runner, dependency);
        add_edge(flow_semantic_id(semantic, "intake-valve"), runner, chamber,
                 dependency);
        add_edge(flow_semantic_id(semantic, "exhaust-valve"), chamber, primary,
                 dependency);
        add_edge(flow_semantic_id(semantic, "primary-to-collector"), primary, collector,
                 dependency);
        add_edge(flow_semantic_id(semantic, "blowby"), chamber, "volume.atmosphere",
                 dependency);
    }
    for (const auto &route : ordered_routes(context)) {
        add_edge(flow_semantic_id(route.exhaust->id.value, "collector-outlet"),
                 "volume.atmosphere", collector_semantic_id(route.exhaust->id.value),
                 "engine.routes." + route.route->id.value + ".semantic_id");
    }

    for (const auto &resolved : ordered_routes(context)) {
        const auto semantic = resolved.route->id.value;
        const auto base = "engine.routes." + semantic;
        engine.routes.push_back({
            route_id(context, semantic),
            emitter.authored(semantic, base + ".semantic_id"),
            emitter.derived(contract::SourceRouteKind::exhaust_outlet, base + ".kind",
                            derived_method_identity("exhaust-source-route-kind-v1"),
                            {base + ".semantic_id"}),
            volume_id(context, collector_semantic_id(resolved.exhaust->id.value)),
            std::nullopt,
            std::nullopt,
        });
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
