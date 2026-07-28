#include "excitation/captured_exhaust_excitation.hpp"

#include "excitation/captured_exhaust_excitation_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::excitation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

constexpr contract::Sha256Digest kLegacyLowOrderV1ConfigurationSha256{{
    0x43, 0x54, 0x41, 0x89, 0x0e, 0x0a, 0x5f, 0x8d, 0x01, 0xe8, 0x19,
    0x95, 0xf6, 0x4f, 0x33, 0xd4, 0xc5, 0x54, 0x14, 0x4f, 0x5b, 0x14,
    0x36, 0x89, 0x5e, 0x68, 0x16, 0xf6, 0xdb, 0x85, 0xe3, 0x4c,
}};

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
    const contract::ResolvedValue<contract::MethodIdentity> &method) noexcept {
    return method.value.id == "legacy_low_order_v1" && method.value.version == 1U &&
           method.value.configuration_sha256 == kLegacyLowOrderV1ConfigurationSha256;
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

CapturedExhaustExcitationCompileResult
compile_captured_exhaust_excitation_session(const contract::EngineSpec &engine) {
    ValidationReport report;
    const auto *profile =
        std::get_if<contract::LegacyLowOrderV1Profile>(&engine.physics_profile);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "captured exhaust excitation requires a LegacyLowOrderV1Profile");
    require(report, engine.id.valid(), ContractIssueCode::invalid_value, "engine.id",
            "captured exhaust excitation requires a valid engine identity");
    require(report, exact_excitation_method(engine.methods.excitation),
            ContractIssueCode::unsupported_value, "engine.methods.excitation",
            "captured exhaust excitation requires the exact admitted "
            "legacy_low_order_v1 method identity");
    require(report, engine.cylinders.size() == kCapturedExcitationCylinderCount,
            ContractIssueCode::inconsistent_shape, "engine.cylinders",
            "captured exhaust excitation requires exactly six cylinders");
    require(report, engine.routes.size() == kCapturedExcitationRouteCount,
            ContractIssueCode::inconsistent_shape, "engine.routes",
            "captured exhaust excitation requires exactly two routes");
    if (profile == nullptr ||
        engine.cylinders.size() != kCapturedExcitationCylinderCount ||
        engine.routes.size() != kCapturedExcitationRouteCount) {
        return report;
    }

    const auto &source = profile->excitation;
    require(report, source.delay_rate.value == contract::RationalRateHz{10000, 1},
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.excitation.delay_rate",
            "captured exhaust excitation requires the exact 10000/1 Hz delay rate");
    require(report, source.filtered_speed_exponent.value == 3U,
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.excitation.filtered_speed_exponent",
            "captured exhaust excitation admits only the exact cubic speed ramp");
    require(report, source.inverse_length_exponent.value == 2.0,
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.excitation.inverse_length_exponent",
            "captured exhaust excitation admits only inverse-square distance");
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
            "captured exhaust excitation parameters are outside their finite domains");
    require(report,
            same_binary64(source.cylinder_count_divisor.value,
                          static_cast<double>(engine.cylinders.size())),
            ContractIssueCode::inconsistent_semantics,
            "engine.physics_profile.excitation.cylinder_count_divisor",
            "excitation divisor must equal the compiled cylinder count");
    require(report, source.cylinder_paths.size() == kCapturedExcitationCylinderCount,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.cylinder_paths",
            "captured exhaust excitation requires exactly one path per cylinder");
    require(report,
            source.cylinder_accumulation_order.value.size() ==
                kCapturedExcitationCylinderCount,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.cylinder_accumulation_order",
            "captured exhaust excitation requires a complete six-cylinder order");
    require(report, source.routes.size() == kCapturedExcitationRouteCount,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.excitation.routes",
            "captured exhaust excitation requires exactly two route records");
    require(report,
            profile->gas_path.exhaust_routes.size() == kCapturedExcitationRouteCount,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.exhaust_routes",
            "captured exhaust excitation requires exactly two gas-path routes");
    require(report,
            profile->mechanism.cylinders.size() == kCapturedExcitationCylinderCount,
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "captured exhaust excitation requires six mechanism cylinders");
    if (!report.ok()) {
        return report;
    }

    auto state = std::make_unique<detail::CapturedExhaustExcitationState>();
    state->engine_id = engine.id;
    state->model_id = engine.methods.excitation.value.id;
    state->profile_id = engine.profile_id.value;
    state->reference_atmosphere_pa_abs = source.reference_atmosphere_pa_abs.value;
    state->excitation_scale = source.excitation_scale.value;
    state->filtered_speed_threshold_rpm = source.filtered_speed_threshold_rpm.value;
    state->gauge_static_gain = source.pressure_gains.gauge_static.value;
    state->dynamic_forward_gain = source.pressure_gains.dynamic_forward.value;
    state->dynamic_reverse_gain = source.pressure_gains.dynamic_reverse.value;
    state->cylinder_count_divisor = source.cylinder_count_divisor.value;

    std::array<bool, kCapturedExcitationRouteCount> gas_route_seen{};
    for (std::size_t index = 0; index < kCapturedExcitationRouteCount; ++index) {
        const auto &declared = engine.routes[index];
        const auto &configured = source.routes[index];
        const auto gas_route_index =
            find_index(profile->gas_path.exhaust_routes, configured.route_id,
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
            const auto &gas_route = profile->gas_path.exhaust_routes[*gas_route_index];
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

        state->route_layout[index] = {
            declared.id,
            declared.kind.value,
            declared.source_volume_id,
            declared.default_parent_route_id,
            declared.emitter_anchor_id.has_value()
                ? std::optional<std::string>{declared.emitter_anchor_id->value}
                : std::nullopt,
        };
        state->route_ids[index] = configured.route_id;
        state->routes[index] = {
            configured.route_id,
            configured.exhaust_system_length_m.value,
            configured.audio_volume_linear.value,
        };
    }

    std::array<bool, kCapturedExcitationCylinderCount> path_seen{};
    std::array<bool, kCapturedExcitationCylinderCount> mechanism_seen{};
    std::array<std::uint32_t, kCapturedExcitationCylinderCount> delay_samples{};
    for (std::size_t index = 0; index < kCapturedExcitationCylinderCount; ++index) {
        const auto cylinder_id = engine.cylinders[index].id;
        state->cylinder_ids[index] = cylinder_id;
        const auto path_index =
            find_index(source.cylinder_paths, cylinder_id,
                       [](const contract::LegacyExcitationCylinderPath &path) {
                           return path.cylinder_id;
                       });
        const auto mechanism_index =
            find_index(profile->mechanism.cylinders, cylinder_id,
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
        const auto &mechanism = profile->mechanism.cylinders[*mechanism_index];
        const auto route_index =
            find_index(source.routes, configured.route_id,
                       [](const contract::LegacyExcitationRoute &route) {
                           return route.route_id;
                       });
        require(report, route_index.has_value(), ContractIssueCode::dangling_reference,
                path_name + ".route_id",
                "excitation cylinder path route does not resolve");
        std::optional<std::uint32_t> expected_delay;
        if (route_index.has_value()) {
            expected_delay = resolve_delay_samples(
                configured.header_primary_length_m.value,
                source.routes[*route_index].exhaust_system_length_m.value,
                source.legacy_propagation_speed_m_s.value, source.delay_rate.value);
        }
        require(report,
                finite_nonnegative(configured.header_primary_length_m.value) &&
                    finite_nonnegative(configured.sound_attenuation_linear.value) &&
                    same_binary64(configured.header_primary_length_m.value,
                                  mechanism.parameters.header_primary_length_m.value) &&
                    configured.route_id == mechanism.topology.exhaust_route_id &&
                    expected_delay.has_value() &&
                    configured.resolved_delay_samples.value == *expected_delay,
                ContractIssueCode::inconsistent_semantics, path_name,
                "excitation path geometry, route, or resolved delay is incoherent");
        if (!route_index.has_value()) {
            continue;
        }

        state->cylinders[index] = {
            cylinder_id, index, *route_index, configured.sound_attenuation_linear.value,
            {},
        };
        delay_samples[index] = configured.resolved_delay_samples.value;
    }

    std::array<bool, kCapturedExcitationCylinderCount> accumulation_seen{};
    for (std::size_t order = 0; order < kCapturedExcitationCylinderCount; ++order) {
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

    if (!report.ok()) {
        return report;
    }
    for (std::size_t cylinder = 0; cylinder < kCapturedExcitationCylinderCount;
         ++cylinder) {
        state->cylinders[cylinder].delay.history.assign(delay_samples[cylinder], +0.0);
        state->prospective_delays[cylinder] = state->cylinders[cylinder].delay;
    }
    return CapturedExhaustExcitationSession{std::move(state)};
}

} // namespace engine_sim_offline::excitation
