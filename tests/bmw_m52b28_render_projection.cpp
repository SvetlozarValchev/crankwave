#include "bmw_m52b28_migration_test_support.hpp"

#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace engine_sim_offline::test::bmw_m52b28_migration {
namespace {

void add_rates(ExecutionProjection &out, const std::string &path,
               const contract::RenderRates &rates) {
    out.rate(path + ".physics", rates.physics);
    out.rate(path + ".capture", rates.capture);
    out.rate(path + ".source_processing", rates.source_processing);
    out.rate(path + ".acoustic", rates.acoustic);
    out.rate(path + ".delivery", rates.delivery);
}

} // namespace

ExecutionProjection
project_presentation(const contract::PresentationCalibration &presentation,
                     const contract::EngineSpec &engine) {
    ExecutionProjection out;
    const ExecutionNames names{engine};
    std::unordered_map<std::uint32_t, std::string> audio_names;
    for (const auto &asset : presentation.assets) {
        audio_names.emplace(asset.id.value, asset.semantic_id.value);
    }
    const auto audio_name = [&](const contract::AudioAssetId id) {
        const auto found = audio_names.find(id.value);
        if (found == audio_names.end()) {
            throw std::runtime_error{"unmapped presentation audio-asset ID"};
        }
        return found->second;
    };
    out.method("presentation.methods.reconstruction",
               presentation.methods.reconstruction.value);
    out.method("presentation.methods.conditioning",
               presentation.methods.conditioning.value);
    out.method("presentation.methods.impulse_response_conversion",
               presentation.methods.impulse_response_conversion.value);
    out.method("presentation.methods.convolution",
               presentation.methods.convolution.value);
    out.method("presentation.methods.publication",
               presentation.methods.publication.value);
    out.method("presentation.methods.audition_mix",
               presentation.methods.audition_mix.value);
    out.binary64("presentation.conditioning.jitter_scale",
                 presentation.conditioning.jitter_scale.value);
    out.binary64("presentation.conditioning.jitter_modulation_cutoff_hz",
                 presentation.conditioning.jitter_modulation_cutoff_hz.value);
    out.binary64("presentation.conditioning.derivative_mix_01",
                 presentation.conditioning.derivative_mix_01.value);
    out.binary64("presentation.conditioning.air_noise_mix_01",
                 presentation.conditioning.air_noise_mix_01.value);
    out.binary64("presentation.conditioning.air_noise_cutoff_hz",
                 presentation.conditioning.air_noise_cutoff_hz.value);
    out.count("presentation.assets.count", presentation.assets.size());
    const auto assets = ordered_by_name(
        presentation.assets, [&](const contract::AudioAssetSpec &asset) {
            return audio_name(asset.id);
        });
    for (std::size_t index = 0; index < assets.size(); ++index) {
        const auto base =
            "presentation.assets[" + std::to_string(index) + "]";
        const auto &asset = *assets[index];
        out.text(base + ".role", audio_name(asset.id));
        out.digest(base + ".content_sha256", asset.content_sha256.value);
        out.enumeration(base + ".media.encoding", asset.media.value.encoding);
        out.enumeration(base + ".media.channel_layout",
                        asset.media.value.channel_layout);
        out.rate(base + ".media.sample_rate", asset.media.value.sample_rate);
        out.integer(base + ".media.frame_count", asset.media.value.frame_count);
    }
    out.count("presentation.routes.count", presentation.routes.size());
    const auto routes = ordered_by_name(
        presentation.routes, [&](const contract::RoutePresentation &route) {
            return names.route(route.route_id);
        });
    for (std::size_t index = 0; index < routes.size(); ++index) {
        const auto base =
            "presentation.routes[" + std::to_string(index) + "]";
        const auto &route = *routes[index];
        out.text(base + ".route", names.route(route.route_id));
        out.text(base + ".impulse_response_asset",
                 audio_name(route.impulse_response_asset_id));
        out.binary64(base + ".impulse_response_gain_linear",
                     route.impulse_response_gain_linear.value);
        out.binary64(base + ".wet_mix_01", route.wet_mix_01.value);
    }
    out.binary64("presentation.publication.calibration_gain_linear",
                 presentation.publication.calibration_gain_linear.value);
    out.count("presentation.audition.selected_routes.count",
              presentation.audition.selected_routes.value.size());
    for (std::size_t index = 0;
         index < presentation.audition.selected_routes.value.size(); ++index) {
        out.text("presentation.audition.selected_routes[" +
                     std::to_string(index) + "]",
                 names.route(
                     presentation.audition.selected_routes.value[index]));
    }
    out.binary64("presentation.audition.monitoring_gain_linear",
                 presentation.audition.monitoring_gain_linear.value);
    out.binary64("presentation.audition.fade_in_duration_s",
                 presentation.audition.fade_in_duration_s.value);
    out.binary64("presentation.audition.fade_out_duration_s",
                 presentation.audition.fade_out_duration_s.value);
    return out;
}

