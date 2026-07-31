#include "compile/engine_resolver_internal.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
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

[[nodiscard]] bool supported_curve_shape(const authoring::CurveDefinition &curve,
                                         authoring::QuantityDimension input,
                                         authoring::QuantityDimension output) noexcept {
    return curve.input_dimension == input && curve.output_dimension == output &&
           curve.evaluation == authoring::CurveEvaluation::triangle_weighted_samples &&
           curve.triangle_filter_radius.has_value() &&
           curve.below_domain == authoring::CurveBoundaryBehavior::clamp &&
           curve.above_domain == authoring::CurveBoundaryBehavior::clamp &&
           curve.samples.size() >= 2U;
}

void admit_harmonic_lobes(const authoring::CamshaftDefinition &camshaft,
                          const ModelContext &context,
                          authoring::PortKind expected_kind,
                          authoring::DiagnosticReport &report,
                          std::unordered_set<std::string> &used_lobes) {
    std::unordered_set<std::string> covered_cylinders;
    const auto &engine = context.document.engine;
    for (const auto &reference : camshaft.lobes) {
        const auto found = context.cam_lobes.find(reference.value);
        if (found == context.cam_lobes.end()) {
            continue;
        }
        const auto &lobe = *found->second;
        if (lobe.port_kind != expected_kind) {
            continue;
        }
        used_lobes.insert(lobe.id.value);
        covered_cylinders.insert(lobe.cylinder.value);
        if (!std::holds_alternative<authoring::HarmonicCamLobe>(lobe.shape)) {
            add(report, authoring::DiagnosticCode::unsupported_capability,
                "/engine/cam_lobes",
                "the current fixed valvetrain admits harmonic cam lobes only");
            continue;
        }
        const auto &shape = std::get<authoring::HarmonicCamLobe>(lobe.shape);
        const double reference_lift_m = legacy_si_value(shape.reference_lift);
        const double admitted_reference_lift_m =
            50.0 * (((1.0 / 100.0) * 2.54) / 1000.0);
        if (!std::isfinite(reference_lift_m) ||
            std::abs(reference_lift_m - admitted_reference_lift_m) > 1.0e-15) {
            add(report, authoring::DiagnosticCode::unsupported_capability,
                "/engine/cam_lobes",
                "legacy_low_order_v1 admits the exact 0.050-inch harmonic "
                "reference lift only");
        }
        if (!(shape.gamma > 0.0 && shape.sample_count >= 6U &&
              shape.sample_count <= 1'000'000U)) {
            add(report, authoring::DiagnosticCode::unsupported_capability,
                "/engine/cam_lobes",
                "harmonic gamma must be positive and sample_count must be in "
                "[6,1000000] for the current fixed valvetrain");
        }
    }
    if (covered_cylinders.size() != engine.cylinders.size()) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/engine/camshafts",
            "each admitted camshaft role requires exactly one lobe per cylinder");
    }
}

[[nodiscard]] bool
equivalent_harmonic_shapes(const authoring::CamshaftDefinition &camshaft,
                           const ModelContext &context,
                           authoring::PortKind expected_kind) {
    const authoring::HarmonicCamLobe *first = nullptr;
    for (const auto &reference : camshaft.lobes) {
        const auto found = context.cam_lobes.find(reference.value);
        if (found == context.cam_lobes.end() ||
            found->second->port_kind != expected_kind) {
            continue;
        }
        const auto *shape =
            std::get_if<authoring::HarmonicCamLobe>(&found->second->shape);
        if (shape == nullptr) {
            return false;
        }
        if (first == nullptr) {
            first = shape;
        } else if (*first != *shape) {
            return false;
        }
    }
    return first != nullptr;
}

} // namespace

