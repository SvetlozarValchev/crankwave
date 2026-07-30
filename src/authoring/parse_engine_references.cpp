#include "authoring/parse_engine_references.hpp"

#include "authoring/parse_engine_detail.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace engine_sim_offline::authoring::detail {
namespace {

using IdSet = std::unordered_set<std::string_view>;

struct EngineIndexes {
    IdSet curves;
    IdSet crankshafts;
    IdSet journals;
    IdSet connecting_rods;
    IdSet pistons;
    IdSet banks;
    IdSet intakes;
    IdSet exhausts;
    IdSet ports;
    IdSet cam_lobes;
    IdSet camshafts;
    IdSet valvetrains;
    IdSet heads;
    IdSet fuels;
    IdSet ignition_wires;
    IdSet throttle_controllers;
    IdSet cylinders;
    IdSet source_routes;
    IdSet audio_assets;
    IdSet audio_buses;
    IdSet gears;
};

template <class Definitions>
[[nodiscard]] IdSet collect_ids(DocumentReader &reader,
                                const Definitions &definitions,
                                std::string_view array_path,
                                std::string_view kind) {
    IdSet result;
    result.reserve(definitions.size());
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto &id = definitions[index].id.value;
        if (id.empty()) {
            continue;
        }
        if (!result.insert(id).second) {
            reader.add(DiagnosticCode::duplicate_id,
                       pointer_member(pointer_index(array_path, index), "id"),
                       "duplicate " + std::string{kind} + " ID '" + id + "'",
                       subject(std::string{kind}, id));
        }
    }
    return result;
}

void require_count(DocumentReader &reader, std::size_t actual,
                   std::size_t minimum, std::string_view path,
                   std::string_view description,
                   const std::optional<DiagnosticSubject> &owner) {
    if (actual < minimum) {
        reader.add(DiagnosticCode::missing_value, path,
                   std::string{description} + " requires at least " +
                       std::to_string(minimum) + " item" +
                       (minimum == 1U ? "" : "s"),
                   owner);
    }
}

template <class Ref>
void require_reference(DocumentReader &reader, const IdSet &ids,
                       const Ref &reference, std::string_view path,
                       std::string_view kind,
                       const std::optional<DiagnosticSubject> &owner = {}) {
    if (!reference.value.empty() && !ids.contains(reference.value)) {
        reader.add(DiagnosticCode::dangling_reference, path,
                   std::string{kind} + " reference '" + reference.value +
                       "' does not resolve",
                   owner);
    }
}

void validate_flow_restriction(
    DocumentReader &reader, const EngineIndexes &indexes,
    const FlowRestriction &restriction, std::string_view path,
    const std::optional<DiagnosticSubject> &owner) {
    if (const auto *curve = std::get_if<CurveRestriction>(&restriction)) {
        require_reference(reader, indexes.curves, curve->pressure_drop_to_flow,
                          pointer_member(path, "pressure_drop_to_flow"), "curve",
                          owner);
    }
}

