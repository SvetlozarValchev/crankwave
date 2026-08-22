#pragma once

#include "crankwave/contract/common.hpp"
#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/provenance.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crankwave::contract {

struct RenderScenario;

enum class AudioSampleEncoding : std::uint8_t {
    pcm_s16le,
    float32le,
    pcm_s24le,
};

enum class AudioChannelLayout : std::uint8_t {
    mono,
};

struct AudioMediaContract {
    AudioSampleEncoding encoding = AudioSampleEncoding::pcm_s16le;
    AudioChannelLayout channel_layout = AudioChannelLayout::mono;
    RationalRateHz sample_rate;
    std::uint64_t frame_count = 0;

    friend bool operator==(const AudioMediaContract &,
                           const AudioMediaContract &) = default;
};

struct AuthoredAudioAssetRef {
    AuthoredValue<std::string> semantic_id;
    AuthoredValue<std::string> evidence_source_id;
    AuthoredValue<Sha256Digest> content_sha256;
    AuthoredValue<AudioMediaContract> media;

    friend bool operator==(const AuthoredAudioAssetRef &,
                           const AuthoredAudioAssetRef &) = default;
};

struct AudioAssetSpec {
    AudioAssetId id;
    ResolvedValue<std::string> semantic_id;
    ResolvedValue<std::string> evidence_source_id;
    ResolvedValue<Sha256Digest> content_sha256;
    ResolvedValue<AudioMediaContract> media;

    friend bool operator==(const AudioAssetSpec &, const AudioAssetSpec &) = default;
};

struct AuthoredPresentationMethods {
    AuthoredValue<MethodSelection> reconstruction;
    AuthoredValue<MethodSelection> conditioning;
    AuthoredValue<MethodSelection> impulse_response_conversion;
    AuthoredValue<MethodSelection> convolution;
    AuthoredValue<MethodSelection> publication;
    AuthoredValue<MethodSelection> audition_mix;

    friend bool operator==(const AuthoredPresentationMethods &,
                           const AuthoredPresentationMethods &) = default;
};

struct PresentationMethods {
    ResolvedValue<MethodIdentity> reconstruction;
    ResolvedValue<MethodIdentity> conditioning;
    ResolvedValue<MethodIdentity> impulse_response_conversion;
    ResolvedValue<MethodIdentity> convolution;
    ResolvedValue<MethodIdentity> publication;
    ResolvedValue<MethodIdentity> audition_mix;

    friend bool operator==(const PresentationMethods &,
                           const PresentationMethods &) = default;
};

struct AuthoredPresentationConditioning {
    AuthoredValue<double> jitter_scale;
    AuthoredValue<double> jitter_modulation_cutoff_hz;
    AuthoredValue<double> derivative_mix_01;
    AuthoredValue<double> air_noise_mix_01;
    AuthoredValue<double> air_noise_cutoff_hz;

    friend bool operator==(const AuthoredPresentationConditioning &,
                           const AuthoredPresentationConditioning &) = default;
};

struct PresentationConditioning {
    ResolvedValue<double> jitter_scale;
    ResolvedValue<double> jitter_modulation_cutoff_hz;
    ResolvedValue<double> derivative_mix_01;
    ResolvedValue<double> air_noise_mix_01;
    ResolvedValue<double> air_noise_cutoff_hz;

    friend bool operator==(const PresentationConditioning &,
                           const PresentationConditioning &) = default;
};

struct AuthoredRoutePresentation {
    AuthoredValue<std::string> route_semantic_id;
    AuthoredValue<double> source_gain_linear;
    AuthoredValue<std::string> impulse_response_asset_id;
    AuthoredValue<double> impulse_response_gain_linear;
    AuthoredValue<double> wet_mix_01;

    friend bool operator==(const AuthoredRoutePresentation &,
                           const AuthoredRoutePresentation &) = default;
};

struct RoutePresentation {
    RouteId route_id;
    ResolvedValue<double> source_gain_linear;
    std::optional<AudioAssetId> impulse_response_asset_id;
    ResolvedValue<double> impulse_response_gain_linear;
    ResolvedValue<double> wet_mix_01;

    friend bool operator==(const RoutePresentation &,
                           const RoutePresentation &) = default;
};

struct AuthoredStemPublication {
    AuthoredValue<double> calibration_gain_linear;

    friend bool operator==(const AuthoredStemPublication &,
                           const AuthoredStemPublication &) = default;
};

struct StemPublication {
    ResolvedValue<double> calibration_gain_linear;

    friend bool operator==(const StemPublication &, const StemPublication &) = default;
};

struct AuthoredAuditionMix {
    AuthoredValue<std::vector<std::string>> selected_route_semantic_ids;
    AuthoredValue<double> volume_linear;
    AuthoredValue<double> fade_in_duration_s;
    AuthoredValue<double> fade_out_duration_s;

    friend bool operator==(const AuthoredAuditionMix &,
                           const AuthoredAuditionMix &) = default;
};

struct AuditionMix {
    // Vector order is the deterministic arithmetic reduction order for every
    // active exhaust-source route.
    ResolvedValue<std::vector<RouteId>> selected_routes;
    ResolvedValue<double> volume_linear;
    ResolvedValue<double> fade_in_duration_s;
    ResolvedValue<double> fade_out_duration_s;

    friend bool operator==(const AuditionMix &, const AuditionMix &) = default;
};

struct AuthoredPresentationCalibration {
    std::uint32_t schema_version = 0;
    std::string calibration_id;
    AuthoredValue<std::string> engine_profile_id;
    AuthoredPresentationMethods methods;
    AuthoredPresentationConditioning conditioning;
    std::vector<AuthoredAudioAssetRef> assets;
    std::vector<AuthoredRoutePresentation> routes;
    AuthoredStemPublication publication;
    AuthoredAuditionMix audition;
    ProvenanceLedger provenance;

    friend bool operator==(const AuthoredPresentationCalibration &,
                           const AuthoredPresentationCalibration &) = default;
};

struct PresentationCalibration {
    std::uint32_t schema_version = 0;
    std::string calibration_id;
    ResolvedValue<std::string> engine_profile_id;
    PresentationMethods methods;
    PresentationConditioning conditioning;
    std::vector<AudioAssetSpec> assets;
    std::vector<RoutePresentation> routes;
    StemPublication publication;
    AuditionMix audition;
    std::string provenance_schema_id;

    friend bool operator==(const PresentationCalibration &,
                           const PresentationCalibration &) = default;
};

struct PresentationSourceRouteContext {
    RouteId route_id;
    std::string semantic_id;
    SourceRouteKind kind = SourceRouteKind::unspecified;

    friend bool operator==(const PresentationSourceRouteContext &,
                           const PresentationSourceRouteContext &) = default;
};

struct PresentationValidationContext {
    std::string engine_profile_id;
    std::vector<PresentationSourceRouteContext> routes;
    RenderRates rates;
    double total_duration_s = 0.0;
    double audible_start_s = 0.0;
    double audible_duration_s = 0.0;

    friend bool operator==(const PresentationValidationContext &,
                           const PresentationValidationContext &) = default;
};

[[nodiscard]] ValidationReport
validate(const AuthoredPresentationCalibration &calibration);
[[nodiscard]] ValidationReport validate(const PresentationCalibration &calibration,
                                        const EngineSpec &engine,
                                        const RenderScenario &scenario,
                                        const ProvenanceLedger &provenance);
[[nodiscard]] ValidationReport validate(const PresentationCalibration &calibration,
                                        const PresentationValidationContext &context,
                                        const ProvenanceLedger &provenance);
} // namespace crankwave::contract
