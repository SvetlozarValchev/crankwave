#include "authoring/parse_engine_references.hpp"

#include "authoring/parse_engine_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace crankwave::authoring::detail {
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
    IdSet accessory_configurations;
    IdSet ignition_wires;
    IdSet throttle_controllers;
    IdSet cylinders;
    IdSet source_routes;
    IdSet audio_assets;
    IdSet audio_buses;
    IdSet gears;
};

template <class Definitions>
[[nodiscard]] IdSet collect_ids(DocumentReader &reader, const Definitions &definitions,
                                std::string_view array_path, std::string_view kind) {
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

void require_count(DocumentReader &reader, std::size_t actual, std::size_t minimum,
                   std::string_view path, std::string_view description,
                   const std::optional<DiagnosticSubject> &owner) {
    if (actual < minimum) {
        reader.add(DiagnosticCode::missing_value, path,
                   std::string{description} + " requires at least " +
                       std::to_string(minimum) + " item" + (minimum == 1U ? "" : "s"),
                   owner);
    }
}

template <class Ref>
void require_reference(DocumentReader &reader, const IdSet &ids, const Ref &reference,
                       std::string_view path, std::string_view kind,
                       const std::optional<DiagnosticSubject> &owner = {}) {
    if (!reference.value.empty() && !ids.contains(reference.value)) {
        reader.add(DiagnosticCode::dangling_reference, path,
                   std::string{kind} + " reference '" + reference.value +
                       "' does not resolve",
                   owner);
    }
}

void validate_flow_restriction(DocumentReader &reader, const EngineIndexes &indexes,
                               const FlowRestriction &restriction,
                               std::string_view path,
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
    indexes.curves = collect_ids(reader, engine.curves, "/engine/curves", "curve");
    indexes.crankshafts =
        collect_ids(reader, engine.crankshafts, "/engine/crankshafts", "crankshaft");
    indexes.journals =
        collect_ids(reader, engine.journals, "/engine/journals", "journal");
    indexes.connecting_rods = collect_ids(reader, engine.connecting_rods,
                                          "/engine/connecting_rods", "connecting_rod");
    indexes.pistons = collect_ids(reader, engine.pistons, "/engine/pistons", "piston");
    indexes.banks = collect_ids(reader, engine.banks, "/engine/banks", "bank");
    indexes.intakes = collect_ids(reader, engine.intakes, "/engine/intakes", "intake");
    indexes.exhausts =
        collect_ids(reader, engine.exhausts, "/engine/exhausts", "exhaust");
    indexes.ports = collect_ids(reader, engine.ports, "/engine/ports", "port");
    indexes.cam_lobes =
        collect_ids(reader, engine.cam_lobes, "/engine/cam_lobes", "cam_lobe");
    indexes.camshafts =
        collect_ids(reader, engine.camshafts, "/engine/camshafts", "camshaft");
    indexes.valvetrains =
        collect_ids(reader, engine.valvetrains, "/engine/valvetrains", "valvetrain");
    indexes.heads = collect_ids(reader, engine.heads, "/engine/heads", "head");
    indexes.fuels = collect_ids(reader, engine.fuels, "/engine/fuels", "fuel");
    indexes.accessory_configurations =
        collect_ids(reader, engine.accessory_configurations,
                    "/engine/accessory_configurations", "accessory_configuration");
    indexes.ignition_wires = collect_ids(reader, engine.ignition.wires,
                                         "/engine/ignition/wires", "ignition_wire");
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
        indexes.gears = collect_ids(reader, document.rig->transmission->gears,
                                    "/rig/transmission/gears", "gear");
    }
    return indexes;
}

