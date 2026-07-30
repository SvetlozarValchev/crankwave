#include "compile/engine_resolver_internal.hpp"

#include "presentation/presentation_method_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

// This is the incumbent seed-derivation namespace carried by the admitted
// randomness method. It preserves derivation bytes; it is not an engine selector.
inline constexpr std::string_view kIncumbentSeedNamespace =
    "baked.loaded_acceleration";

template <class Id>
[[nodiscard]] Id runtime_id(const IdNamespace &ids,
                            std::string_view semantic_id) {
    return Id{ids.by_semantic_id.at(std::string{semantic_id})};
}

[[nodiscard]] std::string audio_evidence_id(std::string_view semantic_id) {
    return "engine.asset.audio." + std::string{semantic_id};
}

[[nodiscard]] std::vector<const authoring::AudioAssetDefinition *>
ordered_assets(const ModelContext &context) {
    std::vector<const authoring::AudioAssetDefinition *> result;
    result.reserve(context.document.presentation.assets.size());
    for (const auto &asset : context.document.presentation.assets) {
        result.push_back(&asset);
    }
    std::ranges::sort(result, [&](const auto *left, const auto *right) {
        return runtime_id<contract::AudioAssetId>(
                   context.ids.audio_assets, left->id.value)
                   .value <
               runtime_id<contract::AudioAssetId>(
                   context.ids.audio_assets, right->id.value)
                   .value;
    });
    return result;
}

[[nodiscard]] std::vector<const authoring::RoutePresentation *>
ordered_route_presentations(const ModelContext &context) {
    std::vector<const authoring::RoutePresentation *> result;
    result.reserve(context.document.presentation.routes.size());
    for (const auto &route : context.document.presentation.routes) {
        result.push_back(&route);
    }
    std::ranges::sort(result, [&](const auto *left, const auto *right) {
        return runtime_id<contract::RouteId>(context.ids.routes,
                                             left->route.value)
                   .value <
               runtime_id<contract::RouteId>(context.ids.routes,
                                             right->route.value)
                   .value;
    });
    return result;
}

[[nodiscard]] std::vector<contract::RouteId>
resolved_bus_routes(const ModelContext &context,
                    const authoring::AudioBusDefinition &bus) {
    std::vector<contract::RouteId> result;
    result.reserve(bus.routes.size());
    for (const auto &route : bus.routes) {
        result.push_back(runtime_id<contract::RouteId>(
            context.ids.routes, route.value));
    }
    return result;
}

[[nodiscard]] std::vector<contract::RouteId>
resolved_audition_routes(const ModelContext &context) {
    std::vector<contract::RouteId> result;
    for (const auto &bus_reference :
         context.document.presentation.audition.buses) {
        const auto bus = std::ranges::find(
            context.document.presentation.buses, bus_reference.value,
            [](const auto &candidate) -> const std::string & {
                return candidate.id.value;
            });
        for (const auto &route : bus->routes) {
            result.push_back(runtime_id<contract::RouteId>(
                context.ids.routes, route.value));
        }
    }
    return result;
}

} // namespace