EngineIndexes build_indexes(DocumentReader &reader,
                            const EnginePackageDocument &document) {
    const auto &engine = document.engine;
    EngineIndexes indexes;
    indexes.curves =
        collect_ids(reader, engine.curves, "/engine/curves", "curve");
    indexes.crankshafts = collect_ids(reader, engine.crankshafts,
                                     "/engine/crankshafts", "crankshaft");
    indexes.journals =
        collect_ids(reader, engine.journals, "/engine/journals", "journal");
    indexes.connecting_rods =
        collect_ids(reader, engine.connecting_rods, "/engine/connecting_rods",
                    "connecting_rod");
    indexes.pistons =
        collect_ids(reader, engine.pistons, "/engine/pistons", "piston");
    indexes.banks = collect_ids(reader, engine.banks, "/engine/banks", "bank");
    indexes.intakes =
        collect_ids(reader, engine.intakes, "/engine/intakes", "intake");
    indexes.exhausts =
        collect_ids(reader, engine.exhausts, "/engine/exhausts", "exhaust");
    indexes.ports = collect_ids(reader, engine.ports, "/engine/ports", "port");
    indexes.cam_lobes =
        collect_ids(reader, engine.cam_lobes, "/engine/cam_lobes", "cam_lobe");
    indexes.camshafts =
        collect_ids(reader, engine.camshafts, "/engine/camshafts", "camshaft");
    indexes.valvetrains = collect_ids(reader, engine.valvetrains,
                                     "/engine/valvetrains", "valvetrain");
    indexes.heads = collect_ids(reader, engine.heads, "/engine/heads", "head");
    indexes.fuels = collect_ids(reader, engine.fuels, "/engine/fuels", "fuel");
    indexes.ignition_wires =
        collect_ids(reader, engine.ignition.wires, "/engine/ignition/wires",
                    "ignition_wire");
    if (engine.throttle_controllers) {
        indexes.throttle_controllers =
            collect_ids(reader, *engine.throttle_controllers,
                        "/engine/throttle_controllers", "throttle_controller");
    }
    indexes.cylinders =
        collect_ids(reader, engine.cylinders, "/engine/cylinders", "cylinder");
    indexes.source_routes = collect_ids(reader, engine.source_routes,
                                       "/engine/source_routes", "source_route");
    indexes.audio_assets = collect_ids(reader, document.presentation.assets,
                                       "/presentation/assets", "audio_asset");
    indexes.audio_buses = collect_ids(reader, document.presentation.buses,
                                     "/presentation/buses", "audio_bus");
    if (document.rig && document.rig->transmission) {
        indexes.gears =
            collect_ids(reader, document.rig->transmission->gears,
                        "/rig/transmission/gears", "gear");
    }
    return indexes;
}

void validate_required_collections(
    DocumentReader &reader, const EnginePackageDocument &document) {
    const auto &engine = document.engine;
    const auto owner = subject("engine", engine.identity.id.value);
    require_count(reader, engine.crankshafts.size(), 1U, "/engine/crankshafts",
                  "engine crankshaft collection", owner);
    require_count(reader, engine.journals.size(), 1U, "/engine/journals",
                  "engine journal collection", owner);
    require_count(reader, engine.connecting_rods.size(), 1U,
                  "/engine/connecting_rods", "engine connecting-rod collection",
                  owner);
    require_count(reader, engine.pistons.size(), 1U, "/engine/pistons",
                  "engine piston collection", owner);
    require_count(reader, engine.banks.size(), 1U, "/engine/banks",
                  "engine bank collection", owner);
    require_count(reader, engine.intakes.size(), 1U, "/engine/intakes",
                  "engine intake collection", owner);
    require_count(reader, engine.exhausts.size(), 1U, "/engine/exhausts",
                  "engine exhaust collection", owner);
    require_count(reader, engine.ports.size(), 1U, "/engine/ports",
                  "engine port collection", owner);
    require_count(reader, engine.camshafts.size(), 1U, "/engine/camshafts",
                  "engine camshaft collection", owner);
    require_count(reader, engine.valvetrains.size(), 1U, "/engine/valvetrains",
                  "engine valvetrain collection", owner);
    require_count(reader, engine.heads.size(), 1U, "/engine/heads",
                  "engine head collection", owner);
    require_count(reader, engine.fuels.size(), 1U, "/engine/fuels",
                  "engine fuel collection", owner);
    require_count(reader, engine.ignition.wires.size(), 1U,
                  "/engine/ignition/wires", "ignition wire collection", owner);
    require_count(reader, engine.ignition.firing_order.size(), 1U,
                  "/engine/ignition/firing_order", "ignition firing order", owner);
    require_count(reader, engine.cylinders.size(), 1U, "/engine/cylinders",
                  "engine cylinder collection", owner);
    require_count(reader, engine.source_routes.size(), 1U,
                  "/engine/source_routes", "engine source-route collection", owner);
    if (document.rig && document.rig->transmission) {
        const auto transmission_owner =
            subject("transmission", document.rig->transmission->id.value);
        require_count(reader, document.rig->transmission->gears.size(), 1U,
                      "/rig/transmission/gears", "transmission gear collection",
                      transmission_owner);
    }
}

