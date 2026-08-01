#include "compile/engine_resolver_internal.hpp"

#include "simulation/cycle_accounting_method_registry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

using CamshaftByCylinder =
    std::unordered_map<std::string, const authoring::CamshaftDefinition *>;

[[nodiscard]] contract::LegacyCamshaftProfile
resolve_camshaft(const ModelContext &context,
                 const authoring::CamshaftDefinition &representative_camshaft,
                 const CamshaftByCylinder &camshaft_by_cylinder,
                 const authoring::PortKind port_kind, std::string role,
                 ResolutionEmitter &emitter) {
    contract::LegacyCamshaftProfile resolved;
    resolved.profiles.push_back(
        resolve_cam_shape(context, representative_camshaft, port_kind,
                          role + ".profiles.profile-0", emitter));
    for (const auto &cylinder : context.document.engine.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto &camshaft = *camshaft_by_cylinder.at(semantic);
        const auto &lobe =
            cam_lobe_for_cylinder(context, camshaft, semantic, port_kind);
        resolved.lobes.push_back({
            cylinder_id(context, semantic),
            port_id(context, port_semantic_id(semantic, port_kind)),
            0U,
            emitter.authored(legacy_si_value(lobe.centerline),
                             profile_path("valvetrain." + role + ".lobes." + semantic +
                                          ".crank_center_rad")),
        });
    }
    return resolved;
}

} // namespace

void resolve_valvetrain(const ModelContext &context, ResolutionEmitter &emitter,
                        contract::LowOrderEngineCoreV1 &core) {
    core.valvetrain.intake = resolve_camshaft(
        context, *context.intake_camshaft, context.intake_camshaft_for_cylinder,
        authoring::PortKind::intake, "intake", emitter);
    core.valvetrain.exhaust = resolve_camshaft(
        context, *context.exhaust_camshaft, context.exhaust_camshaft_for_cylinder,
        authoring::PortKind::exhaust, "exhaust", emitter);

    if (context.alternate_intake_camshaft == nullptr ||
        context.alternate_exhaust_camshaft == nullptr) {
        return;
    }

    const auto &vtec = std::get<authoring::VtecValvetrain>(context.valvetrain->kind);
    contract::LegacyVtecAlternateCamProfile alternate;
    alternate.intake =
        resolve_camshaft(context, *context.alternate_intake_camshaft,
                         context.alternate_intake_camshaft_for_cylinder,
                         authoring::PortKind::intake, "alternate.intake", emitter);
    alternate.exhaust =
        resolve_camshaft(context, *context.alternate_exhaust_camshaft,
                         context.alternate_exhaust_camshaft_for_cylinder,
                         authoring::PortKind::exhaust, "alternate.exhaust", emitter);
    const auto activation_base = profile_path("valvetrain.alternate.activation");
    alternate.activation.minimum_engine_speed_rad_s =
        emitter.authored(legacy_si_value(vtec.activation.minimum_engine_speed),
                         activation_base + ".minimum_engine_speed_rad_s");
    alternate.activation.minimum_mean_manifold_pressure_pa_abs =
        emitter.authored(legacy_si_value(vtec.activation.minimum_manifold_pressure_abs),
                         activation_base + ".minimum_mean_manifold_pressure_pa_abs");
    alternate.activation.minimum_throttle_linkage_opening_01 =
        emitter.authored(vtec.activation.minimum_throttle_linkage_opening_01,
                         activation_base + ".minimum_throttle_linkage_opening_01");
    core.valvetrain.alternate.emplace(std::move(alternate));
}

