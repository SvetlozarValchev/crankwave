#include "compile/engine_resolver_internal.hpp"

#include <utility>

namespace engine_sim_offline::compile::detail::engine_resolution {

void resolve_gas_path(const ModelContext &context, ResolutionEmitter &emitter,
                      contract::LowOrderEngineCoreV1 &core) {
    const auto &source = context.document.engine;
    for (const auto *intake : ordered_intakes(context)) {
        const auto intake_base = profile_path("gas_path.intakes." + intake->id.value);
        const auto flow_owner = "intake." + intake->id.value;
        core.gas_path.intakes.push_back({
            {
                intake_id(context, intake->id.value),
                volume_id(context, intake_plenum_semantic_id(intake->id.value)),
                edge_id(context, flow_semantic_id(flow_owner, "main-throttle")),
                edge_id(context, flow_semantic_id(flow_owner, "idle-bypass")),
            },
            {
                emitter.authored(legacy_si_value(intake->plenum_volume),
                                 intake_base + ".plenum_volume_m3"),
                emitter.authored(legacy_si_value(intake->plenum_cross_section_area),
                                 intake_base + ".plenum_cross_section_area_m2"),
                emitter.authored(legacy_si_value(intake->runner_length),
                                 intake_base + ".runner_length_m"),
                emitter.authored(intake->runner_velocity_decay_01,
                                 intake_base + ".velocity_decay"),
                emitter.authored(intake->idle_throttle_position_01,
                                 intake_base + ".idle_throttle_plate_position_01"),
                resolve_restriction(intake->main_restriction,
                                    intake_base + ".main_throttle", emitter),
                resolve_restriction(intake->idle_bypass_restriction,
                                    intake_base + ".idle_bypass", emitter),
                resolve_restriction(intake->runner_restriction,
                                    intake_base + ".plenum_to_runner", emitter),
            },
        });
    }

    for (const auto *bank : ordered_banks(context)) {
        const auto &head = *context.heads.at(bank->head.value);
        const auto &intake_port = *context.intake_port_for_head.at(head.id.value);
        const auto &exhaust_port = *context.exhaust_port_for_head.at(head.id.value);
        const auto &intake_curve = *context.curves.at(intake_port.flow_curve.value);
        const auto &exhaust_curve = *context.curves.at(exhaust_port.flow_curve.value);
        const auto head_base = profile_path("gas_path.heads." + bank->id.value);

        contract::LegacyBankHeadProfile resolved_head;
        resolved_head.bank_id = bank_id(context, bank->id.value);
        resolved_head.chamber_volume_m3 = emitter.authored(
            legacy_si_value(head.chamber_volume), head_base + ".chamber_volume_m3");
        resolved_head.intake_runner_base_volume_m3 =
            emitter.authored(legacy_si_value(intake_port.runner_volume),
                             head_base + ".intake_runner_base_volume_m3");
        resolved_head.intake_runner_cross_section_area_m2 =
            emitter.authored(legacy_si_value(intake_port.runner_cross_section_area),
                             head_base + ".intake_runner_cross_section_area_m2");
        resolved_head.exhaust_runner_base_volume_m3 =
            emitter.authored(legacy_si_value(exhaust_port.runner_volume),
                             head_base + ".exhaust_runner_base_volume_m3");
        resolved_head.exhaust_runner_cross_section_area_m2 =
            emitter.authored(legacy_si_value(exhaust_port.runner_cross_section_area),
                             head_base + ".exhaust_runner_cross_section_area_m2");
        resolved_head.intake_flow_triangle_radius_m =
            emitter.authored(legacy_si_value(*intake_curve.triangle_filter_radius),
                             head_base + ".intake_flow_triangle_radius_m");
        resolved_head.exhaust_flow_triangle_radius_m =
            emitter.authored(legacy_si_value(*exhaust_curve.triangle_filter_radius),
                             head_base + ".exhaust_flow_triangle_radius_m");
        for (std::size_t index = 0; index < intake_curve.samples.size(); ++index) {
            resolved_head.intake_flow.push_back(
                resolve_valve_flow_point(intake_curve.samples[index], index,
                                         head_base + ".intake_flow", emitter));
        }
        for (std::size_t index = 0; index < exhaust_curve.samples.size(); ++index) {
            resolved_head.exhaust_flow.push_back(
                resolve_valve_flow_point(exhaust_curve.samples[index], index,
                                         head_base + ".exhaust_flow", emitter));
        }
        core.gas_path.heads.push_back(std::move(resolved_head));
    }

    for (const auto &resolved : ordered_routes(context)) {
        const auto &route_source = *resolved.route;
        const auto &exhaust = *resolved.exhaust;
        const auto route_semantic = route_source.id.value;
        const auto route_base =
            profile_path("gas_path.exhaust_routes." + route_semantic);
        const auto volume_path = route_base + ".collector_volume_m3";
        const auto area_path = route_base + ".collector_cross_section_area_m2";
        const auto length_path = route_base + ".exhaust_system_length_m";
        const double collector_area_m2 =
            legacy_si_value(exhaust.collector_cross_section_area);
        const bool volume_authored = exhaust.collector_volume.has_value();
        const double exhaust_length_m =
            volume_authored
                ? legacy_si_value(*exhaust.collector_volume) / collector_area_m2
                : legacy_si_value(*exhaust.collector_length);
        const double collector_volume_m3 =
            volume_authored ? legacy_si_value(*exhaust.collector_volume)
                            : exhaust_length_m * collector_area_m2;

        contract::ResolvedValue<double> resolved_length;
        contract::ResolvedValue<double> resolved_volume;
        if (volume_authored) {
            resolved_volume = emitter.authored(collector_volume_m3, volume_path);
            resolved_length = emitter.derived(
                exhaust_length_m, length_path,
                derived_method_identity("collector-volume-over-area-v1"),
                {volume_path, area_path});
        } else {
            resolved_length = emitter.authored(exhaust_length_m, length_path);
            resolved_volume = emitter.derived(
                collector_volume_m3, volume_path,
                derived_method_identity("collector-length-times-area-v1"),
                {length_path, area_path});
        }

        const auto &presentation = *context.route_presentations.at(route_semantic);
        core.gas_path.exhaust_routes.push_back({
            {
                route_id(context, route_semantic),
                volume_id(context, collector_semantic_id(exhaust.id.value)),
                edge_id(context,
                        flow_semantic_id(exhaust.id.value, "collector-outlet")),
            },
            {
                std::move(resolved_volume),
                emitter.authored(collector_area_m2, area_path),
                std::move(resolved_length),
                emitter.authored(legacy_si_value(exhaust.primary_tube_length),
                                 route_base + ".primary_tube_length_m"),
                emitter.authored(exhaust.velocity_decay_01,
                                 route_base + ".velocity_decay"),
                emitter.authored(presentation.source_gain_linear,
                                 route_base + ".audio_volume_linear"),
                resolve_restriction(exhaust.primary_restriction,
                                    route_base + ".primary_to_collector", emitter),
                resolve_restriction(exhaust.outlet_restriction,
                                    route_base + ".collector_outlet", emitter),
            },
        });
    }

    const auto &first_piston =
        *context.pistons.at(source.cylinders.front().piston.value);
    core.gas_path.piston_blowby = resolve_restriction(
        *first_piston.blowby, profile_path("gas_path.piston_blowby"), emitter);
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
