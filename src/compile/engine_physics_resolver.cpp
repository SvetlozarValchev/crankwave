#include "compile/engine_resolver_internal.hpp"

#include "simulation/cycle_accounting_method_registry.hpp"
#include "simulation/legacy_flow_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {

constexpr std::string_view kProfileRoot = "engine.physics.low-order-operating-point-v1";
constexpr double kLegacyRpmScale = 0.104719755;

[[nodiscard]] std::string profile_path(std::string_view suffix) {
    return std::string{kProfileRoot} + "." + std::string{suffix};
}

template <class Id>
[[nodiscard]] Id runtime_id(const IdNamespace &ids, std::string_view semantic_id) {
    return Id{ids.by_semantic_id.at(std::string{semantic_id})};
}

[[nodiscard]] contract::BankId bank_id(const ModelContext &context,
                                       std::string_view semantic_id) {
    return runtime_id<contract::BankId>(context.ids.banks, semantic_id);
}

[[nodiscard]] contract::CylinderId cylinder_id(const ModelContext &context,
                                               std::string_view semantic_id) {
    return runtime_id<contract::CylinderId>(context.ids.cylinders, semantic_id);
}

[[nodiscard]] contract::PortId port_id(const ModelContext &context,
                                       std::string_view semantic_id) {
    return runtime_id<contract::PortId>(context.ids.ports, semantic_id);
}

[[nodiscard]] contract::GasVolumeId volume_id(const ModelContext &context,
                                              std::string_view semantic_id) {
    return runtime_id<contract::GasVolumeId>(context.ids.gas_volumes, semantic_id);
}

[[nodiscard]] contract::FlowEdgeId edge_id(const ModelContext &context,
                                           std::string_view semantic_id) {
    return runtime_id<contract::FlowEdgeId>(context.ids.flow_edges, semantic_id);
}

[[nodiscard]] contract::RouteId route_id(const ModelContext &context,
                                         std::string_view semantic_id) {
    return runtime_id<contract::RouteId>(context.ids.routes, semantic_id);
}

[[nodiscard]] std::string port_semantic_id(std::string_view cylinder,
                                           authoring::PortKind kind) {
    return "port." + std::string{cylinder} +
           (kind == authoring::PortKind::intake ? ".intake" : ".exhaust");
}

[[nodiscard]] std::string volume_semantic_id(std::string_view cylinder,
                                             std::string_view role) {
    return "volume." + std::string{cylinder} + "." + std::string{role};
}

[[nodiscard]] std::string collector_semantic_id(std::string_view exhaust) {
    return "volume.exhaust." + std::string{exhaust} + ".collector";
}

[[nodiscard]] std::string flow_semantic_id(std::string_view owner,
                                           std::string_view role) {
    return "flow." + std::string{owner} + "." + std::string{role};
}

[[nodiscard]] contract::LegacyRestrictionCalibration
restriction_calibration(const authoring::FlowBenchRestriction &restriction) {
    return restriction.rated_flow.standard ==
                   std::optional<std::string>{"port_28_inh2o"}
               ? contract::LegacyRestrictionCalibration::cfm_at_28_inh2o
               : contract::LegacyRestrictionCalibration::carb_at_1p5_inhg;
}

[[nodiscard]] const authoring::FlowBenchRestriction &
flow_bench(const authoring::FlowRestriction &restriction) {
    return std::get<authoring::FlowBenchRestriction>(restriction);
}

[[nodiscard]] contract::LegacyRestriction
resolve_restriction(const authoring::FlowRestriction &source, std::string base_path,
                    ResolutionEmitter &emitter) {
    const auto &flow = flow_bench(source);
    const auto calibration = restriction_calibration(flow);
    const double source_rating = flow.rated_flow.value;
    const auto calibration_path = base_path + ".calibration";
    const auto source_rating_path = base_path + ".source_rating";
    return {
        emitter.authored(calibration, calibration_path),
        emitter.authored(source_rating, source_rating_path),
        emitter.derived(simulation::legacy_flow_bench_restriction_coefficient(
                            calibration, source_rating),
                        base_path + ".resolved_k",
                        derived_method_identity("legacy-flow-constant-v1"),
                        {calibration_path, source_rating_path}),
    };
}

[[nodiscard]] double rpm_value(const authoring::Quantity &quantity) {
    return quantity.unit == "rpm" ? quantity.value
                                  : legacy_si_value(quantity) / kLegacyRpmScale;
}