ExecutionProjection project_scenario(const contract::RenderScenario &scenario) {
    ExecutionProjection out;
    out.binary64("scenario.ambient.pressure_pa_abs",
                 scenario.ambient.pressure_pa_abs.value);
    out.binary64("scenario.ambient.temperature_k",
                 scenario.ambient.temperature_k.value);
    out.binary64("scenario.ambient.relative_humidity_01",
                 scenario.ambient.relative_humidity_01.value);
    // Scenario/fuel/profile/quality IDs are product identity, not executor input.
    out.binary64("scenario.fuel.lower_heating_value_j_per_kg",
                 scenario.fuel.lower_heating_value_j_per_kg.value);
    out.binary64("scenario.fuel.stoichiometric_air_fuel_mass_ratio",
                 scenario.fuel.stoichiometric_air_fuel_mass_ratio.value);
    out.binary64("scenario.initial_thermal_state.gas_temperature_k",
                 scenario.initial_thermal_state.gas_temperature_k.value);
    out.binary64("scenario.initial_thermal_state.wall_temperature_k",
                 scenario.initial_thermal_state.wall_temperature_k.value);
    out.binary64("scenario.initial_thermal_state.coolant_temperature_k",
                 scenario.initial_thermal_state.coolant_temperature_k.value);
    out.binary64("scenario.initial_thermal_state.oil_temperature_k",
                 scenario.initial_thermal_state.oil_temperature_k.value);
    out.binary64("scenario.crankcase.pressure_pa_abs",
                 scenario.crankcase.pressure_pa_abs.value);
    out.binary64("scenario.crankcase.temperature_k",
                 scenario.crankcase.temperature_k.value);

    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(
            &scenario.preparation);
    if (preparation == nullptr) {
        throw std::runtime_error{
            "BMW migration comparison requires fixed-horizon preparation"};
    }
    out.method("scenario.preparation.method", preparation->method.value);
    out.binary64("scenario.preparation.fixed_preparation_horizon_s",
                 preparation->fixed_preparation_horizon_s.value);
    out.integer("scenario.preparation.trailing_complete_cycle_count",
                preparation->trailing_complete_cycle_count.value);
    out.count("scenario.operating_state.count",
              scenario.operating_state.value.size());
    for (std::size_t index = 0;
         index < scenario.operating_state.value.size(); ++index) {
        const auto base =
            "scenario.operating_state[" + std::to_string(index) + "]";
        const auto &point = scenario.operating_state.value[index];
        out.binary64(base + ".time_s", point.time_s);
        out.boolean(base + ".ignition_enabled",
                    point.state.ignition_enabled);
        out.boolean(base + ".fuel_enabled", point.state.fuel_enabled);
        out.boolean(base + ".starter_enabled", point.state.starter_enabled);
        out.boolean(base + ".dyno_enabled", point.state.dyno_enabled);
        out.boolean(base + ".limiter_enabled", point.state.limiter_enabled);
    }
    out.binary64("scenario.total_duration_s", scenario.total_duration_s.value);
    out.binary64("scenario.audible_start_s", scenario.audible_start_s.value);
    out.binary64("scenario.audible_duration_s",
                 scenario.audible_duration_s.value);
    add_rates(out, "scenario.rates", scenario.rates);
    out.integer("scenario.quality.version", scenario.quality.value.version);
    out.integer("scenario.quality.capture_block_capacity_frames",
                scenario.quality.value.capture_block_capacity_frames);
    out.integer("scenario.quality.event_journal_capacity_records",
                scenario.quality.value.event_journal_capacity_records);
    out.integer("scenario.public_seed", scenario.public_seed.value);

    const auto *dyno = std::get_if<contract::InertialDyno>(&scenario.mode);
    if (dyno == nullptr) {
        throw std::runtime_error{
            "BMW migration comparison requires inertial-dyno mode"};
    }
    out.binary64("scenario.mode.initial_engine_speed_rpm",
                 dyno->initial_engine_speed_rpm.value);
    out.binary64("scenario.mode.initial_theta_rad",
                 dyno->initial_theta_rad.value);
    out.binary64("scenario.mode.equivalent_inertia_kg_m2",
                 dyno->equivalent_inertia_kg_m2.value);
    out.enumeration("scenario.mode.throttle_01.interpolation",
                    dyno->throttle_01.interpolation);
    out.count("scenario.mode.throttle_01.points.count",
              dyno->throttle_01.points.size());
    for (std::size_t index = 0; index < dyno->throttle_01.points.size();
         ++index) {
        const auto base = "scenario.mode.throttle_01.points[" +
                          std::to_string(index) + "]";
        out.binary64(base + ".time_s", dyno->throttle_01.points[index].time_s);
        out.binary64(base + ".value", dyno->throttle_01.points[index].value);
    }
    out.count("scenario.mode.brake_curve.count", dyno->brake_curve.size());
    for (std::size_t index = 0; index < dyno->brake_curve.size(); ++index) {
        const auto base =
            "scenario.mode.brake_curve[" + std::to_string(index) + "]";
        out.binary64(base + ".angular_speed_rad_s",
                     dyno->brake_curve[index].angular_speed_rad_s);
        out.binary64(base + ".resisting_torque_nm",
                     dyno->brake_curve[index].resisting_torque_nm);
    }
    out.method("scenario.mode.crank_dynamics_method",
               dyno->crank_dynamics_method.value);
    out.binary64("scenario.mode.target_engine_speed_rpm",
                 dyno->target_engine_speed_rpm.value);
    out.method("scenario.mode.brake_torque_method",
               dyno->brake_torque_method.value);
    return out;
}

