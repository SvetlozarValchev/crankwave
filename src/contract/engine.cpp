#include "engine_sim_offline/contract/engine.hpp"

#include "physics_profile_support.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

template <class T>
void validate_authored(ValidationReport &report, const AuthoredValue<T> &value,
                       const ProvenanceLedger &ledger, const std::string &path) {
    detail::validate_authored_value(report, value, ledger, path);
}

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &ledger, const std::string &path) {
    detail::validate_resolved_value(report, value, ledger, path);
}

template <class T>
void validate_optional_authored(ValidationReport &report,
                                const std::optional<AuthoredValue<T>> &value,
                                const ProvenanceLedger &ledger,
                                const std::string &path) {
    if (value.has_value()) {
        validate_authored(report, *value, ledger, path);
    }
}

bool known(EngineCycle value) noexcept {
    return value == EngineCycle::four_stroke;
}

bool known(IgnitionKind value) noexcept {
    return value == IgnitionKind::spark_ignition;
}

bool known(CylinderLayoutKind value) noexcept {
    switch (value) {
    case CylinderLayoutKind::inline_engine:
    case CylinderLayoutKind::vee_engine:
    case CylinderLayoutKind::flat_engine:
    case CylinderLayoutKind::other:
        return true;
    case CylinderLayoutKind::unspecified:
        return false;
    }
    return false;
}

bool known(PortKind value) noexcept {
    return value == PortKind::intake || value == PortKind::exhaust;
}

bool known(GasVolumeKind value) noexcept {
    switch (value) {
    case GasVolumeKind::atmosphere:
    case GasVolumeKind::intake_plenum:
    case GasVolumeKind::intake_runner:
    case GasVolumeKind::cylinder:
    case GasVolumeKind::exhaust_primary:
    case GasVolumeKind::exhaust_collector:
        return true;
    case GasVolumeKind::unspecified:
        return false;
    }
    return false;
}

bool known(SourceRouteKind value) noexcept {
    switch (value) {
    case SourceRouteKind::exhaust_outlet:
    case SourceRouteKind::intake_inlet:
    case SourceRouteKind::mechanical_engine:
    case SourceRouteKind::mechanical_starter:
        return true;
    case SourceRouteKind::unspecified:
        return false;
    }
    return false;
}

void validate_selection(ValidationReport &report, const MethodSelection &selection,
                        const std::string &path) {
    detail::require(report, is_valid_semantic_id(selection.id),
                    ContractIssueCode::invalid_value, path + ".id",
                    "method selection ID must be canonical");
    detail::require(report, selection.version > 0, ContractIssueCode::invalid_value,
                    path + ".version", "method selection version must be positive");
}

template <class Function>
void for_each_authored_method(const AuthoredModelMethods &methods, Function function) {
    function(methods.mechanism, "engine.methods.mechanism");
    function(methods.valvetrain, "engine.methods.valvetrain");
    function(methods.gas_exchange, "engine.methods.gas_exchange");
    function(methods.ignition, "engine.methods.ignition");
    function(methods.combustion, "engine.methods.combustion");
    function(methods.heat_transfer, "engine.methods.heat_transfer");
    function(methods.losses, "engine.methods.losses");
    function(methods.excitation, "engine.methods.excitation");
}

template <class Function>
void for_each_method(const EngineSpec::ModelMethods &methods, Function function) {
    function(methods.mechanism, "engine.methods.mechanism");
    function(methods.valvetrain, "engine.methods.valvetrain");
    function(methods.gas_exchange, "engine.methods.gas_exchange");
    function(methods.ignition, "engine.methods.ignition");
    function(methods.combustion, "engine.methods.combustion");
    function(methods.heat_transfer, "engine.methods.heat_transfer");
    function(methods.losses, "engine.methods.losses");
    function(methods.excitation, "engine.methods.excitation");
}

void require_authored_legacy_low_order_method(
    ValidationReport &report, const AuthoredValue<MethodSelection> &method,
    std::string_view path) {
    detail::require(
        report, method.value.id == "legacy_low_order_v1" && method.value.version == 1,
        ContractIssueCode::unsupported_value, std::string(path) + ".value",
        "low-order core requires legacy_low_order_v1 version 1");
}

void require_authored_chen_flynn_aggregate_loss_method(
    ValidationReport &report, const AuthoredValue<MethodSelection> &method,
    std::string_view path) {
    detail::require(report,
                    method.value.id == "chen-flynn-cycle-mean-aggregate-loss-v1" &&
                        method.value.version == 1,
                    ContractIssueCode::unsupported_value, std::string(path) + ".value",
                    "operating-point loss accounting requires "
                    "chen-flynn-cycle-mean-aggregate-loss-v1 version 1");
}

void validate_authored_low_order_core_method_policy(
    ValidationReport &report, const AuthoredModelMethods &methods) {
    require_authored_legacy_low_order_method(report, methods.mechanism,
                                             "engine.methods.mechanism");
    require_authored_legacy_low_order_method(report, methods.valvetrain,
                                             "engine.methods.valvetrain");
    require_authored_legacy_low_order_method(report, methods.gas_exchange,
                                             "engine.methods.gas_exchange");
    require_authored_legacy_low_order_method(report, methods.ignition,
                                             "engine.methods.ignition");
    require_authored_legacy_low_order_method(report, methods.combustion,
                                             "engine.methods.combustion");
    require_authored_legacy_low_order_method(report, methods.heat_transfer,
                                             "engine.methods.heat_transfer");
    require_authored_legacy_low_order_method(report, methods.excitation,
                                             "engine.methods.excitation");
}

void validate_authored_profile_method_policy(
    ValidationReport &report, const AuthoredModelMethods &methods,
    const AuthoredLowOrderOperatingPointV1Profile &) {
    validate_authored_low_order_core_method_policy(report, methods);
    require_authored_chen_flynn_aggregate_loss_method(report, methods.losses,
                                                      "engine.methods.losses");
}

template <class Id, class ParentFunction>
bool has_parent_cycle(const std::vector<Id> &ids, ParentFunction parent_of) {
    enum class Visit : std::uint8_t {
        unseen,
        active,
        complete,
    };
    std::unordered_map<std::string, Visit> visits;
    for (const auto &id : ids) {
        visits.emplace(id, Visit::unseen);
    }
    const auto visit = [&](const auto &self, const std::string &id) -> bool {
        auto &state = visits[id];
        if (state == Visit::active) {
            return true;
        }
        if (state == Visit::complete) {
            return false;
        }
        state = Visit::active;
        const auto parent = parent_of(id);
        if (parent.has_value() && visits.contains(*parent) && self(self, *parent)) {
            return true;
        }
        state = Visit::complete;
        return false;
    };
    return std::ranges::any_of(ids, [&](const auto &id) { return visit(visit, id); });
}

std::string resolved_path(std::string_view collection, std::string_view semantic_id) {
    return "engine." + std::string(collection) + "." + std::string(semantic_id);
}

template <class Range, class Projection>
const typename Range::value_type *find_authored_by_id(const Range &range,
                                                      std::string_view semantic_id,
                                                      Projection projection) {
    const auto iterator = std::ranges::find_if(
        range, [&](const auto &item) { return projection(item) == semantic_id; });
    return iterator == range.end() ? nullptr : &*iterator;
}