[[nodiscard]] std::string sample_id(std::size_t index) {
    return "sample-" + std::to_string(index + 1U);
}

[[nodiscard]] std::vector<ResolvedRouteSource>
ordered_routes(const ModelContext &context) {
    std::vector<ResolvedRouteSource> result;
    result.reserve(context.document.engine.source_routes.size());
    for (const auto &route : context.document.engine.source_routes) {
        const auto &source = std::get<authoring::ExhaustRouteSource>(route.source);
        result.push_back({
            &route,
            context.exhausts.at(source.exhaust.value),
        });
    }
    std::ranges::sort(result, [&](const auto &left, const auto &right) {
        return route_id(context, left.route->id.value).value <
               route_id(context, right.route->id.value).value;
    });
    return result;
}

[[nodiscard]] const authoring::PortDefinition &
authored_port(const ModelContext &context, authoring::PortKind kind) {
    const auto iterator =
        std::ranges::find_if(context.document.engine.ports,
                             [&](const auto &port) { return port.kind == kind; });
    return *iterator;
}

[[nodiscard]] const authoring::CamLobeDefinition &
first_cam_lobe(const ModelContext &context,
               const authoring::CamshaftDefinition &camshaft,
               authoring::PortKind kind) {
    for (const auto &reference : camshaft.lobes) {
        const auto &lobe = *context.cam_lobes.at(reference.value);
        if (lobe.port_kind == kind) {
            return lobe;
        }
    }
    return *context.cam_lobes.at(camshaft.lobes.front().value);
}

[[nodiscard]] const authoring::CamLobeDefinition &
cam_lobe_for_cylinder(const ModelContext &context,
                      const authoring::CamshaftDefinition &camshaft,
                      std::string_view cylinder, authoring::PortKind kind) {
    for (const auto &reference : camshaft.lobes) {
        const auto &lobe = *context.cam_lobes.at(reference.value);
        if (lobe.cylinder.value == cylinder && lobe.port_kind == kind) {
            return lobe;
        }
    }
    return first_cam_lobe(context, camshaft, kind);
}

[[nodiscard]] contract::LegacyCamShape resolve_cam_shape(
    const ModelContext &context, const authoring::CamshaftDefinition &camshaft,
    authoring::PortKind kind, std::string role, ResolutionEmitter &emitter) {
    const auto &lobe = first_cam_lobe(context, camshaft, kind);
    const auto base = profile_path("valvetrain." + role + ".shape");
    if (const auto *shape = std::get_if<authoring::HarmonicCamLobe>(&lobe.shape)) {
        return contract::LegacyHarmonicCamShape{
            emitter.authored(legacy_si_value(shape->maximum_lift),
                             base + ".maximum_lift_m"),
            emitter.authored(legacy_si_value(shape->duration_at_reference_lift),
                             base + ".duration_at_reference_lift_rad"),
            emitter.authored(shape->gamma, base + ".exponent"),
            emitter.authored(shape->sample_count, base + ".construction_steps"),
            emitter.authored(legacy_si_value(camshaft.advance), base + ".advance_rad"),
            emitter.authored(legacy_si_value(camshaft.base_radius),
                             base + ".base_radius_m"),
        };
    }

    const auto &shape = std::get<authoring::SampledCamLobe>(lobe.shape);
    const auto &curve = *context.curves.at(shape.lift_curve.value);
    contract::LegacySampledCamShape resolved;
    resolved.triangle_radius_rad = emitter.authored(
        legacy_si_value(*curve.triangle_filter_radius), base + ".triangle_radius_rad");
    resolved.samples.reserve(curve.samples.size());
    for (std::size_t index = 0; index < curve.samples.size(); ++index) {
        const auto id = sample_id(index);
        const auto path = base + ".samples." + id;
        resolved.samples.push_back({
            emitter.authored(id, path + ".sample_id"),
            emitter.authored(legacy_si_value(curve.samples[index].input),
                             path + ".angle_rad"),
            emitter.authored(legacy_si_value(curve.samples[index].output),
                             path + ".lift_m"),
        });
    }
    resolved.advance_rad =
        emitter.authored(legacy_si_value(camshaft.advance), base + ".advance_rad");
    resolved.base_radius_m = emitter.authored(legacy_si_value(camshaft.base_radius),
                                              base + ".base_radius_m");
    return resolved;
}

