#include "engine_sim_offline/contract/presentation.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "validation_support.hpp"

#include <algorithm>
#include <bit>
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

const RouteSpec *find_route(const EngineSpec &engine, RouteId id) {
    const auto iterator = std::ranges::find(engine.routes, id, &RouteSpec::id);
    return iterator == engine.routes.end() ? nullptr : &*iterator;
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

std::string route_path(const EngineSpec &engine, RouteId id) {
    const auto *route = find_route(engine, id);
    if (route == nullptr || !is_valid_semantic_id(route->semantic_id.value)) {
        return "presentation.routes.unknown";
    }
    return "presentation.routes." + route->semantic_id.value;
}

Sha256Digest digest_from_hex(std::string_view text) {
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        return static_cast<std::uint8_t>(10 + value - 'a');
    };

    Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2]) << 4U) | nibble(text[index * 2 + 1]));
    }
    return digest;
}

bool exact_bits(double value, std::uint64_t expected) noexcept {
    return std::bit_cast<std::uint64_t>(value) == expected;
}

void require_method(ValidationReport &report,
                    const ResolvedValue<MethodIdentity> &method,
                    std::string_view expected_id, const std::string &path) {
    detail::require(report, method.value.id == expected_id && method.value.version == 1,
                    ContractIssueCode::inconsistent_semantics, path,
                    "P1.8 presentation method identity does not match the frozen "
                    "reference");
}

} // namespace