void validate_authored_low_order_core_topology(
    ValidationReport &report, const AuthoredEngineDefinition &definition,
    const AuthoredLowOrderEngineCoreV1 &core, std::string_view physics_root) {
    using detail::require;

    const auto find_cylinder = [&](std::string_view id) {
        return find_authored_by_id(
            definition.cylinders, id,
            [](const AuthoredCylinderDefinition &cylinder) -> const std::string & {
                return cylinder.semantic_id.value;
            });
    };
    const auto find_port = [&](std::string_view id) {
        return find_authored_by_id(
            definition.ports, id,
            [](const AuthoredPortDefinition &port) -> const std::string & {
                return port.semantic_id.value;
            });
    };
    const auto find_volume = [&](std::string_view id) {
        return find_authored_by_id(
            definition.gas_volumes, id,
            [](const AuthoredGasVolumeDefinition &volume) -> const std::string & {
                return volume.semantic_id.value;
            });
    };
    const auto find_edge = [&](std::string_view id) {
        return find_authored_by_id(
            definition.flow_edges, id,
            [](const AuthoredFlowEdgeDefinition &edge) -> const std::string & {
                return edge.semantic_id.value;
            });
    };
    const auto find_route = [&](std::string_view id) {
        return find_authored_by_id(
            definition.routes, id,
            [](const AuthoredRouteDefinition &route) -> const std::string & {
                return route.semantic_id.value;
            });
    };
    const auto find_exhaust_profile = [&](std::string_view route_id) {
        return find_authored_by_id(
            core.gas_path.exhaust_routes, route_id,
            [](const AuthoredLegacyExhaustRouteProfile &route) -> const std::string & {
                return route.topology.route_id.value;
            });
    };
    const auto find_cylinder_profile = [&](std::string_view cylinder_id) {
        return find_authored_by_id(
            core.mechanism.cylinders, cylinder_id,
            [](const AuthoredLegacyCylinderAssembly &cylinder) -> const std::string & {
                return cylinder.topology.cylinder_id.value;
            });
    };
    const auto find_head_profile = [&](std::string_view bank_id) {
        return find_authored_by_id(
            core.gas_path.heads, bank_id,
            [](const AuthoredLegacyBankHeadProfile &head) -> const std::string & {
                return head.bank_id.value;
            });
    };

    const auto require_volume_kind = [&](std::string_view id,
                                         GasVolumeKind expected_kind,
                                         const std::string &path,
                                         std::string_view description) {
        const auto *volume = find_volume(id);
        require(report, volume != nullptr, ContractIssueCode::dangling_reference, path,
                std::string(description) + " references an unknown gas volume");
        if (volume != nullptr) {
            require(report, volume->kind.value == expected_kind,
                    ContractIssueCode::inconsistent_semantics, path,
                    std::string(description) +
                        " references a gas volume with the wrong role");
        }
        return volume != nullptr && volume->kind.value == expected_kind;
    };
    const auto require_oriented_edge = [&](std::string_view id,
                                           std::string_view endpoint_0,
                                           std::string_view endpoint_1,
                                           const std::string &path,
                                           std::string_view description) {
        const auto *edge = find_edge(id);
        require(report, edge != nullptr, ContractIssueCode::dangling_reference, path,
                std::string(description) + " references an unknown flow edge");
        if (edge != nullptr) {
            require(report,
                    edge->endpoint_0_volume_id.value == endpoint_0 &&
                        edge->endpoint_1_volume_id.value == endpoint_1,
                    ContractIssueCode::inconsistent_semantics, path,
                    std::string(description) +
                        " must preserve endpoint-0 to endpoint-1 "
                        "orientation");
        }
    };
    const auto require_oriented_boundary_edge = [&](std::string_view id,
                                                    GasVolumeKind endpoint_0_kind,
                                                    std::string_view endpoint_1,
                                                    const std::string &path,
                                                    std::string_view description) {
        const auto *edge = find_edge(id);
        require(report, edge != nullptr, ContractIssueCode::dangling_reference, path,
                std::string(description) + " references an unknown flow edge");
        if (edge == nullptr) {
            return;
        }
        const auto *endpoint_0_volume = find_volume(edge->endpoint_0_volume_id.value);
        require(report,
                endpoint_0_volume != nullptr &&
                    endpoint_0_volume->kind.value == endpoint_0_kind &&
                    edge->endpoint_1_volume_id.value == endpoint_1,
                ContractIssueCode::inconsistent_semantics, path,
                std::string(description) +
                    " must preserve its boundary-to-volume orientation");
    };
    const auto require_oriented_to_boundary = [&](std::string_view id,
                                                  std::string_view endpoint_0,
                                                  GasVolumeKind endpoint_1_kind,
                                                  const std::string &path,
                                                  std::string_view description) {
        const auto *edge = find_edge(id);
        require(report, edge != nullptr, ContractIssueCode::dangling_reference, path,
                std::string(description) + " references an unknown flow edge");
        if (edge == nullptr) {
            return;
        }
        const auto *endpoint_1_volume = find_volume(edge->endpoint_1_volume_id.value);
        require(report,
                edge->endpoint_0_volume_id.value == endpoint_0 &&
                    endpoint_1_volume != nullptr &&
                    endpoint_1_volume->kind.value == endpoint_1_kind,
                ContractIssueCode::inconsistent_semantics, path,
                std::string(description) +
                    " must preserve its volume-to-boundary orientation");
    };
    const auto require_unique_binding =
        [&](std::unordered_set<std::string> &seen, std::string_view value,
            const std::string &path, std::string_view description) {
            require(report, seen.insert(std::string(value)).second,
                    ContractIssueCode::inconsistent_shape, path,
                    std::string(description) +
                        " must be bound exactly once by the legacy profile");
        };

    std::unordered_set<std::string> cylinder_bindings;
    std::unordered_set<std::string> intake_port_bindings;
    std::unordered_set<std::string> exhaust_port_bindings;
    std::unordered_set<std::string> runner_bindings;
    std::unordered_set<std::string> chamber_bindings;
    std::unordered_set<std::string> primary_bindings;
    std::unordered_set<std::string> exhaust_route_bindings;
    std::unordered_set<std::string> collector_bindings;
    std::unordered_set<std::string> expected_edge_bindings;

    std::vector<std::string> expected_head_banks;
    expected_head_banks.reserve(definition.banks.size());
    for (const auto &bank : definition.banks) {
        expected_head_banks.push_back(bank.value);
    }
    std::ranges::sort(expected_head_banks);
    bool exact_ordered_head_coverage =
        core.gas_path.heads.size() == expected_head_banks.size();
    for (std::size_t index = 0; index < core.gas_path.heads.size(); ++index) {
        exact_ordered_head_coverage =
            exact_ordered_head_coverage && index < expected_head_banks.size() &&
            core.gas_path.heads[index].bank_id.value == expected_head_banks[index];
    }
    require(report, exact_ordered_head_coverage,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".gas_path.heads",
            "legacy bank heads must cover every authored bank exactly once in "
            "semantic BankId order");

    const auto &intake_topology = core.gas_path.intake_topology;
    const auto intake_path = std::string(physics_root) + ".gas_path.intake_topology";
    require_volume_kind(intake_topology.plenum_volume_id.value,
                        GasVolumeKind::intake_plenum, intake_path + ".plenum_volume_id",
                        "legacy intake plenum");
    require_unique_binding(expected_edge_bindings,
                           intake_topology.main_throttle_edge_id.value,
                           intake_path + ".main_throttle_edge_id", "legacy flow edge");
    require_unique_binding(expected_edge_bindings,
                           intake_topology.idle_bypass_edge_id.value,
                           intake_path + ".idle_bypass_edge_id", "legacy flow edge");
    require_oriented_boundary_edge(
        intake_topology.main_throttle_edge_id.value, GasVolumeKind::atmosphere,
        intake_topology.plenum_volume_id.value, intake_path + ".main_throttle_edge_id",
        "legacy main-throttle edge");
    require_oriented_boundary_edge(
        intake_topology.idle_bypass_edge_id.value, GasVolumeKind::atmosphere,
        intake_topology.plenum_volume_id.value, intake_path + ".idle_bypass_edge_id",
        "legacy idle-bypass edge");

    for (std::size_t index = 0; index < core.gas_path.exhaust_routes.size(); ++index) {
        const auto &exhaust = core.gas_path.exhaust_routes[index];
        const auto path = std::string(physics_root) + ".gas_path.exhaust_routes[" +
                          std::to_string(index) + "].topology";
        const auto &topology = exhaust.topology;
        require_unique_binding(exhaust_route_bindings, topology.route_id.value,
                               path + ".route_id", "legacy exhaust route");
        require_unique_binding(collector_bindings, topology.collector_volume_id.value,
                               path + ".collector_volume_id",
                               "legacy exhaust collector");
        require_unique_binding(expected_edge_bindings,
                               topology.collector_outlet_edge_id.value,
                               path + ".collector_outlet_edge_id", "legacy flow edge");

        require_volume_kind(topology.collector_volume_id.value,
                            GasVolumeKind::exhaust_collector,
                            path + ".collector_volume_id", "legacy exhaust collector");
        const auto *route = find_route(topology.route_id.value);
        require(report, route != nullptr, ContractIssueCode::dangling_reference,
                path + ".route_id",
                "legacy exhaust profile references an unknown source route");
        if (route != nullptr) {
            require(report, route->kind.value == SourceRouteKind::exhaust_outlet,
                    ContractIssueCode::inconsistent_semantics, path + ".route_id",
                    "legacy exhaust profile must bind an exhaust-outlet "
                    "source route");
            require(report,
                    route->source_volume_id.has_value() &&
                        route->source_volume_id->value ==
                            topology.collector_volume_id.value,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".collector_volume_id",
                    "exhaust route source volume must be its bound collector");
        }
        require_oriented_boundary_edge(
            topology.collector_outlet_edge_id.value, GasVolumeKind::atmosphere,
            topology.collector_volume_id.value, path + ".collector_outlet_edge_id",
            "legacy collector-outlet edge");
    }

    require(report, core.mechanism.cylinders.size() == definition.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".mechanism.cylinders",
            "legacy mechanism must bind every authored cylinder exactly once");
    for (std::size_t index = 0; index < core.mechanism.cylinders.size(); ++index) {
        const auto &assembly = core.mechanism.cylinders[index];
        const auto &topology = assembly.topology;
        const auto &parameters = assembly.parameters;
        const auto path = std::string(physics_root) + ".mechanism.cylinders[" +
                          std::to_string(index) + "].topology";
        require_unique_binding(cylinder_bindings, topology.cylinder_id.value,
                               path + ".cylinder_id", "authored cylinder");
        require_unique_binding(intake_port_bindings, topology.intake_port_id.value,
                               path + ".intake_port_id", "cylinder intake port");
        require_unique_binding(exhaust_port_bindings, topology.exhaust_port_id.value,
                               path + ".exhaust_port_id", "cylinder exhaust port");
        require_unique_binding(runner_bindings, topology.intake_runner_volume_id.value,
                               path + ".intake_runner_volume_id",
                               "cylinder intake runner");
        require_unique_binding(chamber_bindings, topology.chamber_volume_id.value,
                               path + ".chamber_volume_id", "cylinder chamber");
        require_unique_binding(
            primary_bindings, topology.exhaust_primary_volume_id.value,
            path + ".exhaust_primary_volume_id", "cylinder exhaust primary");
        require_unique_binding(expected_edge_bindings,
                               topology.plenum_to_runner_edge_id.value,
                               path + ".plenum_to_runner_edge_id", "legacy flow edge");
        require_unique_binding(expected_edge_bindings,
                               topology.intake_valve_edge_id.value,
                               path + ".intake_valve_edge_id", "legacy flow edge");
        require_unique_binding(expected_edge_bindings,
                               topology.exhaust_valve_edge_id.value,
                               path + ".exhaust_valve_edge_id", "legacy flow edge");
        require_unique_binding(
            expected_edge_bindings, topology.primary_to_collector_edge_id.value,
            path + ".primary_to_collector_edge_id", "legacy flow edge");
        require_unique_binding(expected_edge_bindings, topology.blowby_edge_id.value,
                               path + ".blowby_edge_id", "legacy flow edge");

        const auto *cylinder = find_cylinder(topology.cylinder_id.value);
        require(report, cylinder != nullptr, ContractIssueCode::dangling_reference,
                path + ".cylinder_id",
                "legacy mechanism references an unknown cylinder");
        if (cylinder != nullptr) {
            const auto authored_path =
                "engine.cylinders." + cylinder->semantic_id.value;
            const auto *head = find_head_profile(cylinder->bank_id.value);
            require(report, head != nullptr, ContractIssueCode::dangling_reference,
                    authored_path + ".bank_id",
                    "legacy cylinder bank has no matching cylinder-head profile");
            const auto require_optional_match =
                [&](const std::optional<AuthoredValue<double>> &authored_value,
                    double profile_value, std::string_view field) {
                    if (authored_value.has_value()) {
                        require(
                            report,
                            detail::nearly_equal(authored_value->value, profile_value),
                            ContractIssueCode::inconsistent_semantics,
                            authored_path + "." + std::string(field) + ".value",
                            "authored cylinder value must agree with the executable "
                            "physics profile");
                    }
                };
            require_optional_match(cylinder->bore_m, parameters.bore_m.value, "bore_m");
            require_optional_match(cylinder->stroke_m, parameters.stroke_m.value,
                                   "stroke_m");
            require_optional_match(cylinder->connecting_rod_length_m,
                                   parameters.connecting_rod_length_m.value,
                                   "connecting_rod_length_m");
            require_optional_match(cylinder->firing_tdc_offset_rad,
                                   parameters.ignition_wire_angle_rad.value,
                                   "firing_tdc_offset_rad");
            require_optional_match(cylinder->journal_phase_rad,
                                   parameters.journal_angle_rad.value,
                                   "journal_phase_rad");

            if (head != nullptr) {
                const auto piston_area_m2 =
                    std::numbers::pi * parameters.bore_m.value *
                    parameters.bore_m.value / 4.0;
                const auto clearance_volume_m3 =
                    head->chamber_volume_m3.value -
                    parameters.piston_displacement_term_m3.value +
                    piston_area_m2 * (parameters.deck_height_m.value -
                                      (parameters.crank_radius_m.value +
                                       parameters.connecting_rod_length_m.value) -
                                      parameters.piston_compression_height_m.value);
                const auto swept_volume_m3 =
                    piston_area_m2 * parameters.stroke_m.value;
                const auto fixed_geometry_volume_m3 =
                    head->chamber_volume_m3.value -
                    parameters.piston_displacement_term_m3.value +
                    piston_area_m2 * (parameters.deck_height_m.value -
                                      parameters.piston_compression_height_m.value);
                require(report,
                        detail::finite_positive(piston_area_m2) &&
                            detail::finite_positive(clearance_volume_m3) &&
                            detail::finite_positive(swept_volume_m3) &&
                            detail::finite_positive(fixed_geometry_volume_m3),
                        ContractIssueCode::inconsistent_semantics,
                        std::string(physics_root) + ".gas_path.heads." +
                            cylinder->bank_id.value + ".chamber_volume_m3",
                        "bank-head chamber and cylinder geometry must derive "
                        "positive clearance, swept, and fixed volumes");
                if (detail::finite_positive(clearance_volume_m3) &&
                    detail::finite_positive(swept_volume_m3)) {
                    require_optional_match(cylinder->compression_ratio,
                                           (clearance_volume_m3 + swept_volume_m3) /
                                               clearance_volume_m3,
                                           "compression_ratio");
                }
            }
        }
        const auto *intake_port = find_port(topology.intake_port_id.value);
        require(report,
                intake_port != nullptr &&
                    intake_port->cylinder_id.value == topology.cylinder_id.value &&
                    intake_port->kind.value == PortKind::intake,
                ContractIssueCode::inconsistent_semantics, path + ".intake_port_id",
                "legacy intake port must be an intake port owned by the "
                "bound cylinder");
        const auto *exhaust_port = find_port(topology.exhaust_port_id.value);
        require(report,
                exhaust_port != nullptr &&
                    exhaust_port->cylinder_id.value == topology.cylinder_id.value &&
                    exhaust_port->kind.value == PortKind::exhaust,
                ContractIssueCode::inconsistent_semantics, path + ".exhaust_port_id",
                "legacy exhaust port must be an exhaust port owned by the "
                "bound cylinder");
        require_volume_kind(topology.intake_runner_volume_id.value,
                            GasVolumeKind::intake_runner,
                            path + ".intake_runner_volume_id", "legacy intake runner");
        require_volume_kind(topology.chamber_volume_id.value, GasVolumeKind::cylinder,
                            path + ".chamber_volume_id", "legacy chamber");
        require_volume_kind(
            topology.exhaust_primary_volume_id.value, GasVolumeKind::exhaust_primary,
            path + ".exhaust_primary_volume_id", "legacy exhaust primary");

        const auto *exhaust = find_exhaust_profile(topology.exhaust_route_id.value);
        require(report, exhaust != nullptr, ContractIssueCode::dangling_reference,
                path + ".exhaust_route_id",
                "legacy cylinder references an unknown exhaust profile");
        require_oriented_edge(topology.plenum_to_runner_edge_id.value,
                              intake_topology.plenum_volume_id.value,
                              topology.intake_runner_volume_id.value,
                              path + ".plenum_to_runner_edge_id",
                              "legacy plenum-to-runner edge");
        require_oriented_edge(
            topology.intake_valve_edge_id.value, topology.intake_runner_volume_id.value,
            topology.chamber_volume_id.value, path + ".intake_valve_edge_id",
            "legacy intake-valve edge");
        require_oriented_edge(
            topology.exhaust_valve_edge_id.value, topology.chamber_volume_id.value,
            topology.exhaust_primary_volume_id.value, path + ".exhaust_valve_edge_id",
            "legacy exhaust-valve edge");
        if (exhaust != nullptr) {
            require_oriented_edge(topology.primary_to_collector_edge_id.value,
                                  topology.exhaust_primary_volume_id.value,
                                  exhaust->topology.collector_volume_id.value,
                                  path + ".primary_to_collector_edge_id",
                                  "legacy primary-to-collector edge");
        }
        require_oriented_to_boundary(
            topology.blowby_edge_id.value, topology.chamber_volume_id.value,
            GasVolumeKind::atmosphere, path + ".blowby_edge_id", "legacy blowby edge");
    }

    const auto require_exact_role_coverage =
        [&](const auto &range, auto expected_kind,
            const std::unordered_set<std::string> &bindings, const std::string &path,
            std::string_view description) {
            std::unordered_set<std::string> expected;
            for (const auto &item : range) {
                if (item.kind.value == expected_kind) {
                    expected.insert(item.semantic_id.value);
                }
            }
            require(report, expected == bindings, ContractIssueCode::inconsistent_shape,
                    path,
                    std::string(description) +
                        " must be covered exactly once by the legacy profile");
        };
    require_exact_role_coverage(
        definition.ports, PortKind::intake, intake_port_bindings,
        std::string(physics_root) + ".mechanism.cylinders", "authored intake ports");
    require_exact_role_coverage(
        definition.ports, PortKind::exhaust, exhaust_port_bindings,
        std::string(physics_root) + ".mechanism.cylinders", "authored exhaust ports");
    require_exact_role_coverage(
        definition.gas_volumes, GasVolumeKind::intake_runner, runner_bindings,
        std::string(physics_root) + ".mechanism.cylinders", "authored intake runners");
    require_exact_role_coverage(
        definition.gas_volumes, GasVolumeKind::cylinder, chamber_bindings,
        std::string(physics_root) + ".mechanism.cylinders", "authored chamber volumes");
    require_exact_role_coverage(definition.gas_volumes, GasVolumeKind::exhaust_primary,
                                primary_bindings,
                                std::string(physics_root) + ".mechanism.cylinders",
                                "authored exhaust primaries");
    require_exact_role_coverage(definition.gas_volumes,
                                GasVolumeKind::exhaust_collector, collector_bindings,
                                std::string(physics_root) + ".gas_path.exhaust_routes",
                                "authored exhaust collectors");
    const auto plenum_count = std::ranges::count_if(
        definition.gas_volumes, [](const AuthoredGasVolumeDefinition &volume) {
            return volume.kind.value == GasVolumeKind::intake_plenum;
        });
    require(report, plenum_count == 1, ContractIssueCode::inconsistent_shape,
            intake_path + ".plenum_volume_id",
            "legacy intake topology requires exactly one authored plenum");

    std::unordered_set<std::string> expected_cylinders;
    for (const auto &cylinder : definition.cylinders) {
        expected_cylinders.insert(cylinder.semantic_id.value);
    }
    require(report, expected_cylinders == cylinder_bindings,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".mechanism.cylinders",
            "legacy mechanism must cover every authored cylinder exactly once");
    std::unordered_set<std::string> expected_exhaust_routes;
    for (const auto &route : definition.routes) {
        if (route.kind.value == SourceRouteKind::exhaust_outlet) {
            expected_exhaust_routes.insert(route.semantic_id.value);
        }
    }
    require(report, expected_exhaust_routes == exhaust_route_bindings,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".gas_path.exhaust_routes",
            "legacy gas path must cover every authored exhaust route exactly "
            "once");
    std::unordered_set<std::string> authored_edges;
    for (const auto &edge : definition.flow_edges) {
        authored_edges.insert(edge.semantic_id.value);
    }
    require(report, authored_edges == expected_edge_bindings,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".gas_path",
            "authored flow edges must be covered exactly once by the legacy "
            "topology");

    const auto validate_camshaft = [&](const AuthoredLegacyCamshaftProfile &camshaft,
                                       PortKind expected_port_kind,
                                       const std::string &path) {
        std::unordered_set<std::string> lobe_cylinders;
        std::unordered_set<std::string> lobe_ports;
        require(report, camshaft.lobes.size() == definition.cylinders.size(),
                ContractIssueCode::inconsistent_shape, path + ".lobes",
                "legacy camshaft must have exactly one lobe per cylinder");
        for (std::size_t index = 0; index < camshaft.lobes.size(); ++index) {
            const auto &lobe = camshaft.lobes[index];
            const auto lobe_path = path + ".lobes[" + std::to_string(index) + "]";
            require_unique_binding(lobe_cylinders, lobe.cylinder_id.value,
                                   lobe_path + ".cylinder_id", "cam lobe cylinder");
            require_unique_binding(lobe_ports, lobe.port_id.value,
                                   lobe_path + ".port_id", "cam lobe port");
            const auto *port = find_port(lobe.port_id.value);
            require(report,
                    find_cylinder(lobe.cylinder_id.value) != nullptr &&
                        port != nullptr &&
                        port->cylinder_id.value == lobe.cylinder_id.value &&
                        port->kind.value == expected_port_kind,
                    ContractIssueCode::inconsistent_semantics, lobe_path,
                    "cam lobe must bind the expected port kind on its "
                    "declared cylinder");
            const auto *assembly = find_cylinder_profile(lobe.cylinder_id.value);
            if (assembly != nullptr) {
                const auto &expected_port_id =
                    expected_port_kind == PortKind::intake
                        ? assembly->topology.intake_port_id.value
                        : assembly->topology.exhaust_port_id.value;
                require(report, lobe.port_id.value == expected_port_id,
                        ContractIssueCode::inconsistent_semantics,
                        lobe_path + ".port_id",
                        "cam lobe must bind the cylinder topology's "
                        "designated port");
            }
        }
        require(report, lobe_cylinders == expected_cylinders,
                ContractIssueCode::inconsistent_shape, path + ".lobes",
                "legacy camshaft must cover every cylinder exactly once");
    };
    validate_camshaft(core.valvetrain.intake, PortKind::intake,
                      std::string(physics_root) + ".valvetrain.intake");
    validate_camshaft(core.valvetrain.exhaust, PortKind::exhaust,
                      std::string(physics_root) + ".valvetrain.exhaust");

    const auto require_exact_cylinder_sequence =
        [&](const std::vector<std::string> &sequence, const std::string &path,
            std::string_view description) {
            std::unordered_set<std::string> entries;
            for (std::size_t index = 0; index < sequence.size(); ++index) {
                require(report, is_valid_semantic_id(sequence[index]),
                        ContractIssueCode::invalid_value,
                        path + "[" + std::to_string(index) + "]",
                        std::string(description) +
                            " contains a noncanonical cylinder ID");
                require_unique_binding(entries, sequence[index],
                                       path + "[" + std::to_string(index) + "]",
                                       description);
            }
            require(report, entries == expected_cylinders,
                    ContractIssueCode::inconsistent_shape, path,
                    std::string(description) +
                        " must contain every authored cylinder exactly once");
        };
    require_exact_cylinder_sequence(core.ignition.firing_order.value,
                                    std::string(physics_root) +
                                        ".ignition.firing_order.value",
                                    "legacy firing order");
    require_exact_cylinder_sequence(
        core.excitation.cylinder_accumulation_order.value,
        std::string(physics_root) +
            ".reference_excitation.cylinder_accumulation_order.value",
        "legacy excitation accumulation order");

    std::unordered_set<std::string> excitation_cylinders;
    for (std::size_t index = 0; index < core.excitation.cylinder_paths.size();
         ++index) {
        const auto &excitation_path = core.excitation.cylinder_paths[index];
        const auto path = std::string(physics_root) +
                          ".reference_excitation.cylinder_paths[" +
                          std::to_string(index) + "]";
        require_unique_binding(excitation_cylinders, excitation_path.cylinder_id.value,
                               path + ".cylinder_id",
                               "legacy excitation cylinder path");
        const auto *assembly = find_cylinder_profile(excitation_path.cylinder_id.value);
        require(report,
                assembly != nullptr && excitation_path.route_id.value ==
                                           assembly->topology.exhaust_route_id.value,
                ContractIssueCode::inconsistent_semantics, path,
                "legacy excitation path must bind the cylinder's exhaust "
                "route");
    }
    require(report, excitation_cylinders == expected_cylinders,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".reference_excitation.cylinder_paths",
            "legacy excitation must contain one path per authored cylinder");
    std::unordered_set<std::string> excitation_routes;
    for (std::size_t index = 0; index < core.excitation.routes.size(); ++index) {
        require_unique_binding(
            excitation_routes, core.excitation.routes[index].route_id.value,
            std::string(physics_root) + ".reference_excitation.routes[" +
                std::to_string(index) + "].route_id",
            "legacy excitation route");
    }
    require(report, excitation_routes == exhaust_route_bindings,
            ContractIssueCode::inconsistent_shape,
            std::string(physics_root) + ".reference_excitation.routes",
            "legacy excitation routes must cover every legacy exhaust route");
}

} // namespace