[[nodiscard]] contract::LegacyValveFlowPoint
resolve_valve_flow_point(const authoring::CurveSample &source, std::size_t index,
                         std::string base, ResolutionEmitter &emitter) {
    const auto id = sample_id(index);
    const auto path = base + "." + id;
    const auto source_path = path + ".source_cfm_at_28_inh2o";
    return {
        emitter.authored(id, path + ".sample_id"),
        emitter.authored(legacy_si_value(source.input), path + ".lift_m"),
        emitter.authored(source.output.value, source_path),
        emitter.derived(simulation::legacy_flow_bench_restriction_coefficient(
                            contract::LegacyRestrictionCalibration::cfm_at_28_inh2o,
                            source.output.value),
                        path + ".resolved_k",
                        derived_method_identity("legacy-flow-constant-v1"),
                        {source_path}),
    };
}

[[nodiscard]] contract::TorqueCapability operating_torque_capability() {
    return {
        {
            contract::Availability::available,
            contract::Completeness::complete,
            contract::known_torque_term_mask(),
            0,
        },
        {
            contract::Availability::available,
            contract::Completeness::complete,
            contract::known_torque_term_mask(),
            0,
        },
        true,
    };
}

contract::MethodIdentity legacy_low_order_method_identity() {
    return contract::legacy_low_order_v1_method_identity();
}

contract::MethodIdentity derived_method_identity(const std::string_view method_id) {
    const std::string descriptor = "engine-sim-offline.compiler-derived-method." +
                                   std::string{method_id} + ".configuration-v1";
    return {
        std::string{method_id},
        1U,
        contract::sha256(
            std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()})),
    };
}

contract::EngineSpec assemble_engine(const ModelContext &context,
                                     ResolutionEmitter &emitter) {
    const auto &source = context.document.engine;
    contract::EngineSpec engine;
    engine.schema_version = 1U;
    engine.id = contract::EngineId{1U};
    engine.engine_id = emitter.authored(source.identity.id.value, "engine.engine_id");
    engine.profile_id = emitter.derived(context.profile_id, "engine.profile_id",
                                        derived_method_identity("engine-profile-id-v1"),
                                        {"engine.engine_id"});
    engine.display_name =
        emitter.authored(source.identity.display_name, "engine.display_name");
    engine.cycle = emitter.authored(contract::EngineCycle::four_stroke, "engine.cycle");
    engine.ignition = emitter.derived(
        contract::IgnitionKind::spark_ignition, "engine.ignition",
        derived_method_identity("spark-ignition-kind-v1"), {"engine.profile_id"});
    engine.cylinder_layout =
        emitter.authored(source.layout == authoring::CylinderLayout::v_engine
                             ? contract::CylinderLayoutKind::vee_engine
                             : contract::CylinderLayoutKind::inline_engine,
                         "engine.cylinder_layout");

    resolve_public_topology(context, emitter, engine);

    const auto method_dependency = "engine.profile_id";
    const auto low_order = contract::legacy_low_order_v1_method_identity();
    const auto resolve_low_order_method = [&](std::string path) {
        return emitter.derived(
            low_order, std::move(path),
            derived_method_identity("implemented-low-order-selection-v1"),
            {method_dependency});
    };
    engine.methods = {
        resolve_low_order_method("engine.methods.mechanism"),
        resolve_low_order_method("engine.methods.valvetrain"),
        resolve_low_order_method("engine.methods.gas_exchange"),
        resolve_low_order_method("engine.methods.ignition"),
        resolve_low_order_method("engine.methods.combustion"),
        resolve_low_order_method("engine.methods.heat_transfer"),
        emitter.derived(
            simulation::implemented_cycle_accounting_method_identities().aggregate_loss,
            "engine.methods.losses",
            derived_method_identity("implemented-aggregate-loss-selection-v1"),
            {method_dependency}),
        resolve_low_order_method("engine.methods.excitation"),
    };

    contract::LowOrderOperatingPointV1Profile profile;
    resolve_mechanism(context, emitter, profile.core);
    resolve_gas_path(context, emitter, profile.core);
    resolve_valvetrain(context, emitter, profile.core);
    resolve_ignition_and_fuel(context, emitter, profile.core);
    resolve_excitation(context, emitter, profile.core);
    resolve_operating_accounting(context, emitter, profile);
    engine.physics_profile = std::move(profile);
    engine.torque_capability =
        emitter.derived(operating_torque_capability(), "engine.torque_capability",
                        derived_method_identity("operating-point-torque-capability-v1"),
                        {"engine.methods.losses"});
    return engine;
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
