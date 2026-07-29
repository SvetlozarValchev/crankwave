#include "engine_sim_offline/contract/exhaust_acoustics.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

[[nodiscard]] std::string child(std::string_view root, std::string_view suffix) {
    return std::string(root) + "." + std::string(suffix);
}

[[nodiscard]] bool known(AcousticDuctKind kind) noexcept {
    return kind == AcousticDuctKind::primary || kind == AcousticDuctKind::downstream;
}

template <class T>
void authored_leaf(ValidationReport &report, const AuthoredValue<T> &value,
                   const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_authored_value(report, value, provenance, path);
}

template <class T>
void resolved_leaf(ValidationReport &report, const ResolvedValue<T> &value,
                   const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

void append_rate_report(ValidationReport &report, const RationalRateHz &rate,
                        const std::string &path) {
    detail::append_prefixed(report, validate(rate), path);
}

void validate_authored_method(ValidationReport &report,
                              const AuthoredValue<MethodSelection> &method,
                              const ProvenanceLedger &provenance,
                              const std::string &path) {
    authored_leaf(report, method, provenance, path);
    detail::require(report, is_valid_semantic_id(method.value.id),
                    ContractIssueCode::invalid_value, path + ".value.id",
                    "method selection ID must be canonical");
    detail::require(report, method.value.version != 0,
                    ContractIssueCode::invalid_value, path + ".value.version",
                    "method selection version must be positive");
}

void validate_resolved_method(ValidationReport &report,
                              const ResolvedValue<MethodIdentity> &method,
                              const ProvenanceLedger &provenance,
                              const std::string &path) {
    resolved_leaf(report, method, provenance, path);
    detail::append_prefixed(report, validate(method.value), path + ".value");
}

template <class Range, class Projection>
[[nodiscard]] bool unique_strings(const Range &range, Projection projection) {
    std::unordered_set<std::string> seen;
    return std::ranges::all_of(range, [&](const auto &value) {
        return seen.insert(std::invoke(projection, value)).second;
    });
}

template <class Id>
[[nodiscard]] bool insert_unique(std::unordered_set<std::uint32_t> &seen, Id id) {
    return id.valid() && seen.insert(id.value).second;
}

[[nodiscard]] const AuthoredCylinderDefinition *
find_cylinder(const AuthoredEngineDefinition &engine, std::string_view semantic_id) {
    const auto found = std::ranges::find_if(engine.cylinders, [&](const auto &value) {
        return value.semantic_id.value == semantic_id;
    });
    return found == engine.cylinders.end() ? nullptr : &*found;
}

[[nodiscard]] const AuthoredPortDefinition *
find_port(const AuthoredEngineDefinition &engine, std::string_view semantic_id) {
    const auto found = std::ranges::find_if(engine.ports, [&](const auto &value) {
        return value.semantic_id.value == semantic_id;
    });
    return found == engine.ports.end() ? nullptr : &*found;
}

[[nodiscard]] const AuthoredRouteDefinition *
find_route(const AuthoredEngineDefinition &engine, std::string_view semantic_id) {
    const auto found = std::ranges::find_if(engine.routes, [&](const auto &value) {
        return value.semantic_id.value == semantic_id;
    });
    return found == engine.routes.end() ? nullptr : &*found;
}

[[nodiscard]] const CylinderSpec *find_cylinder(const EngineSpec &engine,
                                                CylinderId id) {
    const auto found = std::ranges::find(engine.cylinders, id, &CylinderSpec::id);
    return found == engine.cylinders.end() ? nullptr : &*found;
}

[[nodiscard]] const PortSpec *find_port(const EngineSpec &engine, PortId id) {
    const auto found = std::ranges::find(engine.ports, id, &PortSpec::id);
    return found == engine.ports.end() ? nullptr : &*found;
}

[[nodiscard]] const RouteSpec *find_route(const EngineSpec &engine, RouteId id) {
    const auto found = std::ranges::find(engine.routes, id, &RouteSpec::id);
    return found == engine.routes.end() ? nullptr : &*found;
}

[[nodiscard]] std::string cylinder_name(const EngineSpec &engine, CylinderId id) {
    const auto *value = find_cylinder(engine, id);
    return value == nullptr ? "unknown-cylinder-" + std::to_string(id.value)
                            : value->semantic_id.value;
}

[[nodiscard]] std::string route_name(const EngineSpec &engine, RouteId id) {
    const auto *value = find_route(engine, id);
    return value == nullptr ? "unknown-route-" + std::to_string(id.value)
                            : value->semantic_id.value;
}

void validate_authored_scalar_fields(ValidationReport &report,
                                     const AuthoredExhaustAcousticAssembly &assembly,
                                     const ProvenanceLedger &provenance,
                                     std::string_view root) {
    const auto path = [&](std::string_view suffix) { return child(root, suffix); };
    authored_leaf(report, assembly.assembly_id, provenance, path("assembly_id"));
    detail::require(report, is_valid_semantic_id(assembly.assembly_id.value),
                    ContractIssueCode::invalid_value, path("assembly_id.value"),
                    "exhaust acoustic assembly ID must be canonical");

    validate_authored_method(report, assembly.methods.source_properties, provenance,
                             path("methods.source_properties"));
    validate_authored_method(report, assembly.methods.reconstruction, provenance,
                             path("methods.reconstruction"));
    validate_authored_method(report, assembly.methods.waveguide, provenance,
                             path("methods.waveguide"));
    validate_authored_method(report, assembly.methods.junction, provenance,
                             path("methods.junction"));
    validate_authored_method(report, assembly.methods.outlet_reflection, provenance,
                             path("methods.outlet_reflection"));
    validate_authored_method(report, assembly.methods.exterior_radiation, provenance,
                             path("methods.exterior_radiation"));

    authored_leaf(report, assembly.source_interval_rate, provenance,
                  path("source_interval_rate"));
    authored_leaf(report, assembly.acoustic_rate, provenance, path("acoustic_rate"));
    authored_leaf(report, assembly.universal_gas_constant_j_per_mol_k, provenance,
                  path("universal_gas_constant_j_per_mol_k"));
    authored_leaf(report, assembly.source_molar_mass_kg_per_mol, provenance,
                  path("source_molar_mass_kg_per_mol"));
    authored_leaf(report, assembly.source_heat_capacity_ratio, provenance,
                  path("source_heat_capacity_ratio"));
    authored_leaf(report, assembly.pa_per_full_scale, provenance,
                  path("pa_per_full_scale"));

    append_rate_report(report, assembly.source_interval_rate.value,
                       path("source_interval_rate.value"));
    append_rate_report(report, assembly.acoustic_rate.value,
                       path("acoustic_rate.value"));
    detail::require(report,
                    assembly.source_interval_rate.value == RationalRateHz{80000, 1},
                    ContractIssueCode::unsupported_value,
                    path("source_interval_rate.value"),
                    "the admitted exhaust source interval rate is exactly 80000/1 Hz");
    detail::require(report, assembly.acoustic_rate.value == RationalRateHz{192000, 1},
                    ContractIssueCode::unsupported_value, path("acoustic_rate.value"),
                    "the admitted exhaust acoustic rate is exactly 192000/1 Hz");
    detail::require(
        report,
        detail::finite_positive(assembly.universal_gas_constant_j_per_mol_k.value) &&
            detail::finite_positive(assembly.source_molar_mass_kg_per_mol.value) &&
            detail::finite(assembly.source_heat_capacity_ratio.value) &&
            assembly.source_heat_capacity_ratio.value > 1.0 &&
            detail::finite_positive(assembly.pa_per_full_scale.value),
        ContractIssueCode::invalid_value, std::string(root),
        "gas-property and Pa calibration scalars must be finite and physical");
}

void validate_resolved_scalar_fields(ValidationReport &report,
                                     const ExhaustAcousticAssembly &assembly,
                                     const ProvenanceLedger &provenance,
                                     std::string_view root) {
    const auto path = [&](std::string_view suffix) { return child(root, suffix); };
    resolved_leaf(report, assembly.assembly_id, provenance, path("assembly_id"));
    detail::require(report, is_valid_semantic_id(assembly.assembly_id.value),
                    ContractIssueCode::invalid_value, path("assembly_id.value"),
                    "exhaust acoustic assembly ID must be canonical");

    validate_resolved_method(report, assembly.methods.source_properties, provenance,
                             path("methods.source_properties"));
    validate_resolved_method(report, assembly.methods.reconstruction, provenance,
                             path("methods.reconstruction"));
    validate_resolved_method(report, assembly.methods.waveguide, provenance,
                             path("methods.waveguide"));
    validate_resolved_method(report, assembly.methods.junction, provenance,
                             path("methods.junction"));
    validate_resolved_method(report, assembly.methods.outlet_reflection, provenance,
                             path("methods.outlet_reflection"));
    validate_resolved_method(report, assembly.methods.exterior_radiation, provenance,
                             path("methods.exterior_radiation"));

    resolved_leaf(report, assembly.source_interval_rate, provenance,
                  path("source_interval_rate"));
    resolved_leaf(report, assembly.acoustic_rate, provenance, path("acoustic_rate"));
    resolved_leaf(report, assembly.universal_gas_constant_j_per_mol_k, provenance,
                  path("universal_gas_constant_j_per_mol_k"));
    resolved_leaf(report, assembly.source_molar_mass_kg_per_mol, provenance,
                  path("source_molar_mass_kg_per_mol"));
    resolved_leaf(report, assembly.source_heat_capacity_ratio, provenance,
                  path("source_heat_capacity_ratio"));
    resolved_leaf(report, assembly.pa_per_full_scale, provenance,
                  path("pa_per_full_scale"));

    append_rate_report(report, assembly.source_interval_rate.value,
                       path("source_interval_rate.value"));
    append_rate_report(report, assembly.acoustic_rate.value,
                       path("acoustic_rate.value"));
    detail::require(report,
                    assembly.source_interval_rate.value == RationalRateHz{80000, 1},
                    ContractIssueCode::unsupported_value,
                    path("source_interval_rate.value"),
                    "the admitted exhaust source interval rate is exactly 80000/1 Hz");
    detail::require(report, assembly.acoustic_rate.value == RationalRateHz{192000, 1},
                    ContractIssueCode::unsupported_value, path("acoustic_rate.value"),
                    "the admitted exhaust acoustic rate is exactly 192000/1 Hz");
    detail::require(
        report,
        detail::finite_positive(assembly.universal_gas_constant_j_per_mol_k.value) &&
            detail::finite_positive(assembly.source_molar_mass_kg_per_mol.value) &&
            detail::finite(assembly.source_heat_capacity_ratio.value) &&
            assembly.source_heat_capacity_ratio.value > 1.0 &&
            detail::finite_positive(assembly.pa_per_full_scale.value),
        ContractIssueCode::invalid_value, std::string(root),
        "gas-property and Pa calibration scalars must be finite and physical");
}

} // namespace