ValidationReport validate(const AuthoredEngineDefinition &definition) {
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report = validate(definition.provenance);
    require(report, definition.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version", "engine-definition schema version must be positive");
    require(report, is_valid_semantic_id(definition.definition_id),
            ContractIssueCode::invalid_value, "definition_id",
            "definition ID must be a canonical semantic ID");

    validate_authored(report, definition.engine_id, definition.provenance,
                      "engine.engine_id");
    validate_authored(report, definition.profile_id, definition.provenance,
                      "engine.profile_id");
    validate_authored(report, definition.display_name, definition.provenance,
                      "engine.display_name");
    validate_authored(report, definition.cycle, definition.provenance, "engine.cycle");
    validate_authored(report, definition.ignition, definition.provenance,
                      "engine.ignition");
    validate_authored(report, definition.cylinder_layout, definition.provenance,
                      "engine.cylinder_layout");
    require(report, is_valid_semantic_id(definition.engine_id.value),
            ContractIssueCode::invalid_value, "engine.engine_id.value",
            "engine ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(definition.profile_id.value),
            ContractIssueCode::invalid_value, "engine.profile_id.value",
            "profile ID must be a canonical semantic ID");
    require(report, !definition.display_name.value.empty(),
            ContractIssueCode::missing_value, "engine.display_name.value",
            "display name must be present");
    require(report, known(definition.cycle.value), ContractIssueCode::unsupported_value,
            "engine.cycle.value", "engine cycle is not recognized");
    require(report, known(definition.ignition.value),
            ContractIssueCode::unsupported_value, "engine.ignition.value",
            "ignition kind is not recognized");
    require(report, known(definition.cylinder_layout.value),
            ContractIssueCode::unsupported_value, "engine.cylinder_layout.value",
            "cylinder layout is not recognized");
    require(report, !definition.banks.empty(), ContractIssueCode::missing_value,
            "engine.banks", "an engine needs at least one bank");
    require(report, !definition.cylinders.empty(), ContractIssueCode::missing_value,
            "engine.cylinders", "an engine needs at least one cylinder");

    std::unordered_set<std::string> bank_ids;
    for (std::size_t index = 0; index < definition.banks.size(); ++index) {
        const auto path = "engine.banks[" + std::to_string(index) + "]";
        const auto &bank = definition.banks[index];
        validate_authored(report, bank, definition.provenance, path);
        require(report, is_valid_semantic_id(bank.value),
                ContractIssueCode::invalid_value, path + ".value",
                "bank ID must be a canonical semantic ID");
        if (!bank_ids.insert(bank.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".value",
                       "bank IDs must be unique");
        }
    }

    std::unordered_set<std::string> cylinder_ids;
    for (std::size_t index = 0; index < definition.cylinders.size(); ++index) {
        const auto path = "engine.cylinders[" + std::to_string(index) + "]";
        const auto &cylinder = definition.cylinders[index];
        validate_authored(report, cylinder.semantic_id, definition.provenance,
                          path + ".semantic_id");
        validate_authored(report, cylinder.bank_id, definition.provenance,
                          path + ".bank_id");
        require(report, is_valid_semantic_id(cylinder.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "cylinder ID must be a canonical semantic ID");
        if (!cylinder_ids.insert(cylinder.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "cylinder IDs must be unique");
        }
        require(report, bank_ids.contains(cylinder.bank_id.value),
                ContractIssueCode::dangling_reference, path + ".bank_id.value",
                "cylinder references an unknown bank");
        validate_optional_authored(report, cylinder.bore_m, definition.provenance,
                                   path + ".bore_m");
        validate_optional_authored(report, cylinder.stroke_m, definition.provenance,
                                   path + ".stroke_m");
        validate_optional_authored(report, cylinder.connecting_rod_length_m,
                                   definition.provenance,
                                   path + ".connecting_rod_length_m");
        validate_optional_authored(report, cylinder.compression_ratio,
                                   definition.provenance, path + ".compression_ratio");
        validate_optional_authored(report, cylinder.firing_tdc_offset_rad,
                                   definition.provenance,
                                   path + ".firing_tdc_offset_rad");
        validate_optional_authored(report, cylinder.journal_phase_rad,
                                   definition.provenance, path + ".journal_phase_rad");
        if (cylinder.bore_m.has_value()) {
            require(report, finite_positive(cylinder.bore_m->value),
                    ContractIssueCode::invalid_value, path + ".bore_m.value",
                    "bore must be finite and positive");
        }
        if (cylinder.stroke_m.has_value()) {
            require(report, finite_positive(cylinder.stroke_m->value),
                    ContractIssueCode::invalid_value, path + ".stroke_m.value",
                    "stroke must be finite and positive");
        }
        if (cylinder.connecting_rod_length_m.has_value()) {
            require(report, finite_positive(cylinder.connecting_rod_length_m->value),
                    ContractIssueCode::invalid_value,
                    path + ".connecting_rod_length_m.value",
                    "connecting-rod length must be finite and positive");
        }
        if (cylinder.compression_ratio.has_value()) {
            require(report,
                    finite(cylinder.compression_ratio->value) &&
                        cylinder.compression_ratio->value > 1.0,
                    ContractIssueCode::invalid_value, path + ".compression_ratio.value",
                    "compression ratio must be finite and greater than one");
        }
        if (cylinder.firing_tdc_offset_rad.has_value()) {
            require(report, finite(cylinder.firing_tdc_offset_rad->value),
                    ContractIssueCode::invalid_value,
                    path + ".firing_tdc_offset_rad.value",
                    "firing TDC offset must be finite");
        }
        if (cylinder.journal_phase_rad.has_value()) {
            require(report, finite(cylinder.journal_phase_rad->value),
                    ContractIssueCode::invalid_value, path + ".journal_phase_rad.value",
                    "journal phase must be finite");
        }
    }

    std::unordered_set<std::string> port_ids;
    for (std::size_t index = 0; index < definition.ports.size(); ++index) {
        const auto path = "engine.ports[" + std::to_string(index) + "]";
        const auto &port = definition.ports[index];
        validate_authored(report, port.semantic_id, definition.provenance,
                          path + ".semantic_id");
        validate_authored(report, port.cylinder_id, definition.provenance,
                          path + ".cylinder_id");
        validate_authored(report, port.kind, definition.provenance, path + ".kind");
        require(report, is_valid_semantic_id(port.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "port ID must be a canonical semantic ID");
        require(report, known(port.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "port kind is not recognized");
        if (!port_ids.insert(port.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "port IDs must be unique");
        }
        require(report, cylinder_ids.contains(port.cylinder_id.value),
                ContractIssueCode::dangling_reference, path + ".cylinder_id.value",
                "port references an unknown cylinder");
    }

    std::unordered_set<std::string> volume_ids;
    for (std::size_t index = 0; index < definition.gas_volumes.size(); ++index) {
        const auto path = "engine.gas_volumes[" + std::to_string(index) + "]";
        const auto &volume = definition.gas_volumes[index];
        validate_authored(report, volume.semantic_id, definition.provenance,
                          path + ".semantic_id");
        validate_authored(report, volume.kind, definition.provenance, path + ".kind");
        require(report, is_valid_semantic_id(volume.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "volume ID must be a canonical semantic ID");
        require(report, known(volume.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "gas-volume kind is not recognized");
        if (!volume_ids.insert(volume.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "gas-volume IDs must be unique");
        }
    }

    std::unordered_set<std::string> edge_ids;
    for (std::size_t index = 0; index < definition.flow_edges.size(); ++index) {
        const auto path = "engine.flow_edges[" + std::to_string(index) + "]";
        const auto &edge = definition.flow_edges[index];
        validate_authored(report, edge.semantic_id, definition.provenance,
                          path + ".semantic_id");
        validate_authored(report, edge.endpoint_0_volume_id, definition.provenance,
                          path + ".endpoint_0_volume_id");
        validate_authored(report, edge.endpoint_1_volume_id, definition.provenance,
                          path + ".endpoint_1_volume_id");
        require(report, is_valid_semantic_id(edge.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "flow-edge ID must be a canonical semantic ID");
        if (!edge_ids.insert(edge.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "flow-edge IDs must be unique");
        }
        require(report,
                volume_ids.contains(edge.endpoint_0_volume_id.value) &&
                    volume_ids.contains(edge.endpoint_1_volume_id.value),
                ContractIssueCode::dangling_reference, path,
                "flow edge references an unknown gas volume");
        require(report,
                edge.endpoint_0_volume_id.value != edge.endpoint_1_volume_id.value,
                ContractIssueCode::inconsistent_semantics, path,
                "a flow edge must connect two different volumes");
    }

    std::unordered_set<std::string> route_ids;
    std::vector<std::string> route_order;
    for (std::size_t index = 0; index < definition.routes.size(); ++index) {
        const auto path = "engine.routes[" + std::to_string(index) + "]";
        const auto &route = definition.routes[index];
        validate_authored(report, route.semantic_id, definition.provenance,
                          path + ".semantic_id");
        validate_authored(report, route.kind, definition.provenance, path + ".kind");
        validate_optional_authored(report, route.source_volume_id,
                                   definition.provenance, path + ".source_volume_id");
        validate_optional_authored(report, route.default_parent_route_id,
                                   definition.provenance,
                                   path + ".default_parent_route_id");
        validate_optional_authored(report, route.emitter_anchor_id,
                                   definition.provenance, path + ".emitter_anchor_id");
        require(report, is_valid_semantic_id(route.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "route ID must be a canonical semantic ID");
        require(report, known(route.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "source-route kind is not recognized");
        if (route_ids.insert(route.semantic_id.value).second) {
            route_order.push_back(route.semantic_id.value);
        } else {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "route IDs must be unique");
        }
        if (route.source_volume_id.has_value()) {
            require(report, is_valid_semantic_id(route.source_volume_id->value),
                    ContractIssueCode::invalid_value, path + ".source_volume_id.value",
                    "route source-volume ID must be canonical");
            require(report, volume_ids.contains(route.source_volume_id->value),
                    ContractIssueCode::dangling_reference,
                    path + ".source_volume_id.value",
                    "route source references an unknown gas volume");
        }
        if (route.default_parent_route_id.has_value()) {
            require(report, is_valid_semantic_id(route.default_parent_route_id->value),
                    ContractIssueCode::invalid_value,
                    path + ".default_parent_route_id.value",
                    "route parent ID must be canonical");
        }
        if (route.emitter_anchor_id.has_value()) {
            require(report, is_valid_semantic_id(route.emitter_anchor_id->value),
                    ContractIssueCode::invalid_value, path + ".emitter_anchor_id.value",
                    "emitter anchor ID must be canonical");
        }
        const auto gas_route = route.kind.value == SourceRouteKind::exhaust_outlet ||
                               route.kind.value == SourceRouteKind::intake_inlet;
        const auto mechanical_route =
            route.kind.value == SourceRouteKind::mechanical_engine ||
            route.kind.value == SourceRouteKind::mechanical_starter;
        if (gas_route) {
            require(report, route.source_volume_id.has_value(),
                    ContractIssueCode::missing_value, path + ".source_volume_id",
                    "gas source route requires a physical source volume");
        }
        if (mechanical_route) {
            require(report, !route.source_volume_id.has_value(),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".source_volume_id",
                    "mechanical source route cannot bind a gas volume");
            require(report, route.emitter_anchor_id.has_value(),
                    ContractIssueCode::missing_value, path + ".emitter_anchor_id",
                    "mechanical source route requires a canonical emitter "
                    "anchor");
        }
    }
    for (std::size_t index = 0; index < definition.routes.size(); ++index) {
        const auto &route = definition.routes[index];
        if (route.default_parent_route_id.has_value()) {
            require(report, route_ids.contains(route.default_parent_route_id->value),
                    ContractIssueCode::dangling_reference,
                    "engine.routes[" + std::to_string(index) +
                        "].default_parent_route_id.value",
                    "route parent references an unknown route");
        }
    }
    const auto authored_parent =
        [&](const std::string &id) -> std::optional<std::string> {
        const auto iterator = std::ranges::find_if(
            definition.routes, [&](const AuthoredRouteDefinition &route) {
                return route.semantic_id.value == id;
            });
        if (iterator == definition.routes.end() ||
            !iterator->default_parent_route_id.has_value()) {
            return std::nullopt;
        }
        return iterator->default_parent_route_id->value;
    };
    require(report, !has_parent_cycle(route_order, authored_parent),
            ContractIssueCode::inconsistent_semantics, "engine.routes",
            "source-route parent graph must be acyclic");

    for_each_authored_method(
        definition.methods, [&](const auto &method, const std::string &path) {
            validate_authored(report, method, definition.provenance, path);
            validate_selection(report, method.value, path + ".value");
        });
    std::visit(
        [&](const auto &profile) {
            validate_authored_profile_method_policy(report, definition.methods,
                                                    profile);
        },
        definition.physics_profile);
    detail::append_prefixed(report,
                            validate(definition.physics_profile, definition.provenance),
                            "physics_profile");
    std::visit(
        [&](const auto &profile) {
            validate_authored_low_order_core_topology(report, definition, profile.core,
                                                      profile_support::root(profile));
        },
        definition.physics_profile);
    return report;
}

ValidationReport validate(const EngineSpec &spec, const ProvenanceLedger &provenance) {
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report = validate(provenance);
    require(report, spec.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version", "engine-spec schema version must be positive");
    require(report, spec.id.valid(), ContractIssueCode::invalid_value, "id",
            "engine runtime ID must be nonzero");
    require(report, spec.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "engine spec and provenance ledger schema IDs must match");

    validate_resolved(report, spec.engine_id, provenance, "engine.engine_id");
    validate_resolved(report, spec.profile_id, provenance, "engine.profile_id");
    validate_resolved(report, spec.display_name, provenance, "engine.display_name");
    validate_resolved(report, spec.cycle, provenance, "engine.cycle");
    validate_resolved(report, spec.ignition, provenance, "engine.ignition");
    validate_resolved(report, spec.cylinder_layout, provenance,
                      "engine.cylinder_layout");
    validate_resolved(report, spec.total_displacement_m3, provenance,
                      "engine.total_displacement_m3");
    require(report, is_valid_semantic_id(spec.engine_id.value),
            ContractIssueCode::invalid_value, "engine.engine_id.value",
            "engine ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(spec.profile_id.value),
            ContractIssueCode::invalid_value, "engine.profile_id.value",
            "profile ID must be a canonical semantic ID");
    require(report, !spec.display_name.value.empty(), ContractIssueCode::missing_value,
            "engine.display_name.value", "display name must be present");
    require(report, known(spec.cycle.value), ContractIssueCode::unsupported_value,
            "engine.cycle.value", "engine cycle is not recognized");
    require(report, known(spec.ignition.value), ContractIssueCode::unsupported_value,
            "engine.ignition.value", "ignition kind is not recognized");
    require(report, known(spec.cylinder_layout.value),
            ContractIssueCode::unsupported_value, "engine.cylinder_layout.value",
            "cylinder layout is not recognized");
    require(report, finite_positive(spec.total_displacement_m3.value),
            ContractIssueCode::invalid_value, "engine.total_displacement_m3.value",
            "total displacement must be finite and positive");
    require(report, !spec.banks.empty(), ContractIssueCode::missing_value,
            "engine.banks", "resolved engine needs at least one bank");
    require(report, !spec.cylinders.empty(), ContractIssueCode::missing_value,
            "engine.cylinders", "resolved engine needs at least one cylinder");

    detail::require_unique_numeric_ids(
        report, spec.banks, [](const BankSpec &bank) { return bank.id; },
        "engine.banks");
    detail::require_unique_numeric_ids(
        report, spec.cylinders,
        [](const CylinderSpec &cylinder) { return cylinder.id; }, "engine.cylinders");
    detail::require_unique_numeric_ids(
        report, spec.ports, [](const PortSpec &port) { return port.id; },
        "engine.ports");
    detail::require_unique_numeric_ids(
        report, spec.gas_volumes, [](const GasVolumeSpec &volume) { return volume.id; },
        "engine.gas_volumes");
    detail::require_unique_numeric_ids(
        report, spec.flow_edges, [](const FlowEdgeSpec &edge) { return edge.id; },
        "engine.flow_edges");
    detail::require_unique_numeric_ids(
        report, spec.routes, [](const RouteSpec &route) { return route.id; },
        "engine.routes");

    std::unordered_set<std::uint32_t> bank_ids;
    std::unordered_set<std::string> bank_semantic_ids;
    for (const auto &bank : spec.banks) {
        const auto path = resolved_path("banks", bank.semantic_id.value);
        bank_ids.insert(bank.id.value);
        validate_resolved(report, bank.semantic_id, provenance, path + ".semantic_id");
        if (bank.angle_rad.has_value()) {
            validate_resolved(report, *bank.angle_rad, provenance, path + ".angle_rad");
            require(report, finite(bank.angle_rad->value),
                    ContractIssueCode::invalid_value, path + ".angle_rad.value",
                    "bank angle must be finite");
        }
        require(report, is_valid_semantic_id(bank.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "bank semantic ID must be canonical");
        if (!bank_semantic_ids.insert(bank.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "bank semantic IDs must be unique");
        }
    }
    if (spec.cylinder_layout.value == CylinderLayoutKind::inline_engine) {
        require(report,
                spec.banks.size() == 1U &&
                    (!spec.banks.front().angle_rad.has_value() ||
                     detail::nearly_equal(spec.banks.front().angle_rad->value, 0.0)),
                ContractIssueCode::inconsistent_shape, "engine.banks",
                "an inline engine requires one zero-angle bank");
    } else if (spec.cylinder_layout.value == CylinderLayoutKind::vee_engine) {
        const bool complete_angles = spec.banks.size() == 2U &&
                                     spec.banks[0].angle_rad.has_value() &&
                                     spec.banks[1].angle_rad.has_value();
        require(report, complete_angles, ContractIssueCode::inconsistent_shape,
                "engine.banks", "a V engine requires two banks with explicit angles");
        if (complete_angles) {
            require(report,
                    !detail::nearly_equal(spec.banks[0].angle_rad->value,
                                          spec.banks[1].angle_rad->value),
                    ContractIssueCode::inconsistent_semantics, "engine.banks",
                    "V-engine bank angles must be distinct");
        }
    } else if (spec.cylinder_layout.value == CylinderLayoutKind::flat_engine) {
        const bool complete_angles = spec.banks.size() == 2U &&
                                     spec.banks[0].angle_rad.has_value() &&
                                     spec.banks[1].angle_rad.has_value();
        require(report, complete_angles, ContractIssueCode::inconsistent_shape,
                "engine.banks",
                "a flat engine requires two banks with explicit angles");
        if (complete_angles) {
            const double separation = std::abs(std::remainder(
                spec.banks[0].angle_rad->value - spec.banks[1].angle_rad->value,
                2.0 * std::numbers::pi));
            require(report, detail::nearly_equal(separation, std::numbers::pi),
                    ContractIssueCode::inconsistent_semantics, "engine.banks",
                    "flat-engine bank axes must be antipodal");
        }
    } else if (spec.cylinder_layout.value == CylinderLayoutKind::other) {
        require(report,
                std::ranges::all_of(
                    spec.banks,
                    [](const BankSpec &bank) { return bank.angle_rad.has_value(); }),
                ContractIssueCode::inconsistent_shape, "engine.banks",
                "a custom engine requires an explicit axis for every bank");
    }

    std::unordered_set<std::uint32_t> cylinder_ids;
    std::unordered_set<std::string> cylinder_semantic_ids;
    std::unordered_map<std::string, std::size_t> shared_ignition_wire_counts;
    double computed_displacement_m3 = 0.0;
    for (const auto &cylinder : spec.cylinders) {
        const auto path = resolved_path("cylinders", cylinder.semantic_id.value);
        cylinder_ids.insert(cylinder.id.value);
        validate_resolved(report, cylinder.semantic_id, provenance,
                          path + ".semantic_id");
        validate_resolved(report, cylinder.bore_m, provenance, path + ".bore_m");
        validate_resolved(report, cylinder.stroke_m, provenance, path + ".stroke_m");
        validate_resolved(report, cylinder.connecting_rod_length_m, provenance,
                          path + ".connecting_rod_length_m");
        validate_resolved(report, cylinder.compression_ratio, provenance,
                          path + ".compression_ratio");
        validate_resolved(report, cylinder.firing_tdc_offset_rad, provenance,
                          path + ".firing_tdc_offset_rad");
        validate_resolved(report, cylinder.journal_phase_rad, provenance,
                          path + ".journal_phase_rad");
        if (cylinder.shared_ignition_wire_semantic_id.has_value()) {
            validate_resolved(report, *cylinder.shared_ignition_wire_semantic_id,
                              provenance,
                              path + ".shared_ignition_wire_semantic_id");
        }
        if (cylinder.master_rod_attachment.has_value()) {
            const auto &attachment = *cylinder.master_rod_attachment;
            validate_resolved(report, attachment.throw_radius_m, provenance,
                              path + ".master_rod_attachment.throw_radius_m");
            require(report, attachment.master_cylinder_id.valid(),
                    ContractIssueCode::invalid_value,
                    path + ".master_rod_attachment.master_cylinder_id",
                    "master-rod attachment requires a valid master cylinder ID");
            require(report, finite_positive(attachment.throw_radius_m.value),
                    ContractIssueCode::invalid_value,
                    path + ".master_rod_attachment.throw_radius_m.value",
                    "master-rod throw radius must be finite and positive");
        }
        require(report, is_valid_semantic_id(cylinder.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "cylinder semantic ID must be canonical");
        if (!cylinder_semantic_ids.insert(cylinder.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value",
                       "cylinder semantic IDs must be unique");
        }
        if (cylinder.shared_ignition_wire_semantic_id.has_value()) {
            ++shared_ignition_wire_counts
                  [cylinder.shared_ignition_wire_semantic_id->value];
            require(
                report,
                is_valid_semantic_id(
                    cylinder.shared_ignition_wire_semantic_id->value),
                ContractIssueCode::invalid_value,
                path + ".shared_ignition_wire_semantic_id.value",
                "shared ignition wire semantic ID must be canonical");
        }
        require(report, bank_ids.contains(cylinder.bank_id.value),
                ContractIssueCode::dangling_reference, path + ".bank_id",
                "cylinder references an unknown bank");
        require(report, finite_positive(cylinder.bore_m.value),
                ContractIssueCode::invalid_value, path + ".bore_m.value",
                "bore must be finite and positive");
        require(report, finite_positive(cylinder.stroke_m.value),
                ContractIssueCode::invalid_value, path + ".stroke_m.value",
                "stroke must be finite and positive");
        require(
            report,
            finite_positive(cylinder.connecting_rod_length_m.value) &&
                cylinder.connecting_rod_length_m.value > 0.5 * cylinder.stroke_m.value,
            ContractIssueCode::invalid_value, path + ".connecting_rod_length_m.value",
            "rod length must exceed crank radius");
        require(report,
                finite(cylinder.compression_ratio.value) &&
                    cylinder.compression_ratio.value > 1.0,
                ContractIssueCode::invalid_value, path + ".compression_ratio.value",
                "compression ratio must be finite and greater than one");
        require(report,
                finite(cylinder.firing_tdc_offset_rad.value) &&
                    finite(cylinder.journal_phase_rad.value),
                ContractIssueCode::invalid_value, path,
                "cylinder phase angles must be finite");
        if (finite_positive(cylinder.bore_m.value) &&
            finite_positive(cylinder.stroke_m.value)) {
            computed_displacement_m3 += std::numbers::pi * cylinder.bore_m.value *
                                        cylinder.bore_m.value *
                                        cylinder.stroke_m.value / 4.0;
        }
    }
    for (const auto &[wire, count] : shared_ignition_wire_counts) {
        require(report, count >= 2U, ContractIssueCode::inconsistent_shape,
                "engine.cylinders",
                "shared ignition wire '" + wire +
                    "' must identify at least two cylinders");
    }
    for (const auto &cylinder : spec.cylinders) {
        if (!cylinder.master_rod_attachment.has_value()) {
            continue;
        }
        const auto path = resolved_path("cylinders", cylinder.semantic_id.value) +
                          ".master_rod_attachment.master_cylinder_id";
        const auto master_id = cylinder.master_rod_attachment->master_cylinder_id;
        const auto master =
            std::ranges::find(spec.cylinders, master_id, &CylinderSpec::id);
        require(report, master != spec.cylinders.end(),
                ContractIssueCode::dangling_reference, path,
                "master-rod attachment references an unknown master cylinder");
        require(report, master_id != cylinder.id,
                ContractIssueCode::inconsistent_semantics, path,
                "master-rod attachment cannot reference its own cylinder");
        if (master != spec.cylinders.end()) {
            require(report, !master->master_rod_attachment.has_value(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "master cylinder must use a direct crankshaft journal");
        }
    }
    const auto displacement_scale = std::max(std::abs(spec.total_displacement_m3.value),
                                             std::abs(computed_displacement_m3));
    require(report,
            finite(computed_displacement_m3) &&
                std::abs(spec.total_displacement_m3.value - computed_displacement_m3) <=
                    1.0e-12 * std::max(1.0, displacement_scale),
            ContractIssueCode::inconsistent_semantics,
            "engine.total_displacement_m3.value",
            "total displacement must equal the sum of cylinder swept volumes");

    std::unordered_set<std::uint32_t> volume_ids;
    for (const auto &volume : spec.gas_volumes) {
        const auto path = resolved_path("gas_volumes", volume.semantic_id.value);
        volume_ids.insert(volume.id.value);
        validate_resolved(report, volume.semantic_id, provenance,
                          path + ".semantic_id");
        validate_resolved(report, volume.kind, provenance, path + ".kind");
        require(report, is_valid_semantic_id(volume.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "gas-volume semantic ID must be canonical");
        require(report, known(volume.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "gas-volume kind is not recognized");
    }
    detail::require_unique_semantic_ids(
        report, spec.gas_volumes,
        [](const GasVolumeSpec &volume) -> const std::string & {
            return volume.semantic_id.value;
        },
        "engine.gas_volumes");

    for (const auto &port : spec.ports) {
        const auto path = resolved_path("ports", port.semantic_id.value);
        validate_resolved(report, port.semantic_id, provenance, path + ".semantic_id");
        validate_resolved(report, port.kind, provenance, path + ".kind");
        require(report, is_valid_semantic_id(port.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "port semantic ID must be canonical");
        require(report, known(port.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "port kind is not recognized");
        require(report, cylinder_ids.contains(port.cylinder_id.value),
                ContractIssueCode::dangling_reference, path + ".cylinder_id",
                "port references an unknown cylinder");
    }
    detail::require_unique_semantic_ids(
        report, spec.ports,
        [](const PortSpec &port) -> const std::string & {
            return port.semantic_id.value;
        },
        "engine.ports");

    for (const auto &edge : spec.flow_edges) {
        const auto path = resolved_path("flow_edges", edge.semantic_id.value);
        validate_resolved(report, edge.semantic_id, provenance, path + ".semantic_id");
        require(report, is_valid_semantic_id(edge.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "flow-edge semantic ID must be canonical");
        require(report,
                volume_ids.contains(edge.endpoint_0_volume_id.value) &&
                    volume_ids.contains(edge.endpoint_1_volume_id.value),
                ContractIssueCode::dangling_reference, path,
                "flow edge references an unknown gas volume");
        require(report, edge.endpoint_0_volume_id != edge.endpoint_1_volume_id,
                ContractIssueCode::inconsistent_semantics, path,
                "flow edge must connect two different gas volumes");
    }
    detail::require_unique_semantic_ids(
        report, spec.flow_edges,
        [](const FlowEdgeSpec &edge) -> const std::string & {
            return edge.semantic_id.value;
        },
        "engine.flow_edges");

    std::unordered_set<std::uint32_t> route_ids;
    std::vector<std::string> route_semantic_ids;
    std::unordered_map<std::string, std::optional<std::string>> route_parents;
    for (const auto &route : spec.routes) {
        route_ids.insert(route.id.value);
    }
    for (const auto &route : spec.routes) {
        const auto path = resolved_path("routes", route.semantic_id.value);
        validate_resolved(report, route.semantic_id, provenance, path + ".semantic_id");
        validate_resolved(report, route.kind, provenance, path + ".kind");
        require(report, is_valid_semantic_id(route.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "route semantic ID must be canonical");
        require(report, known(route.kind.value), ContractIssueCode::unsupported_value,
                path + ".kind.value", "source-route kind is not recognized");
        if (route.source_volume_id.has_value()) {
            require(report, volume_ids.contains(route.source_volume_id->value),
                    ContractIssueCode::dangling_reference, path + ".source_volume_id",
                    "route source references an unknown gas volume");
        }
        const auto gas_route = route.kind.value == SourceRouteKind::exhaust_outlet ||
                               route.kind.value == SourceRouteKind::intake_inlet;
        const auto mechanical_route =
            route.kind.value == SourceRouteKind::mechanical_engine ||
            route.kind.value == SourceRouteKind::mechanical_starter;
        if (gas_route) {
            require(report, route.source_volume_id.has_value(),
                    ContractIssueCode::missing_value, path + ".source_volume_id",
                    "gas source route requires a physical source volume");
        }
        if (mechanical_route) {
            require(report, !route.source_volume_id.has_value(),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".source_volume_id",
                    "mechanical source route cannot bind a gas volume");
            require(report, route.emitter_anchor_id.has_value(),
                    ContractIssueCode::missing_value, path + ".emitter_anchor_id",
                    "mechanical source route requires a canonical emitter "
                    "anchor");
        }
        std::optional<std::string> parent_semantic_id;
        if (route.default_parent_route_id.has_value()) {
            require(report, route_ids.contains(route.default_parent_route_id->value),
                    ContractIssueCode::dangling_reference,
                    path + ".default_parent_route_id",
                    "route parent references an unknown route");
            const auto parent = std::ranges::find(
                spec.routes, *route.default_parent_route_id, &RouteSpec::id);
            if (parent != spec.routes.end()) {
                parent_semantic_id = parent->semantic_id.value;
            }
        }
        if (route.emitter_anchor_id.has_value()) {
            validate_resolved(report, *route.emitter_anchor_id, provenance,
                              path + ".emitter_anchor_id");
            require(report, is_valid_semantic_id(route.emitter_anchor_id->value),
                    ContractIssueCode::invalid_value, path + ".emitter_anchor_id.value",
                    "emitter anchor ID must be canonical");
        }
        route_semantic_ids.push_back(route.semantic_id.value);
        route_parents.emplace(route.semantic_id.value, std::move(parent_semantic_id));
    }
    detail::require_unique_semantic_ids(
        report, spec.routes,
        [](const RouteSpec &route) -> const std::string & {
            return route.semantic_id.value;
        },
        "engine.routes");
    const auto resolved_parent =
        [&](const std::string &id) -> std::optional<std::string> {
        const auto iterator = route_parents.find(id);
        return iterator == route_parents.end() ? std::nullopt : iterator->second;
    };
    require(report, !has_parent_cycle(route_semantic_ids, resolved_parent),
            ContractIssueCode::inconsistent_semantics, "engine.routes",
            "source-route parent graph must be acyclic");

    for_each_method(spec.methods, [&](const auto &method, const std::string &path) {
        validate_resolved(report, method, provenance, path);
        detail::append_prefixed(report, validate(method.value), path + ".value");
    });
    detail::append_prefixed(report, validate(spec.physics_profile, spec, provenance),
                            "physics_profile");
    validate_resolved(report, spec.torque_capability, provenance,
                      "engine.torque_capability");
    detail::append_prefixed(report, validate(spec.torque_capability.value),
                            "engine.torque_capability.value");
    return report;
}

} // namespace engine_sim_offline::contract