ValidationReport validate(const AuthoredPresentationCalibration &calibration) {
    using detail::finite_nonnegative;
    using detail::require;
    using detail::unit_interval;

    ValidationReport report = validate(calibration.provenance);
    require(report, calibration.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version",
            "presentation-calibration schema version must be positive");
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

    const auto &algorithm_record = calibration.algorithm_record;
    constexpr std::string_view algorithm_path = "presentation.algorithm_record";
    validate_authored(report, algorithm_record.semantic_id, calibration.provenance,
                      std::string(algorithm_path) + ".semantic_id");
    validate_authored(report, algorithm_record.evidence_source_id,
                      calibration.provenance,
                      std::string(algorithm_path) + ".evidence_source_id");
    validate_authored(report, algorithm_record.content_sha256, calibration.provenance,
                      std::string(algorithm_path) + ".content_sha256");
    require(report, is_valid_semantic_id(algorithm_record.semantic_id.value),
            ContractIssueCode::invalid_value,
            std::string(algorithm_path) + ".semantic_id.value",
            "presentation algorithm-record ID must be canonical");
    require(report, is_valid_semantic_id(algorithm_record.evidence_source_id.value),
            ContractIssueCode::invalid_value,
            std::string(algorithm_path) + ".evidence_source_id.value",
            "presentation algorithm-record evidence ID must be canonical");
    require(report, !algorithm_record.content_sha256.value.is_zero(),
            ContractIssueCode::missing_value,
            std::string(algorithm_path) + ".content_sha256.value",
            "presentation algorithm-record digest must be present");
    const auto *algorithm_evidence = find_evidence(
        calibration.provenance, algorithm_record.evidence_source_id.value);
    require(report, algorithm_evidence != nullptr,
            ContractIssueCode::dangling_reference,
            std::string(algorithm_path) + ".evidence_source_id.value",
            "presentation algorithm record references unknown evidence");
    if (algorithm_evidence != nullptr) {
        require(report,
                algorithm_evidence->content_sha256.has_value() &&
                    *algorithm_evidence->content_sha256 ==
                        algorithm_record.content_sha256.value,
                ContractIssueCode::inconsistent_semantics,
                std::string(algorithm_path) + ".content_sha256.value",
                "presentation algorithm-record digest must exactly match its "
                "content-addressed evidence source");
    }

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
                          const EngineSpec &engine, const RenderScenario &scenario,
                          const ProvenanceLedger &provenance) {
    using detail::append_prefixed;
    using detail::finite_nonnegative;
    using detail::require;
    using detail::unit_interval;

    ValidationReport report = validate(provenance);
    require(report, calibration.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version",
            "presentation-calibration schema version must be positive");
    require(report, is_valid_semantic_id(calibration.calibration_id),
            ContractIssueCode::invalid_value, "calibration_id",
            "presentation-calibration ID must be canonical");
    require(report, calibration.provenance_schema_id == provenance.schema_id,
            ContractIssueCode::inconsistent_semantics, "provenance_schema_id",
            "presentation calibration and provenance schema IDs must match");
    validate_resolved(report, calibration.engine_profile_id, provenance,
                      "presentation.engine_profile_id");
    require(report,
            calibration.engine_profile_id.value == engine.profile_id.value &&
                scenario.engine_profile_id == engine.profile_id.value,
            ContractIssueCode::inconsistent_semantics,
            "presentation.engine_profile_id.value",
            "engine, scenario, and presentation profile IDs must match");

    for_each_method(
        calibration.methods, [&](const auto &method, const std::string &path) {
            validate_resolved(report, method, provenance, path);
            append_prefixed(report, validate(method.value), path + ".value");
        });

    const auto &algorithm_record = calibration.algorithm_record;
    constexpr std::string_view algorithm_path = "presentation.algorithm_record";
    validate_resolved(report, algorithm_record.semantic_id, provenance,
                      std::string(algorithm_path) + ".semantic_id");
    validate_resolved(report, algorithm_record.evidence_source_id, provenance,
                      std::string(algorithm_path) + ".evidence_source_id");
    validate_resolved(report, algorithm_record.content_sha256, provenance,
                      std::string(algorithm_path) + ".content_sha256");
    require(report, is_valid_semantic_id(algorithm_record.semantic_id.value),
            ContractIssueCode::invalid_value,
            std::string(algorithm_path) + ".semantic_id.value",
            "presentation algorithm-record ID must be canonical");
    require(report, is_valid_semantic_id(algorithm_record.evidence_source_id.value),
            ContractIssueCode::invalid_value,
            std::string(algorithm_path) + ".evidence_source_id.value",
            "presentation algorithm-record evidence ID must be canonical");
    require(report, !algorithm_record.content_sha256.value.is_zero(),
            ContractIssueCode::missing_value,
            std::string(algorithm_path) + ".content_sha256.value",
            "presentation algorithm-record digest must be present");
    const auto *algorithm_evidence =
        find_evidence(provenance, algorithm_record.evidence_source_id.value);
    require(report, algorithm_evidence != nullptr,
            ContractIssueCode::dangling_reference,
            std::string(algorithm_path) + ".evidence_source_id.value",
            "presentation algorithm record references unknown evidence");
    if (algorithm_evidence != nullptr) {
        require(report,
                algorithm_evidence->content_sha256.has_value() &&
                    *algorithm_evidence->content_sha256 ==
                        algorithm_record.content_sha256.value,
                ContractIssueCode::inconsistent_semantics,
                std::string(algorithm_path) + ".content_sha256.value",
                "presentation algorithm-record digest must exactly match its "
                "content-addressed evidence source");
    }

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
        static_cast<double>(scenario.rates.source_processing.numerator) /
        static_cast<double>(scenario.rates.source_processing.denominator);
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
        const auto path = route_path(engine, route.route_id);
        const auto *engine_route = find_route(engine, route.route_id);
        require(report, engine_route != nullptr, ContractIssueCode::dangling_reference,
                path + ".route_id", "presentation references an unknown source route");
        require(report,
                engine_route != nullptr &&
                    engine_route->kind.value == SourceRouteKind::exhaust_outlet,
                ContractIssueCode::unsupported_value, path + ".route_id",
                "current convolution presentation accepts exhaust routes only");
        if (!configured_routes.insert(route.route_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".route_id",
                       "presentation route IDs must be unique");
        }
        require(
            report, find_asset(calibration, route.impulse_response_asset_id) != nullptr,
            ContractIssueCode::dangling_reference, path + ".impulse_response_asset_id",
            "route references an unknown audio asset");
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
                scenario.audible_duration_s.value,
            ContractIssueCode::inconsistent_semantics, "presentation.audition",
            "audition fades must fit inside the audible interval");
    return report;
}