ExecutionProjection
project_randomness(const contract::ResolvedRandomnessPolicy &policy,
                   const contract::RandomPlan &plan,
                   const contract::EngineSpec &engine) {
    ExecutionProjection out;
    const ExecutionNames names{engine};
    out.text("randomness.seed_namespace_id",
             policy.seed_namespace_id.value);
    out.method("randomness.generator", policy.generator.value);
    out.method("randomness.derivation", policy.derivation.value);
    out.method("random_plan.generator", plan.generator);
    out.integer("random_plan.public_seed", plan.public_seed);
    out.method("random_plan.derivation", plan.derivation);
    out.count("random_plan.component_seeds.count",
              plan.component_seeds.size());
    for (std::size_t index = 0; index < plan.component_seeds.size(); ++index) {
        const auto base =
            "random_plan.component_seeds[" + std::to_string(index) + "]";
        const auto &seed = plan.component_seeds[index];
        out.enumeration(base + ".kind", seed.kind);
        out.boolean(base + ".cylinder_id.present",
                    seed.cylinder_id.has_value());
        if (seed.cylinder_id) {
            out.text(base + ".cylinder",
                     names.cylinder(*seed.cylinder_id));
        }
        out.boolean(base + ".route_id.present", seed.route_id.has_value());
        if (seed.route_id) {
            out.text(base + ".route", names.route(*seed.route_id));
        }
        out.integer(base + ".initial_state", seed.initial_state);
        out.integer(base + ".stream", seed.stream);
    }
    return out;
}

} // namespace engine_sim_offline::test::bmw_m52b28_migration
