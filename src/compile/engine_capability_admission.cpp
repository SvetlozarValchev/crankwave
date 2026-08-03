#include "compile/engine_resolver_internal.hpp"

#include "authoring/parse_engine_references.hpp"
#include "compile/diagnostics.hpp"
#include "compile/stable_id.hpp"
#include "presentation/pcm16_ir_decoder.hpp"
#include "simulation/legacy_flow_calibration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

using authoring::DiagnosticCode;
using authoring::DiagnosticReport;

void add(DiagnosticReport &report, DiagnosticCode code, std::string path,
         std::string message) {
    authoring::Diagnostic diagnostic_value;
    diagnostic_value.code = code;
    diagnostic_value.json_pointer = std::move(path);
    diagnostic_value.message = std::move(message);
    report.diagnostics.push_back(std::move(diagnostic_value));
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool antipodal_bank_axes(double left, double right) noexcept {
    constexpr double kLegacyPi = 3.14159265359;
    const double separation = std::abs(std::remainder(left - right, 2.0 * kLegacyPi));
    return std::isfinite(separation) && std::abs(separation - kLegacyPi) <= 1.0e-12;
}

template <class Range, class Projection>
void require_canonical_ids(DiagnosticReport &report, const Range &range,
                           std::string_view base, Projection projection) {
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < range.size(); ++index) {
        const auto &id = projection(range[index]);
        if (!contract::is_valid_semantic_id(id)) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index(base, index) + "/id",
                "the executable contract requires lowercase canonical semantic IDs");
        }
        if (!ids.insert(id).second) {
            add(report, DiagnosticCode::duplicate_id,
                pointer_index(base, index) + "/id",
                "semantic IDs must be unique within their authored collection");
        }
    }
}

} // namespace

