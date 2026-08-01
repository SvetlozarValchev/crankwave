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
    for (std::size_t index = 0; index < engine.journals.size(); ++index) {
        const auto &journal = engine.journals[index];
        const auto *attachment =
            std::get_if<authoring::CrankshaftJournalAttachment>(&journal.attachment);
        if (attachment != nullptr &&
            attachment->crankshaft.value != resolved.crankshaft->id.value) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/journals", index),
                "each direct journal must attach to the sole admitted crankshaft");
        }
    }
    for (std::size_t index = 0; index < engine.connecting_rods.size(); ++index) {
        const auto &rod = engine.connecting_rods[index];
        if (rod.center_of_mass_from_crank_pin.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/connecting_rods", index),
                "rod center-of-mass execution is not admitted");
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
    for (std::size_t index = 0; index < engine.intakes.size(); ++index) {
        const auto &intake = engine.intakes[index];
        const auto path = pointer_index("/engine/intakes", index);
        if (!std::isfinite(intake.idle_throttle_position_01) ||
            intake.idle_throttle_position_01 < 0.0 ||
            intake.idle_throttle_position_01 > 1.0) {
            add(report, DiagnosticCode::unsupported_capability,
                path + "/idle_throttle_position_01",
                "legacy_low_order_v1 requires a finite idle throttle plate "
                "position in [0,1]");
        }
        require_carb(intake.main_restriction, path + "/main_restriction");
        require_carb(intake.idle_bypass_restriction, path + "/idle_bypass_restriction");
        require_carb(intake.runner_restriction, path + "/runner_restriction");
    }

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

    for (std::size_t index = 0; index < engine.ports.size(); ++index) {
        const auto &port = engine.ports[index];
        const auto path = pointer_index("/engine/ports", index);
        if (!resolved.heads.contains(port.head.value)) {
            add(report, DiagnosticCode::dangling_reference, path + "/head",
                "port head reference did not resolve");
            continue;
        }
        auto &ports = port.kind == authoring::PortKind::intake
                          ? resolved.intake_port_for_head
                          : resolved.exhaust_port_for_head;
        if (!ports.emplace(port.head.value, &port).second) {
            add(report, DiagnosticCode::unsupported_capability, path,
                "each admitted head requires exactly one authored port per kind");
        }
    }

    for (std::size_t index = 0; index < engine.heads.size(); ++index) {
        const auto &head = engine.heads[index];
        const auto path = pointer_index("/engine/heads", index);
        const auto intake = resolved.intake_port_for_head.find(head.id.value);
        const auto exhaust = resolved.exhaust_port_for_head.find(head.id.value);
        if (intake == resolved.intake_port_for_head.end() ||
            exhaust == resolved.exhaust_port_for_head.end()) {
            add(report, DiagnosticCode::unsupported_capability, path + "/ports",
                "each admitted head requires one intake and one exhaust port");
            continue;
        }
        std::unordered_set<std::string> head_ports;
        for (const auto &port : head.ports) {
            head_ports.insert(port.value);
        }
        const std::unordered_set<std::string> expected_ports{
            intake->second->id.value,
            exhaust->second->id.value,
        };
        if (head_ports.size() != head.ports.size() || head_ports != expected_ports) {
            add(report, DiagnosticCode::unsupported_capability, path + "/ports",
                "a head port list must exactly and reciprocally cover its intake "
                "and exhaust ports");
        }
    }

    const authoring::PortDefinition *intake_port = nullptr;
    const authoring::PortDefinition *exhaust_port = nullptr;
    const authoring::HeadDefinition *representative_head =
        engine.heads.empty() ? nullptr : &engine.heads.front();
    if (representative_head != nullptr) {
        const auto intake =
            resolved.intake_port_for_head.find(representative_head->id.value);
        const auto exhaust =
            resolved.exhaust_port_for_head.find(representative_head->id.value);
        if (intake != resolved.intake_port_for_head.end() &&
            exhaust != resolved.exhaust_port_for_head.end()) {
            intake_port = intake->second;
            exhaust_port = exhaust->second;
        }
    }
    if (representative_head == nullptr || intake_port == nullptr ||
        exhaust_port == nullptr) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/heads",
            "the admitted head topology requires a representative intake and "
            "exhaust port pair");
    }

    std::unordered_set<std::string> used_journals;
    std::unordered_set<std::string> used_rods;
    std::unordered_set<std::string> used_pistons;
    std::unordered_set<std::string> used_intakes;
    std::unordered_set<std::string> used_exhausts;
    std::unordered_set<std::string> used_banks;
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &cylinder = engine.cylinders[index];
        const auto path = pointer_index("/engine/cylinders", index);
        const authoring::PortDefinition *cylinder_intake_port = nullptr;
        const authoring::PortDefinition *cylinder_exhaust_port = nullptr;
        const auto bank = resolved.banks.find(cylinder.bank.value);
        if (bank != resolved.banks.end()) {
            const auto intake =
                resolved.intake_port_for_head.find(bank->second->head.value);
            const auto exhaust =
                resolved.exhaust_port_for_head.find(bank->second->head.value);
            if (intake != resolved.intake_port_for_head.end()) {
                cylinder_intake_port = intake->second;
            }
            if (exhaust != resolved.exhaust_port_for_head.end()) {
                cylinder_exhaust_port = exhaust->second;
            }
        }
        if (bank == resolved.banks.end() || cylinder_intake_port == nullptr ||
            cylinder_exhaust_port == nullptr ||
            !resolved.intakes.contains(cylinder.intake.value) ||
            cylinder.intake_port.value != cylinder_intake_port->id.value ||
            cylinder.exhaust_port.value != cylinder_exhaust_port->id.value) {
            add(report, DiagnosticCode::unsupported_capability, path,
                "every admitted cylinder must use a declared bank and intake, "
                "and its bank head's exact ports");
        }
        used_banks.insert(cylinder.bank.value);
        used_journals.insert(cylinder.journal.value);
        used_rods.insert(cylinder.connecting_rod.value);
        used_pistons.insert(cylinder.piston.value);
        used_intakes.insert(cylinder.intake.value);
        used_exhausts.insert(cylinder.exhaust.value);
        if (!resolved.journals.contains(cylinder.journal.value) ||
            !resolved.rods.contains(cylinder.connecting_rod.value) ||
            !resolved.pistons.contains(cylinder.piston.value) ||
            !resolved.exhausts.contains(cylinder.exhaust.value)) {
            add(report, DiagnosticCode::dangling_reference, path,
                "cylinder mechanism or exhaust reference did not resolve");
        }
        resolved.cylinders_for_wire[cylinder.ignition_wire.value].push_back(
            cylinder.id.value);
    }
    if (used_banks.size() != engine.banks.size() ||
        used_journals.size() != engine.journals.size() ||
        used_rods.size() != engine.connecting_rods.size() ||
        used_pistons.size() != engine.pistons.size() ||
        used_intakes.size() != engine.intakes.size() ||
        used_exhausts.size() != engine.exhausts.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/cylinders",
            "all declared banks, journals, rods, pistons, intakes, and exhausts must "
            "be "
            "reachable from the admitted cylinder order");
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
