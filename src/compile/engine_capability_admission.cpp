#include "compile/engine_resolver_internal.hpp"

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
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
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

template <class Range, class Projection>
[[nodiscard]] const typename Range::value_type *
find_by_text(const Range &range, std::string_view id, Projection projection) {
    const auto found = std::ranges::find_if(
        range, [&](const auto &value) { return projection(value) == id; });
    return found == range.end() ? nullptr : &*found;
}

template <class Id> [[nodiscard]] std::string_view text(const Id &id) noexcept {
    return id.value;
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

template <class Range, class Projection>
void require_canonical_ids(DiagnosticReport &report, const Range &range,
                           std::string_view base, Projection projection) {
    for (std::size_t index = 0; index < range.size(); ++index) {
        if (!contract::is_valid_semantic_id(projection(range[index]))) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index(base, index) + "/id",
                "the executable contract requires lowercase canonical semantic IDs");
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
        engine.layout != authoring::CylinderLayout::v_engine) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/layout",
            "the current executable topology admits inline and V engines only");
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
    require_count(engine.crankshafts.size(), 1U, "/engine/crankshafts",
                  "engine crankshaft collection");
    if (engine.layout == authoring::CylinderLayout::inline_engine) {
        require_count(engine.banks.size(), 1U, "/engine/banks",
                      "inline engine bank collection");
    } else if (engine.layout == authoring::CylinderLayout::v_engine) {
        require_count(engine.banks.size(), 2U, "/engine/banks",
                      "V-engine bank collection");
    }
    require_count(engine.intakes.size(), 1U, "/engine/intakes",
                  "engine intake collection");
    require_count(engine.heads.size(), 1U, "/engine/heads", "engine head collection");
    require_count(engine.valvetrains.size(), 1U, "/engine/valvetrains",
                  "engine valvetrain collection");
    require_count(engine.fuels.size(), 1U, "/engine/fuels", "engine fuel collection");
    require_count(engine.accessory_configurations.size(), 1U,
                  "/engine/accessory_configurations",
                  "accessory-configuration collection");
    if (engine.source_routes.empty() || engine.exhausts.empty() ||
        engine.source_routes.size() != engine.exhausts.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/source_routes",
            "the executable exhaust presentation requires one or more source routes "
            "with exactly one route per declared exhaust");
    }
    require_count(engine.ports.size(), 2U, "/engine/ports",
                  "shared-head intake/exhaust port collection");
    if (engine.cylinders.empty() ||
        engine.ignition.wires.size() != engine.cylinders.size() ||
        engine.ignition.firing_order.size() != engine.cylinders.size() ||
        document.presentation.cylinder_routes.size() != engine.cylinders.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/cylinders",
            "the admitted engine requires nonempty, equally sized cylinder, "
            "wire, firing-event, and cylinder-presentation collections");
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

    if (report.has_errors()) {
        return report;
    }

    ModelContext resolved{document};
    resolved.profile_id = engine.identity.id.value + "-low-order-operating-point-v1";
    resolved.calibration_id = engine.identity.id.value + "-presentation-v1";
    resolved.crankshaft = &engine.crankshafts.front();
    resolved.head = &engine.heads.front();
    resolved.valvetrain = &engine.valvetrains.front();
    resolved.intake = &engine.intakes.front();
    resolved.fuel = &engine.fuels.front();
    resolved.accessory_configuration = &engine.accessory_configurations.front();

    const auto index = [](auto &destination, const auto &source, auto projection) {
        for (const auto &value : source) {
            destination.emplace(projection(value), &value);
        }
    };
    index(resolved.curves, engine.curves,
          [](const auto &value) { return value.id.value; });
    index(resolved.banks, engine.banks,
          [](const auto &value) { return value.id.value; });
    index(resolved.journals, engine.journals,
          [](const auto &value) { return value.id.value; });
    index(resolved.rods, engine.connecting_rods,
          [](const auto &value) { return value.id.value; });
    index(resolved.pistons, engine.pistons,
          [](const auto &value) { return value.id.value; });
    index(resolved.authored_ports, engine.ports,
          [](const auto &value) { return value.id.value; });
    index(resolved.cam_lobes, engine.cam_lobes,
          [](const auto &value) { return value.id.value; });
    index(resolved.exhausts, engine.exhausts,
          [](const auto &value) { return value.id.value; });
    index(resolved.source_routes, engine.source_routes,
          [](const auto &value) { return value.id.value; });

    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        const auto &bank = engine.banks[index];
        const double bank_angle_rad = legacy_si_value(bank.angle);
        if (bank.head.value != resolved.head->id.value ||
            !std::isfinite(bank_angle_rad) ||
            (engine.layout == authoring::CylinderLayout::inline_engine &&
             !same_binary64(bank_angle_rad, 0.0))) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/banks", index),
                "every admitted bank must have a finite supported angle and "
                "reference the sole shared head; an inline bank must have exact "
                "zero angle");
        }
    }
    if (engine.layout == authoring::CylinderLayout::v_engine &&
        engine.banks.size() == 2U &&
        legacy_si_value(engine.banks[0].angle) ==
            legacy_si_value(engine.banks[1].angle)) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/banks",
            "an admitted V engine requires two distinct bank angles");
    }
    if (resolved.head->valvetrain.value != resolved.valvetrain->id.value) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/heads/0",
            "the sole shared head must reference the sole valvetrain");
    }

    if (const auto *standard =
            std::get_if<authoring::StandardValvetrain>(&resolved.valvetrain->kind)) {
        resolved.intake_camshaft = find_by_text(
            engine.camshafts, standard->intake_camshaft.value,
            [](const auto &value) -> const std::string & { return value.id.value; });
        resolved.exhaust_camshaft = find_by_text(
            engine.camshafts, standard->exhaust_camshaft.value,
            [](const auto &value) -> const std::string & { return value.id.value; });
        if (resolved.intake_camshaft == nullptr ||
            resolved.exhaust_camshaft == nullptr) {
            add(report, DiagnosticCode::dangling_reference, "/engine/valvetrains/0",
                "standard valvetrain camshaft references did not resolve");
        }
    } else if (const auto *vtec =
                   std::get_if<authoring::VtecValvetrain>(&resolved.valvetrain->kind)) {
        const auto find_camshaft = [&](const authoring::CamshaftRef &reference) {
            return find_by_text(engine.camshafts, reference.value,
                                [](const auto &value) -> const std::string & {
                                    return value.id.value;
                                });
        };
        resolved.intake_camshaft = find_camshaft(vtec->base_intake_camshaft);
        resolved.exhaust_camshaft = find_camshaft(vtec->base_exhaust_camshaft);
        resolved.alternate_intake_camshaft =
            find_camshaft(vtec->alternate_intake_camshaft);
        resolved.alternate_exhaust_camshaft =
            find_camshaft(vtec->alternate_exhaust_camshaft);
        if (resolved.intake_camshaft == nullptr ||
            resolved.exhaust_camshaft == nullptr ||
            resolved.alternate_intake_camshaft == nullptr ||
            resolved.alternate_exhaust_camshaft == nullptr) {
            add(report, DiagnosticCode::dangling_reference, "/engine/valvetrains/0",
                "VTEC valvetrain camshaft references did not resolve");
        }
    } else {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/valvetrains/0/type",
            "the executable valvetrain type did not resolve");
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