ValidationReport validate(const AuthoredExhaustAcousticAssembly &assembly,
                          const AuthoredEngineDefinition &engine,
                          const ProvenanceLedger &provenance,
                          std::string_view root_path) {
    ValidationReport report;
    detail::require(report, !root_path.empty(), ContractIssueCode::missing_value, "",
                    "exhaust acoustic provenance root must be nonempty");
    validate_authored_scalar_fields(report, assembly, provenance, root_path);

    detail::require(report, !assembly.ducts.empty(), ContractIssueCode::missing_value,
                    child(root_path, "ducts"),
                    "exhaust acoustic assembly requires ducts");
    detail::require(report, !assembly.primary_bindings.empty(),
                    ContractIssueCode::missing_value,
                    child(root_path, "primary_bindings"),
                    "exhaust acoustic assembly requires primary bindings");
    detail::require(report, !assembly.junctions.empty(), ContractIssueCode::missing_value,
                    child(root_path, "junctions"),
                    "exhaust acoustic assembly requires junctions");
    detail::require(report, !assembly.outlets.empty(), ContractIssueCode::missing_value,
                    child(root_path, "outlets"),
                    "exhaust acoustic assembly requires exterior outlets");

    detail::require(report,
                    unique_strings(assembly.ducts, [](const auto &value) {
                        return value.semantic_id.value;
                    }),
                    ContractIssueCode::duplicate_identity, child(root_path, "ducts"),
                    "acoustic duct semantic IDs must be unique");
    detail::require(report,
                    unique_strings(assembly.junctions, [](const auto &value) {
                        return value.semantic_id.value;
                    }),
                    ContractIssueCode::duplicate_identity,
                    child(root_path, "junctions"),
                    "acoustic junction semantic IDs must be unique");

    std::unordered_map<std::string, AcousticDuctKind> duct_kinds;
    for (std::size_t index = 0; index < assembly.ducts.size(); ++index) {
        const auto &duct = assembly.ducts[index];
        const auto base = child(root_path, "ducts." + duct.semantic_id.value);
        authored_leaf(report, duct.semantic_id, provenance, base + ".semantic_id");
        authored_leaf(report, duct.kind, provenance, base + ".kind");
        authored_leaf(report, duct.length_m, provenance, base + ".length_m");
        authored_leaf(report, duct.inner_diameter_m, provenance,
                      base + ".inner_diameter_m");
        authored_leaf(report, duct.reference_temperature_k, provenance,
                      base + ".reference_temperature_k");
        authored_leaf(report, duct.propagation_loss_np_per_m, provenance,
                      base + ".propagation_loss_np_per_m");
        detail::require(report, is_valid_semantic_id(duct.semantic_id.value),
                        ContractIssueCode::invalid_value, base + ".semantic_id.value",
                        "acoustic duct semantic ID must be canonical");
        detail::require(
            report,
            known(duct.kind.value) && detail::finite_positive(duct.length_m.value) &&
                detail::finite_positive(duct.inner_diameter_m.value) &&
                detail::finite_positive(duct.reference_temperature_k.value) &&
                detail::finite_nonnegative(duct.propagation_loss_np_per_m.value),
            ContractIssueCode::invalid_value, base,
            "acoustic duct kind and physical scalars are outside their domain");
        duct_kinds.emplace(duct.semantic_id.value, duct.kind.value);
    }

    std::unordered_map<std::string, const AuthoredExhaustAcousticJunction *> junctions;
    std::unordered_set<std::string> junction_downstream;
    std::unordered_set<std::string> junction_primaries;
    for (const auto &junction : assembly.junctions) {
        const auto base = child(root_path, "junctions." + junction.semantic_id.value);
        authored_leaf(report, junction.semantic_id, provenance, base + ".semantic_id");
        authored_leaf(report, junction.downstream_duct_id, provenance,
                      base + ".downstream_duct_id");
        detail::require(report, is_valid_semantic_id(junction.semantic_id.value),
                        ContractIssueCode::invalid_value, base + ".semantic_id.value",
                        "acoustic junction semantic ID must be canonical");
        detail::require(report, !junction.primary_duct_ids.empty(),
                        ContractIssueCode::inconsistent_shape,
                        base + ".primary_duct_ids",
                        "an exhaust junction requires at least one primary duct");
        std::unordered_set<std::string> local;
        for (std::size_t index = 0; index < junction.primary_duct_ids.size(); ++index) {
            const auto &id = junction.primary_duct_ids[index];
            authored_leaf(report, id, provenance,
                          base + ".primary_duct_ids[" + std::to_string(index) + "]");
            const auto kind = duct_kinds.find(id.value);
            detail::require(report,
                            kind != duct_kinds.end() &&
                                kind->second == AcousticDuctKind::primary,
                            ContractIssueCode::dangling_reference,
                            base + ".primary_duct_ids[" + std::to_string(index) +
                                "].value",
                            "junction primary must reference a primary duct");
            detail::require(report, local.insert(id.value).second,
                            ContractIssueCode::duplicate_identity,
                            base + ".primary_duct_ids",
                            "junction primary duct IDs must be unique");
            detail::require(report, junction_primaries.insert(id.value).second,
                            ContractIssueCode::duplicate_identity,
                            base + ".primary_duct_ids",
                            "a primary duct cannot enter multiple junctions");
        }
        const auto downstream = duct_kinds.find(junction.downstream_duct_id.value);
        detail::require(report,
                        downstream != duct_kinds.end() &&
                            downstream->second == AcousticDuctKind::downstream,
                        ContractIssueCode::dangling_reference,
                        base + ".downstream_duct_id.value",
                        "junction downstream must reference a downstream duct");
        detail::require(report,
                        junction_downstream.insert(junction.downstream_duct_id.value)
                            .second,
                        ContractIssueCode::duplicate_identity,
                        base + ".downstream_duct_id.value",
                        "a downstream duct cannot leave multiple junctions");
        junctions.emplace(junction.semantic_id.value, &junction);
    }

    std::unordered_set<std::string> bound_cylinders;
    std::unordered_set<std::string> bound_ports;
    std::unordered_set<std::string> bound_primaries;
    for (std::size_t index = 0; index < assembly.primary_bindings.size(); ++index) {
        const auto &binding = assembly.primary_bindings[index];
        const auto base = child(root_path, "primary_bindings[" +
                                               std::to_string(index) + "]");
        authored_leaf(report, binding.cylinder_id, provenance, base + ".cylinder_id");
        authored_leaf(report, binding.exhaust_port_id, provenance,
                      base + ".exhaust_port_id");
        authored_leaf(report, binding.primary_duct_id, provenance,
                      base + ".primary_duct_id");
        authored_leaf(report, binding.junction_id, provenance, base + ".junction_id");

        const auto *cylinder = find_cylinder(engine, binding.cylinder_id.value);
        const auto *port = find_port(engine, binding.exhaust_port_id.value);
        const auto duct = duct_kinds.find(binding.primary_duct_id.value);
        const auto junction = junctions.find(binding.junction_id.value);
        detail::require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                        base + ".cylinder_id.value",
                        "primary binding cylinder is absent from the engine");
        detail::require(
            report,
            port != nullptr && port->kind.value == PortKind::exhaust &&
                port->cylinder_id.value == binding.cylinder_id.value,
            ContractIssueCode::dangling_reference, base + ".exhaust_port_id.value",
            "primary binding must reference that cylinder's exhaust port");
        detail::require(report,
                        duct != duct_kinds.end() &&
                            duct->second == AcousticDuctKind::primary,
                        ContractIssueCode::dangling_reference,
                        base + ".primary_duct_id.value",
                        "primary binding must reference a primary duct");
        detail::require(report, junction != junctions.end(),
                        ContractIssueCode::dangling_reference,
                        base + ".junction_id.value",
                        "primary binding must reference an acoustic junction");
        if (junction != junctions.end()) {
            detail::require(
                report,
                std::ranges::any_of(junction->second->primary_duct_ids,
                                    [&](const auto &id) {
                                        return id.value == binding.primary_duct_id.value;
                                    }),
                ContractIssueCode::inconsistent_semantics, base,
                "primary binding junction does not contain its primary duct");
        }
        detail::require(report, bound_cylinders.insert(binding.cylinder_id.value).second,
                        ContractIssueCode::duplicate_identity, base + ".cylinder_id.value",
                        "a cylinder may own only one exhaust acoustic primary");
        detail::require(report, bound_ports.insert(binding.exhaust_port_id.value).second,
                        ContractIssueCode::duplicate_identity,
                        base + ".exhaust_port_id.value",
                        "an exhaust port may own only one acoustic primary");
        detail::require(report,
                        bound_primaries.insert(binding.primary_duct_id.value).second,
                        ContractIssueCode::duplicate_identity,
                        base + ".primary_duct_id.value",
                        "a primary duct may have only one source binding");
    }

    std::size_t exhaust_port_count = 0;
    for (const auto &port : engine.ports) {
        if (port.kind.value == PortKind::exhaust) {
            ++exhaust_port_count;
            detail::require(report, bound_ports.contains(port.semantic_id.value),
                            ContractIssueCode::missing_value,
                            child(root_path, "primary_bindings"),
                            "every engine exhaust port requires one primary binding");
        }
    }
    detail::require(report, assembly.primary_bindings.size() == exhaust_port_count,
                    ContractIssueCode::inconsistent_shape,
                    child(root_path, "primary_bindings"),
                    "primary binding count must equal engine exhaust-port count");
    detail::require(report, bound_primaries == junction_primaries,
                    ContractIssueCode::inconsistent_semantics,
                    child(root_path, "primary_bindings"),
                    "junction and source-binding primary duct sets must match exactly");

    std::unordered_set<std::string> outlet_routes;
    std::unordered_set<std::string> outlet_downstream;
    for (std::size_t index = 0; index < assembly.outlets.size(); ++index) {
        const auto &outlet = assembly.outlets[index];
        const auto base = child(root_path, "outlets[" + std::to_string(index) + "]");
        authored_leaf(report, outlet.route_id, provenance, base + ".route_id");
        authored_leaf(report, outlet.downstream_duct_id, provenance,
                      base + ".downstream_duct_id");
        authored_leaf(report, outlet.observation_distance_m, provenance,
                      base + ".observation_distance_m");
        const auto *route = find_route(engine, outlet.route_id.value);
        const auto duct = duct_kinds.find(outlet.downstream_duct_id.value);
        detail::require(report,
                        route != nullptr &&
                            route->kind.value == SourceRouteKind::exhaust_outlet,
                        ContractIssueCode::dangling_reference, base + ".route_id.value",
                        "outlet route must reference an engine exhaust-outlet route");
        detail::require(report,
                        duct != duct_kinds.end() &&
                            duct->second == AcousticDuctKind::downstream &&
                            junction_downstream.contains(outlet.downstream_duct_id.value),
                        ContractIssueCode::dangling_reference,
                        base + ".downstream_duct_id.value",
                        "outlet must terminate one junction downstream duct");
        detail::require(report,
                        detail::finite_positive(outlet.observation_distance_m.value),
                        ContractIssueCode::invalid_value,
                        base + ".observation_distance_m.value",
                        "outlet observation distance must be finite and positive");
        detail::require(report, outlet_routes.insert(outlet.route_id.value).second,
                        ContractIssueCode::duplicate_identity, base + ".route_id.value",
                        "an exterior route may appear only once");
        detail::require(
            report, outlet_downstream.insert(outlet.downstream_duct_id.value).second,
            ContractIssueCode::duplicate_identity, base + ".downstream_duct_id.value",
            "a downstream duct may terminate at only one exterior outlet");
    }
    detail::require(report, outlet_downstream == junction_downstream,
                    ContractIssueCode::inconsistent_semantics,
                    child(root_path, "outlets"),
                    "every junction downstream duct must terminate at one outlet");
    for (const auto &route : engine.routes) {
        if (route.kind.value == SourceRouteKind::exhaust_outlet) {
            detail::require(report, outlet_routes.contains(route.semantic_id.value),
                            ContractIssueCode::missing_value,
                            child(root_path, "outlets"),
                            "every engine exhaust-outlet route requires one outlet");
        }
    }

    return report;
}

