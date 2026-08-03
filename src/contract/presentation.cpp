#include "engine_sim_offline/contract/presentation.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

template <class T>
void validate_authored(ValidationReport &report, const AuthoredValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_authored_value(report, value, provenance, path);
}

template <class T>
void validate_resolved(ValidationReport &report, const ResolvedValue<T> &value,
                       const ProvenanceLedger &provenance, const std::string &path) {
    detail::validate_resolved_value(report, value, provenance, path);
}

bool known(AudioSampleEncoding encoding) noexcept {
    switch (encoding) {
    case AudioSampleEncoding::pcm_s16le:
    case AudioSampleEncoding::float32le:
    case AudioSampleEncoding::pcm_s24le:
        return true;
    }
    return false;
}

bool known(AudioChannelLayout layout) noexcept {
    return layout == AudioChannelLayout::mono;
}

void validate_media(ValidationReport &report, const AudioMediaContract &media,
                    const std::string &path) {
    detail::append_prefixed(report, validate(media.sample_rate), path + ".sample_rate");
    detail::require(report, known(media.encoding), ContractIssueCode::unsupported_value,
                    path + ".encoding", "audio encoding is not recognized");
    detail::require(report, known(media.channel_layout),
                    ContractIssueCode::unsupported_value, path + ".channel_layout",
                    "audio channel layout is not recognized");
    detail::require(report, media.frame_count > 0, ContractIssueCode::invalid_value,
                    path + ".frame_count",
                    "audio asset must contain at least one frame");
}

void validate_selection(ValidationReport &report, const MethodSelection &selection,
                        const std::string &path) {
    detail::require(report, is_valid_semantic_id(selection.id),
                    ContractIssueCode::invalid_value, path + ".id",
                    "method selection ID must be canonical");
    detail::require(report, selection.version > 0, ContractIssueCode::invalid_value,
                    path + ".version", "method selection version must be positive");
}

template <class Function>
void for_each_authored_method(const AuthoredPresentationMethods &methods,
                              Function function) {
    function(methods.reconstruction, "presentation.methods.reconstruction");
    function(methods.conditioning, "presentation.methods.conditioning");
    function(methods.impulse_response_conversion,
             "presentation.methods.impulse_response_conversion");
    function(methods.convolution, "presentation.methods.convolution");
    function(methods.publication, "presentation.methods.publication");
    function(methods.audition_mix, "presentation.methods.audition_mix");
}

template <class Function>
void for_each_method(const PresentationMethods &methods, Function function) {
    function(methods.reconstruction, "presentation.methods.reconstruction");
    function(methods.conditioning, "presentation.methods.conditioning");
    function(methods.impulse_response_conversion,
             "presentation.methods.impulse_response_conversion");
    function(methods.convolution, "presentation.methods.convolution");
    function(methods.publication, "presentation.methods.publication");
    function(methods.audition_mix, "presentation.methods.audition_mix");
}

const PresentationSourceRouteContext *
find_route(const PresentationValidationContext &context, RouteId id) {
    const auto iterator = std::ranges::find(context.routes, id,
                                            &PresentationSourceRouteContext::route_id);
    return iterator == context.routes.end() ? nullptr : &*iterator;
}

const AudioAssetSpec *find_asset(const PresentationCalibration &calibration,
                                 AudioAssetId id) {
    const auto iterator =
        std::ranges::find(calibration.assets, id, &AudioAssetSpec::id);
    return iterator == calibration.assets.end() ? nullptr : &*iterator;
}

const EvidenceSource *find_evidence(const ProvenanceLedger &provenance,
                                    std::string_view id) {
    const auto iterator =
        std::ranges::find(provenance.evidence, id, &EvidenceSource::id);
    return iterator == provenance.evidence.end() ? nullptr : &*iterator;
}

std::string route_path(const PresentationValidationContext &context, RouteId id) {
    const auto *route = find_route(context, id);
    if (route == nullptr || !is_valid_semantic_id(route->semantic_id)) {
        return "presentation.routes.unknown";
    }
    return "presentation.routes." + route->semantic_id;
}