void resolve_ignition_and_fuel(const ModelContext &context, ResolutionEmitter &emitter,
                               contract::LowOrderEngineCoreV1 &core) {
    const auto &engine = context.document.engine;
    std::vector<contract::CylinderId> firing_order;
    firing_order.reserve(engine.cylinders.size());
    for (const auto &event : engine.ignition.firing_order) {
        for (const auto &cylinder : context.cylinders_for_wire.at(event.wire.value)) {
            firing_order.push_back(cylinder_id(context, cylinder));
        }
    }
    const auto ignition_base = profile_path("ignition");
    core.ignition.firing_order =
        emitter.authored(std::move(firing_order), ignition_base + ".firing_order");
    const auto &timing_curve = *context.curves.at(engine.ignition.timing_curve.value);
    core.ignition.timing_curve_triangle_radius_rad_s =
        emitter.authored(legacy_si_value(*timing_curve.triangle_filter_radius),
                         ignition_base + ".timing_curve_triangle_radius_rad_s");
    for (std::size_t index = 0; index < timing_curve.samples.size(); ++index) {
        const auto id = sample_id(index);
        const auto base = ignition_base + ".timing_curve." + id;
        core.ignition.timing_curve.push_back({
            emitter.authored(id, base + ".sample_id"),
            emitter.authored(legacy_si_value(timing_curve.samples[index].input),
                             base + ".angular_speed_rad_s"),
            emitter.authored(legacy_si_value(timing_curve.samples[index].output),
                             base + ".timing_advance_rad"),
        });
    }
    core.ignition.limiter_speed_rpm =
        emitter.authored(rpm_value(engine.ignition.limiter.activation_speed),
                         ignition_base + ".limiter_speed_rpm");
    core.ignition.limiter_hold_s =
        emitter.authored(legacy_si_value(engine.ignition.limiter.cut_duration),
                         ignition_base + ".limiter_hold_s");
    core.ignition.declared_redline_rpm = emitter.authored(
        rpm_value(engine.limits.redline), ignition_base + ".declared_redline_rpm");

    const auto fuel_base = profile_path("fuel");
    core.fuel.fuel_id =
        emitter.authored(context.fuel->id.value, fuel_base + ".fuel_id");
    core.fuel.molecular_mass_kg_per_mol =
        emitter.authored(legacy_si_value(context.fuel->molecular_mass),
                         fuel_base + ".molecular_mass_kg_per_mol");
    core.fuel.energy_density_j_per_kg =
        emitter.authored(legacy_si_value(context.fuel->lower_heating_value),
                         fuel_base + ".energy_density_j_per_kg");
    core.fuel.molecular_afr =
        emitter.authored(context.fuel->stoichiometric_air_fuel_molar_ratio,
                         fuel_base + ".molecular_afr");
    core.fuel.maximum_burning_efficiency_01 =
        emitter.authored(context.fuel->combustion.maximum_efficiency_01,
                         fuel_base + ".maximum_burning_efficiency_01");
    core.fuel.burning_efficiency_randomness_01 =
        emitter.authored(context.fuel->combustion.cycle_variation_01,
                         fuel_base + ".burning_efficiency_randomness_01");
    core.fuel.low_efficiency_attenuation_01 =
        emitter.authored(context.fuel->combustion.low_efficiency_attenuation_01,
                         fuel_base + ".low_efficiency_attenuation_01");
    core.fuel.maximum_turbulence_effect =
        emitter.authored(context.fuel->combustion.maximum_turbulence_effect,
                         fuel_base + ".maximum_turbulence_effect");
    core.fuel.maximum_dilution_effect =
        emitter.authored(context.fuel->combustion.maximum_dilution_effect,
                         fuel_base + ".maximum_dilution_effect");
    core.fuel.lbv_multiplier =
        emitter.derived(1.0, fuel_base + ".lbv_multiplier",
                        derived_method_identity("pristine-spark-fuel-lbv-unity-v1"),
                        {"engine.methods.combustion"});

    const auto &curve =
        *context.curves.at(context.fuel->turbulence_to_flame_speed.value);
    core.fuel.turbulence_to_flame_speed_ratio_triangle_radius = emitter.authored(
        legacy_si_value(*curve.triangle_filter_radius),
        fuel_base + ".turbulence_to_flame_speed_ratio_triangle_radius");
    for (std::size_t index = 0; index < curve.samples.size(); ++index) {
        const auto id = sample_id(index);
        const auto base = fuel_base + ".turbulence_to_flame_speed_ratio." + id;
        core.fuel.turbulence_to_flame_speed_ratio.push_back({
            emitter.authored(id, base + ".sample_id"),
            emitter.authored(legacy_si_value(curve.samples[index].input),
                             base + ".turbulence"),
            emitter.authored(legacy_si_value(curve.samples[index].output),
                             base + ".flame_speed_ratio"),
        });
    }
}