void validate_mechanism_references(DocumentReader &reader,
                                   const EngineDefinition &engine,
                                   const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.crankshafts.size(); ++index) {
        const auto &crankshaft = engine.crankshafts[index];
        const auto path = pointer_index("/engine/crankshafts", index);
        const auto owner = subject("crankshaft", crankshaft.id.value);
        for (std::size_t journal_index = 0;
             journal_index < crankshaft.journals.size(); ++journal_index) {
            require_reference(
                reader, indexes.journals, crankshaft.journals[journal_index],
                pointer_index(pointer_member(path, "journals"), journal_index),
                "journal", owner);
        }
    }
    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto &journal = engine.journals[index];
        const auto path = pointer_index("/engine/journals", index);
        const auto owner = subject("journal", journal.id.value);
        require_reference(reader, indexes.crankshafts, journal.crankshaft,
                          pointer_member(path, "crankshaft"), "crankshaft", owner);
        if (journal.master_journal) {
            require_reference(reader, indexes.journals, *journal.master_journal,
                              pointer_member(path, "master_journal"), "journal",
                              owner);
        }
    }
    for (std::size_t index = 0; index < engine.pistons.size(); ++index) {
        const auto &piston = engine.pistons[index];
        if (piston.blowby) {
            validate_flow_restriction(
                reader, indexes, *piston.blowby,
                pointer_member(pointer_index("/engine/pistons", index), "blowby"),
                subject("piston", piston.id.value));
        }
    }
    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        const auto &bank = engine.banks[index];
        require_reference(
            reader, indexes.heads, bank.head,
            pointer_member(pointer_index("/engine/banks", index), "head"), "head",
            subject("bank", bank.id.value));
    }
}

void validate_gas_references(DocumentReader &reader,
                             const EngineDefinition &engine,
                             const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.intakes.size(); ++index) {
        const auto &intake = engine.intakes[index];
        const auto path = pointer_index("/engine/intakes", index);
        const auto owner = subject("intake", intake.id.value);
        validate_flow_restriction(
            reader, indexes, intake.main_restriction,
            pointer_member(path, "main_restriction"), owner);
        validate_flow_restriction(
            reader, indexes, intake.idle_bypass_restriction,
            pointer_member(path, "idle_bypass_restriction"), owner);
        validate_flow_restriction(
            reader, indexes, intake.runner_restriction,
            pointer_member(path, "runner_restriction"), owner);
    }
    for (std::size_t index = 0; index < engine.exhausts.size(); ++index) {
        const auto &exhaust = engine.exhausts[index];
        const auto path = pointer_index("/engine/exhausts", index);
        const auto owner = subject("exhaust", exhaust.id.value);
        validate_flow_restriction(
            reader, indexes, exhaust.outlet_restriction,
            pointer_member(path, "outlet_restriction"), owner);
        validate_flow_restriction(
            reader, indexes, exhaust.primary_restriction,
            pointer_member(path, "primary_restriction"), owner);
    }
    for (std::size_t index = 0; index < engine.ports.size(); ++index) {
        const auto &port = engine.ports[index];
        const auto path = pointer_index("/engine/ports", index);
        const auto owner = subject("port", port.id.value);
        require_reference(reader, indexes.heads, port.head,
                          pointer_member(path, "head"), "head", owner);
        require_reference(reader, indexes.curves, port.flow_curve,
                          pointer_member(path, "flow_curve"), "curve", owner);
    }
}