PresentationValidationContext
make_presentation_context(const EngineSpec &engine, const RenderScenario &scenario) {
    PresentationValidationContext context;
    context.engine_profile_id = engine.profile_id.value;
    context.routes.reserve(engine.routes.size());
    for (const auto &route : engine.routes) {
        context.routes.push_back({route.id, route.semantic_id.value, route.kind.value});
    }
    context.rates = scenario.rates;
    context.total_duration_s = scenario.total_duration_s.value;
    context.audible_start_s = scenario.audible_start_s.value;
    context.audible_duration_s = scenario.audible_duration_s.value;
    return context;
}

} // namespace

ValidationReport validate(const AuthoredPresentationCalibration &calibration) {
    using detail::finite_nonnegative;
    using detail::require;
    using detail::unit_interval;

    ValidationReport report = validate(calibration.provenance);
    require(report, calibration.schema_version == 2,
            ContractIssueCode::unsupported_value, "schema_version",
            "presentation-calibration schema version must be exactly 2");
    require(report, is_valid_semantic_id(calibration.calibration_id),
            ContractIssueCode::invalid_value, "calibration_id",
            "presentation-calibration ID must be canonical");
    validate_authored(report, calibration.engine_profile_id, calibration.provenance,
                      "presentation.engine_profile_id");
    require(report, is_valid_semantic_id(calibration.engine_profile_id.value),
            ContractIssueCode::invalid_value, "presentation.engine_profile_id.value",
            "engine profile ID must be canonical");

    for_each_authored_method(
        calibration.methods, [&](const auto &method, const std::string &path) {
            validate_authored(report, method, calibration.provenance, path);
            validate_selection(report, method.value, path + ".value");
        });

    const auto validate_conditioning = [&](const AuthoredValue<double> &value,
                                           const std::string &path,
                                           bool is_unit_interval) {
        validate_authored(report, value, calibration.provenance, path);
        require(report,
                is_unit_interval ? unit_interval(value.value)
                                 : finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "presentation conditioning value is outside its domain");
    };
    validate_conditioning(calibration.conditioning.jitter_scale,
                          "presentation.conditioning.jitter_scale", false);
    validate_conditioning(calibration.conditioning.jitter_modulation_cutoff_hz,
                          "presentation.conditioning.jitter_modulation_cutoff_hz",
                          false);
    validate_conditioning(calibration.conditioning.derivative_mix_01,
                          "presentation.conditioning.derivative_mix_01", true);
    validate_conditioning(calibration.conditioning.air_noise_mix_01,
                          "presentation.conditioning.air_noise_mix_01", true);
    validate_conditioning(calibration.conditioning.air_noise_cutoff_hz,
                          "presentation.conditioning.air_noise_cutoff_hz", false);

    std::unordered_set<std::string> asset_ids;
    for (std::size_t index = 0; index < calibration.assets.size(); ++index) {
        const auto &asset = calibration.assets[index];
        const auto path = "presentation.assets[" + std::to_string(index) + "]";
        validate_authored(report, asset.semantic_id, calibration.provenance,
                          path + ".semantic_id");
        validate_authored(report, asset.evidence_source_id, calibration.provenance,
                          path + ".evidence_source_id");
        validate_authored(report, asset.content_sha256, calibration.provenance,
                          path + ".content_sha256");
        validate_authored(report, asset.media, calibration.provenance, path + ".media");
        require(report, is_valid_semantic_id(asset.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "audio asset ID must be canonical");
        if (!asset_ids.insert(asset.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value", "audio asset IDs must be unique");
        }
        require(report, !asset.content_sha256.value.is_zero(),
                ContractIssueCode::missing_value, path + ".content_sha256.value",
                "audio asset digest must be present");
        require(report, is_valid_semantic_id(asset.evidence_source_id.value),
                ContractIssueCode::invalid_value, path + ".evidence_source_id.value",
                "audio-asset evidence ID must be canonical");
        const auto *evidence =
            find_evidence(calibration.provenance, asset.evidence_source_id.value);
        require(report, evidence != nullptr, ContractIssueCode::dangling_reference,
                path + ".evidence_source_id.value",
                "audio asset references unknown evidence");
        if (evidence != nullptr) {
            require(report,
                    evidence->content_sha256.has_value() &&
                        *evidence->content_sha256 == asset.content_sha256.value,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".content_sha256.value",
                    "audio asset digest must exactly match its content-addressed "
                    "evidence source");
        }
        validate_media(report, asset.media.value, path + ".media.value");
    }

    std::unordered_set<std::string> configured_routes;
    for (std::size_t index = 0; index < calibration.routes.size(); ++index) {
        const auto &route = calibration.routes[index];
        const auto path = "presentation.routes[" + std::to_string(index) + "]";
        validate_authored(report, route.route_semantic_id, calibration.provenance,
                          path + ".route_semantic_id");
        validate_authored(report, route.impulse_response_asset_id,
                          calibration.provenance, path + ".impulse_response_asset_id");
        validate_authored(report, route.impulse_response_gain_linear,
                          calibration.provenance,
                          path + ".impulse_response_gain_linear");
        validate_authored(report, route.wet_mix_01, calibration.provenance,
                          path + ".wet_mix_01");
        require(report, is_valid_semantic_id(route.route_semantic_id.value),
                ContractIssueCode::invalid_value, path + ".route_semantic_id.value",
                "route semantic ID must be canonical");
        if (!configured_routes.insert(route.route_semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".route_semantic_id.value",
                       "presentation routes must be unique");
        }
        require(report, asset_ids.contains(route.impulse_response_asset_id.value),
                ContractIssueCode::dangling_reference,
                path + ".impulse_response_asset_id.value",
                "route references an unknown audio asset");
        require(report, finite_nonnegative(route.impulse_response_gain_linear.value),
                ContractIssueCode::invalid_value,
                path + ".impulse_response_gain_linear.value",
                "impulse-response gain must be finite and nonnegative");
        require(report, unit_interval(route.wet_mix_01.value),
                ContractIssueCode::invalid_value, path + ".wet_mix_01.value",
                "wet mix must be in [0, 1]");
    }

    validate_authored(report, calibration.publication.calibration_gain_linear,
                      calibration.provenance,
                      "presentation.publication.calibration_gain_linear");
    require(report,
            finite_nonnegative(calibration.publication.calibration_gain_linear.value),
            ContractIssueCode::invalid_value,
            "presentation.publication.calibration_gain_linear.value",
            "publication calibration gain must be finite and nonnegative");

    validate_authored(report, calibration.audition.selected_route_semantic_ids,
                      calibration.provenance,
                      "presentation.audition.selected_route_semantic_ids");
    require(report, !calibration.audition.selected_route_semantic_ids.value.empty(),
            ContractIssueCode::missing_value,
            "presentation.audition.selected_route_semantic_ids",
            "audition mix must select at least one route");
    std::unordered_set<std::string> audition_routes;
    for (std::size_t index = 0;
         index < calibration.audition.selected_route_semantic_ids.value.size();
         ++index) {
        const auto &route =
            calibration.audition.selected_route_semantic_ids.value[index];
        const auto path = "presentation.audition.selected_route_semantic_ids[" +
                          std::to_string(index) + "]";
        require(report, configured_routes.contains(route),
                ContractIssueCode::dangling_reference, path,
                "audition mix references an unconfigured route");
        if (!audition_routes.insert(route).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "audition route selection must be unique");
        }
    }
    const auto validate_audition_scalar = [&](const AuthoredValue<double> &value,
                                              const std::string &path) {
        validate_authored(report, value, calibration.provenance, path);
        require(report, finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "audition scalar must be finite and nonnegative");
    };
    validate_audition_scalar(calibration.audition.monitoring_gain_linear,
                             "presentation.audition.monitoring_gain_linear");
    validate_audition_scalar(calibration.audition.fade_in_duration_s,
                             "presentation.audition.fade_in_duration_s");
    validate_audition_scalar(calibration.audition.fade_out_duration_s,
                             "presentation.audition.fade_out_duration_s");
    return report;
}

ValidationReport validate(const PresentationCalibration &calibration,
                          const PresentationValidationContext &context,
                          const ProvenanceLedger &provenance) {
    using detail::append_prefixed;
    using detail::finite_nonnegative;
    using detail::require;
    using detail::unit_interval;

    ValidationReport report = validate(provenance);
    require(report, calibration.schema_version == 2,
            ContractIssueCode::unsupported_value, "schema_version",
            "presentation-calibration schema version must be exactly 2");
    require(report, is_valid_semantic_id(calibration.calibration_id),
            ContractIssueCode::invalid_value, "calibration_id",
            "presentation-calibration ID must be canonical");
    require(report, calibration.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "presentation calibration and provenance schema IDs must match");
    validate_resolved(report, calibration.engine_profile_id, provenance,
                      "presentation.engine_profile_id");
    require(report, calibration.engine_profile_id.value == context.engine_profile_id,
            ContractIssueCode::inconsistent_semantics,
            "presentation.engine_profile_id.value",
            "render context and presentation profile IDs must match");

    for_each_method(
        calibration.methods, [&](const auto &method, const std::string &path) {
            validate_resolved(report, method, provenance, path);
            append_prefixed(report, validate(method.value), path + ".value");
        });

    const auto validate_conditioning = [&](const ResolvedValue<double> &value,
                                           const std::string &path,
                                           bool is_unit_interval, double nyquist_hz) {
        validate_resolved(report, value, provenance, path);
        require(report,
                is_unit_interval ? unit_interval(value.value)
                                 : finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "presentation conditioning value is outside its domain");
        if (nyquist_hz > 0.0) {
            require(report, value.value > 0.0 && value.value < nyquist_hz,
                    ContractIssueCode::invalid_value, path + ".value",
                    "filter cutoff must lie strictly below Nyquist");
        }
    };
    const auto source_rate =
        static_cast<double>(context.rates.source_processing.numerator) /
        static_cast<double>(context.rates.source_processing.denominator);
    validate_conditioning(calibration.conditioning.jitter_scale,
                          "presentation.conditioning.jitter_scale", false, 0.0);
    validate_conditioning(calibration.conditioning.jitter_modulation_cutoff_hz,
                          "presentation.conditioning.jitter_modulation_cutoff_hz",
                          false, source_rate / 2.0);
    validate_conditioning(calibration.conditioning.derivative_mix_01,
                          "presentation.conditioning.derivative_mix_01", true, 0.0);
    validate_conditioning(calibration.conditioning.air_noise_mix_01,
                          "presentation.conditioning.air_noise_mix_01", true, 0.0);
    validate_conditioning(calibration.conditioning.air_noise_cutoff_hz,
                          "presentation.conditioning.air_noise_cutoff_hz", false,
                          source_rate / 2.0);

    detail::require_unique_numeric_ids(
        report, calibration.assets,
        [](const AudioAssetSpec &asset) { return asset.id; }, "presentation.assets");
    std::unordered_set<std::string> asset_semantic_ids;
    for (const auto &asset : calibration.assets) {
        const auto path = "presentation.assets." + asset.semantic_id.value;
        validate_resolved(report, asset.semantic_id, provenance, path + ".semantic_id");
        validate_resolved(report, asset.evidence_source_id, provenance,
                          path + ".evidence_source_id");
        validate_resolved(report, asset.content_sha256, provenance,
                          path + ".content_sha256");
        validate_resolved(report, asset.media, provenance, path + ".media");
        require(report, is_valid_semantic_id(asset.semantic_id.value),
                ContractIssueCode::invalid_value, path + ".semantic_id.value",
                "audio asset semantic ID must be canonical");
        if (!asset_semantic_ids.insert(asset.semantic_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".semantic_id.value",
                       "audio asset semantic IDs must be unique");
        }
        require(report, !asset.content_sha256.value.is_zero(),
                ContractIssueCode::missing_value, path + ".content_sha256.value",
                "audio asset digest must be present");
        require(report, is_valid_semantic_id(asset.evidence_source_id.value),
                ContractIssueCode::invalid_value, path + ".evidence_source_id.value",
                "audio-asset evidence ID must be canonical");
        const auto *evidence =
            find_evidence(provenance, asset.evidence_source_id.value);
        require(report, evidence != nullptr, ContractIssueCode::dangling_reference,
                path + ".evidence_source_id.value",
                "audio asset references unknown evidence");
        if (evidence != nullptr) {
            require(report,
                    evidence->content_sha256.has_value() &&
                        *evidence->content_sha256 == asset.content_sha256.value,
                    ContractIssueCode::inconsistent_semantics,
                    path + ".content_sha256.value",
                    "audio asset digest must exactly match its content-addressed "
                    "evidence source");
        }
        validate_media(report, asset.media.value, path + ".media.value");
    }

    std::unordered_set<std::uint32_t> configured_routes;
    for (const auto &route : calibration.routes) {
        const auto path = route_path(context, route.route_id);
        const auto *source_route = find_route(context, route.route_id);
        require(report, source_route != nullptr, ContractIssueCode::dangling_reference,
                path + ".route_id", "presentation references an unknown source route");
        require(report,
                source_route != nullptr &&
                    (source_route->kind == SourceRouteKind::exhaust_outlet ||
                     source_route->kind == SourceRouteKind::intake_inlet),
                ContractIssueCode::unsupported_value, path + ".route_id",
                "current convolution presentation accepts gas source routes only");
        if (!configured_routes.insert(route.route_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".route_id",
                       "presentation route IDs must be unique");
        }
        const bool exhaust = source_route != nullptr &&
                             source_route->kind == SourceRouteKind::exhaust_outlet;
        const bool intake = source_route != nullptr &&
                            source_route->kind == SourceRouteKind::intake_inlet;
        require(
            report,
            (exhaust && route.impulse_response_asset_id.has_value() &&
             find_asset(calibration, *route.impulse_response_asset_id) != nullptr) ||
                (intake && !route.impulse_response_asset_id.has_value()),
            ContractIssueCode::dangling_reference, path + ".impulse_response_asset_id",
            "active exhaust routes require a known transfer asset while "
            "declared-silent intake routes require none");
        validate_resolved(report, route.impulse_response_gain_linear, provenance,
                          path + ".impulse_response_gain_linear");
        validate_resolved(report, route.wet_mix_01, provenance, path + ".wet_mix_01");
        require(report, finite_nonnegative(route.impulse_response_gain_linear.value),
                ContractIssueCode::invalid_value,
                path + ".impulse_response_gain_linear.value",
                "impulse-response gain must be finite and nonnegative");
        require(report, unit_interval(route.wet_mix_01.value),
                ContractIssueCode::invalid_value, path + ".wet_mix_01.value",
                "wet mix must be in [0, 1]");
    }

    validate_resolved(report, calibration.publication.calibration_gain_linear,
                      provenance, "presentation.publication.calibration_gain_linear");
    require(report,
            finite_nonnegative(calibration.publication.calibration_gain_linear.value),
            ContractIssueCode::invalid_value,
            "presentation.publication.calibration_gain_linear.value",
            "publication calibration gain must be finite and nonnegative");

    validate_resolved(report, calibration.audition.selected_routes, provenance,
                      "presentation.audition.selected_routes");
    require(report, !calibration.audition.selected_routes.value.empty(),
            ContractIssueCode::missing_value, "presentation.audition.selected_routes",
            "audition mix must select at least one route");
    std::unordered_set<std::uint32_t> selected_routes;
    for (std::size_t index = 0;
         index < calibration.audition.selected_routes.value.size(); ++index) {
        const auto route = calibration.audition.selected_routes.value[index];
        require(report, configured_routes.contains(route.value),
                ContractIssueCode::dangling_reference,
                "presentation.audition.selected_routes[" + std::to_string(index) + "]",
                "audition mix references an unconfigured route");
        if (!selected_routes.insert(route.value).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       "presentation.audition.selected_routes[" +
                           std::to_string(index) + "]",
                       "audition route selection must be unique");
        }
    }
    const auto validate_audition_scalar = [&](const ResolvedValue<double> &value,
                                              const std::string &path) {
        validate_resolved(report, value, provenance, path);
        require(report, finite_nonnegative(value.value),
                ContractIssueCode::invalid_value, path + ".value",
                "audition scalar must be finite and nonnegative");
    };
    validate_audition_scalar(calibration.audition.monitoring_gain_linear,
                             "presentation.audition.monitoring_gain_linear");
    validate_audition_scalar(calibration.audition.fade_in_duration_s,
                             "presentation.audition.fade_in_duration_s");
    validate_audition_scalar(calibration.audition.fade_out_duration_s,
                             "presentation.audition.fade_out_duration_s");
    require(report,
            calibration.audition.fade_in_duration_s.value +
                    calibration.audition.fade_out_duration_s.value <=
                context.audible_duration_s,
            ContractIssueCode::inconsistent_semantics, "presentation.audition",
            "audition fades must fit inside the audible interval");
    return report;
}

ValidationReport validate(const PresentationCalibration &calibration,
                          const EngineSpec &engine, const RenderScenario &scenario,
                          const ProvenanceLedger &provenance) {
    auto report =
        validate(calibration, make_presentation_context(engine, scenario), provenance);
    detail::require(report, scenario.engine_profile_id == engine.profile_id.value,
                    ContractIssueCode::inconsistent_semantics,
                    "presentation.engine_profile_id.value",
                    "engine, scenario, and presentation profile IDs must match");
    return report;
}

} // namespace engine_sim_offline::contract
