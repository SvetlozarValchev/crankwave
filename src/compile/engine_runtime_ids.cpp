#include "compile/engine_resolver_internal.hpp"

#include "compile/stable_id.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

[[nodiscard]] std::variant<IdNamespace, authoring::DiagnosticReport>
make_namespace(std::string name, const std::vector<std::string> &semantic_ids,
               std::string_view pointer_base) {
    std::vector<std::string> json_pointers;
    json_pointers.reserve(semantic_ids.size());
    std::vector<StableIdSource> sources;
    sources.reserve(semantic_ids.size());
    for (std::size_t index = 0; index < semantic_ids.size(); ++index) {
        json_pointers.push_back(pointer_index(pointer_base, index));
        sources.push_back({semantic_ids[index], json_pointers.back()});
    }
    auto result = assign_stable_runtime_ids(name, sources);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&result)) {
        return *report;
    }
    IdNamespace output;
    output.name = std::move(name);
    for (const auto &assignment :
         std::get<std::vector<StableIdAssignment>>(result)) {
        output.by_semantic_id.emplace(assignment.authored_id,
                                      assignment.runtime_id);
    }
    return output;
}

void append_assignments(RuntimeIds &ids, const IdNamespace &values) {
    for (const auto &[semantic_id, runtime_id] : values.by_semantic_id) {
        ids.assignments.push_back({
            values.name,
            semantic_id,
            runtime_id,
        });
    }
}

} // namespace

