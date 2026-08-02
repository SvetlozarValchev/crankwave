#include "compile/engine_resolver_internal.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

void admit_sampled_cam_curve(const authoring::CurveDefinition &curve,
                             authoring::DiagnosticReport &report,
                             std::string_view path) {
    if (!supported_curve_shape(curve, authoring::QuantityDimension::angle,
                               authoring::QuantityDimension::length)) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            std::string{path},
            "sampled cam lobes require a clamped triangle-weighted angle-to-length "
            "curve with an explicit radius and at least two samples");
    }

    if (!curve.triangle_filter_radius.has_value() ||
        !std::isfinite(legacy_si_value(*curve.triangle_filter_radius)) ||
        legacy_si_value(*curve.triangle_filter_radius) <= 0.0) {
        add(report, authoring::DiagnosticCode::invalid_value, std::string{path},
            "sampled cam-lobe triangle radius must be finite and positive");
    }

    double previous_angle_rad = -std::numeric_limits<double>::infinity();
    for (const auto &sample : curve.samples) {
        const double angle_rad = legacy_si_value(sample.input);
        const double lift_m = legacy_si_value(sample.output);
        if (!(std::isfinite(angle_rad) && angle_rad > previous_angle_rad)) {
            add(report, authoring::DiagnosticCode::invalid_value, std::string{path},
                "sampled cam-lobe angles must be finite and strictly increasing");
            break;
        }
        previous_angle_rad = angle_rad;
        if (!(std::isfinite(lift_m) && lift_m >= 0.0)) {
            add(report, authoring::DiagnosticCode::invalid_value, std::string{path},
                "sampled cam-lobe lifts must be finite and nonnegative");
            break;
        }
    }
}

void admit_fixed_cam_lobes(const authoring::CamshaftDefinition &camshaft,
                           const ModelContext &context,
                           authoring::PortKind expected_kind,
                           const std::unordered_set<std::string> &expected_cylinders,
                           authoring::DiagnosticReport &report,
                           std::unordered_set<std::string> &used_lobes,
                           std::unordered_set<std::string> &used_curves) {
    std::unordered_set<std::string> covered_cylinders;
    for (const auto &reference : camshaft.lobes) {
        const auto found = context.cam_lobes.find(reference.value);
        if (found == context.cam_lobes.end()) {
            continue;
        }
        const auto &lobe = *found->second;
        if (lobe.port_kind != expected_kind ||
            !expected_cylinders.contains(lobe.cylinder.value)) {
            continue;
        }
        used_lobes.insert(lobe.id.value);
        covered_cylinders.insert(lobe.cylinder.value);
        if (const auto *shape = std::get_if<authoring::HarmonicCamLobe>(&lobe.shape)) {
            const double reference_lift_m = legacy_si_value(shape->reference_lift);
            const double admitted_reference_lift_m =
                50.0 * (((1.0 / 100.0) * 2.54) / 1000.0);
            if (!std::isfinite(reference_lift_m) ||
                std::abs(reference_lift_m - admitted_reference_lift_m) > 1.0e-15) {
                add(report, authoring::DiagnosticCode::unsupported_capability,
                    "/engine/cam_lobes",
                    "legacy_low_order_v1 admits the exact 0.050-inch harmonic "
                    "reference lift only");
            }
            if (!(shape->gamma > 0.0 && shape->sample_count >= 6U &&
                  shape->sample_count <= 1'000'000U)) {
                add(report, authoring::DiagnosticCode::unsupported_capability,
                    "/engine/cam_lobes",
                    "harmonic gamma must be positive and sample_count must be in "
                    "[6,1000000] for the current fixed valvetrain");
            }
            continue;
        }

        const auto &shape = std::get<authoring::SampledCamLobe>(lobe.shape);
        const auto curve = context.curves.find(shape.lift_curve.value);
        if (curve == context.curves.end()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                "/engine/cam_lobes",
                "sampled cam-lobe lift-curve reference did not resolve");
            continue;
        }
        used_curves.insert(shape.lift_curve.value);
        admit_sampled_cam_curve(*curve->second, report, "/engine/cam_lobes");
    }
    if (camshaft.lobes.size() != expected_cylinders.size() ||
        covered_cylinders != expected_cylinders) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/engine/camshafts",
            "each selected camshaft must contain exactly one matching lobe for "
            "every cylinder assigned to that camshaft role");
    }
}