void admit_engine_operating_systems(ModelContext &resolved,
                                    authoring::DiagnosticReport &report) {
    const auto &engine = resolved.document.engine;
    using authoring::DiagnosticCode;

    if (!engine.throttle_controllers.has_value() ||
        !engine.throttle_controller.has_value() ||
        engine.throttle_controllers->size() != 1U) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/throttle_controllers",
            "the current engine requires one explicitly selected direct throttle "
            "controller");
    } else {
        resolved.throttle_controller = &engine.throttle_controllers->front();
        const auto *direct = std::get_if<authoring::DirectThrottleController>(
            &resolved.throttle_controller->kind);
        if (engine.throttle_controller->value !=
                resolved.throttle_controller->id.value ||
            direct == nullptr || !std::isfinite(direct->gamma) ||
            direct->gamma <= 0.0) {
            add(report, DiagnosticCode::unsupported_capability,
                "/engine/throttle_controller",
                "legacy_low_order_v1 requires a selected direct controller with "
                "finite positive gamma");
        }
    }
    if (engine.default_fuel.value != resolved.fuel->id.value) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/default_fuel",
            "the sole admitted fuel must be selected as the engine default");
    }
    if (resolved.fuel->density.has_value()) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/fuels/0/density",
            "fuel density is parsed but not consumed by legacy_low_order_v1");
    }

    const auto *loss = std::get_if<authoring::ChenFlynnLossDefinition>(&engine.losses);
    if (loss == nullptr || loss->accessory_configuration_id.value !=
                               resolved.accessory_configuration->id.value) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/losses",
            "the operating profile requires Chen-Flynn loss accounting bound to "
            "the declared accessory configuration");
    }

    std::unordered_set<std::string> declared_wires;
    for (std::size_t index = 0; index < engine.ignition.wires.size(); ++index) {
        const auto &wire = engine.ignition.wires[index];
        if (!contract::is_valid_semantic_id(wire.id.value) ||
            !declared_wires.insert(wire.id.value).second) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/ignition/wires", index) + "/id",
                "ignition wire IDs must be unique canonical semantic IDs");
        }
    }
    std::unordered_set<std::string> cylinder_wires;
    for (const auto &[wire, cylinder] : resolved.cylinder_for_wire) {
        static_cast<void>(cylinder);
        cylinder_wires.insert(wire);
    }
    if (declared_wires != cylinder_wires) {
        add(report, DiagnosticCode::unsupported_capability, "/engine/ignition/wires",
            "declared ignition wires must exactly cover the cylinder-owned "
            "ignition wires");
    }

    std::unordered_set<std::string> firing_wires;
    for (std::size_t index = 0; index < engine.ignition.firing_order.size(); ++index) {
        const auto &event = engine.ignition.firing_order[index];
        if (!firing_wires.insert(event.wire.value).second ||
            !resolved.cylinder_for_wire.contains(event.wire.value)) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/ignition/firing_order", index),
                "firing order must cover each cylinder-owned ignition wire once");
        }
        resolved.firing_angle_for_wire_rad.emplace(event.wire.value,
                                                   legacy_si_value(event.crank_angle));
    }
    if (firing_wires.size() != engine.cylinders.size()) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/ignition/firing_order",
            "firing order must exactly cover the admitted cylinders");
    }
    if (firing_wires != declared_wires) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/ignition/firing_order",
            "firing order must exactly cover the declared ignition wires");
    }
    if (!(legacy_si_value(engine.ignition.limiter.cut_duration) > 0.0)) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/ignition/limiter/cut_duration",
            "the current limiter requires a positive cut duration");
    }

    std::unordered_set<std::string> used_curves;
    const auto validate_curve = [&](std::string_view curve_id,
                                    authoring::QuantityDimension input_dimension,
                                    authoring::QuantityDimension output_dimension,
                                    std::string_view path, bool flow_curve) {
        const auto found = resolved.curves.find(std::string{curve_id});
        if (found == resolved.curves.end()) {
            add(report, DiagnosticCode::dangling_reference, std::string{path},
                "curve reference did not resolve");
            return;
        }
        used_curves.insert(std::string{curve_id});
        const auto &curve = *found->second;
        if (!supported_curve_shape(curve, input_dimension, output_dimension)) {
            add(report, DiagnosticCode::unsupported_capability, std::string{path},
                "legacy_low_order_v1 requires a clamped triangle-weighted "
                "curve with an explicit radius and at least two samples");
        }
        double previous = -std::numeric_limits<double>::infinity();
        for (const auto &sample : curve.samples) {
            const double input_value = legacy_si_value(sample.input);
            if (!(std::isfinite(input_value) && input_value > previous)) {
                add(report, DiagnosticCode::invalid_value, std::string{path},
                    "curve abscissas must be finite and strictly increasing");
                break;
            }
            previous = input_value;
            if (flow_curve && (sample.output.unit != "cfm" ||
                               sample.output.standard !=
                                   std::optional<std::string>{"port_28_inh2o"})) {
                add(report, DiagnosticCode::unsupported_capability, std::string{path},
                    "valve-flow curves require CFM values calibrated at "
                    "28 inH2O");
                break;
            }
        }
    };
    const auto intake_port = std::ranges::find(
        engine.ports, authoring::PortKind::intake, &authoring::PortDefinition::kind);
    const auto exhaust_port = std::ranges::find(
        engine.ports, authoring::PortKind::exhaust, &authoring::PortDefinition::kind);
    if (intake_port != engine.ports.end() && exhaust_port != engine.ports.end()) {
        validate_curve(
            intake_port->flow_curve.value, authoring::QuantityDimension::length,
            authoring::QuantityDimension::volume_flow_rate, "/engine/ports", true);
        validate_curve(
            exhaust_port->flow_curve.value, authoring::QuantityDimension::length,
            authoring::QuantityDimension::volume_flow_rate, "/engine/ports", true);
    }
    validate_curve(
        engine.ignition.timing_curve.value, authoring::QuantityDimension::angular_speed,
        authoring::QuantityDimension::angle, "/engine/ignition/timing_curve", false);
    validate_curve(resolved.fuel->turbulence_to_flame_speed.value,
                   authoring::QuantityDimension::dimensionless,
                   authoring::QuantityDimension::speed,
                   "/engine/fuels/0/turbulence_to_flame_speed", false);
    if (used_curves.size() != engine.curves.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/curves",
            "all declared curves must be consumed by the admitted gas, ignition, "
            "or fuel paths");
    }

    std::unordered_set<std::string> used_lobes;
    if (resolved.intake_camshaft != nullptr && resolved.exhaust_camshaft != nullptr) {
        admit_harmonic_lobes(*resolved.intake_camshaft, resolved,
                             authoring::PortKind::intake, report, used_lobes);
        admit_harmonic_lobes(*resolved.exhaust_camshaft, resolved,
                             authoring::PortKind::exhaust, report, used_lobes);
        if (!equivalent_harmonic_shapes(*resolved.intake_camshaft, resolved,
                                        authoring::PortKind::intake) ||
            !equivalent_harmonic_shapes(*resolved.exhaust_camshaft, resolved,
                                        authoring::PortKind::exhaust)) {
            add(report, DiagnosticCode::unsupported_capability, "/engine/cam_lobes",
                "each camshaft role currently requires one shared harmonic shape");
        }
        const std::unordered_set<std::string> used_camshafts{
            resolved.intake_camshaft->id.value,
            resolved.exhaust_camshaft->id.value,
        };
        if (used_camshafts.size() != engine.camshafts.size() ||
            used_lobes.size() != engine.cam_lobes.size()) {
            add(report, DiagnosticCode::disconnected_object, "/engine/camshafts",
                "all declared camshafts and lobes must belong to the selected "
                "standard valvetrain");
        }
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