void validate_valvetrain_references(DocumentReader &reader,
                                    const EngineDefinition &engine,
                                    const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.cam_lobes.size(); ++index) {
        const auto &lobe = engine.cam_lobes[index];
        const auto path = pointer_index("/engine/cam_lobes", index);
        const auto owner = subject("cam_lobe", lobe.id.value);
        require_reference(reader, indexes.cylinders, lobe.cylinder,
                          pointer_member(path, "cylinder"), "cylinder", owner);
        if (const auto *sampled = std::get_if<SampledCamLobe>(&lobe.shape)) {
            require_reference(reader, indexes.curves, sampled->lift_curve,
                              pointer_member(path, "lift_curve"), "curve", owner);
        }
    }
    for (std::size_t index = 0; index < engine.camshafts.size(); ++index) {
        const auto &camshaft = engine.camshafts[index];
        const auto path = pointer_index("/engine/camshafts", index);
        const auto owner = subject("camshaft", camshaft.id.value);
        for (std::size_t lobe_index = 0; lobe_index < camshaft.lobes.size();
             ++lobe_index) {
            require_reference(
                reader, indexes.cam_lobes, camshaft.lobes[lobe_index],
                pointer_index(pointer_member(path, "lobes"), lobe_index),
                "cam_lobe", owner);
        }
    }
    for (std::size_t index = 0; index < engine.valvetrains.size(); ++index) {
        const auto &valvetrain = engine.valvetrains[index];
        const auto path = pointer_index("/engine/valvetrains", index);
        const auto owner = subject("valvetrain", valvetrain.id.value);
        if (const auto *standard =
                std::get_if<StandardValvetrain>(&valvetrain.kind)) {
            require_reference(reader, indexes.camshafts,
                              standard->intake_camshaft,
                              pointer_member(path, "intake_camshaft"), "camshaft",
                              owner);
            require_reference(reader, indexes.camshafts,
                              standard->exhaust_camshaft,
                              pointer_member(path, "exhaust_camshaft"), "camshaft",
                              owner);
        } else if (const auto *vtec =
                       std::get_if<VtecValvetrain>(&valvetrain.kind)) {
            require_reference(reader, indexes.camshafts,
                              vtec->base_intake_camshaft,
                              pointer_member(path, "base_intake_camshaft"),
                              "camshaft", owner);
            require_reference(reader, indexes.camshafts,
                              vtec->base_exhaust_camshaft,
                              pointer_member(path, "base_exhaust_camshaft"),
                              "camshaft", owner);
            require_reference(reader, indexes.camshafts,
                              vtec->alternate_intake_camshaft,
                              pointer_member(path, "alternate_intake_camshaft"),
                              "camshaft", owner);
            require_reference(reader, indexes.camshafts,
                              vtec->alternate_exhaust_camshaft,
                              pointer_member(path, "alternate_exhaust_camshaft"),
                              "camshaft", owner);
        }
    }
    for (std::size_t index = 0; index < engine.heads.size(); ++index) {
        const auto &head = engine.heads[index];
        const auto path = pointer_index("/engine/heads", index);
        const auto owner = subject("head", head.id.value);
        require_reference(reader, indexes.valvetrains, head.valvetrain,
                          pointer_member(path, "valvetrain"), "valvetrain", owner);
        for (std::size_t port_index = 0; port_index < head.ports.size();
             ++port_index) {
            require_reference(
                reader, indexes.ports, head.ports[port_index],
                pointer_index(pointer_member(path, "ports"), port_index), "port",
                owner);
        }
    }
}

void validate_system_references(DocumentReader &reader,
                                const EngineDefinition &engine,
                                const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.fuels.size(); ++index) {
        const auto &fuel = engine.fuels[index];
        require_reference(
            reader, indexes.curves, fuel.turbulence_to_flame_speed,
            pointer_member(pointer_index("/engine/fuels", index),
                           "turbulence_to_flame_speed"),
            "curve", subject("fuel", fuel.id.value));
    }
    const auto engine_owner = subject("engine", engine.identity.id.value);
    require_reference(reader, indexes.fuels, engine.default_fuel,
                      "/engine/default_fuel", "fuel", engine_owner);
    require_reference(reader, indexes.curves, engine.ignition.timing_curve,
                      "/engine/ignition/timing_curve", "curve", engine_owner);
    for (std::size_t index = 0; index < engine.ignition.firing_order.size();
         ++index) {
        require_reference(
            reader, indexes.ignition_wires,
            engine.ignition.firing_order[index].wire,
            pointer_member(pointer_index("/engine/ignition/firing_order", index),
                           "wire"),
            "ignition_wire", engine_owner);
    }
    if (engine.throttle_controller) {
        require_reference(reader, indexes.throttle_controllers,
                          *engine.throttle_controller,
                          "/engine/throttle_controller", "throttle_controller",
                          engine_owner);
    }
}

