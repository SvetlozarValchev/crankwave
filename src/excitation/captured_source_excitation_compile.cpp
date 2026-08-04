#include "excitation/captured_source_excitation.hpp"

#include "excitation/captured_source_excitation_internal.hpp"
#include "simulation/legacy_flow_calibration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::excitation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite_nonnegative(double value) noexcept {
    return finite(value) && value >= 0.0;
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return finite(value) && value > 0.0;
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool exact_excitation_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method) {
    return method.value == contract::legacy_low_order_v1_method_identity();
}

template <class Range, class Id, class Projection>
[[nodiscard]] std::optional<std::size_t> find_index(const Range &range, Id id,
                                                    Projection projection) {
    const auto found = std::ranges::find_if(
        range, [&](const auto &entry) { return projection(entry) == id; });
    if (found == range.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - range.begin());
}

[[nodiscard]] std::optional<std::uint32_t>
resolve_delay_samples(double header_length_m, double route_length_m,
                      double propagation_speed_m_s,
                      contract::RationalRateHz rate) noexcept {
    const double delay_seconds =
        (header_length_m + route_length_m) / propagation_speed_m_s;
    const double delay_rate_hz =
        static_cast<double>(rate.numerator) / static_cast<double>(rate.denominator);
    const double requested_delay_samples = delay_seconds * delay_rate_hz;
    const double rounded_delay_samples = std::round(requested_delay_samples);
    if (!finite_nonnegative(rounded_delay_samples) ||
        rounded_delay_samples >
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(rounded_delay_samples);
}

} // namespace

