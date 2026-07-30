#include "compile/engine_resolver_internal.hpp"

#include "simulation/legacy_flow_calibration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

constexpr double kLegacyIntakePlateMultiplier = 0.994;

void add(authoring::DiagnosticReport &report, authoring::DiagnosticCode code,
         std::string path, std::string message) {
    authoring::Diagnostic value;
    value.code = code;
    value.json_pointer = std::move(path);
    value.message = std::move(message);
    report.diagnostics.push_back(std::move(value));
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool
supported_flow_bench(const authoring::FlowRestriction &restriction,
                     contract::LegacyRestrictionCalibration expected_calibration =
                         contract::LegacyRestrictionCalibration::unspecified) {
    const auto *flow = std::get_if<authoring::FlowBenchRestriction>(&restriction);
    if (flow == nullptr || flow->rated_flow.unit != "cfm" ||
        !flow->rated_flow.standard.has_value()) {
        return false;
    }
    const auto calibration =
        *flow->rated_flow.standard == "carburetor_1p5_inhg"
            ? contract::LegacyRestrictionCalibration::carb_at_1p5_inhg
        : *flow->rated_flow.standard == "port_28_inh2o"
            ? contract::LegacyRestrictionCalibration::cfm_at_28_inh2o
            : contract::LegacyRestrictionCalibration::unspecified;
    if (calibration == contract::LegacyRestrictionCalibration::unspecified ||
        (expected_calibration != contract::LegacyRestrictionCalibration::unspecified &&
         calibration != expected_calibration)) {
        return false;
    }
    const double pressure_drop_pa = legacy_si_value(flow->pressure_drop);
    const double expected_drop =
        simulation::legacy_flow_calibration_pressure_drop_pa(calibration);
    return std::isfinite(flow->rated_flow.value) && flow->rated_flow.value >= 0.0 &&
           same_binary64(pressure_drop_pa, expected_drop);
}

} // namespace

