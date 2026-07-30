#include "bmw_m52b28_migration_test_support.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace engine_sim_offline::test::bmw_m52b28_migration {

ExecutionNames::ExecutionNames(const contract::EngineSpec &engine) {
    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &engine.physics_profile);
    if (profile == nullptr) {
        throw std::runtime_error{
            "BMW migration ID normalization requires operating-point v1"};
    }

    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        insert(banks_, engine.banks[index].id.value,
               "bank[" + std::to_string(index) + "]");
    }
    for (const auto &cylinder : engine.cylinders) {
        insert(cylinders_, cylinder.id.value, cylinder.semantic_id.value);
    }
    for (const auto &route : engine.routes) {
        insert(routes_, route.id.value, route.semantic_id.value);
    }

    for (const auto &volume : engine.gas_volumes) {
        if (volume.kind.value == contract::GasVolumeKind::atmosphere) {
            insert(volumes_, volume.id.value, "atmosphere");
        }
    }
    const auto &core = profile->core;
    insert(volumes_, core.gas_path.intake_topology.plenum_volume_id.value,
           "intake.plenum");
    insert(edges_, core.gas_path.intake_topology.main_throttle_edge_id.value,
           "intake.main-throttle");
    insert(edges_, core.gas_path.intake_topology.idle_bypass_edge_id.value,
           "intake.idle-bypass");

    for (const auto &assembly : core.mechanism.cylinders) {
        const auto owner = cylinder(assembly.topology.cylinder_id);
        insert(ports_, assembly.topology.intake_port_id.value,
               owner + "/port.intake");
        insert(ports_, assembly.topology.exhaust_port_id.value,
               owner + "/port.exhaust");
        insert(volumes_, assembly.topology.intake_runner_volume_id.value,
               owner + "/volume.intake-runner");
        insert(volumes_, assembly.topology.chamber_volume_id.value,
               owner + "/volume.chamber");
        insert(volumes_, assembly.topology.exhaust_primary_volume_id.value,
               owner + "/volume.exhaust-primary");
        insert(edges_, assembly.topology.plenum_to_runner_edge_id.value,
               owner + "/flow.plenum-to-runner");
        insert(edges_, assembly.topology.intake_valve_edge_id.value,
               owner + "/flow.intake-valve");
        insert(edges_, assembly.topology.exhaust_valve_edge_id.value,
               owner + "/flow.exhaust-valve");
        insert(edges_, assembly.topology.primary_to_collector_edge_id.value,
               owner + "/flow.primary-to-collector");
        insert(edges_, assembly.topology.blowby_edge_id.value,
               owner + "/flow.blowby");
    }
    for (const auto &route : core.gas_path.exhaust_routes) {
        const auto owner = this->route(route.topology.route_id);
        insert(volumes_, route.topology.collector_volume_id.value,
               owner + "/volume.collector");
        insert(edges_, route.topology.collector_outlet_edge_id.value,
               owner + "/flow.collector-outlet");
    }

    require_complete("bank", banks_, engine.banks.size());
    require_complete("cylinder", cylinders_, engine.cylinders.size());
    require_complete("port", ports_, engine.ports.size());
    require_complete("gas volume", volumes_, engine.gas_volumes.size());
    require_complete("flow edge", edges_, engine.flow_edges.size());
    require_complete("route", routes_, engine.routes.size());
}

std::string ExecutionNames::bank(const contract::BankId id) const {
    return lookup(banks_, id.value, "bank");
}

std::string ExecutionNames::cylinder(const contract::CylinderId id) const {
    return lookup(cylinders_, id.value, "cylinder");
}

std::string ExecutionNames::port(const contract::PortId id) const {
    return lookup(ports_, id.value, "port");
}

std::string ExecutionNames::volume(const contract::GasVolumeId id) const {
    return lookup(volumes_, id.value, "gas volume");
}

std::string ExecutionNames::edge(const contract::FlowEdgeId id) const {
    return lookup(edges_, id.value, "flow edge");
}

std::string ExecutionNames::route(const contract::RouteId id) const {
    return lookup(routes_, id.value, "route");
}