void validate_required_collections(DocumentReader &reader,
                                   const EnginePackageDocument &document) {
    const auto &engine = document.engine;
    const auto owner = subject("engine", engine.identity.id.value);
    require_count(reader, engine.crankshafts.size(), 1U, "/engine/crankshafts",
                  "engine crankshaft collection", owner);
    require_count(reader, engine.journals.size(), 1U, "/engine/journals",
                  "engine journal collection", owner);
    require_count(reader, engine.connecting_rods.size(), 1U, "/engine/connecting_rods",
                  "engine connecting-rod collection", owner);
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
    require_count(reader, engine.accessory_configurations.size(), 1U,
                  "/engine/accessory_configurations",
                  "engine accessory-configuration collection", owner);
    require_count(reader, engine.ignition.wires.size(), 1U, "/engine/ignition/wires",
                  "ignition wire collection", owner);
    require_count(reader, engine.ignition.firing_order.size(), 1U,
                  "/engine/ignition/firing_order", "ignition firing order", owner);
    require_count(reader, engine.cylinders.size(), 1U, "/engine/cylinders",
                  "engine cylinder collection", owner);
    require_count(reader, engine.source_routes.size(), 1U, "/engine/source_routes",
                  "engine source-route collection", owner);
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
    require_reference(reader, indexes.crankshafts, engine.output_crankshaft,
                      "/engine/output_crankshaft", "crankshaft",
                      subject("engine", engine.identity.id.value));
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
        require_reference(reader, indexes.heads, bank.head,
                          pointer_member(pointer_index("/engine/banks", index), "head"),
                          "head", subject("bank", bank.id.value));
    }
}

void validate_gas_references(DocumentReader &reader, const EngineDefinition &engine,
                             const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.intakes.size(); ++index) {
        const auto &intake = engine.intakes[index];
        const auto path = pointer_index("/engine/intakes", index);
        const auto owner = subject("intake", intake.id.value);
        validate_flow_restriction(reader, indexes, intake.main_restriction,
                                  pointer_member(path, "main_restriction"), owner);
        validate_flow_restriction(reader, indexes, intake.idle_bypass_restriction,
                                  pointer_member(path, "idle_bypass_restriction"),
                                  owner);
        validate_flow_restriction(reader, indexes, intake.runner_restriction,
                                  pointer_member(path, "runner_restriction"), owner);
    }
    for (std::size_t index = 0; index < engine.exhausts.size(); ++index) {
        const auto &exhaust = engine.exhausts[index];
        const auto path = pointer_index("/engine/exhausts", index);
        const auto owner = subject("exhaust", exhaust.id.value);
        validate_flow_restriction(reader, indexes, exhaust.outlet_restriction,
                                  pointer_member(path, "outlet_restriction"), owner);
        validate_flow_restriction(reader, indexes, exhaust.primary_restriction,
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
            require_reference(reader, indexes.cam_lobes, camshaft.lobes[lobe_index],
                              pointer_index(pointer_member(path, "lobes"), lobe_index),
                              "cam_lobe", owner);
        }
    }
    for (std::size_t index = 0; index < engine.valvetrains.size(); ++index) {
        const auto &valvetrain = engine.valvetrains[index];
        const auto path = pointer_index("/engine/valvetrains", index);
        const auto owner = subject("valvetrain", valvetrain.id.value);
        if (const auto *standard = std::get_if<StandardValvetrain>(&valvetrain.kind)) {
            require_reference(reader, indexes.camshafts, standard->intake_camshaft,
                              pointer_member(path, "intake_camshaft"), "camshaft",
                              owner);
            require_reference(reader, indexes.camshafts, standard->exhaust_camshaft,
                              pointer_member(path, "exhaust_camshaft"), "camshaft",
                              owner);
        } else if (const auto *vtec = std::get_if<VtecValvetrain>(&valvetrain.kind)) {
            require_reference(reader, indexes.camshafts, vtec->base_intake_camshaft,
                              pointer_member(path, "base_intake_camshaft"), "camshaft",
                              owner);
            require_reference(reader, indexes.camshafts, vtec->base_exhaust_camshaft,
                              pointer_member(path, "base_exhaust_camshaft"), "camshaft",
                              owner);
            require_reference(
                reader, indexes.camshafts, vtec->alternate_intake_camshaft,
                pointer_member(path, "alternate_intake_camshaft"), "camshaft", owner);
            require_reference(
                reader, indexes.camshafts, vtec->alternate_exhaust_camshaft,
                pointer_member(path, "alternate_exhaust_camshaft"), "camshaft", owner);
        }
    }
    for (std::size_t index = 0; index < engine.heads.size(); ++index) {
        const auto &head = engine.heads[index];
        const auto path = pointer_index("/engine/heads", index);
        const auto owner = subject("head", head.id.value);
        require_reference(reader, indexes.valvetrains, head.valvetrain,
                          pointer_member(path, "valvetrain"), "valvetrain", owner);
        for (std::size_t port_index = 0; port_index < head.ports.size(); ++port_index) {
            require_reference(reader, indexes.ports, head.ports[port_index],
                              pointer_index(pointer_member(path, "ports"), port_index),
                              "port", owner);
        }
    }
}