ValidationReport validate(const ExhaustAcousticAssembly &assembly,
                          const EngineSpec &engine,
                          const ProvenanceLedger &provenance,
                          std::string_view root_path) {
    ValidationReport report;
    detail::require(report, !root_path.empty(), ContractIssueCode::missing_value, "",
                    "exhaust acoustic provenance root must be nonempty");
    validate_resolved_scalar_fields(report, assembly, provenance, root_path);

    detail::require(report, !assembly.ducts.empty(), ContractIssueCode::missing_value,
                    child(root_path, "ducts"),
                    "exhaust acoustic assembly requires ducts");
    detail::require(report, !assembly.primary_bindings.empty(),
                    ContractIssueCode::missing_value,
                    child(root_path, "primary_bindings"),
                    "exhaust acoustic assembly requires primary bindings");
    detail::require(report, !assembly.junctions.empty(), ContractIssueCode::missing_value,
                    child(root_path, "junctions"),
                    "exhaust acoustic assembly requires junctions");
    detail::require(report, !assembly.outlets.empty(), ContractIssueCode::missing_value,
                    child(root_path, "outlets"),
                    "exhaust acoustic assembly requires exterior outlets");

    std::unordered_map<std::uint32_t, AcousticDuctKind> duct_kinds;
    std::unordered_set<std::uint32_t> duct_ids;
    std::unordered_set<std::string> duct_names;
    for (const auto &duct : assembly.ducts) {
        const auto base = child(root_path, "ducts." + duct.semantic_id.value);
        resolved_leaf(report, duct.semantic_id, provenance, base + ".semantic_id");
        resolved_leaf(report, duct.kind, provenance, base + ".kind");
        resolved_leaf(report, duct.length_m, provenance, base + ".length_m");
        resolved_leaf(report, duct.inner_diameter_m, provenance,
                      base + ".inner_diameter_m");
        resolved_leaf(report, duct.reference_temperature_k, provenance,
                      base + ".reference_temperature_k");
        resolved_leaf(report, duct.propagation_loss_np_per_m, provenance,
                      base + ".propagation_loss_np_per_m");
        detail::require(report, insert_unique(duct_ids, duct.id),
                        duct.id.valid() ? ContractIssueCode::duplicate_identity
                                        : ContractIssueCode::invalid_value,
                        base + ".id", "acoustic duct IDs must be nonzero and unique");
        detail::require(report,
                        is_valid_semantic_id(duct.semantic_id.value) &&
                            duct_names.insert(duct.semantic_id.value).second,
                        ContractIssueCode::duplicate_identity, base + ".semantic_id.value",
                        "acoustic duct semantic IDs must be canonical and unique");
        detail::require(
            report,
            known(duct.kind.value) && detail::finite_positive(duct.length_m.value) &&
                detail::finite_positive(duct.inner_diameter_m.value) &&
                detail::finite_positive(duct.reference_temperature_k.value) &&
                detail::finite_nonnegative(duct.propagation_loss_np_per_m.value),
            ContractIssueCode::invalid_value, base,
            "acoustic duct kind and physical scalars are outside their domain");
        duct_kinds.emplace(duct.id.value, duct.kind.value);
    }

    std::unordered_map<std::uint32_t, const ExhaustAcousticJunction *> junctions;
    std::unordered_set<std::uint32_t> junction_ids;
    std::unordered_set<std::string> junction_names;
    std::unordered_set<std::uint32_t> junction_downstream;
    std::unordered_set<std::uint32_t> junction_primaries;
    for (const auto &junction : assembly.junctions) {
        const auto base = child(root_path, "junctions." + junction.semantic_id.value);
        resolved_leaf(report, junction.semantic_id, provenance, base + ".semantic_id");
        detail::require(report, insert_unique(junction_ids, junction.id),
                        junction.id.valid() ? ContractIssueCode::duplicate_identity
                                            : ContractIssueCode::invalid_value,
                        base + ".id",
                        "acoustic junction IDs must be nonzero and unique");
        detail::require(report,
                        is_valid_semantic_id(junction.semantic_id.value) &&
                            junction_names.insert(junction.semantic_id.value).second,
                        ContractIssueCode::duplicate_identity,
                        base + ".semantic_id.value",
                        "acoustic junction semantic IDs must be canonical and unique");
        detail::require(report, !junction.primary_duct_ids.empty(),
                        ContractIssueCode::inconsistent_shape,
                        base + ".primary_duct_ids",
                        "an exhaust junction requires at least one primary duct");
        std::unordered_set<std::uint32_t> local;
        for (const auto id : junction.primary_duct_ids) {
            const auto kind = duct_kinds.find(id.value);
            detail::require(report,
                            id.valid() && kind != duct_kinds.end() &&
                                kind->second == AcousticDuctKind::primary,
                            ContractIssueCode::dangling_reference,
                            base + ".primary_duct_ids",
                            "junction primary must reference a primary duct");
            detail::require(report, local.insert(id.value).second,
                            ContractIssueCode::duplicate_identity,
                            base + ".primary_duct_ids",
                            "junction primary duct IDs must be unique");
            detail::require(report, junction_primaries.insert(id.value).second,
                            ContractIssueCode::duplicate_identity,
                            base + ".primary_duct_ids",
                            "a primary duct cannot enter multiple junctions");
        }
        const auto downstream = duct_kinds.find(junction.downstream_duct_id.value);
        detail::require(report,
                        junction.downstream_duct_id.valid() &&
                            downstream != duct_kinds.end() &&
                            downstream->second == AcousticDuctKind::downstream,
                        ContractIssueCode::dangling_reference,
                        base + ".downstream_duct_id",
                        "junction downstream must reference a downstream duct");
        detail::require(report,
                        junction_downstream.insert(junction.downstream_duct_id.value)
                            .second,
                        ContractIssueCode::duplicate_identity,
                        base + ".downstream_duct_id",
                        "a downstream duct cannot leave multiple junctions");
        junctions.emplace(junction.id.value, &junction);
    }

    std::unordered_set<std::uint32_t> bound_cylinders;
    std::unordered_set<std::uint32_t> bound_ports;
    std::unordered_set<std::uint32_t> bound_primaries;
    for (std::size_t index = 0; index < assembly.primary_bindings.size(); ++index) {
        const auto &binding = assembly.primary_bindings[index];
        const auto base = child(root_path, "primary_bindings." +
                                               cylinder_name(engine, binding.cylinder_id));
        const auto *cylinder = find_cylinder(engine, binding.cylinder_id);
        const auto *port = find_port(engine, binding.exhaust_port_id);
        const auto duct = duct_kinds.find(binding.primary_duct_id.value);
        const auto junction = junctions.find(binding.junction_id.value);
        detail::require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                        base + ".cylinder_id",
                        "primary binding cylinder is absent from the engine");
        detail::require(report,
                        port != nullptr && port->kind.value == PortKind::exhaust &&
                            port->cylinder_id == binding.cylinder_id,
                        ContractIssueCode::dangling_reference,
                        base + ".exhaust_port_id",
                        "primary binding must reference that cylinder's exhaust port");
        detail::require(report,
                        duct != duct_kinds.end() &&
                            duct->second == AcousticDuctKind::primary,
                        ContractIssueCode::dangling_reference, base + ".primary_duct_id",
                        "primary binding must reference a primary duct");
        detail::require(report, junction != junctions.end(),
                        ContractIssueCode::dangling_reference, base + ".junction_id",
                        "primary binding must reference an acoustic junction");
        if (junction != junctions.end()) {
            detail::require(
                report,
                std::ranges::find(junction->second->primary_duct_ids,
                                  binding.primary_duct_id) !=
                    junction->second->primary_duct_ids.end(),
                ContractIssueCode::inconsistent_semantics, base,
                "primary binding junction does not contain its primary duct");
        }
        detail::require(report, insert_unique(bound_cylinders, binding.cylinder_id),
                        ContractIssueCode::duplicate_identity, base + ".cylinder_id",
                        "a cylinder may own only one exhaust acoustic primary");
        detail::require(report, insert_unique(bound_ports, binding.exhaust_port_id),
                        ContractIssueCode::duplicate_identity, base + ".exhaust_port_id",
                        "an exhaust port may own only one acoustic primary");
        detail::require(report, insert_unique(bound_primaries, binding.primary_duct_id),
                        ContractIssueCode::duplicate_identity, base + ".primary_duct_id",
                        "a primary duct may have only one source binding");
    }

    std::size_t exhaust_port_count = 0;
    for (const auto &port : engine.ports) {
        if (port.kind.value == PortKind::exhaust) {
            ++exhaust_port_count;
            detail::require(report, bound_ports.contains(port.id.value),
                            ContractIssueCode::missing_value,
                            child(root_path, "primary_bindings"),
                            "every engine exhaust port requires one primary binding");
        }
    }
    detail::require(report, assembly.primary_bindings.size() == exhaust_port_count,
                    ContractIssueCode::inconsistent_shape,
                    child(root_path, "primary_bindings"),
                    "primary binding count must equal engine exhaust-port count");
    detail::require(report, bound_primaries == junction_primaries,
                    ContractIssueCode::inconsistent_semantics,
                    child(root_path, "primary_bindings"),
                    "junction and source-binding primary duct sets must match exactly");

    std::unordered_set<std::uint32_t> outlet_routes;
    std::unordered_set<std::uint32_t> outlet_downstream;
    for (const auto &outlet : assembly.outlets) {
        const auto base = child(root_path, "outlets." + route_name(engine, outlet.route_id));
        resolved_leaf(report, outlet.observation_distance_m, provenance,
                      base + ".observation_distance_m");
        const auto *route = find_route(engine, outlet.route_id);
        const auto duct = duct_kinds.find(outlet.downstream_duct_id.value);
        detail::require(report,
                        route != nullptr &&
                            route->kind.value == SourceRouteKind::exhaust_outlet,
                        ContractIssueCode::dangling_reference, base + ".route_id",
                        "outlet route must reference an engine exhaust-outlet route");
        detail::require(report,
                        duct != duct_kinds.end() &&
                            duct->second == AcousticDuctKind::downstream &&
                            junction_downstream.contains(outlet.downstream_duct_id.value),
                        ContractIssueCode::dangling_reference,
                        base + ".downstream_duct_id",
                        "outlet must terminate one junction downstream duct");
        detail::require(report,
                        detail::finite_positive(outlet.observation_distance_m.value),
                        ContractIssueCode::invalid_value,
                        base + ".observation_distance_m.value",
                        "outlet observation distance must be finite and positive");
        detail::require(report, insert_unique(outlet_routes, outlet.route_id),
                        ContractIssueCode::duplicate_identity, base + ".route_id",
                        "an exterior route may appear only once");
        detail::require(report,
                        insert_unique(outlet_downstream, outlet.downstream_duct_id),
                        ContractIssueCode::duplicate_identity,
                        base + ".downstream_duct_id",
                        "a downstream duct may terminate at only one exterior outlet");
    }
    detail::require(report, outlet_downstream == junction_downstream,
                    ContractIssueCode::inconsistent_semantics,
                    child(root_path, "outlets"),
                    "every junction downstream duct must terminate at one outlet");
    for (const auto &route : engine.routes) {
        if (route.kind.value == SourceRouteKind::exhaust_outlet) {
            detail::require(report, outlet_routes.contains(route.id.value),
                            ContractIssueCode::missing_value,
                            child(root_path, "outlets"),
                            "every engine exhaust-outlet route requires one outlet");
        }
    }

    return report;
}

} // namespace engine_sim_offline::contract
