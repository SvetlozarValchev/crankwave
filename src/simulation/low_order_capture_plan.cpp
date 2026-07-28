#include "simulation/low_order_capture_plan.hpp"

#include "simulation/legacy_low_order_simulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

template <class Range, class Id>
[[nodiscard]] std::optional<std::size_t> find_id_index(const Range &range, Id id) {
    const auto found =
        std::ranges::find_if(range, [&](const auto &value) { return value.id == id; });
    if (found == range.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - range.begin());
}

[[nodiscard]] std::optional<std::size_t>
find_cylinder_profile_index(const contract::LowOrderEngineCoreV1 &core,
                            contract::CylinderId cylinder_id) {
    const auto &cylinders = core.mechanism.cylinders;
    const auto found = std::ranges::find_if(cylinders, [&](const auto &cylinder) {
        return cylinder.topology.cylinder_id == cylinder_id;
    });
    if (found == cylinders.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - cylinders.begin());
}

[[nodiscard]] std::optional<std::size_t>
find_exhaust_profile_index(const contract::LowOrderEngineCoreV1 &core,
                           contract::RouteId route_id) {
    const auto &routes = core.gas_path.exhaust_routes;
    const auto found = std::ranges::find_if(
        routes, [&](const auto &route) { return route.topology.route_id == route_id; });
    if (found == routes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - routes.begin());
}

[[nodiscard]] bool reserve_product_representable(std::size_t entity_count) noexcept {
    return entity_count <=
           std::numeric_limits<std::size_t>::max() / kLegacyCaptureFramesPerBlock;
}

struct TopologyPhysicalVolume {
    contract::GasVolumeId id;
    contract::GasVolumeKind kind = contract::GasVolumeKind::unspecified;

    friend bool operator==(const TopologyPhysicalVolume &,
                           const TopologyPhysicalVolume &) = default;
};

void sort_physical_volumes(std::vector<TopologyPhysicalVolume> &volumes) {
    std::ranges::sort(volumes, {}, &TopologyPhysicalVolume::id);
}

void append_layout_issues(ValidationReport &report,
                          const detail::LegacyCaptureBufferPlan &plan) {
    const auto layout = contract::CaptureLayoutView::borrow_for_callback(
        plan.engine_id, plan.cylinders, plan.ports, plan.gas_volumes, plan.flow_edges,
        plan.routes);
    const auto layout_report = contract::validate(layout);
    for (const auto &issue : layout_report.issues) {
        report.add(issue.code, "capture.layout." + issue.path, issue.message);
    }
}

} // namespace