void validate_system_references(DocumentReader &reader, const EngineDefinition &engine,
                                const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.fuels.size(); ++index) {
        const auto &fuel = engine.fuels[index];
        require_reference(reader, indexes.curves, fuel.turbulence_to_flame_speed,
                          pointer_member(pointer_index("/engine/fuels", index),
                                         "turbulence_to_flame_speed"),
                          "curve", subject("fuel", fuel.id.value));
    }
    const auto engine_owner = subject("engine", engine.identity.id.value);
    require_reference(reader, indexes.fuels, engine.default_fuel,
                      "/engine/default_fuel", "fuel", engine_owner);
    if (const auto *loss = std::get_if<ChenFlynnLossDefinition>(&engine.losses)) {
        require_reference(reader, indexes.accessory_configurations,
                          loss->accessory_configuration_id,
                          "/engine/losses/accessory_configuration_id",
                          "accessory_configuration", engine_owner);
    }
    require_reference(reader, indexes.curves, engine.ignition.timing_curve,
                      "/engine/ignition/timing_curve", "curve", engine_owner);
    for (std::size_t index = 0; index < engine.ignition.firing_order.size(); ++index) {
        require_reference(
            reader, indexes.ignition_wires, engine.ignition.firing_order[index].wire,
            pointer_member(pointer_index("/engine/ignition/firing_order", index),
                           "wire"),
            "ignition_wire", engine_owner);
    }
    if (engine.throttle_controller) {
        require_reference(reader, indexes.throttle_controllers,
                          *engine.throttle_controller, "/engine/throttle_controller",
                          "throttle_controller", engine_owner);
    }
}

void validate_cylinder_and_route_references(DocumentReader &reader,
                                            const EngineDefinition &engine,
                                            const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        const auto path = pointer_index("/engine/cylinders", index);
        const auto owner = subject("cylinder", cylinder.id.value);
        require_reference(reader, indexes.banks, cylinder.bank,
                          pointer_member(path, "bank"), "bank", owner);
        require_reference(reader, indexes.journals, cylinder.journal,
                          pointer_member(path, "journal"), "journal", owner);
        require_reference(reader, indexes.connecting_rods, cylinder.connecting_rod,
                          pointer_member(path, "connecting_rod"), "connecting_rod",
                          owner);
        require_reference(reader, indexes.pistons, cylinder.piston,
                          pointer_member(path, "piston"), "piston", owner);
        require_reference(reader, indexes.intakes, cylinder.intake,
                          pointer_member(path, "intake"), "intake", owner);
        require_reference(reader, indexes.exhausts, cylinder.exhaust,
                          pointer_member(path, "exhaust"), "exhaust", owner);
        require_reference(reader, indexes.ignition_wires, cylinder.ignition_wire,
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
        }
    }
}

void validate_presentation_references(DocumentReader &reader,
                                      const PresentationDefinition &presentation,
                                      const EngineIndexes &indexes) {
    for (std::size_t index = 0; index < presentation.cylinder_routes.size(); ++index) {
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
            require_reference(reader, indexes.audio_assets, *route.impulse_response,
                              pointer_member(path, "impulse_response"), "audio_asset",
                              owner);
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
    for (std::size_t index = 0; index < presentation.audition.buses.size(); ++index) {
        require_reference(
            reader, indexes.audio_buses, presentation.audition.buses[index],
            pointer_index("/presentation/audition/buses", index), "audio_bus");
    }
}

} // namespace