void validate_cylinder_and_route_references(
    DocumentReader &reader, const EngineDefinition &engine,
    const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        const auto path = pointer_index("/engine/cylinders", index);
        const auto owner = subject("cylinder", cylinder.id.value);
        require_reference(reader, indexes.banks, cylinder.bank,
                          pointer_member(path, "bank"), "bank", owner);
        require_reference(reader, indexes.crankshafts, cylinder.crankshaft,
                          pointer_member(path, "crankshaft"), "crankshaft", owner);
        require_reference(reader, indexes.journals, cylinder.journal,
                          pointer_member(path, "journal"), "journal", owner);
        if (cylinder.slave_journal) {
            require_reference(reader, indexes.journals, *cylinder.slave_journal,
                              pointer_member(path, "slave_journal"), "journal",
                              owner);
        }
        require_reference(reader, indexes.connecting_rods, cylinder.connecting_rod,
                          pointer_member(path, "connecting_rod"), "connecting_rod",
                          owner);
        require_reference(reader, indexes.pistons, cylinder.piston,
                          pointer_member(path, "piston"), "piston", owner);
        require_reference(reader, indexes.intakes, cylinder.intake,
                          pointer_member(path, "intake"), "intake", owner);
        require_reference(reader, indexes.exhausts, cylinder.exhaust,
                          pointer_member(path, "exhaust"), "exhaust", owner);
        require_reference(reader, indexes.ignition_wires,
                          cylinder.ignition_wire,
                          pointer_member(path, "ignition_wire"), "ignition_wire",
                          owner);
        require_reference(reader, indexes.ports, cylinder.intake_port,
                          pointer_member(path, "intake_port"), "port", owner);
        require_reference(reader, indexes.ports, cylinder.exhaust_port,
                          pointer_member(path, "exhaust_port"), "port", owner);
    }
    for (std::size_t index = 0; index < engine.source_routes.size(); ++index) {
        const auto &route = engine.source_routes[index];
        const auto path = pointer_index("/engine/source_routes", index);
        const auto owner = subject("source_route", route.id.value);
        if (const auto *exhaust = std::get_if<ExhaustRouteSource>(&route.source)) {
            require_reference(reader, indexes.exhausts, exhaust->exhaust,
                              pointer_member(path, "exhaust"), "exhaust", owner);
        } else if (const auto *intake =
                       std::get_if<IntakeRouteSource>(&route.source)) {
            require_reference(reader, indexes.intakes, intake->intake,
                              pointer_member(path, "intake"), "intake", owner);
        }
    }
}

void validate_presentation_references(
    DocumentReader &reader, const PresentationDefinition &presentation,
    const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < presentation.cylinder_routes.size();
         ++index) {
        const auto &route = presentation.cylinder_routes[index];
        const auto path = pointer_index("/presentation/cylinder_routes", index);
        const auto owner = subject("cylinder", route.cylinder.value);
        require_reference(reader, indexes.cylinders, route.cylinder,
                          pointer_member(path, "cylinder"), "cylinder", owner);
        require_reference(reader, indexes.source_routes, route.route,
                          pointer_member(path, "route"), "source_route", owner);
    }
    for (std::size_t index = 0; index < presentation.routes.size(); ++index) {
        const auto &route = presentation.routes[index];
        const auto path = pointer_index("/presentation/routes", index);
        const auto owner = subject("source_route", route.route.value);
        require_reference(reader, indexes.source_routes, route.route,
                          pointer_member(path, "route"), "source_route", owner);
        if (route.impulse_response) {
            require_reference(reader, indexes.audio_assets,
                              *route.impulse_response,
                              pointer_member(path, "impulse_response"),
                              "audio_asset", owner);
        }
    }
    for (std::size_t index = 0; index < presentation.buses.size(); ++index) {
        const auto &bus = presentation.buses[index];
        const auto path = pointer_index("/presentation/buses", index);
        const auto owner = subject("audio_bus", bus.id.value);
        for (std::size_t route_index = 0; route_index < bus.routes.size();
             ++route_index) {
            require_reference(
                reader, indexes.source_routes, bus.routes[route_index],
                pointer_index(pointer_member(path, "routes"), route_index),
                "source_route", owner);
        }
    }
    for (std::size_t index = 0; index < presentation.audition.buses.size();
         ++index) {
        require_reference(
            reader, indexes.audio_buses, presentation.audition.buses[index],
            pointer_index("/presentation/audition/buses", index), "audio_bus");
    }
}

} // namespace

void validate_engine_document(DocumentReader &reader,
                              const EnginePackageDocument &document) {
    validate_required_collections(reader, document);
    auto indexes = build_indexes(reader, document);
    validate_mechanism_references(reader, document.engine, indexes);
    validate_gas_references(reader, document.engine, indexes);
    validate_valvetrain_references(reader, document.engine, indexes);
    validate_system_references(reader, document.engine, indexes);
    validate_cylinder_and_route_references(reader, document.engine, indexes);
    validate_presentation_references(reader, document.presentation, indexes);
}

} // namespace engine_sim_offline::authoring::detail