ValidationReport
validate_p18_reference_presentation(const PresentationCalibration &calibration,
                                    const EngineSpec &engine,
                                    const RenderScenario &scenario) {
    using detail::require;

    ValidationReport report;
    require(
        report,
        calibration.algorithm_record.semantic_id.value ==
                "bmw-m52b28-p18-presentation-renderer-v1" &&
            calibration.algorithm_record.content_sha256.value ==
                digest_from_hex("0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c"
                                "182c96a008"),
        ContractIssueCode::inconsistent_semantics, "presentation.algorithm_record",
        "P1.8 presentation must bind the complete content-addressed "
        "renderer record");
    require_method(report, calibration.methods.reconstruction,
                   "kaiser_windowed_sinc_257tap_4096phase_causal_polyphase_beta12_"
                   "cutoff0p95_source_nyquist_unity_dc_binary64_v2",
                   "presentation.methods.reconstruction");
    require_method(report, calibration.methods.conditioning,
                   "p18-synthesizer-conditioning-v1",
                   "presentation.methods.conditioning");
    require_method(report, calibration.methods.impulse_response_conversion,
                   "blackman_windowed_sinc_24tap_4096phase_antialiased_per_source_area_"
                   "binary64_v3",
                   "presentation.methods.impulse_response_conversion");
    require_method(report, calibration.methods.convolution,
                   "causal_overlap_save_radix2_dit_fft_fixed_topology_binary64_v1",
                   "presentation.methods.convolution");
    require_method(report, calibration.methods.publication,
                   "p18-float32-stem-publication-v1",
                   "presentation.methods.publication");
    require_method(report, calibration.methods.audition_mix,
                   "p18-reference-audition-mix-v1",
                   "presentation.methods.audition_mix");

    require(report,
            exact_bits(calibration.conditioning.jitter_scale.value,
                       std::bit_cast<std::uint64_t>(0.5)),
            ContractIssueCode::inconsistent_semantics,
            "presentation.conditioning.jitter_scale.value",
            "P1.8 jitter scale must be exactly 0.5");
    require(report,
            exact_bits(calibration.conditioning.jitter_modulation_cutoff_hz.value,
                       std::bit_cast<std::uint64_t>(10000.0)),
            ContractIssueCode::inconsistent_semantics,
            "presentation.conditioning.jitter_modulation_cutoff_hz.value",
            "P1.8 jitter cutoff must be exactly 10 kHz");
    require(report,
            exact_bits(calibration.conditioning.derivative_mix_01.value,
                       UINT64_C(0x3f847ae140000000)),
            ContractIssueCode::inconsistent_semantics,
            "presentation.conditioning.derivative_mix_01.value",
            "P1.8 derivative mix must preserve the widened Float32 value");
    require(report,
            exact_bits(calibration.conditioning.air_noise_mix_01.value,
                       std::bit_cast<std::uint64_t>(1.0)) &&
                exact_bits(calibration.conditioning.air_noise_cutoff_hz.value,
                           std::bit_cast<std::uint64_t>(2000.0)),
            ContractIssueCode::inconsistent_semantics,
            "presentation.conditioning.air_noise_mix_01.value",
            "P1.8 air-noise values do not match the frozen reference");

    require(report, calibration.assets.size() == 1,
            ContractIssueCode::inconsistent_shape, "presentation.assets",
            "P1.8 requires exactly one configured impulse response");
    if (calibration.assets.size() == 1) {
        const auto &asset = calibration.assets.front();
        require(report,
                asset.semantic_id.value == "smooth-39" &&
                    asset.content_sha256.value ==
                        digest_from_hex("75de9db47063395665d36b6d4232f477aae385feaa9ba1"
                                        "58353fbdaf122db5cc") &&
                    asset.media.value.encoding == AudioSampleEncoding::pcm_s16le &&
                    asset.media.value.channel_layout == AudioChannelLayout::mono &&
                    asset.media.value.sample_rate == RationalRateHz{44100, 1} &&
                    asset.media.value.frame_count == 33705,
                ContractIssueCode::inconsistent_semantics,
                "presentation.assets.smooth-39",
                "P1.8 impulse-response identity or media contract differs from "
                "the frozen reference");
    }

    require(report, calibration.routes.size() == 2,
            ContractIssueCode::inconsistent_shape, "presentation.routes",
            "P1.8 requires exactly two exhaust route presentations");
    if (calibration.routes.size() == 2) {
        const auto *route_0 = find_route(engine, calibration.routes[0].route_id);
        const auto *route_1 = find_route(engine, calibration.routes[1].route_id);
        require(report,
                route_0 != nullptr &&
                    route_0->semantic_id.value == "exhaust.reference.0" &&
                    route_1 != nullptr &&
                    route_1->semantic_id.value == "exhaust.reference.1",
                ContractIssueCode::inconsistent_semantics, "presentation.routes",
                "P1.8 route order must be exhaust.reference.0 then "
                "exhaust.reference.1");
    }
    for (const auto &route : calibration.routes) {
        const auto *engine_route = find_route(engine, route.route_id);
        require(report,
                engine_route != nullptr &&
                    (engine_route->semantic_id.value == "exhaust.reference.0" ||
                     engine_route->semantic_id.value == "exhaust.reference.1"),
                ContractIssueCode::inconsistent_semantics, "presentation.routes",
                "P1.8 route identity must be exhaust.reference.0 or .1");
        require(
            report,
            exact_bits(route.impulse_response_gain_linear.value,
                       UINT64_C(0x3f50624dd2f1a9fc)) &&
                exact_bits(route.wet_mix_01.value, std::bit_cast<std::uint64_t>(1.0)),
            ContractIssueCode::inconsistent_semantics, "presentation.routes",
            "P1.8 route IR gain and selected wet mix must match exactly");
        if (!calibration.assets.empty()) {
            require(report,
                    route.impulse_response_asset_id == calibration.assets.front().id,
                    ContractIssueCode::inconsistent_semantics, "presentation.routes",
                    "both P1.8 routes must use smooth-39");
        }
    }

    require(report,
            exact_bits(calibration.publication.calibration_gain_linear.value,
                       std::bit_cast<std::uint64_t>(0x1.0p-26)),
            ContractIssueCode::inconsistent_semantics,
            "presentation.publication.calibration_gain_linear.value",
            "P1.8 source calibration must be exactly 2^-26");
    require(report,
            calibration.audition.selected_routes.value.size() == 2 &&
                calibration.routes.size() == 2 &&
                calibration.audition.selected_routes.value[0] ==
                    calibration.routes[0].route_id &&
                calibration.audition.selected_routes.value[1] ==
                    calibration.routes[1].route_id,
            ContractIssueCode::inconsistent_semantics,
            "presentation.audition.selected_routes",
            "P1.8 audition reduction order must be route 0 then route 1");
    require(report,
            exact_bits(calibration.audition.monitoring_gain_linear.value,
                       std::bit_cast<std::uint64_t>(128.0)) &&
                exact_bits(calibration.audition.fade_in_duration_s.value,
                           std::bit_cast<std::uint64_t>(0.02)) &&
                exact_bits(calibration.audition.fade_out_duration_s.value,
                           std::bit_cast<std::uint64_t>(0.02)),
            ContractIssueCode::inconsistent_semantics, "presentation.audition",
            "P1.8 audition gain and quarter-sine fade durations must match exactly");
    require(report,
            scenario.rates.physics == RationalRateHz{10000, 1} &&
                scenario.rates.capture == RationalRateHz{10000, 1} &&
                scenario.rates.source_processing == RationalRateHz{192000, 1} &&
                scenario.rates.acoustic == RationalRateHz{192000, 1} &&
                scenario.rates.delivery == RationalRateHz{192000, 1},
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "P1.8 requires the frozen 10 kHz to 192 kHz clock plan");
    require(report,
            exact_bits(scenario.total_duration_s.value,
                       std::bit_cast<std::uint64_t>(17.0)) &&
                exact_bits(scenario.audible_start_s.value,
                           std::bit_cast<std::uint64_t>(2.0)) &&
                exact_bits(scenario.audible_duration_s.value,
                           std::bit_cast<std::uint64_t>(15.0)),
            ContractIssueCode::inconsistent_semantics, "scenario.audible_interval",
            "P1.8 must retain exactly [2 s, 17 s)");
    require(report, scenario.quality.value.capture_block_capacity_frames >= 200,
            ContractIssueCode::inconsistent_semantics,
            "scenario.quality.value.capture_block_capacity_frames",
            "P1.8 capture transport must hold its fixed 200-frame method block");
    require(report, scenario.quality.value.event_journal_capacity_records >= 19U * 200U,
            ContractIssueCode::inconsistent_semantics,
            "scenario.quality.value.event_journal_capacity_records",
            "P1.8 capture transport must hold 19 event records for each frame in "
            "its fixed 200-frame method block");
    return report;
}

} // namespace engine_sim_offline::contract