void resolve_excitation(const ModelContext &context, ResolutionEmitter &emitter,
                        contract::LowOrderEngineCoreV1 &core) {
    const auto base = profile_path("reference_excitation");
    const auto constant = [&](auto value, std::string suffix) {
        return emitter.derived(
            value, base + "." + suffix,
            derived_method_identity("legacy-reference-excitation-configuration-v1"),
            {"engine.methods.excitation"});
    };
    core.excitation.reference_atmosphere_pa_abs =
        constant(101325.0, "reference_atmosphere_pa_abs");
    core.excitation.legacy_propagation_speed_m_s =
        constant(343.0, "legacy_propagation_speed_m_s");
    core.excitation.excitation_scale = constant(1600.0, "excitation_scale");
    core.excitation.filtered_speed_threshold_rpm =
        constant(40.0, "filtered_speed_threshold_rpm");
    core.excitation.filtered_speed_exponent =
        constant(std::uint32_t{3}, "filtered_speed_exponent");
    core.excitation.pressure_gains = {
        constant(1.0, "pressure_gains.gauge_static"),
        constant(0.1, "pressure_gains.dynamic_forward"),
        constant(0.1, "pressure_gains.dynamic_reverse"),
    };
    core.excitation.cylinder_count_divisor =
        constant(static_cast<double>(context.document.engine.cylinders.size()),
                 "cylinder_count_divisor");
    core.excitation.inverse_length_exponent = constant(2.0, "inverse_length_exponent");
    core.excitation.delay_rate =
        constant(contract::RationalRateHz{10000U, 1U}, "delay_rate");

    std::vector<contract::CylinderId> accumulation_order;
    accumulation_order.reserve(context.document.engine.cylinders.size());
    for (const auto &cylinder : context.document.engine.cylinders) {
        accumulation_order.push_back(cylinder_id(context, cylinder.id.value));
    }
    core.excitation.cylinder_accumulation_order = emitter.authored(
        std::move(accumulation_order), base + ".cylinder_accumulation_order");

    for (const auto &resolved : ordered_routes(context)) {
        const auto semantic = resolved.route->id.value;
        const auto route_base = base + ".routes." + semantic;
        const auto gas_base = profile_path("gas_path.exhaust_routes." + semantic);
        const auto &presentation = *context.route_presentations.at(semantic);
        const double collector_area =
            legacy_si_value(resolved.exhaust->collector_cross_section_area);
        const double exhaust_length =
            resolved.exhaust->collector_volume
                ? legacy_si_value(*resolved.exhaust->collector_volume) / collector_area
                : legacy_si_value(*resolved.exhaust->collector_length);
        core.excitation.routes.push_back({
            route_id(context, semantic),
            emitter.derived(
                exhaust_length, route_base + ".exhaust_system_length_m",
                derived_method_identity("excitation-route-geometry-copy-v1"),
                {gas_base + ".exhaust_system_length_m"}),
            emitter.authored(presentation.source_gain_linear,
                             route_base + ".audio_volume_linear"),
        });
    }

    constexpr double propagation_speed_m_s = 343.0;
    constexpr contract::RationalRateHz delay_rate{10000U, 1U};
    for (const auto &cylinder : context.document.engine.cylinders) {
        const auto semantic = cylinder.id.value;
        const auto route_semantic =
            context.route_for_exhaust.at(cylinder.exhaust.value);
        const auto cylinder_base = base + ".cylinder_paths." + semantic;
        const auto route_base = base + ".routes." + route_semantic;
        const auto &presentation = *context.cylinder_presentations.at(semantic);
        const double header_length =
            legacy_si_value(cylinder.exhaust_header_primary_length);
        const auto &exhaust = *context.exhausts.at(cylinder.exhaust.value);
        const double collector_area =
            legacy_si_value(exhaust.collector_cross_section_area);
        const double exhaust_length =
            exhaust.collector_volume
                ? legacy_si_value(*exhaust.collector_volume) / collector_area
                : legacy_si_value(*exhaust.collector_length);
        const double requested_samples =
            ((header_length + exhaust_length) / propagation_speed_m_s) *
            (static_cast<double>(delay_rate.numerator) /
             static_cast<double>(delay_rate.denominator));
        core.excitation.cylinder_paths.push_back({
            cylinder_id(context, semantic),
            route_id(context, route_semantic),
            emitter.authored(header_length, cylinder_base + ".header_primary_length_m"),
            emitter.authored(presentation.gain_linear,
                             cylinder_base + ".sound_attenuation_linear"),
            emitter.derived(
                static_cast<std::uint32_t>(std::round(requested_samples)),
                cylinder_base + ".resolved_delay_samples",
                derived_method_identity("legacy-propagation-delay-round-v1"),
                {
                    cylinder_base + ".header_primary_length_m",
                    route_base + ".exhaust_system_length_m",
                    base + ".legacy_propagation_speed_m_s",
                    base + ".delay_rate",
                }),
        });
    }
}