void ExecutionNames::insert(Names &names, const std::uint32_t id,
                            std::string name) {
    const auto found = names.find(id);
    if (found != names.end() && found->second != name) {
        throw std::runtime_error{
            "one runtime ID resolved to multiple normalized roles"};
    }
    if (found == names.end()) {
        names.emplace(id, std::move(name));
    }
}

std::string ExecutionNames::lookup(const Names &names, const std::uint32_t id,
                                   const std::string_view kind) {
    const auto found = names.find(id);
    if (found == names.end()) {
        throw std::runtime_error{"unmapped " + std::string{kind} +
                                 " runtime ID " + std::to_string(id)};
    }
    return found->second;
}

void ExecutionNames::require_complete(const std::string_view kind,
                                      const Names &names,
                                      const std::size_t expected) {
    if (names.size() != expected) {
        throw std::runtime_error{
            "normalized " + std::string{kind} + " inventory has " +
            std::to_string(names.size()) + " entries; engine exposes " +
            std::to_string(expected)};
    }
}

ExecutionProjection project_engine(const contract::EngineSpec &engine) {
    ExecutionProjection out;
    const ExecutionNames names{engine};
    out.enumeration("engine.cycle", engine.cycle.value);
    out.enumeration("engine.ignition", engine.ignition.value);
    out.enumeration("engine.cylinder_layout", engine.cylinder_layout.value);
    out.binary64("engine.total_displacement_m3",
                 engine.total_displacement_m3.value);

    out.count("engine.banks.count", engine.banks.size());
    const auto banks = ordered_by_name(
        engine.banks,
        [&](const contract::BankSpec &bank) { return names.bank(bank.id); });
    for (std::size_t index = 0; index < banks.size(); ++index) {
        out.text("engine.banks[" + std::to_string(index) + "].role",
                 names.bank(banks[index]->id));
    }
    out.count("engine.cylinders.count", engine.cylinders.size());
    const auto cylinders = ordered_by_name(
        engine.cylinders, [&](const contract::CylinderSpec &cylinder) {
            return names.cylinder(cylinder.id);
        });
    for (std::size_t index = 0; index < cylinders.size(); ++index) {
        const auto base = "engine.cylinders[" + std::to_string(index) + "]";
        const auto &cylinder = *cylinders[index];
        out.text(base + ".role", names.cylinder(cylinder.id));
        out.text(base + ".bank", names.bank(cylinder.bank_id));
        out.binary64(base + ".bore_m", cylinder.bore_m.value);
        out.binary64(base + ".stroke_m", cylinder.stroke_m.value);
        out.binary64(base + ".connecting_rod_length_m",
                     cylinder.connecting_rod_length_m.value);
        out.binary64(base + ".compression_ratio",
                     cylinder.compression_ratio.value);
        out.binary64(base + ".firing_tdc_offset_rad",
                     cylinder.firing_tdc_offset_rad.value);
        out.binary64(base + ".journal_phase_rad",
                     cylinder.journal_phase_rad.value);
    }
    out.count("engine.ports.count", engine.ports.size());
    const auto ports = ordered_by_name(
        engine.ports,
        [&](const contract::PortSpec &port) { return names.port(port.id); });
    for (std::size_t index = 0; index < ports.size(); ++index) {
        const auto base = "engine.ports[" + std::to_string(index) + "]";
        const auto &port = *ports[index];
        out.text(base + ".role", names.port(port.id));
        out.text(base + ".cylinder", names.cylinder(port.cylinder_id));
        out.enumeration(base + ".kind", port.kind.value);
    }
    out.count("engine.gas_volumes.count", engine.gas_volumes.size());
    const auto volumes = ordered_by_name(
        engine.gas_volumes, [&](const contract::GasVolumeSpec &volume) {
            return names.volume(volume.id);
        });
    for (std::size_t index = 0; index < volumes.size(); ++index) {
        const auto base = "engine.gas_volumes[" + std::to_string(index) + "]";
        const auto &volume = *volumes[index];
        out.text(base + ".role", names.volume(volume.id));
        out.enumeration(base + ".kind", volume.kind.value);
    }
    out.count("engine.flow_edges.count", engine.flow_edges.size());
    const auto edges = ordered_by_name(
        engine.flow_edges, [&](const contract::FlowEdgeSpec &edge) {
            return names.edge(edge.id);
        });
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto base = "engine.flow_edges[" + std::to_string(index) + "]";
        const auto &edge = *edges[index];
        out.text(base + ".role", names.edge(edge.id));
        out.text(base + ".endpoint_0_volume",
                 names.volume(edge.endpoint_0_volume_id));
        out.text(base + ".endpoint_1_volume",
                 names.volume(edge.endpoint_1_volume_id));
    }
    out.count("engine.routes.count", engine.routes.size());
    const auto routes = ordered_by_name(
        engine.routes,
        [&](const contract::RouteSpec &route) { return names.route(route.id); });
    for (std::size_t index = 0; index < routes.size(); ++index) {
        const auto base = "engine.routes[" + std::to_string(index) + "]";
        const auto &route = *routes[index];
        out.text(base + ".role", names.route(route.id));
        out.enumeration(base + ".kind", route.kind.value);
        out.boolean(base + ".source_volume_id.present",
                    route.source_volume_id.has_value());
        if (route.source_volume_id) {
            out.text(base + ".source_volume",
                     names.volume(*route.source_volume_id));
        }
        out.boolean(base + ".default_parent_route_id.present",
                    route.default_parent_route_id.has_value());
        if (route.default_parent_route_id) {
            out.text(base + ".default_parent_route",
                     names.route(*route.default_parent_route_id));
        }
    }

    out.method("engine.methods.mechanism", engine.methods.mechanism.value);
    out.method("engine.methods.valvetrain", engine.methods.valvetrain.value);
    out.method("engine.methods.gas_exchange", engine.methods.gas_exchange.value);
    out.method("engine.methods.ignition", engine.methods.ignition.value);
    out.method("engine.methods.combustion", engine.methods.combustion.value);
    out.method("engine.methods.heat_transfer",
               engine.methods.heat_transfer.value);
    out.method("engine.methods.losses", engine.methods.losses.value);
    out.method("engine.methods.excitation", engine.methods.excitation.value);

    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &engine.physics_profile);
    if (profile == nullptr) {
        throw std::runtime_error{
            "BMW migration comparison requires low-order operating-point v1"};
    }
    const std::string root = "engine.physics";
    add_mechanism(out, root + ".mechanism", profile->core.mechanism, names);
    add_gas_path(out, root + ".gas_path", profile->core.gas_path, names);
    add_camshaft(out, root + ".valvetrain.intake",
                 profile->core.valvetrain.intake, names);
    add_camshaft(out, root + ".valvetrain.exhaust",
                 profile->core.valvetrain.exhaust, names);
    add_ignition_and_fuel(out, root, profile->core, names);
    add_excitation(out, root + ".excitation", profile->core.excitation, names);

    out.binary64(root + ".aggregate_loss.constant_fmep_bar",
                 profile->aggregate_loss.constant_fmep_bar.value);
    out.binary64(root + ".aggregate_loss.peak_pressure_coefficient",
                 profile->aggregate_loss.peak_pressure_coefficient.value);
    out.binary64(
        root + ".aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m",
        profile->aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value);
    out.binary64(
        root +
            ".aggregate_loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2",
        profile->aggregate_loss
            .mean_piston_speed_squared_coefficient_bar_s2_per_m2.value);
    out.binary64(root + ".aggregate_loss.required_oil_temperature_k",
                 profile->aggregate_loss.required_oil_temperature_k.value);
    out.integer(root + ".aggregate_loss.included_terms",
                profile->aggregate_loss.included_terms.value);
    // The clean authored accessory ID replaces the temporary oracle ID. Its
    // hash-bound content remains execution identity.
    out.digest(root + ".accessory_configuration.content_sha256",
               profile->accessory_configuration.content_sha256.value);
    out.boolean(root + ".starter.mechanically_disengaged",
                profile->starter.mechanically_disengaged.value);
    out.integer(root + ".starter.included_terms",
                profile->starter.included_terms.value);
    out.method(root + ".cycle_quadrature", profile->cycle_quadrature.value);
    add_torque_capability(out, "engine.torque_capability",
                          engine.torque_capability.value);
    return out;
}

} // namespace engine_sim_offline::test::bmw_m52b28_migration