CapturedSourceExcitationCompileResult
compile_captured_source_excitation_session(const contract::EngineSpec &engine,
                                           const contract::LowOrderEngineCoreV1 &core,
                                           const contract::RenderScenario &scenario) {
    ValidationReport report;
    std::vector<const contract::RouteSpec *> exhaust_routes;
    std::vector<std::size_t> intake_route_indices;
    exhaust_routes.reserve(engine.routes.size());
    intake_route_indices.reserve(engine.routes.size());
    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        const auto &route = engine.routes[index];
        if (route.kind.value == contract::SourceRouteKind::exhaust_outlet) {
            exhaust_routes.push_back(&route);
        } else if (route.kind.value == contract::SourceRouteKind::intake_inlet) {
            intake_route_indices.push_back(index);
            const auto path = "engine.routes[" + std::to_string(index) + "]";
            require(report, route.id.valid(), ContractIssueCode::invalid_value,
                    path + ".id",
                    "captured intake pressure requires a valid route identity");
            require(report, route.source_volume_id.has_value(),
                    ContractIssueCode::dangling_reference, path + ".source_volume_id",
                    "captured intake pressure requires its plenum source volume");
        }
    }
    const std::size_t route_count = exhaust_routes.size();
    const std::size_t intake_route_count = intake_route_indices.size();
    require(report, engine.id.valid(), ContractIssueCode::invalid_value, "engine.id",
            "captured source excitation requires a valid engine identity");
    require(report, exact_excitation_method(engine.methods.excitation),
            ContractIssueCode::unsupported_value, "engine.methods.excitation",
            "captured source excitation requires the exact admitted "
            "legacy_low_order_v1 method identity");
    require(report, !engine.cylinders.empty(), ContractIssueCode::inconsistent_shape,
            "engine.cylinders",
            "captured source excitation requires at least one cylinder");
    require(report, !exhaust_routes.empty(), ContractIssueCode::inconsistent_shape,
            "engine.routes",
            "captured source excitation requires at least one exhaust route");
    require(report, scenario.engine_profile_id == engine.profile_id.value,
            ContractIssueCode::inconsistent_semantics, "scenario.engine_profile_id",
            "captured source excitation requires the selected engine profile");
    require(report, contract::validate(scenario.rates.capture).ok(),
            ContractIssueCode::invalid_value, "scenario.rates.capture",
            "captured source excitation requires a valid capture rate");
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates.capture",
            "captured source excitation requires equal physics and capture rates");
    require(report,
            scenario.rates.capture == kPreviewCapturedSourceRateHz ||
                scenario.rates.capture == kCapturedSourceRateHz,
            ContractIssueCode::unsupported_value, "scenario.rates.capture",
            "captured source excitation uses an exact 10 or 20 kHz clock");
    require(report, finite_positive(scenario.crankcase.pressure_pa_abs.value),
            ContractIssueCode::invalid_value,
            "scenario.crankcase.pressure_pa_abs.value",
            "axial pressure-force capture requires finite positive crankcase "
            "absolute pressure");
    const auto block_capacity = scenario.quality.value.capture_block_capacity_frames;
    require(report, block_capacity > 0U, ContractIssueCode::invalid_value,
            "scenario.quality.value.capture_block_capacity_frames",
            "captured source excitation requires a positive block capacity");
    require(report,
            block_capacity > 0U &&
                engine.cylinders.size() <=
                    std::numeric_limits<std::size_t>::max() / block_capacity &&
                route_count <=
                    std::numeric_limits<std::size_t>::max() / block_capacity &&
                intake_route_count <=
                    std::numeric_limits<std::size_t>::max() / block_capacity,
            ContractIssueCode::invalid_value, "engine",
            "captured source excitation block storage size is unrepresentable");
    if (!report.ok()) {
        return report;
    }
    const std::size_t cylinder_count = engine.cylinders.size();
    const auto &source = core.excitation;
    require(report, source.filtered_speed_exponent.value == 3U,
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.excitation.filtered_speed_exponent",
            "captured source excitation admits only the exact cubic speed ramp");
    require(report, source.inverse_length_exponent.value == 2.0,
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.excitation.inverse_length_exponent",
            "captured source excitation admits only inverse-square distance");
    require(report,
            finite_positive(source.reference_atmosphere_pa_abs.value) &&
                finite_positive(source.legacy_propagation_speed_m_s.value) &&
                finite_nonnegative(source.excitation_scale.value) &&
                finite_positive(source.filtered_speed_threshold_rpm.value) &&
                finite(source.pressure_gains.gauge_static.value) &&
                finite(source.pressure_gains.dynamic_forward.value) &&
                finite(source.pressure_gains.dynamic_reverse.value) &&
                finite_positive(source.cylinder_count_divisor.value),
            ContractIssueCode::invalid_value, "engine.physics_profile.excitation",
            "captured source excitation parameters are outside their finite "
            "domains");
    require(report,
            same_binary64(source.cylinder_count_divisor.value,
                          static_cast<double>(engine.cylinders.size())),
            ContractIssueCode::inconsistent_semantics,
            "engine.physics_profile.excitation.cylinder_count_divisor",
            "excitation divisor must equal the compiled cylinder count");
    require(report, source.cylinder_paths.size() == cylinder_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.cylinder_paths",
            "captured source excitation requires one path per cylinder");
    require(report, source.cylinder_accumulation_order.value.size() == cylinder_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.cylinder_accumulation_order",
            "captured source excitation requires a complete cylinder order");
    require(report, source.routes.size() == route_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.routes",
            "captured source excitation requires one record per exhaust route");
    require(report, core.gas_path.exhaust_routes.size() == route_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.exhaust_routes",
            "captured source excitation requires matching gas-path routes");
    require(report, core.mechanism.cylinders.size() == cylinder_count,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "captured source excitation requires one mechanism record per "
            "cylinder");
    if (!report.ok()) {
        return report;
    }

    auto state = std::make_unique<detail::CapturedSourceExcitationState>();
    state->engine_id = engine.id;
    state->model_id = engine.methods.excitation.value.id;
    state->profile_id = engine.profile_id.value;
    state->sample_rate = scenario.rates.capture;
    state->block_capacity_frames = block_capacity;
    state->reference_atmosphere_pa_abs = source.reference_atmosphere_pa_abs.value;
    state->excitation_scale = source.excitation_scale.value;
    state->filtered_speed_threshold_rpm = source.filtered_speed_threshold_rpm.value;
    state->gauge_static_gain = source.pressure_gains.gauge_static.value;
    state->dynamic_forward_gain = source.pressure_gains.dynamic_forward.value;
    state->dynamic_reverse_gain = source.pressure_gains.dynamic_reverse.value;
    state->cylinder_count_divisor = source.cylinder_count_divisor.value;
    state->cylinder_ids.resize(cylinder_count);
    state->port_layout.reserve(engine.ports.size());
    for (const auto &port : engine.ports) {
        state->port_layout.push_back({port.id, port.cylinder_id, port.kind.value});
    }
    state->piston_crown_areas_m2.resize(cylinder_count);
    state->crankcase_pressure_pa_abs = scenario.crankcase.pressure_pa_abs.value;
    state->route_layout.reserve(engine.routes.size());
    state->intake_route_ids.reserve(intake_route_count);
    state->intake_capture_route_indices.reserve(intake_route_count);
    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        const auto &declared = engine.routes[index];
        state->route_layout.push_back({
            declared.id,
            declared.kind.value,
            declared.source_volume_id,
            declared.default_parent_route_id,
            declared.emitter_anchor_id.has_value()
                ? std::optional<std::string>{declared.emitter_anchor_id->value}
                : std::nullopt,
        });
        if (declared.kind.value == contract::SourceRouteKind::intake_inlet) {
            state->intake_route_ids.push_back(declared.id);
            state->intake_capture_route_indices.push_back(index);
        }
    }
    state->route_ids.resize(route_count);
    state->exhaust_valve_reference_mass_flow_kg_s.assign(route_count, +0.0);
    state->cylinders.resize(cylinder_count);
    state->prospective_delays.resize(cylinder_count);
    state->prospective_exhaust_flow_delays.resize(cylinder_count);
    state->accumulation_order.resize(cylinder_count);
    state->routes.resize(route_count);
    state->prospective_route_delays.resize(route_count);
    state->prospective_exhaust_flow_route_delays.resize(route_count);
    state->pre_delay.assign(static_cast<std::size_t>(block_capacity) * cylinder_count,
                            +0.0);
    state->post_delay.assign(static_cast<std::size_t>(block_capacity) * cylinder_count,
                             +0.0);
    state->collector_bus_values.assign(
        static_cast<std::size_t>(block_capacity) * route_count, +0.0);
    state->route_bus_values.assign(
        static_cast<std::size_t>(block_capacity) * route_count, +0.0);
    state->collector_absolute_exhaust_valve_mass_flow_kg_s.assign(
        static_cast<std::size_t>(block_capacity) * route_count, +0.0);
    state->route_absolute_exhaust_valve_mass_flow_kg_s.assign(
        static_cast<std::size_t>(block_capacity) * route_count, +0.0);
    state->intake_pressure_pa_abs.assign(
        static_cast<std::size_t>(block_capacity) * intake_route_count, +0.0);
    state->axial_pressure_force_n.assign(
        static_cast<std::size_t>(block_capacity) * cylinder_count, +0.0);

    std::vector<bool> gas_route_seen(route_count, false);
    std::vector<std::uint32_t> route_delay_samples(route_count, 0U);
    for (std::size_t index = 0; index < route_count; ++index) {
        const auto &declared = *exhaust_routes[index];
        const auto &configured = source.routes[index];
        const auto gas_route_index =
            find_index(core.gas_path.exhaust_routes, configured.route_id,
                       [](const contract::LegacyExhaustRouteProfile &route) {
                           return route.topology.route_id;
                       });
        const std::string path =
            "engine.physics_profile.excitation.routes[" + std::to_string(index) + "]";
        require(report,
                declared.id.valid() &&
                    declared.kind.value == contract::SourceRouteKind::exhaust_outlet &&
                    configured.route_id == declared.id,
                ContractIssueCode::inconsistent_semantics, path,
                "excitation route order must exactly match the exhaust capture layout");
        require(report,
                finite_positive(configured.exhaust_system_length_m.value) &&
                    finite_nonnegative(configured.audio_volume_linear.value),
                ContractIssueCode::invalid_value, path,
                "excitation route length or volume is outside its finite domain");
        require(report,
                gas_route_index.has_value() &&
                    !gas_route_seen[gas_route_index.value_or(0U)],
                gas_route_index.has_value() ? ContractIssueCode::duplicate_identity
                                            : ContractIssueCode::dangling_reference,
                path + ".route_id",
                "excitation route must resolve uniquely in the gas-path topology");
        if (gas_route_index.has_value()) {
            gas_route_seen[*gas_route_index] = true;
            const auto &gas_route = core.gas_path.exhaust_routes[*gas_route_index];
            require(
                report,
                declared.source_volume_id ==
                        std::optional<contract::GasVolumeId>{
                            gas_route.topology.collector_volume_id} &&
                    same_binary64(configured.exhaust_system_length_m.value,
                                  gas_route.parameters.exhaust_system_length_m.value) &&
                    same_binary64(configured.audio_volume_linear.value,
                                  gas_route.parameters.audio_volume_linear.value),
                ContractIssueCode::inconsistent_semantics, path,
                "excitation route values must match their gas-path route");
        }

        state->route_ids[index] = configured.route_id;
        state->routes[index] = {
            configured.route_id,
            configured.exhaust_system_length_m.value,
            configured.audio_volume_linear.value,
            {},
            {},
        };
        const auto downstream_delay = resolve_delay_samples(
            0.0, configured.exhaust_system_length_m.value,
            source.legacy_propagation_speed_m_s.value, scenario.rates.capture);
        require(report, downstream_delay.has_value(), ContractIssueCode::invalid_value,
                path + ".exhaust_system_length_m",
                "route propagation delay is outside its representable domain");
        if (downstream_delay.has_value()) {
            route_delay_samples[index] = *downstream_delay;
        }
    }

    std::vector<bool> path_seen(cylinder_count, false);
    std::vector<bool> mechanism_seen(cylinder_count, false);
    std::vector<std::uint32_t> delay_samples(cylinder_count, 0U);
    for (std::size_t index = 0; index < cylinder_count; ++index) {
        const auto &engine_cylinder = engine.cylinders[index];
        const auto cylinder_id = engine_cylinder.id;
        state->cylinder_ids[index] = cylinder_id;
        // EngineSpec bore is the immutable public geometry authority at this seam;
        // the simulation's compiled piston area is private runtime state.
        const double bore_squared_m2 =
            engine_cylinder.bore_m.value * engine_cylinder.bore_m.value;
        const double piston_crown_area_m2 =
            (std::numbers::pi_v<double> * bore_squared_m2) / 4.0;
        state->piston_crown_areas_m2[index] = piston_crown_area_m2;
        require(report,
                finite_positive(engine_cylinder.bore_m.value) &&
                    finite_positive(piston_crown_area_m2),
                ContractIssueCode::invalid_value,
                "engine.cylinders[" + std::to_string(index) + "].bore_m.value",
                "axial pressure-force capture requires a representable piston "
                "crown area");
        const auto path_index =
            find_index(source.cylinder_paths, cylinder_id,
                       [](const contract::LegacyExcitationCylinderPath &path) {
                           return path.cylinder_id;
                       });
        const auto mechanism_index =
            find_index(core.mechanism.cylinders, cylinder_id,
                       [](const contract::LegacyCylinderAssembly &cylinder) {
                           return cylinder.topology.cylinder_id;
                       });
        const std::string path_name =
            "engine.physics_profile.excitation.cylinder_paths[" +
            std::to_string(index) + "]";
        require(report, cylinder_id.valid() && path_index.has_value(),
                ContractIssueCode::dangling_reference, path_name,
                "capture cylinder has no excitation path");
        require(report,
                mechanism_index.has_value() && mechanism_index.value_or(index) == index,
                ContractIssueCode::dangling_reference, path_name,
                "capture cylinder order must exactly match the mechanism layout");
        if (!path_index.has_value() || !mechanism_index.has_value()) {
            continue;
        }
        require(report, !path_seen[*path_index], ContractIssueCode::duplicate_identity,
                path_name, "excitation cylinder path was bound more than once");
        path_seen[*path_index] = true;
        require(report, !mechanism_seen[*mechanism_index],
                ContractIssueCode::duplicate_identity, path_name,
                "mechanism cylinder was bound more than once");
        mechanism_seen[*mechanism_index] = true;

        const auto &configured = source.cylinder_paths[*path_index];
        const auto &mechanism = core.mechanism.cylinders[*mechanism_index];
        const auto route_index =
            find_index(source.routes, configured.route_id,
                       [](const contract::LegacyExcitationRoute &route) {
                           return route.route_id;
                       });
        const auto exhaust_port_index =
            find_index(engine.ports, mechanism.topology.exhaust_port_id,
                       [](const contract::PortSpec &port) { return port.id; });
        const bool exhaust_port_matches =
            exhaust_port_index.has_value() &&
            engine.ports[*exhaust_port_index].cylinder_id == cylinder_id &&
            engine.ports[*exhaust_port_index].kind.value == contract::PortKind::exhaust;
        require(report, exhaust_port_matches, ContractIssueCode::dangling_reference,
                path_name + ".exhaust_port_id",
                "excitation cylinder must bind one matching exhaust capture port");

        const auto head_index = find_index(
            core.gas_path.heads, engine_cylinder.bank_id,
            [](const contract::LegacyBankHeadProfile &head) { return head.bank_id; });
        double maximum_exhaust_flow_cfm = +0.0;
        bool valid_exhaust_capacity = head_index.has_value();
        if (head_index.has_value()) {
            const auto &flow = core.gas_path.heads[*head_index].exhaust_flow;
            valid_exhaust_capacity = !flow.empty();
            for (const auto &point : flow) {
                const double source_cfm = point.source_cfm_at_28_inh2o.value;
                valid_exhaust_capacity =
                    valid_exhaust_capacity && finite_nonnegative(source_cfm);
                maximum_exhaust_flow_cfm =
                    std::max(maximum_exhaust_flow_cfm, source_cfm);
            }
        }
        const double exhaust_valve_reference_mass_flow_kg_s =
            simulation::legacy_standard_cfm_mass_flow_kg_s(maximum_exhaust_flow_cfm);
        valid_exhaust_capacity =
            valid_exhaust_capacity &&
            finite_positive(exhaust_valve_reference_mass_flow_kg_s);
        require(report, valid_exhaust_capacity, ContractIssueCode::invalid_value,
                path_name + ".bank_exhaust_flow",
                "excitation cylinder requires a finite positive authored maximum "
                "bank-local exhaust-valve flow-bench capacity");
        require(report, route_index.has_value(), ContractIssueCode::dangling_reference,
                path_name + ".route_id",
                "excitation cylinder path route does not resolve");
        std::optional<std::uint32_t> expected_total_delay;
        if (route_index.has_value()) {
            expected_total_delay = resolve_delay_samples(
                configured.header_primary_length_m.value,
                source.routes[*route_index].exhaust_system_length_m.value,
                source.legacy_propagation_speed_m_s.value, scenario.rates.capture);
        }
        require(report,
                finite_nonnegative(configured.header_primary_length_m.value) &&
                    finite_nonnegative(configured.sound_attenuation_linear.value) &&
                    same_binary64(configured.header_primary_length_m.value,
                                  mechanism.parameters.header_primary_length_m.value) &&
                    configured.route_id == mechanism.topology.exhaust_route_id &&
                    expected_total_delay.has_value() &&
                    (!route_index.has_value() || expected_total_delay.value_or(0U) >=
                                                     route_delay_samples[*route_index]),
                ContractIssueCode::inconsistent_semantics, path_name,
                "excitation path geometry, route, or capture-rate delay is incoherent");
        if (!route_index.has_value() || !exhaust_port_matches ||
            !valid_exhaust_capacity || !expected_total_delay.has_value() ||
            *expected_total_delay < route_delay_samples[*route_index]) {
            continue;
        }

        state->cylinders[index] = {
            cylinder_id,
            index,
            *exhaust_port_index,
            *route_index,
            configured.sound_attenuation_linear.value,
            {},
            {},
        };
        auto &route_reference =
            state->exhaust_valve_reference_mass_flow_kg_s[*route_index];
        const double accumulated_reference =
            route_reference + exhaust_valve_reference_mass_flow_kg_s;
        require(report, finite_positive(accumulated_reference),
                ContractIssueCode::invalid_value, path_name + ".bank_exhaust_flow",
                "route exhaust-valve reference mass-flow sum is not finite and "
                "positive");
        if (finite_positive(accumulated_reference)) {
            route_reference = accumulated_reference;
        }
        // Preserve the old rounded total arrival exactly. The cylinder FIFO owns
        // only the residual primary delay; the common route FIFO is applied after
        // the collector fold.
        delay_samples[index] =
            *expected_total_delay - route_delay_samples[*route_index];
    }

    std::vector<bool> accumulation_seen(cylinder_count, false);
    for (std::size_t order = 0; order < cylinder_count; ++order) {
        const auto cylinder_id = source.cylinder_accumulation_order.value[order];
        const auto cylinder_index = find_index(
            engine.cylinders, cylinder_id,
            [](const contract::CylinderSpec &cylinder) { return cylinder.id; });
        const std::string path =
            "engine.physics_profile.excitation.cylinder_accumulation_order[" +
            std::to_string(order) + "]";
        require(
            report,
            cylinder_index.has_value() &&
                !accumulation_seen[cylinder_index.value_or(0U)],
            cylinder_index.has_value() ? ContractIssueCode::duplicate_identity
                                       : ContractIssueCode::dangling_reference,
            path,
            "excitation accumulation order must contain each capture cylinder once");
        if (cylinder_index.has_value()) {
            accumulation_seen[*cylinder_index] = true;
            state->accumulation_order[order] = *cylinder_index;
        }
    }

    for (std::size_t route = 0; route < route_count; ++route) {
        require(report,
                finite_positive(state->exhaust_valve_reference_mass_flow_kg_s[route]),
                ContractIssueCode::invalid_value,
                "engine.physics_profile.excitation.routes[" + std::to_string(route) +
                    "].exhaust_valve_reference_mass_flow",
                "each exhaust route requires a finite positive sum of authored "
                "cylinder exhaust-valve flow-bench capacities");
    }

    if (!report.ok()) {
        return report;
    }
    for (std::size_t cylinder = 0; cylinder < cylinder_count; ++cylinder) {
        state->cylinders[cylinder].delay.history.assign(delay_samples[cylinder], +0.0);
        state->prospective_delays[cylinder] = state->cylinders[cylinder].delay;
        state->cylinders[cylinder].exhaust_flow_delay.history.assign(
            delay_samples[cylinder], +0.0);
        state->prospective_exhaust_flow_delays[cylinder] =
            state->cylinders[cylinder].exhaust_flow_delay;
    }
    for (std::size_t route = 0; route < route_count; ++route) {
        state->routes[route].downstream_delay.history.assign(route_delay_samples[route],
                                                             +0.0);
        state->prospective_route_delays[route] = state->routes[route].downstream_delay;
        state->routes[route].exhaust_flow_downstream_delay.history.assign(
            route_delay_samples[route], +0.0);
        state->prospective_exhaust_flow_route_delays[route] =
            state->routes[route].exhaust_flow_downstream_delay;
    }
    return CapturedSourceExcitationSession{std::move(state)};
}

} // namespace engine_sim_offline::excitation