void assign_engine_runtime_ids(ModelContext &resolved,
                               authoring::DiagnosticReport &report) {
    const auto &document = resolved.document;
    const auto &engine = document.engine;
    std::vector<std::string> bank_ids{resolved.bank->id.value};
    std::vector<std::string> cylinder_ids;
    std::vector<std::string> port_ids;
    std::vector<std::string> gas_volume_ids{"volume.atmosphere",
                                             "volume.intake.plenum"};
    std::vector<std::string> flow_edge_ids{"flow.intake.main-throttle",
                                            "flow.intake.idle-bypass"};
    std::vector<std::string> route_ids;
    std::vector<std::string> audio_asset_ids;
    std::vector<std::string> audio_bus_ids;
    std::vector<std::string> accessory_ids{
        resolved.accessory_configuration->id.value};
    std::vector<std::string> rig_ids;
    std::vector<std::string> vehicle_ids;
    std::vector<std::string> transmission_ids;
    std::vector<std::string> gear_ids;
    if (document.rig) {
        rig_ids.push_back(document.rig->id.value);
        if (document.rig->vehicle) {
            vehicle_ids.push_back(document.rig->vehicle->id.value);
        }
        if (document.rig->transmission) {
            transmission_ids.push_back(document.rig->transmission->id.value);
            for (const auto &gear : document.rig->transmission->gears) {
                gear_ids.push_back(gear.id.value);
            }
        }
    }
    for (const auto &cylinder : engine.cylinders) {
        cylinder_ids.push_back(cylinder.id.value);
        port_ids.push_back(
            port_semantic_id(cylinder.id.value, authoring::PortKind::intake));
        port_ids.push_back(
            port_semantic_id(cylinder.id.value, authoring::PortKind::exhaust));
        gas_volume_ids.push_back(
            volume_semantic_id(cylinder.id.value, "intake-runner"));
        gas_volume_ids.push_back(
            volume_semantic_id(cylinder.id.value, "chamber"));
        gas_volume_ids.push_back(
            volume_semantic_id(cylinder.id.value, "exhaust-primary"));
        flow_edge_ids.push_back(
            flow_semantic_id(cylinder.id.value, "plenum-to-runner"));
        flow_edge_ids.push_back(
            flow_semantic_id(cylinder.id.value, "intake-valve"));
        flow_edge_ids.push_back(
            flow_semantic_id(cylinder.id.value, "exhaust-valve"));
        flow_edge_ids.push_back(
            flow_semantic_id(cylinder.id.value, "primary-to-collector"));
        flow_edge_ids.push_back(
            flow_semantic_id(cylinder.id.value, "blowby"));
    }
    for (const auto &exhaust : engine.exhausts) {
        gas_volume_ids.push_back(collector_semantic_id(exhaust.id.value));
        flow_edge_ids.push_back(
            flow_semantic_id(exhaust.id.value, "collector-outlet"));
    }
    for (const auto &route : engine.source_routes) {
        route_ids.push_back(route.id.value);
    }
    for (const auto &asset : document.presentation.assets) {
        audio_asset_ids.push_back(asset.id.value);
    }
    for (const auto &bus : document.presentation.buses) {
        audio_bus_ids.push_back(bus.id.value);
    }

    const auto assign =
        [&](IdNamespace &destination, std::string name,
            const std::vector<std::string> &values,
            std::string_view pointer_base) {
            auto result =
                make_namespace(std::move(name), values, pointer_base);
            if (const auto *nested =
                    std::get_if<authoring::DiagnosticReport>(&result)) {
                report.diagnostics.insert(report.diagnostics.end(),
                                          nested->diagnostics.begin(),
                                          nested->diagnostics.end());
            } else {
                destination = std::get<IdNamespace>(std::move(result));
            }
        };
    assign(resolved.ids.banks, "engine.bank", bank_ids, "/engine/banks");
    assign(resolved.ids.cylinders, "engine.cylinder", cylinder_ids,
           "/engine/cylinders");
    assign(resolved.ids.ports, "engine.port", port_ids, "/engine/ports");
    assign(resolved.ids.gas_volumes, "engine.gas-volume", gas_volume_ids,
           "/engine/gas-volumes");
    assign(resolved.ids.flow_edges, "engine.flow-edge", flow_edge_ids,
           "/engine/flow-edges");
    assign(resolved.ids.routes, "engine.route", route_ids,
           "/engine/source_routes");
    assign(resolved.ids.audio_assets, "presentation.audio-asset",
           audio_asset_ids, "/presentation/assets");
    assign(resolved.ids.audio_buses, "presentation.audio-bus", audio_bus_ids,
           "/presentation/buses");
    assign(resolved.ids.accessory_configurations,
           "engine.accessory-configuration", accessory_ids,
           "/engine/accessory_configurations");
    assign(resolved.ids.rigs, "rig", rig_ids, "/rig");
    assign(resolved.ids.vehicles, "rig.vehicle", vehicle_ids, "/rig/vehicle");
    assign(resolved.ids.transmissions, "rig.transmission", transmission_ids,
           "/rig/transmission");
    assign(resolved.ids.gears, "rig.gear", gear_ids, "/rig/transmission/gears");
    if (report.has_errors()) {
        return;
    }

    append_assignments(resolved.ids, resolved.ids.banks);
    append_assignments(resolved.ids, resolved.ids.cylinders);
    append_assignments(resolved.ids, resolved.ids.ports);
    append_assignments(resolved.ids, resolved.ids.gas_volumes);
    append_assignments(resolved.ids, resolved.ids.flow_edges);
    append_assignments(resolved.ids, resolved.ids.routes);
    append_assignments(resolved.ids, resolved.ids.audio_assets);
    append_assignments(resolved.ids, resolved.ids.audio_buses);
    append_assignments(resolved.ids, resolved.ids.accessory_configurations);
    append_assignments(resolved.ids, resolved.ids.rigs);
    append_assignments(resolved.ids, resolved.ids.vehicles);
    append_assignments(resolved.ids, resolved.ids.transmissions);
    append_assignments(resolved.ids, resolved.ids.gears);
    std::ranges::sort(
        resolved.ids.assignments,
        [](const auto &left, const auto &right) {
            return std::tie(left.object_namespace, left.authored_id) <
                   std::tie(right.object_namespace, right.authored_id);
        });
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