void resolve_operating_accounting(const ModelContext &context,
                                  ResolutionEmitter &emitter,
                                  contract::LowOrderOperatingPointV1Profile &profile) {
    const auto &loss =
        std::get<authoring::ChenFlynnLossDefinition>(context.document.engine.losses);
    const auto base = profile_path("aggregate_loss");
    profile.aggregate_loss = {
        emitter.authored(legacy_si_value(loss.constant_fmep) / 100000.0,
                         base + ".constant_fmep_bar"),
        emitter.authored(loss.peak_pressure_coefficient,
                         base + ".peak_pressure_coefficient"),
        emitter.authored(legacy_si_value(loss.mean_piston_speed_coefficient) / 100000.0,
                         base + ".mean_piston_speed_coefficient_bar_s_per_m"),
        emitter.authored(legacy_si_value(loss.mean_piston_speed_squared_coefficient) /
                             100000.0,
                         base + ".mean_piston_speed_squared_coefficient_bar_s2_per_m2"),
        emitter.authored(legacy_si_value(loss.required_oil_temperature),
                         base + ".required_oil_temperature_k"),
        emitter.derived(contract::friction_pump_and_accessory_torque_term_mask(),
                        base + ".included_terms",
                        derived_method_identity("chen-flynn-included-torque-terms-v1"),
                        {"engine.methods.losses"}),
    };

    const auto accessory_base = profile_path("accessory_configuration");
    const auto asset_index =
        context.assets.accessory_by_id.at(context.accessory_configuration->id.value);
    const auto &asset = context.assets.values[asset_index];
    profile.accessory_configuration = {
        emitter.authored(context.accessory_configuration->id.value,
                         accessory_base + ".configuration_id"),
        emitter.authored(asset.content_sha256, accessory_base + ".content_sha256"),
    };

    const auto starter_base = profile_path("starter");
    if (const auto *cranking =
            std::get_if<authoring::CrankingStarter>(&context.document.engine.starter)) {
        profile.starter = {
            emitter.authored(contract::StarterCapabilityType::cranking,
                             starter_base + ".type"),
            emitter.authored(legacy_si_value(cranking->torque),
                             starter_base + ".maximum_torque_nm"),
            emitter.authored(cranking->target_speed.unit == "rad/s"
                                 ? cranking->target_speed.value
                                 : legacy_si_value(cranking->target_speed),
                             starter_base + ".target_speed_rad_s"),
            emitter.derived(
                contract::torque_term_mask(contract::TorqueTerm::starter),
                starter_base + ".included_terms",
                derived_method_identity("starter-capability-included-torque-terms-v1"),
                {starter_base + ".type"}),
        };
    } else {
        profile.starter = {
            emitter.authored(contract::StarterCapabilityType::mechanically_disengaged,
                             starter_base + ".type"),
            emitter.derived(0.0, starter_base + ".maximum_torque_nm",
                            derived_method_identity(
                                "mechanically-disengaged-starter-zero-capability-v1"),
                            {starter_base + ".type"}),
            emitter.derived(0.0, starter_base + ".target_speed_rad_s",
                            derived_method_identity(
                                "mechanically-disengaged-starter-zero-capability-v1"),
                            {starter_base + ".type"}),
            emitter.derived(
                contract::torque_term_mask(contract::TorqueTerm::starter),
                starter_base + ".included_terms",
                derived_method_identity("starter-capability-included-torque-terms-v1"),
                {starter_base + ".type"}),
        };
    }
    profile.cycle_quadrature = emitter.derived(
        simulation::implemented_cycle_accounting_method_identities().cycle_quadrature,
        profile_path("cycle_quadrature"),
        derived_method_identity("implemented-cycle-quadrature-selection-v1"),
        {"engine.profile_id"});
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