void admit_engine_physical_model(ModelContext &resolved,
                                 authoring::DiagnosticReport &report) {
    const auto &engine = resolved.document.engine;
    using authoring::DiagnosticCode;

    if (!resolved.crankshaft->friction_torque.has_value()) {
        add(report, DiagnosticCode::missing_value,
            "/engine/crankshafts/0/friction_torque",
            "the positive-speed crank friction magnitude must be explicit");
    }
    std::unordered_set<std::string> declared_journals;
    for (const auto &journal : engine.journals) {
        declared_journals.insert(journal.id.value);
    }
    std::unordered_set<std::string> crank_journals;
    for (const auto &reference : resolved.crankshaft->journals) {
        crank_journals.insert(reference.value);
    }
    if (crank_journals.size() != resolved.crankshaft->journals.size() ||
        crank_journals != declared_journals) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/crankshafts/0/journals",
            "the sole crankshaft journal list must cover every declared direct "
            "journal exactly once");
    }
    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto &journal = engine.journals[index];
        if (journal.crankshaft.value != resolved.crankshaft->id.value ||
            journal.master_journal.has_value() || journal.slave_throw.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/journals", index),
                "the current centered inline crank admits direct journals only");
        }
    }
    for (std::size_t index = 0; index < engine.connecting_rods.size(); ++index) {
        const auto &rod = engine.connecting_rods[index];
        if (rod.center_of_mass_from_crank_pin.has_value() ||
            rod.slave_throw.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/connecting_rods", index),
                "rod center-of-mass and slave-throw execution are not admitted");
        }
    }
    for (std::size_t index = 0; index < engine.pistons.size(); ++index) {
        const auto &piston = engine.pistons[index];
        if (piston.wrist_pin_position.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/pistons", index) + "/wrist_pin_position",
                "wrist-pin position is not executed by the centered low-order core");
        }
        if (!piston.blowby.has_value() || !supported_flow_bench(*piston.blowby)) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/pistons", index) + "/blowby",
                "the current gas path requires a supported calibrated CFM blowby "
                "restriction");
        }
    }
    if (!engine.pistons.empty()) {
        const auto &first = engine.pistons.front().blowby;
        if (!std::ranges::all_of(engine.pistons, [&](const auto &piston) {
                return piston.blowby == first;
            })) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/pistons",
                "the current shared gas path requires one identical blowby "
                "restriction across all referenced pistons");
        }
    }

    const auto require_carb = [&](const authoring::FlowRestriction &restriction,
                                  std::string_view path) {
        if (!supported_flow_bench(
                restriction,
                contract::LegacyRestrictionCalibration::carb_at_1p5_inhg)) {
            add(report, DiagnosticCode::unsupported_capability, std::string{path},
                "the current gas path requires a 1.5-inHg calibrated CFM "
                "restriction");
        }
    };
    if (!same_binary64(resolved.intake->idle_throttle_position_01,
                       kLegacyIntakePlateMultiplier)) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/intakes/0/idle_throttle_position_01",
            "legacy_low_order_v1 currently admits the exact 0.994 idle plate "
            "position only");
    }
    require_carb(resolved.intake->main_restriction,
                 "/engine/intakes/0/main_restriction");
    require_carb(resolved.intake->idle_bypass_restriction,
                 "/engine/intakes/0/idle_bypass_restriction");
    require_carb(resolved.intake->runner_restriction,
                 "/engine/intakes/0/runner_restriction");

    for (std::size_t index = 0; index < engine.exhausts.size(); ++index) {
        const auto &exhaust = engine.exhausts[index];
        if (exhaust.collector_length.has_value() ==
            exhaust.collector_volume.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/exhausts", index),
                "exactly one of collector_length or collector_volume must author "
                "the collector geometry");
        }
        require_carb(exhaust.outlet_restriction,
                     pointer_index("/engine/exhausts", index) + "/outlet_restriction");
        require_carb(exhaust.primary_restriction,
                     pointer_index("/engine/exhausts", index) + "/primary_restriction");
    }

    const authoring::PortDefinition *intake_port = nullptr;
    const authoring::PortDefinition *exhaust_port = nullptr;
    for (const auto &port : engine.ports) {
        if (port.head.value != resolved.head->id.value) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/ports",
                "every admitted port must belong to the sole shared head");
        }
        auto *&slot =
            port.kind == authoring::PortKind::intake ? intake_port : exhaust_port;
        if (slot != nullptr) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/ports",
                "the shared head requires exactly one authored port per kind");
        }
        slot = &port;
    }
    if (intake_port == nullptr || exhaust_port == nullptr) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/ports",
            "the shared head requires one intake and one exhaust port");
    } else {
        std::unordered_set<std::string> head_ports;
        for (const auto &port : resolved.head->ports) {
            head_ports.insert(port.value);
        }
        if (head_ports != std::unordered_set<std::string>{intake_port->id.value,
                                                          exhaust_port->id.value}) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/heads/0/ports",
                "the shared head port list must exactly cover its intake and "
                "exhaust ports");
        }
        for (std::size_t index = 0; index < engine.exhausts.size(); ++index) {
            if (!same_binary64(
                    legacy_si_value(engine.exhausts[index].primary_cross_section_area),
                    legacy_si_value(exhaust_port->runner_cross_section_area))) {
                add(report, DiagnosticCode::unsupported_capability,
                    pointer_index("/engine/exhausts", index) +
                        "/primary_cross_section_area",
                    "the shared-head runtime requires exhaust primary area to "
                    "match the exhaust-port runner area");
            }
        }
        const auto intake_curve = resolved.curves.find(intake_port->flow_curve.value);
        const auto exhaust_curve = resolved.curves.find(exhaust_port->flow_curve.value);
        if (intake_curve != resolved.curves.end() &&
            exhaust_curve != resolved.curves.end() &&
            (!intake_curve->second->triangle_filter_radius.has_value() ||
             !exhaust_curve->second->triangle_filter_radius.has_value() ||
             !same_binary64(
                 legacy_si_value(*intake_curve->second->triangle_filter_radius),
                 legacy_si_value(*exhaust_curve->second->triangle_filter_radius)))) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/ports",
                "the current shared-head runtime requires bit-identical intake "
                "and exhaust flow-table triangle radii");
        }
    }

    std::unordered_set<std::string> used_journals;
    std::unordered_set<std::string> used_rods;
    std::unordered_set<std::string> used_pistons;
    std::unordered_set<std::string> used_exhausts;
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        const auto path = pointer_index("/engine/cylinders", index);
        if (cylinder.bank.value != resolved.bank->id.value ||
            cylinder.crankshaft.value != resolved.crankshaft->id.value ||
            cylinder.intake.value != resolved.intake->id.value ||
            (intake_port != nullptr &&
             cylinder.intake_port.value != intake_port->id.value) ||
            (exhaust_port != nullptr &&
             cylinder.exhaust_port.value != exhaust_port->id.value) ||
            cylinder.slave_journal.has_value()) {
            add(report, DiagnosticCode::unsupported_capability, path,
                "every admitted cylinder must use the shared inline bank, crank, "
                "intake and head ports without a slave journal");
        }
        used_journals.insert(cylinder.journal.value);
        used_rods.insert(cylinder.connecting_rod.value);
        used_pistons.insert(cylinder.piston.value);
        used_exhausts.insert(cylinder.exhaust.value);
        if (!resolved.journals.contains(cylinder.journal.value) ||
            !resolved.rods.contains(cylinder.connecting_rod.value) ||
            !resolved.pistons.contains(cylinder.piston.value) ||
            !resolved.exhausts.contains(cylinder.exhaust.value)) {
            add(report, DiagnosticCode::dangling_reference, path,
                "cylinder mechanism or exhaust reference did not resolve");
        }
        const auto [wire, inserted] = resolved.cylinder_for_wire.emplace(
            cylinder.ignition_wire.value, cylinder.id.value);
        if (!inserted) {
            add(report, DiagnosticCode::unsupported_capability, path + "/ignition_wire",
                "each admitted cylinder requires a distinct ignition wire");
        }
    }
    if (used_journals.size() != engine.journals.size() ||
        used_rods.size() != engine.connecting_rods.size() ||
        used_pistons.size() != engine.pistons.size() ||
        used_exhausts.size() != engine.exhausts.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/cylinders",
            "all declared journals, rods, pistons, and exhausts must be reachable "
            "from the admitted cylinder order");
    }

    for (std::size_t index = 0; index < engine.source_routes.size(); ++index) {
        const auto &route = engine.source_routes[index];
        const auto *source = std::get_if<authoring::ExhaustRouteSource>(&route.source);
        if (source == nullptr) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/source_routes", index) + "/type",
                "the current presentation admits exhaust source routes only");
            continue;
        }
        if (!resolved.route_for_exhaust.emplace(source->exhaust.value, route.id.value)
                 .second) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/source_routes", index),
                "each exhaust must own exactly one source route");
        }
    }
    if (resolved.route_for_exhaust.size() != engine.exhausts.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/source_routes",
            "source routes must exactly cover all cylinder-referenced exhausts");
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