void assemble_presentation(
    const ModelContext &context, ResolutionEmitter &emitter,
    contract::PresentationCalibration &calibration,
    contract::ResolvedRandomnessPolicy &randomness,
    std::vector<ResolvedAudioBusDescriptor> &audio_buses) {
    const auto &source = context.document.presentation;
    calibration.schema_version = 2U;
    calibration.calibration_id = context.calibration_id;
    calibration.engine_profile_id = emitter.derived(
        context.profile_id, "presentation.engine_profile_id",
        derived_method_identity("presentation-engine-profile-binding-v1"),
        {"engine.profile_id"});

    const auto &methods =
        presentation::implemented_presentation_method_identities();
    const auto method = [&](const contract::MethodIdentity &identity,
                            std::string path) {
        return emitter.derived(
            identity, std::move(path),
            derived_method_identity("implemented-presentation-selection-v1"),
            {"presentation.engine_profile_id"});
    };
    calibration.methods = {
        method(methods.reconstruction,
               "presentation.methods.reconstruction"),
        method(methods.conditioning, "presentation.methods.conditioning"),
        method(methods.impulse_response_conversion,
               "presentation.methods.impulse_response_conversion"),
        method(methods.convolution, "presentation.methods.convolution"),
        method(methods.publication, "presentation.methods.publication"),
        method(methods.audition_mix, "presentation.methods.audition_mix"),
    };

    calibration.conditioning = {
        emitter.authored(source.conditioning.jitter_scale,
                         "presentation.conditioning.jitter_scale"),
        emitter.authored(
            legacy_si_value(
                source.conditioning.jitter_modulation_cutoff_frequency),
            "presentation.conditioning.jitter_modulation_cutoff_hz"),
        emitter.authored(source.conditioning.derivative_mix_01,
                         "presentation.conditioning.derivative_mix_01"),
        emitter.authored(source.conditioning.air_noise_mix_01,
                         "presentation.conditioning.air_noise_mix_01"),
        emitter.authored(
            legacy_si_value(source.conditioning.air_noise_cutoff_frequency),
            "presentation.conditioning.air_noise_cutoff_hz"),
    };

    for (const auto *asset_definition : ordered_assets(context)) {
        const auto semantic = asset_definition->id.value;
        const auto base = "presentation.assets." + semantic;
        const auto verified_index =
            context.assets.audio_by_id.at(semantic);
        const auto &verified = context.assets.values[verified_index];
        calibration.assets.push_back({
            runtime_id<contract::AudioAssetId>(context.ids.audio_assets,
                                               semantic),
            emitter.authored(semantic, base + ".semantic_id"),
            emitter.derived(
                audio_evidence_id(semantic), base + ".evidence_source_id",
                derived_method_identity("content-evidence-id-v1"),
                {base + ".semantic_id"}),
            emitter.authored(verified.content_sha256,
                             base + ".content_sha256"),
            emitter.derived(
                context.assets.audio_media_by_id.at(semantic),
                base + ".media",
                derived_method_identity("pcm16-wave-media-inspection-v1"),
                {base + ".content_sha256"}),
        });
    }

    for (const auto *route : ordered_route_presentations(context)) {
        const auto semantic = route->route.value;
        const auto base = "presentation.routes." + semantic;
        calibration.routes.push_back({
            runtime_id<contract::RouteId>(context.ids.routes, semantic),
            runtime_id<contract::AudioAssetId>(
                context.ids.audio_assets,
                route->impulse_response->value),
            emitter.authored(route->impulse_response_gain_linear,
                             base + ".impulse_response_gain_linear"),
            emitter.authored(route->wet_mix_01, base + ".wet_mix_01"),
        });
    }

    calibration.publication.calibration_gain_linear =
        emitter.authored(source.publication_gain_linear,
                         "presentation.publication.calibration_gain_linear");
    calibration.audition = {
        emitter.authored(resolved_audition_routes(context),
                         "presentation.audition.selected_routes"),
        emitter.authored(source.audition.monitoring_gain_linear,
                         "presentation.audition.monitoring_gain_linear"),
        emitter.authored(legacy_si_value(source.audition.fade_in),
                         "presentation.audition.fade_in_duration_s"),
        emitter.authored(legacy_si_value(source.audition.fade_out),
                         "presentation.audition.fade_out_duration_s"),
    };

    const auto selected_bus_id = source.audition.buses.front().value;
    audio_buses.reserve(source.buses.size());
    for (const auto &bus : source.buses) {
        const bool audition = bus.id.value == selected_bus_id;
        audio_buses.push_back({
            bus.id.value,
            audition ? "master.engine.audition" : "master.engine.raw",
            audition ? contract::OutputBusKind::master_engine_audition
                     : contract::OutputBusKind::master_engine_raw,
            resolved_bus_routes(context, bus),
            bus.gain_linear,
            audition ? contract::AudioSampleEncoding::pcm_s24le
                     : contract::AudioSampleEncoding::float32le,
            bus.publish,
            false,
        });
    }
    std::ranges::sort(audio_buses, [](const auto &left, const auto &right) {
        return std::tie(left.semantic_id, left.authored_id) <
               std::tie(right.semantic_id, right.authored_id);
    });

    randomness.seed_namespace_id = emitter.derived(
        std::string{kIncumbentSeedNamespace},
        "randomness.seed_namespace_id",
        derived_method_identity("engine-seed-namespace-selection-v1"),
        {"engine.profile_id"});
    randomness.generator = emitter.derived(
        contract::pcg32_generator_method_identity(),
        "randomness.generator",
        derived_method_identity("implemented-random-generator-selection-v1"),
        {"randomness.seed_namespace_id"});
    randomness.derivation = emitter.derived(
        contract::component_seed_derivation_method_identity(),
        "randomness.derivation",
        derived_method_identity("implemented-seed-derivation-selection-v1"),
        {"randomness.seed_namespace_id"});
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