DiagnosticReport validate_engine_mechanism_graph(const EngineDefinition &engine) {
    DiagnosticReport report;
    const auto add = [&](DiagnosticCode code, std::string path, std::string message,
                         std::optional<DiagnosticSubject> owner = {}) {
        report.diagnostics.push_back({
            DiagnosticSeverity::error,
            code,
            std::move(path),
            std::move(owner),
            std::nullopt,
            std::move(message),
            {},
        });
    };

    std::unordered_map<std::string_view, std::size_t> crankshafts;
    std::unordered_map<std::string_view, std::size_t> journals;
    std::unordered_map<std::string_view, std::size_t> cylinders;
    for (std::size_t index = 0; index < engine.crankshafts.size(); ++index) {
        const auto &id = engine.crankshafts[index].id.value;
        if (!id.empty()) {
            crankshafts.emplace(id, index);
        }
    }
    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto &id = engine.journals[index].id.value;
        if (!id.empty()) {
            journals.emplace(id, index);
        }
    }
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &id = engine.cylinders[index].id.value;
        if (!id.empty()) {
            cylinders.emplace(id, index);
        }
    }

    std::vector<std::size_t> consumer_counts(engine.journals.size(), 0U);
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto found = journals.find(engine.cylinders[index].journal.value);
        if (found == journals.end()) {
            continue;
        }
        const auto journal_index = found->second;
        if (consumer_counts[journal_index] >= 1U &&
            std::holds_alternative<MasterRodJournalAttachment>(
                engine.journals[journal_index].attachment)) {
            add(DiagnosticCode::inconsistent_value,
                pointer_member(pointer_index("/engine/cylinders", index), "journal"),
                "a master_rod journal cannot be shared by multiple cylinders",
                subject("cylinder", engine.cylinders[index].id.value));
        }
        ++consumer_counts[journal_index];
    }

    // Attachment edges form a functional graph. Classify real cycles before the
    // stricter one-level check so acyclic nesting receives a distinct diagnostic.
    std::vector<std::optional<std::size_t>> master_targets(engine.journals.size());
    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto *master =
            std::get_if<MasterRodJournalAttachment>(&engine.journals[index].attachment);
        if (master == nullptr) {
            continue;
        }
        const auto master_cylinder = cylinders.find(master->master_cylinder.value);
        if (master_cylinder == cylinders.end()) {
            continue;
        }
        const auto master_journal =
            journals.find(engine.cylinders[master_cylinder->second].journal.value);
        if (master_journal != journals.end() &&
            std::holds_alternative<MasterRodJournalAttachment>(
                engine.journals[master_journal->second].attachment)) {
            master_targets[index] = master_journal->second;
        }
    }
    std::vector<std::uint8_t> graph_state(engine.journals.size(), 0U);
    std::vector<bool> cycle_members(engine.journals.size(), false);
    for (std::size_t start = 0; start < engine.journals.size(); ++start) {
        if (graph_state[start] != 0U || !master_targets[start].has_value()) {
            continue;
        }
        std::vector<std::size_t> path;
        auto cursor = start;
        while (graph_state[cursor] == 0U && master_targets[cursor].has_value()) {
            graph_state[cursor] = 1U;
            path.push_back(cursor);
            cursor = *master_targets[cursor];
        }
        if (graph_state[cursor] == 1U) {
            const auto cycle_begin = std::ranges::find(path, cursor);
            if (cycle_begin != path.end()) {
                const auto canonical = *std::min_element(cycle_begin, path.end());
                for (auto member = cycle_begin; member != path.end(); ++member) {
                    cycle_members[*member] = true;
                }
                const auto cycle_path = pointer_index("/engine/journals", canonical);
                add(DiagnosticCode::forbidden_cycle,
                    pointer_member(cycle_path, "master_cylinder"),
                    "master_rod attachment graph contains a cylinder-to-master "
                    "cycle",
                    subject("journal", engine.journals[canonical].id.value));
            }
        }
        for (const auto member : path) {
            graph_state[member] = 2U;
        }
    }

    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto &journal = engine.journals[index];
        const auto path = pointer_index("/engine/journals", index);
        const auto owner = subject("journal", journal.id.value);
        if (!std::isfinite(journal.phase.value)) {
            add(DiagnosticCode::invalid_value,
                pointer_member(pointer_member(path, "phase"), "value"),
                "journal phase must be finite", owner);
        }
        const auto consumer_count = consumer_counts[index];
        if (consumer_count == 0U) {
            add(DiagnosticCode::disconnected_object, path,
                "every declared journal must be referenced by a cylinder", owner);
        }

        if (const auto *direct =
                std::get_if<CrankshaftJournalAttachment>(&journal.attachment)) {
            if (direct->crankshaft.value.empty()) {
                add(DiagnosticCode::missing_value, pointer_member(path, "crankshaft"),
                    "direct journal requires a crankshaft reference", owner);
            } else if (!crankshafts.contains(direct->crankshaft.value)) {
                add(DiagnosticCode::dangling_reference,
                    pointer_member(path, "crankshaft"),
                    "crankshaft reference '" + direct->crankshaft.value +
                        "' does not resolve",
                    owner);
            }
            continue;
        }

        const auto &master = std::get<MasterRodJournalAttachment>(journal.attachment);
        if (!std::isfinite(master.throw_radius.value)) {
            add(DiagnosticCode::invalid_value,
                pointer_member(pointer_member(path, "throw_radius"), "value"),
                "master-rod throw radius must be finite", owner);
        } else if (master.throw_radius.value <= 0.0) {
            add(DiagnosticCode::out_of_range,
                pointer_member(pointer_member(path, "throw_radius"), "value"),
                "master-rod throw radius must be positive", owner);
        }
        if (consumer_count > 1U) {
            add(DiagnosticCode::inconsistent_value, path,
                "a master_rod journal must be referenced by exactly one cylinder",
                owner);
        }
        if (master.master_cylinder.value.empty()) {
            add(DiagnosticCode::missing_value, pointer_member(path, "master_cylinder"),
                "master_rod journal requires a master cylinder reference", owner);
            continue;
        }
        const auto master_cylinder = cylinders.find(master.master_cylinder.value);
        if (master_cylinder == cylinders.end()) {
            add(DiagnosticCode::dangling_reference,
                pointer_member(path, "master_cylinder"),
                "master cylinder reference '" + master.master_cylinder.value +
                    "' does not resolve",
                owner);
            continue;
        }
        const auto master_journal =
            journals.find(engine.cylinders[master_cylinder->second].journal.value);
        if (master_journal == journals.end()) {
            continue;
        }
        const auto &master_attachment =
            engine.journals[master_journal->second].attachment;
        if (cycle_members[index]) {
            continue;
        }
        if (!std::holds_alternative<CrankshaftJournalAttachment>(master_attachment)) {
            add(DiagnosticCode::inconsistent_value,
                pointer_member(path, "master_cylinder"),
                "master_cylinder must use a direct crankshaft journal; nested "
                "attachment is forbidden",
                owner);
            continue;
        }
        const auto &derived_crankshaft =
            std::get<CrankshaftJournalAttachment>(master_attachment).crankshaft;
        if (!derived_crankshaft.value.empty() &&
            !crankshafts.contains(derived_crankshaft.value)) {
            add(DiagnosticCode::dangling_reference,
                pointer_member(
                    pointer_index("/engine/journals", master_journal->second),
                    "crankshaft"),
                "master cylinder's direct crankshaft reference '" +
                    derived_crankshaft.value + "' does not resolve",
                owner);
        }
    }
    return report;
}

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
    for (auto &diagnostic :
         validate_engine_mechanism_graph(document.engine).diagnostics) {
        reader.add(diagnostic.code, diagnostic.json_pointer,
                   std::move(diagnostic.message), diagnostic.subject);
    }
}

} // namespace crankwave::authoring::detail