[[nodiscard]] bool equivalent_cam_shapes(const authoring::CamshaftDefinition &camshaft,
                                         const ModelContext &context,
                                         authoring::PortKind expected_kind) {
    const authoring::CamLobeShape *first = nullptr;
    for (const auto &reference : camshaft.lobes) {
        const auto found = context.cam_lobes.find(reference.value);
        if (found == context.cam_lobes.end() ||
            found->second->port_kind != expected_kind) {
            continue;
        }
        if (first == nullptr) {
            first = &found->second->shape;
        } else if (*first != found->second->shape) {
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
            "the current engine requires one explicitly selected throttle "
            "controller");
    } else {
        resolved.throttle_controller = &engine.throttle_controllers->front();
        if (engine.throttle_controller->value !=
            resolved.throttle_controller->id.value) {
            add(report, DiagnosticCode::unsupported_capability,
                "/engine/throttle_controller",
                "the selected throttle controller does not match the admitted "
                "definition");
        }
        std::visit(
            [&](const auto &controller) {
                if constexpr (requires { controller.minimum_engine_speed; }) {
                    const double minimum_speed =
                        legacy_si_value(controller.minimum_engine_speed);
                    const double maximum_speed =
                        legacy_si_value(controller.maximum_engine_speed);
                    if (!std::isfinite(minimum_speed) || minimum_speed < 0.0 ||
                        !std::isfinite(maximum_speed) || maximum_speed <= 0.0 ||
                        minimum_speed > maximum_speed ||
                        !std::isfinite(controller.minimum_velocity) ||
                        !std::isfinite(controller.maximum_velocity) ||
                        controller.minimum_velocity > controller.maximum_velocity ||
                        !std::isfinite(controller.k_s) || controller.k_s < 0.0 ||
                        !std::isfinite(controller.k_d) || controller.k_d < 0.0 ||
                        !std::isfinite(controller.gamma) || controller.gamma <= 0.0) {
                        add(report, DiagnosticCode::invalid_value,
                            "/engine/throttle_controller",
                            "governor parameters are outside their admitted domain");
                    }
                } else if (!std::isfinite(controller.gamma) ||
                           controller.gamma <= 0.0) {
                    add(report, DiagnosticCode::invalid_value,
                        "/engine/throttle_controller/gamma",
                        "direct throttle gamma must be finite and positive");
                }
            },
            resolved.throttle_controller->kind);
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
    for (const auto &[wire, cylinders] : resolved.cylinders_for_wire) {
        static_cast<void>(cylinders);
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
            !resolved.cylinders_for_wire.contains(event.wire.value)) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/engine/ignition/firing_order", index),
                "firing order must cover each cylinder-connected ignition wire "
                "once");
        }
        resolved.firing_angle_for_wire_rad.emplace(event.wire.value,
                                                   legacy_si_value(event.crank_angle));
    }
    if (firing_wires.size() != declared_wires.size()) {
        add(report, DiagnosticCode::unsupported_capability,
            "/engine/ignition/firing_order",
            "firing order must contain exactly one event per declared ignition "
            "wire");
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
    for (const auto &port : engine.ports) {
        validate_curve(port.flow_curve.value, authoring::QuantityDimension::length,
                       authoring::QuantityDimension::volume_flow_rate, "/engine/ports",
                       true);
    }
    validate_curve(
        engine.ignition.timing_curve.value, authoring::QuantityDimension::angular_speed,
        authoring::QuantityDimension::angle, "/engine/ignition/timing_curve", false);
    validate_curve(resolved.fuel->turbulence_to_flame_speed.value,
                   authoring::QuantityDimension::dimensionless,
                   authoring::QuantityDimension::speed,
                   "/engine/fuels/0/turbulence_to_flame_speed", false);
    std::unordered_set<std::string> used_lobes;
    std::unordered_set<std::string> used_camshafts;
    using CamshaftByCylinder =
        std::unordered_map<std::string, const authoring::CamshaftDefinition *>;
    const auto admit_camshaft_role = [&](const CamshaftByCylinder &by_cylinder,
                                         const authoring::PortKind port_kind) {
        std::unordered_map<std::string, std::unordered_set<std::string>>
            expected_cylinders_by_camshaft;
        for (const auto &cylinder : engine.cylinders) {
            const auto selected = by_cylinder.find(cylinder.id.value);
            if (selected == by_cylinder.end() || selected->second == nullptr) {
                add(report, DiagnosticCode::internal_failure, "/engine/camshafts",
                    "camshaft role did not resolve for every admitted cylinder");
                continue;
            }
            expected_cylinders_by_camshaft[selected->second->id.value].insert(
                cylinder.id.value);
        }
        if (by_cylinder.size() != engine.cylinders.size()) {
            add(report, DiagnosticCode::internal_failure, "/engine/camshafts",
                "camshaft role contains an unexpected per-cylinder binding");
        }

        for (const auto &camshaft : engine.camshafts) {
            const auto expected =
                expected_cylinders_by_camshaft.find(camshaft.id.value);
            if (expected == expected_cylinders_by_camshaft.end()) {
                continue;
            }
            admit_fixed_cam_lobes(camshaft, resolved, port_kind, expected->second,
                                  report, used_lobes, used_curves);
            if (!equivalent_cam_shapes(camshaft, resolved, port_kind)) {
                add(report, DiagnosticCode::unsupported_capability, "/engine/cam_lobes",
                    "each selected camshaft requires one exact shared authored "
                    "lobe shape");
            }
            used_camshafts.insert(camshaft.id.value);
        }
    };
    admit_camshaft_role(resolved.intake_camshaft_for_cylinder,
                        authoring::PortKind::intake);
    admit_camshaft_role(resolved.exhaust_camshaft_for_cylinder,
                        authoring::PortKind::exhaust);
    if (!resolved.alternate_intake_camshaft_for_cylinder.empty() &&
        !resolved.alternate_exhaust_camshaft_for_cylinder.empty()) {
        admit_camshaft_role(resolved.alternate_intake_camshaft_for_cylinder,
                            authoring::PortKind::intake);
        admit_camshaft_role(resolved.alternate_exhaust_camshaft_for_cylinder,
                            authoring::PortKind::exhaust);

        for (std::size_t index = 0; index < engine.valvetrains.size(); ++index) {
            const auto *vtec =
                std::get_if<authoring::VtecValvetrain>(&engine.valvetrains[index].kind);
            if (vtec == nullptr) {
                continue;
            }
            const double minimum_speed_rad_s =
                legacy_si_value(vtec->activation.minimum_engine_speed);
            const double minimum_pressure_pa_abs =
                legacy_si_value(vtec->activation.minimum_manifold_pressure_abs);
            const double minimum_opening =
                vtec->activation.minimum_throttle_linkage_opening_01;
            if (!(std::isfinite(minimum_speed_rad_s) && minimum_speed_rad_s >= 0.0 &&
                  std::isfinite(minimum_pressure_pa_abs) &&
                  minimum_pressure_pa_abs > 0.0 && std::isfinite(minimum_opening) &&
                  minimum_opening >= 0.0 && minimum_opening <= 1.0)) {
                add(report, DiagnosticCode::invalid_value,
                    pointer_index("/engine/valvetrains", index) + "/activation",
                    "VTEC activation requires nonnegative engine speed, positive "
                    "absolute manifold pressure, and a unit-interval throttle "
                    "linkage opening");
            }
        }
    }
    if (used_camshafts.size() != engine.camshafts.size() ||
        used_lobes.size() != engine.cam_lobes.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/camshafts",
            "all declared camshafts and lobes must belong to the selected "
            "valvetrain");
    }
    if (used_curves.size() != engine.curves.size()) {
        add(report, DiagnosticCode::disconnected_object, "/engine/curves",
            "all declared curves must be consumed by the admitted gas, ignition, "
            "fuel, or fixed-valvetrain paths");
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