DiagnosticReport admit_engine_document(const authoring::EnginePackageDocument &document,
                                       std::span<const AssetPayloadView> assets,
                                       std::optional<ModelContext> &context) {
    DiagnosticReport report;
    const auto &engine = document.engine;

    if (document.schema != "engine-sim-offline/engine") {
        add(report, DiagnosticCode::unsupported_schema, "/schema",
            "engine resolver accepts the current engine schema only");
    }
    if (engine.cycle != authoring::EngineCycle::four_stroke) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/cycle",
            "legacy_low_order_v1 admits four-stroke engines only");
    }
    if (engine.layout != authoring::CylinderLayout::inline_engine &&
        engine.layout != authoring::CylinderLayout::v_engine &&
        engine.layout != authoring::CylinderLayout::opposed &&
        engine.layout != authoring::CylinderLayout::custom) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/layout",
            "the executable direct-rod topology requires a known bank layout");
    }
    if (!contract::is_valid_semantic_id(engine.identity.id.value)) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/identity/id",
            "the executable contract requires a lowercase canonical engine ID");
    }

    const auto require_count = [&](std::size_t actual, std::size_t expected,
                                   std::string_view path,
                                   std::string_view description) {
        if (actual != expected) {
            add(report, DiagnosticCode::unsupported_capability, std::string{path},
                std::string{description} + " requires exactly " +
                    std::to_string(expected) + " item(s) in the current topology");
        }
    };
    if (engine.crankshafts.empty()) {
        add(report, DiagnosticCode::missing_value, "/engine/crankshafts",
            "the executable engine requires at least one crankshaft");
    }
    if (engine.output_crankshaft.value.empty()) {
        add(report, DiagnosticCode::missing_value, "/engine/output_crankshaft",
            "the executable engine requires an explicit output crankshaft reference");
    } else {
        const auto output = std::find_if(
            engine.crankshafts.begin(), engine.crankshafts.end(),
            [&](const auto &crankshaft) {
                return crankshaft.id.value == engine.output_crankshaft.value;
            });
        if (output == engine.crankshafts.end()) {
            add(report, DiagnosticCode::dangling_reference, "/engine/output_crankshaft",
                "output crankshaft reference '" + engine.output_crankshaft.value +
                    "' does not resolve");
        } else {
            const double output_tdc_reference_rad =
                legacy_si_value(output->tdc_reference_angle);
            for (std::size_t index = 0; index < engine.crankshafts.size(); ++index) {
                const double tdc_reference_rad =
                    legacy_si_value(engine.crankshafts[index].tdc_reference_angle);
                if (!std::isfinite(output_tdc_reference_rad) ||
                    !std::isfinite(tdc_reference_rad) ||
                    !same_binary64(tdc_reference_rad, output_tdc_reference_rad)) {
                    add(report, DiagnosticCode::unsupported_capability,
                        pointer_index("/engine/crankshafts", index) +
                            "/tdc_reference_angle",
                        "multiple-crankshaft execution requires every "
                        "currently representable crankshaft to share the output "
                        "crankshaft's exact finite TDC reference for a co-phased 1:1 "
                        "rigid group");
                }
            }
        }
    }
    if (engine.layout == authoring::CylinderLayout::inline_engine) {
        require_count(engine.banks.size(), 1U, "/engine/banks",
                      "inline engine bank collection");
    } else if (engine.layout == authoring::CylinderLayout::v_engine) {
        require_count(engine.banks.size(), 2U, "/engine/banks",
                      "V-engine bank collection");
    } else if (engine.layout == authoring::CylinderLayout::opposed) {
        require_count(engine.banks.size(), 2U, "/engine/banks",
                      "opposed engine bank collection");
    }
    if (engine.intakes.empty()) {
        add(report, DiagnosticCode::missing_value, "/engine/intakes",
            "engine intake collection requires at least one item");
    }
    if (engine.heads.empty() || engine.heads.size() > engine.banks.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/heads",
            "the current topology requires one or more bank-referenced heads, no "
            "more than the declared bank count");
    }
    if (engine.valvetrains.empty() || engine.valvetrains.size() > engine.heads.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/valvetrains",
            "the current topology requires one or more head-referenced "
            "valvetrains, no more than the declared head count");
    }
    require_count(engine.fuels.size(), 1U, "/engine/fuels", "engine fuel collection");
    require_count(engine.accessory_configurations.size(), 1U,
                  "/engine/accessory_configurations",
                  "accessory-configuration collection");
    const auto exhaust_source_route_count = static_cast<std::size_t>(
        std::ranges::count_if(engine.source_routes, [](const auto &route) {
            return std::holds_alternative<authoring::ExhaustRouteSource>(route.source);
        }));
    const auto intake_source_route_count = static_cast<std::size_t>(
        std::ranges::count_if(engine.source_routes, [](const auto &route) {
            return std::holds_alternative<authoring::IntakeRouteSource>(route.source);
        }));
    if (engine.source_routes.empty() || engine.exhausts.empty() ||
        exhaust_source_route_count != engine.exhausts.size() ||
        intake_source_route_count > engine.intakes.size() ||
        exhaust_source_route_count + intake_source_route_count !=
            engine.source_routes.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/source_routes",
            "the executable presentation requires exactly one source route per "
            "declared exhaust and at most one source route per declared intake");
    }
    if (engine.ports.size() != engine.heads.size() * 2U) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/ports",
            "each admitted head requires exactly one intake and one exhaust port");
    }
    if (engine.cylinders.empty() || engine.ignition.wires.empty() ||
        engine.ignition.firing_order.empty() ||
        document.presentation.cylinder_routes.size() != engine.cylinders.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/cylinders",
            "the admitted engine requires nonempty cylinder, ignition-wire, and "
            "firing-event collections plus one presentation route per cylinder");
    }
    if (document.presentation.routes.size() != engine.source_routes.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/presentation/routes",
            "presentation routes must exactly cover the declared engine source "
            "routes");
    }

    require_canonical_ids(
        report, engine.curves, "/engine/curves",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.crankshafts, "/engine/crankshafts",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.journals, "/engine/journals",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.connecting_rods, "/engine/connecting_rods",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.pistons, "/engine/pistons",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.banks, "/engine/banks",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.intakes, "/engine/intakes",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.exhausts, "/engine/exhausts",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.ports, "/engine/ports",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.cam_lobes, "/engine/cam_lobes",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.camshafts, "/engine/camshafts",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.valvetrains, "/engine/valvetrains",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.heads, "/engine/heads",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.fuels, "/engine/fuels",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.cylinders, "/engine/cylinders",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, engine.source_routes, "/engine/source_routes",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, document.presentation.assets, "/presentation/assets",
        [](const auto &value) -> const std::string & { return value.id.value; });
    require_canonical_ids(
        report, document.presentation.buses, "/presentation/buses",
        [](const auto &value) -> const std::string & { return value.id.value; });
    for (std::size_t index = 0; index < engine.ports.size(); ++index) {
        const auto kind = engine.ports[index].kind;
        if (kind != authoring::PortKind::intake &&
            kind != authoring::PortKind::exhaust) {
            add(report, DiagnosticCode::invalid_value,
                pointer_index("/engine/ports", index) + "/kind",
                "port kind must be intake or exhaust");
        }
    }

    auto mechanism_graph = authoring::detail::validate_engine_mechanism_graph(engine);
    for (auto &diagnostic : mechanism_graph.diagnostics) {
        report.diagnostics.push_back(std::move(diagnostic));
    }

    if (report.has_errors()) {
        return report;
    }

    ModelContext resolved{document};
    resolved.profile_id = engine.identity.id.value + "-low-order-operating-point-v1";
    resolved.calibration_id = engine.identity.id.value + "-presentation-v1";
    resolved.fuel = &engine.fuels.front();
    resolved.accessory_configuration = &engine.accessory_configurations.front();

    const auto index = [](auto &destination, const auto &source, auto projection) {
        for (const auto &value : source) {
            destination.emplace(projection(value), &value);
        }
    };
    index(resolved.curves, engine.curves,
          [](const auto &value) { return value.id.value; });
    index(resolved.crankshafts, engine.crankshafts,
          [](const auto &value) { return value.id.value; });
    index(resolved.banks, engine.banks,
          [](const auto &value) { return value.id.value; });
    index(resolved.intakes, engine.intakes,
          [](const auto &value) { return value.id.value; });
    index(resolved.journals, engine.journals,
          [](const auto &value) { return value.id.value; });
    index(resolved.rods, engine.connecting_rods,
          [](const auto &value) { return value.id.value; });
    index(resolved.pistons, engine.pistons,
          [](const auto &value) { return value.id.value; });
    index(resolved.heads, engine.heads,
          [](const auto &value) { return value.id.value; });
    index(resolved.authored_ports, engine.ports,
          [](const auto &value) { return value.id.value; });
    index(resolved.cam_lobes, engine.cam_lobes,
          [](const auto &value) { return value.id.value; });
    index(resolved.camshafts, engine.camshafts,
          [](const auto &value) { return value.id.value; });
    index(resolved.valvetrains, engine.valvetrains,
          [](const auto &value) { return value.id.value; });
    index(resolved.exhausts, engine.exhausts,
          [](const auto &value) { return value.id.value; });
    index(resolved.source_routes, engine.source_routes,
          [](const auto &value) { return value.id.value; });

    resolved.output_crankshaft =
        resolved.crankshafts.at(engine.output_crankshaft.value);

    std::unordered_map<std::string, const authoring::CylinderDefinition *>
        cylinder_definitions;
    index(cylinder_definitions, engine.cylinders,
          [](const auto &value) { return value.id.value; });
    for (const auto &cylinder : engine.cylinders) {
        const auto &journal = *resolved.journals.at(cylinder.journal.value);
        const authoring::CrankshaftJournalAttachment *direct =
            std::get_if<authoring::CrankshaftJournalAttachment>(&journal.attachment);
        if (direct == nullptr) {
            const auto &master =
                std::get<authoring::MasterRodJournalAttachment>(journal.attachment);
            const auto &master_cylinder =
                *cylinder_definitions.at(master.master_cylinder.value);
            const auto &master_journal =
                *resolved.journals.at(master_cylinder.journal.value);
            direct = std::get_if<authoring::CrankshaftJournalAttachment>(
                &master_journal.attachment);
        }
        resolved.crankshaft_for_cylinder.emplace(
            cylinder.id.value, resolved.crankshafts.at(direct->crankshaft.value));
    }

    std::unordered_set<std::string> used_heads;
    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        const auto &bank = engine.banks[index];
        const double bank_angle_rad = legacy_si_value(bank.angle);
        if (!resolved.heads.contains(bank.head.value)) {
            add(report, DiagnosticCode::dangling_reference,
                pointer_index("/engine/banks", index) + "/head",
                "bank head reference did not resolve");
        } else {
            used_heads.insert(bank.head.value);
        }
        if (!std::isfinite(bank_angle_rad) ||
            (engine.layout == authoring::CylinderLayout::inline_engine &&
             !same_binary64(bank_angle_rad, 0.0))) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/banks", index) + "/angle",
                "every admitted bank requires a finite supported angle; an inline "
                "bank requires exact zero angle");
        }
    }
    if (used_heads.size() != engine.heads.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/heads",
            "every declared head must be referenced by at least one bank");
    }
    if (engine.layout == authoring::CylinderLayout::v_engine &&
        engine.banks.size() == 2U &&
        legacy_si_value(engine.banks[0].angle) ==
            legacy_si_value(engine.banks[1].angle)) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/banks",
            "an admitted V engine requires two distinct bank angles");
    }
    if (engine.layout == authoring::CylinderLayout::opposed &&
        engine.banks.size() == 2U &&
        !antipodal_bank_axes(legacy_si_value(engine.banks[0].angle),
                             legacy_si_value(engine.banks[1].angle))) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/banks",
            "an opposed engine requires two antipodal bank axes");
    }
    std::unordered_set<std::string> used_valvetrains;
    for (std::size_t index = 0; index < engine.heads.size(); ++index) {
        const auto &head = engine.heads[index];
        if (!resolved.valvetrains.contains(head.valvetrain.value)) {
            add(report, DiagnosticCode::dangling_reference,
                pointer_index("/engine/heads", index) + "/valvetrain",
                "head valvetrain reference did not resolve");
        } else {
            used_valvetrains.insert(head.valvetrain.value);
        }
    }
    if (used_valvetrains.size() != engine.valvetrains.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/valvetrains",
            "every declared valvetrain must be referenced by at least one head");
    }
    if (report.has_errors()) {
        return report;
    }

    struct ValvetrainCams {
        const authoring::CamshaftDefinition *intake = nullptr;
        const authoring::CamshaftDefinition *exhaust = nullptr;
        const authoring::CamshaftDefinition *alternate_intake = nullptr;
        const authoring::CamshaftDefinition *alternate_exhaust = nullptr;
        const authoring::VtecValvetrain *vtec = nullptr;
    };
    std::unordered_map<std::string, ValvetrainCams> valvetrain_cams;
    const auto find_camshaft = [&](const authoring::CamshaftRef &reference,
                                   const std::string &path) {
        const auto found = resolved.camshafts.find(reference.value);
        if (found == resolved.camshafts.end()) {
            add(report, DiagnosticCode::dangling_reference, path,
                "valvetrain camshaft reference did not resolve");
            return static_cast<const authoring::CamshaftDefinition *>(nullptr);
        }
        return found->second;
    };
    for (std::size_t index = 0; index < engine.valvetrains.size(); ++index) {
        const auto &valvetrain = engine.valvetrains[index];
        const auto path = pointer_index("/engine/valvetrains", index);
        ValvetrainCams cams;
        if (const auto *standard =
                std::get_if<authoring::StandardValvetrain>(&valvetrain.kind)) {
            cams.intake =
                find_camshaft(standard->intake_camshaft, path + "/intake_camshaft");
            cams.exhaust =
                find_camshaft(standard->exhaust_camshaft, path + "/exhaust_camshaft");
        } else if (const auto *vtec =
                       std::get_if<authoring::VtecValvetrain>(&valvetrain.kind)) {
            cams.vtec = vtec;
            cams.intake = find_camshaft(vtec->base_intake_camshaft,
                                        path + "/base_intake_camshaft");
            cams.exhaust = find_camshaft(vtec->base_exhaust_camshaft,
                                         path + "/base_exhaust_camshaft");
            cams.alternate_intake = find_camshaft(vtec->alternate_intake_camshaft,
                                                  path + "/alternate_intake_camshaft");
            cams.alternate_exhaust = find_camshaft(
                vtec->alternate_exhaust_camshaft, path + "/alternate_exhaust_camshaft");
        }
        valvetrain_cams.emplace(valvetrain.id.value, cams);
    }
    if (report.has_errors()) {
        return report;
    }

    const bool has_vtec = std::ranges::any_of(
        valvetrain_cams, [](const auto &item) { return item.second.vtec != nullptr; });
    for (const auto &bank : engine.banks) {
        const auto &head = *resolved.heads.at(bank.head.value);
        const auto &cams = valvetrain_cams.at(head.valvetrain.value);
        if (cams.vtec != nullptr) {
            resolved.vtec_valvetrain_for_bank.emplace(bank.id.value, cams.vtec);
        }
    }

    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        const auto bank = resolved.banks.find(cylinder.bank.value);
        if (bank == resolved.banks.end()) {
            add(report, DiagnosticCode::dangling_reference,
                pointer_index("/engine/cylinders", index) + "/bank",
                "cylinder bank reference did not resolve");
            continue;
        }
        const auto &head = *resolved.heads.at(bank->second->head.value);
        const auto &cams = valvetrain_cams.at(head.valvetrain.value);
        resolved.intake_camshaft_for_cylinder.emplace(cylinder.id.value, cams.intake);
        resolved.exhaust_camshaft_for_cylinder.emplace(cylinder.id.value, cams.exhaust);
        if (has_vtec) {
            resolved.alternate_intake_camshaft_for_cylinder.emplace(
                cylinder.id.value,
                cams.alternate_intake != nullptr ? cams.alternate_intake : cams.intake);
            resolved.alternate_exhaust_camshaft_for_cylinder.emplace(
                cylinder.id.value, cams.alternate_exhaust != nullptr
                                       ? cams.alternate_exhaust
                                       : cams.exhaust);
        }
    }
    if (report.has_errors()) {
        return report;
    }

    admit_engine_physical_model(resolved, report);

    admit_engine_operating_systems(resolved, report);

    admit_engine_rig(resolved, report);

    admit_engine_presentation(document, resolved, report);

    verify_engine_assets(document, assets, report, resolved.assets);

    if (report.has_errors()) {
        return report;
    }

    assign_engine_runtime_ids(resolved, report);
    if (report.has_errors()) {
        return report;
    }

    context.emplace(std::move(resolved));
    return report;
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
