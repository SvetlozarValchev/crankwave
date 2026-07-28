#include "simulation/legacy_low_order_simulation.hpp"

#include "simulation/legacy_low_order_capture_buffer.hpp"

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
find_cylinder_profile_index(const contract::LegacyLowOrderV1Profile &profile,
                            contract::CylinderId cylinder_id) {
    const auto &cylinders = profile.mechanism.cylinders;
    const auto found = std::ranges::find_if(cylinders, [&](const auto &cylinder) {
        return cylinder.topology.cylinder_id == cylinder_id;
    });
    if (found == cylinders.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - cylinders.begin());
}

[[nodiscard]] std::optional<std::size_t>
find_exhaust_profile_index(const contract::LegacyLowOrderV1Profile &profile,
                           contract::RouteId route_id) {
    const auto &routes = profile.gas_path.exhaust_routes;
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

} // namespace

LegacySimulationCompileResult
compile_legacy_low_order_simulation_session(const contract::EngineSpec &engine,
                                            const contract::RenderScenario &scenario) {
    auto mechanics_result =
        compile_legacy_low_order_mechanics_session(engine, scenario);
    if (const auto *report = std::get_if<ValidationReport>(&mechanics_result)) {
        return *report;
    }
    auto mechanics =
        std::get<LegacyLowOrderMechanicsSession>(std::move(mechanics_result));

    auto gas_result = compile_legacy_low_order_gas_session(engine, scenario,
                                                           mechanics.cylinder_models());
    if (const auto *report = std::get_if<ValidationReport>(&gas_result)) {
        return *report;
    }
    auto gas = std::get<LegacyLowOrderGasSession>(std::move(gas_result));

    ValidationReport report;
    require(report,
            scenario.rates.physics == contract::RationalRateHz{10000, 1} &&
                scenario.rates.capture == contract::RationalRateHz{10000, 1},
            ContractIssueCode::unsupported_value, "scenario.rates",
            "legacy capture requires exact 10000/1 Hz physics and capture clocks");
    require(report,
            scenario.quality.value.capture_block_capacity_frames ==
                kLegacyCaptureFramesPerBlock,
            ContractIssueCode::unsupported_value,
            "scenario.quality.value.capture_block_capacity_frames",
            "legacy capture requires the canonical 200-frame partition capacity");
    require(report,
            scenario.quality.value.event_journal_capacity_records ==
                kLegacyCaptureFramesPerBlock * kLegacyMaximumEventsPerFrame,
            ContractIssueCode::unsupported_value,
            "scenario.quality.value.event_journal_capacity_records",
            "legacy capture requires the canonical 3800-record event capacity");

    const auto horizon = contract::resolve_frame_index(scenario.total_duration_s.value,
                                                       scenario.rates.capture);
    require(report, horizon.has_value() && *horizon > 0U,
            ContractIssueCode::inconsistent_semantics,
            "scenario.total_duration_s.value",
            "legacy capture horizon must resolve to a positive integral frame count");

    const auto *profile =
        std::get_if<contract::LegacyLowOrderV1Profile>(&engine.physics_profile);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "legacy capture requires a LegacyLowOrderV1Profile");
    require(report,
            reserve_product_representable(engine.cylinders.size()) &&
                reserve_product_representable(engine.ports.size()) &&
                reserve_product_representable(engine.gas_volumes.size()) &&
                reserve_product_representable(engine.flow_edges.size()) &&
                reserve_product_representable(engine.routes.size()),
            ContractIssueCode::unsupported_value, "engine",
            "capture entity count overflows bounded frame-major storage");
    if (!report.ok() || profile == nullptr || !horizon.has_value()) {
        return report;
    }

    detail::LegacyCaptureBufferPlan plan;
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
    for (const auto &volume : engine.gas_volumes) {
        plan.gas_volumes.push_back({volume.id, volume.kind.value});
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
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto profile_index =
            find_cylinder_profile_index(*profile, engine.cylinders[index].id);
        require(report, profile_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "]",
                "capture could not bind cylinder to legacy gas topology");
        if (!profile_index.has_value()) {
            continue;
        }
        const auto &topology = profile->mechanism.cylinders[*profile_index].topology;
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
    }

    plan.port_bindings.resize(engine.ports.size());
    for (std::size_t index = 0; index < engine.ports.size(); ++index) {
        const auto &port = engine.ports[index];
        const auto cylinder_index = find_id_index(engine.cylinders, port.cylinder_id);
        const auto profile_index =
            find_cylinder_profile_index(*profile, port.cylinder_id);
        require(report, cylinder_index.has_value() && profile_index.has_value(),
                ContractIssueCode::dangling_reference,
                "engine.ports[" + std::to_string(index) + "]",
                "capture port cylinder does not resolve in legacy gas topology");
        if (!cylinder_index.has_value() || !profile_index.has_value()) {
            continue;
        }
        const auto &topology = profile->mechanism.cylinders[*profile_index].topology;
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
        const auto profile_index = find_exhaust_profile_index(*profile, route.id);
        const bool exhaust =
            route.kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust && profile_index.has_value(),
                ContractIssueCode::unsupported_value,
                "engine.routes[" + std::to_string(index) + "]",
                "M3 capture admits exhaust-outlet source routes only");
        if (!exhaust || !profile_index.has_value()) {
            continue;
        }
        const auto &topology =
            profile->gas_path.exhaust_routes[*profile_index].topology;
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

    const auto layout = contract::CaptureLayoutView::borrow_for_callback(
        plan.engine_id, plan.cylinders, plan.ports, plan.gas_volumes, plan.flow_edges,
        plan.routes);
    const auto layout_report = contract::validate(layout);
    for (const auto &issue : layout_report.issues) {
        report.add(issue.code, "capture.layout." + issue.path, issue.message);
    }
    if (!report.ok()) {
        return report;
    }

    detail::LegacyLowOrderCaptureBuffer capture{std::move(plan)};
    return LegacyLowOrderSimulationSession{
        std::move(mechanics),
        std::move(gas),
        std::move(capture),
        *horizon,
        engine.methods.gas_exchange.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