LowOrderCapturePlanCompileResult
compile_low_order_capture_plan(const contract::EngineSpec &engine,
                               const contract::LowOrderEngineCoreV1 &core,
                               const contract::RenderScenario &scenario) {
    ValidationReport report;
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates.capture",
            "low-order capture requires identical physics and capture clocks");
    require(report,
            scenario.quality.value.capture_block_capacity_frames ==
                kLegacyCaptureFramesPerBlock,
            ContractIssueCode::unsupported_value,
            "scenario.quality.value.capture_block_capacity_frames",
            "low-order capture requires the canonical 200-frame partition capacity");
    require(report,
            scenario.quality.value.event_journal_capacity_records ==
                kLegacyCaptureFramesPerBlock * kLegacyMaximumEventsPerFrame,
            ContractIssueCode::unsupported_value,
            "scenario.quality.value.event_journal_capacity_records",
            "low-order capture requires the canonical 3800-record event capacity");

    const bool event_count_representable =
        engine.cylinders.size() <= (std::numeric_limits<std::size_t>::max() - 1U) / 3U;
    require(report, event_count_representable, ContractIssueCode::unsupported_value,
            "engine.cylinders", "composed event count is not representable");
    if (event_count_representable) {
        const auto maximum_events_per_frame = 3U * engine.cylinders.size() + 1U;
        const bool fits_frame =
            maximum_events_per_frame <= kLegacyMaximumEventsPerFrame;
        require(report, fits_frame, ContractIssueCode::unsupported_value,
                "engine.cylinders",
                "cylinder count exceeds the canonical per-frame event bound");
        if (fits_frame) {
            const auto required_block_event_capacity =
                static_cast<std::uint64_t>(
                    scenario.quality.value.capture_block_capacity_frames) *
                static_cast<std::uint64_t>(maximum_events_per_frame);
            require(report,
                    required_block_event_capacity <=
                        scenario.quality.value.event_journal_capacity_records,
                    ContractIssueCode::unsupported_value,
                    "scenario.quality.value.event_journal_capacity_records",
                    "event journal cannot hold the worst-case composed event bound "
                    "for one capture block");
        }
    }

    const auto horizon = contract::resolve_frame_index(scenario.total_duration_s.value,
                                                       scenario.rates.capture);
    require(report, horizon.has_value() && *horizon > 0U,
            ContractIssueCode::inconsistent_semantics,
            "scenario.total_duration_s.value",
            "low-order capture horizon must resolve to a positive integral frame "
            "count");

    require(report,
            reserve_product_representable(engine.cylinders.size()) &&
                reserve_product_representable(engine.ports.size()) &&
                reserve_product_representable(engine.gas_volumes.size()) &&
                reserve_product_representable(engine.flow_edges.size()) &&
                reserve_product_representable(engine.routes.size()),
            ContractIssueCode::unsupported_value, "engine",
            "capture entity count overflows bounded frame-major storage");

    LowOrderCapturePlan compiled;
    auto &plan = compiled.capture_buffer;
    plan.engine_id = engine.id;
    plan.rate = scenario.rates.capture;
    plan.declared_block_capacity_frames =
        scenario.quality.value.capture_block_capacity_frames;
    plan.declared_event_capacity_records =
        scenario.quality.value.event_journal_capacity_records;

    plan.cylinders.reserve(engine.cylinders.size());
    for (const auto &cylinder : engine.cylinders) {
        plan.cylinders.push_back(cylinder.id);
    }
    plan.ports.reserve(engine.ports.size());
    for (const auto &port : engine.ports) {
        plan.ports.push_back({port.id, port.cylinder_id, port.kind.value});
    }
    plan.gas_volumes.reserve(engine.gas_volumes.size());
    std::vector<TopologyPhysicalVolume> engine_physical_volumes;
    engine_physical_volumes.reserve(engine.gas_volumes.size());
    std::size_t atmosphere_count = 0;
    for (const auto &volume : engine.gas_volumes) {
        plan.gas_volumes.push_back({volume.id, volume.kind.value});
        if (volume.kind.value == contract::GasVolumeKind::atmosphere) {
            ++atmosphere_count;
        } else {
            engine_physical_volumes.push_back({volume.id, volume.kind.value});
        }
    }
    sort_physical_volumes(engine_physical_volumes);
    require(report, atmosphere_count == 1U, ContractIssueCode::inconsistent_shape,
            "engine.gas_volumes",
            "low-order capture requires exactly one unresolved atmosphere identity");
    require(report,
            std::ranges::adjacent_find(engine_physical_volumes, {},
                                       &TopologyPhysicalVolume::id) ==
                engine_physical_volumes.end(),
            ContractIssueCode::duplicate_identity, "engine.gas_volumes",
            "physical gas-volume IDs must be unique");

    std::vector<TopologyPhysicalVolume> topology_physical_volumes;
    const bool topology_inventory_representable =
        core.gas_path.exhaust_routes.size() <=
            std::numeric_limits<std::size_t>::max() - 1U &&
        core.mechanism.cylinders.size() <= (std::numeric_limits<std::size_t>::max() -
                                            1U - core.gas_path.exhaust_routes.size()) /
                                               3U;
    require(report, topology_inventory_representable,
            ContractIssueCode::unsupported_value, "engine.physics_profile.gas_topology",
            "physical topology inventory size is not representable");
    if (!topology_inventory_representable) {
        return report;
    }
    topology_physical_volumes.reserve(1U + core.gas_path.exhaust_routes.size() +
                                      3U * core.mechanism.cylinders.size());
    topology_physical_volumes.push_back({core.gas_path.intake_topology.plenum_volume_id,
                                         contract::GasVolumeKind::intake_plenum});
    for (const auto &route : core.gas_path.exhaust_routes) {
        topology_physical_volumes.push_back(
            {route.topology.collector_volume_id,
             contract::GasVolumeKind::exhaust_collector});
    }
    for (const auto &cylinder : core.mechanism.cylinders) {
        const auto &topology = cylinder.topology;
        topology_physical_volumes.push_back(
            {topology.intake_runner_volume_id, contract::GasVolumeKind::intake_runner});
        topology_physical_volumes.push_back(
            {topology.chamber_volume_id, contract::GasVolumeKind::cylinder});
        topology_physical_volumes.push_back({topology.exhaust_primary_volume_id,
                                             contract::GasVolumeKind::exhaust_primary});
    }
    sort_physical_volumes(topology_physical_volumes);
    require(report, !topology_physical_volumes.empty(),
            ContractIssueCode::missing_value, "engine.physics_profile.gas_topology",
            "low-order gas topology must resolve at least one physical volume");
    require(report,
            std::ranges::adjacent_find(topology_physical_volumes, {},
                                       &TopologyPhysicalVolume::id) ==
                topology_physical_volumes.end(),
            ContractIssueCode::duplicate_identity,
            "engine.physics_profile.gas_topology",
            "each physical gas volume must have exactly one low-order topology "
            "role");
    require(report, topology_physical_volumes == engine_physical_volumes,
            ContractIssueCode::inconsistent_shape, "engine.gas_volumes",
            "non-atmosphere EngineSpec volumes must exactly equal the low-order "
            "topology's physical identity and role inventory");
    compiled.physical_gas_volume_ids.reserve(topology_physical_volumes.size());
    for (const auto &volume : topology_physical_volumes) {
        compiled.physical_gas_volume_ids.push_back(volume.id);
    }

    plan.flow_edges.reserve(engine.flow_edges.size());
    for (const auto &edge : engine.flow_edges) {
        plan.flow_edges.push_back(
            {edge.id, edge.endpoint_0_volume_id, edge.endpoint_1_volume_id});
    }
    plan.routes.reserve(engine.routes.size());
    for (const auto &route : engine.routes) {
        plan.routes.push_back({
            route.id,
            route.kind.value,
            route.source_volume_id,
            route.default_parent_route_id,
            route.emitter_anchor_id.has_value()
                ? std::optional<std::string>{route.emitter_anchor_id->value}
                : std::nullopt,
        });
    }

    plan.cylinder_bindings.resize(engine.cylinders.size());
    compiled.cylinder_chambers.reserve(engine.cylinders.size());
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto profile_index =
            find_cylinder_profile_index(core, engine.cylinders[index].id);
        require(report, profile_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "]",
                "capture could not bind cylinder to low-order gas topology");
        if (!profile_index.has_value()) {
            continue;
        }
        const auto &topology = core.mechanism.cylinders[*profile_index].topology;
        const auto chamber =
            find_id_index(engine.gas_volumes, topology.chamber_volume_id);
        const auto primary =
            find_id_index(engine.gas_volumes, topology.exhaust_primary_volume_id);
        require(report, chamber.has_value() && primary.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "]",
                "capture cylinder gas volumes do not resolve in engine order");
        if (chamber.has_value() && primary.has_value()) {
            plan.cylinder_bindings[index] = {*chamber, *primary};
        }

        const auto physical_chamber = std::ranges::lower_bound(
            compiled.physical_gas_volume_ids, topology.chamber_volume_id);
        const bool chamber_is_physical =
            physical_chamber != compiled.physical_gas_volume_ids.end() &&
            *physical_chamber == topology.chamber_volume_id;
        require(report, chamber_is_physical, ContractIssueCode::inconsistent_semantics,
                "engine.cylinders[" + std::to_string(index) + "]",
                "cylinder chamber must resolve in the physical gas-volume inventory");
        if (chamber_is_physical) {
            compiled.cylinder_chambers.push_back({
                engine.cylinders[index].id,
                topology.chamber_volume_id,
                static_cast<std::size_t>(physical_chamber -
                                         compiled.physical_gas_volume_ids.begin()),
            });
        }
    }
    std::ranges::sort(compiled.cylinder_chambers, {},
                      &LowOrderCylinderChamberCaptureBinding::cylinder_id);
    require(report,
            compiled.cylinder_chambers.size() == engine.cylinders.size() &&
                std::ranges::adjacent_find(
                    compiled.cylinder_chambers, {},
                    &LowOrderCylinderChamberCaptureBinding::cylinder_id) ==
                    compiled.cylinder_chambers.end(),
            ContractIssueCode::inconsistent_shape, "engine.cylinders",
            "every engine cylinder must have one unique physical chamber binding");

    plan.port_bindings.resize(engine.ports.size());
    for (std::size_t index = 0; index < engine.ports.size(); ++index) {
        const auto &port = engine.ports[index];
        const auto cylinder_index = find_id_index(engine.cylinders, port.cylinder_id);
        const auto profile_index = find_cylinder_profile_index(core, port.cylinder_id);
        require(report, cylinder_index.has_value() && profile_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.ports[" + std::to_string(index) + "]",
                "capture port cylinder does not resolve in low-order gas topology");
        if (!cylinder_index.has_value() || !profile_index.has_value()) {
            continue;
        }
        const auto &topology = core.mechanism.cylinders[*profile_index].topology;
        const bool intake = port.kind.value == contract::PortKind::intake;
        const bool exhaust = port.kind.value == contract::PortKind::exhaust;
        const bool identity_matches = (intake && topology.intake_port_id == port.id) ||
                                      (exhaust && topology.exhaust_port_id == port.id);
        const auto duct = find_id_index(engine.gas_volumes,
                                        intake ? topology.intake_runner_volume_id
                                               : topology.exhaust_primary_volume_id);
        const auto edge =
            find_id_index(engine.flow_edges, intake ? topology.intake_valve_edge_id
                                                    : topology.exhaust_valve_edge_id);
        require(report,
                (intake || exhaust) && identity_matches && duct.has_value() &&
                    edge.has_value(),
                ContractIssueCode::inconsistent_semantics,
                "engine.ports[" + std::to_string(index) + "]",
                "capture port role, duct, or valve edge differs from gas topology");
        if ((intake || exhaust) && identity_matches && duct.has_value() &&
            edge.has_value()) {
            plan.port_bindings[index] = {
                *cylinder_index,
                *duct,
                *edge,
                port.kind.value,
            };
        }
    }

    plan.route_bindings.resize(engine.routes.size());
    std::size_t public_exhaust_index = 0;
    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        const auto &route = engine.routes[index];
        const auto profile_index = find_exhaust_profile_index(core, route.id);
        const bool exhaust =
            route.kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust && profile_index.has_value(),
                ContractIssueCode::unsupported_value,
                "engine.routes[" + std::to_string(index) + "]",
                "low-order capture admits exhaust-outlet source routes only");
        if (!exhaust || !profile_index.has_value()) {
            continue;
        }
        const auto &topology = core.gas_path.exhaust_routes[*profile_index].topology;
        const auto source =
            find_id_index(engine.gas_volumes, topology.collector_volume_id);
        const auto outlet =
            find_id_index(engine.flow_edges, topology.collector_outlet_edge_id);
        require(report,
                route.source_volume_id ==
                        std::optional<contract::GasVolumeId>{
                            topology.collector_volume_id} &&
                    source.has_value() && outlet.has_value(),
                ContractIssueCode::inconsistent_semantics,
                "engine.routes[" + std::to_string(index) + "]",
                "capture route source or outlet differs from gas topology");
        if (source.has_value() && outlet.has_value()) {
            plan.route_bindings[index] = {
                public_exhaust_index,
                *source,
                *outlet,
            };
        }
        ++public_exhaust_index;
    }

    append_layout_issues(report, plan);
    if (!report.ok() || !horizon.has_value()) {
        return report;
    }
    compiled.capture_horizon_frames = *horizon;
    return compiled;
}

} // namespace engine_sim_offline::simulation
