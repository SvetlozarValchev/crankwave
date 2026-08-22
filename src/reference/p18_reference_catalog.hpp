#pragma once

#include "crankwave/contract/presentation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crankwave::reference {

enum class P18ReferenceLineageFile : std::uint8_t {
    manifest,
    parity_evidence,
    audit_input,
    component_seed_input,
    renderer_algorithm_record,
    configured_ir_input,
    kernel_oracle_comparator,
};

inline constexpr std::size_t kP18ReferenceLineageFileCount = 7;

struct P18ExpectedLineageFile {
    P18ReferenceLineageFile file = P18ReferenceLineageFile::manifest;
    std::string_view expected_relative_path;
    std::uint64_t expected_byte_count = 0;
    contract::Sha256Digest expected_sha256;

    friend bool operator==(const P18ExpectedLineageFile &,
                           const P18ExpectedLineageFile &) = default;
};

struct P18ExpectedSemanticMethod {
    std::string_view expected_id;
    std::uint32_t expected_version = 0;

    friend bool operator==(const P18ExpectedSemanticMethod &,
                           const P18ExpectedSemanticMethod &) = default;
};

enum class P18ReferenceRoute : std::uint8_t {
    exhaust_0,
    exhaust_1,
};

inline constexpr std::size_t kP18ReferenceRouteCount = 2;

struct P18ExpectedRoute {
    P18ReferenceRoute route = P18ReferenceRoute::exhaust_0;
    contract::RouteId expected_route_id;
    std::string_view expected_semantic_id;
    contract::SourceRouteKind expected_source_matrix_classification =
        contract::SourceRouteKind::unspecified;

    friend bool operator==(const P18ExpectedRoute &,
                           const P18ExpectedRoute &) = default;
};

enum class P18ReferenceExecutedRandomComponent : std::uint8_t {
    air_noise,
    jitter,
};

inline constexpr std::size_t kP18ReferenceExecutedSeedCount = 4;

struct P18ExpectedExecutedSeed {
    P18ReferenceExecutedRandomComponent component =
        P18ReferenceExecutedRandomComponent::air_noise;
    P18ReferenceRoute route = P18ReferenceRoute::exhaust_0;
    std::uint64_t expected_initial_state = 0;
    std::uint64_t expected_stream = 0;

    friend bool operator==(const P18ExpectedExecutedSeed &,
                           const P18ExpectedExecutedSeed &) = default;
};

struct P18ExpectedCapture {
    contract::RenderRates expected_rates;
    std::uint64_t expected_record_count = 0;
    std::uint64_t expected_consumed_start_record = 0;
    std::uint64_t expected_consumed_end_record_exclusive = 0;
    std::uint64_t expected_audible_start_record = 0;
    std::uint64_t expected_audible_end_record_exclusive = 0;
    std::uint64_t expected_physics_frames_per_block = 0;
    std::uint64_t expected_source_frames_per_block = 0;
    std::uint64_t expected_total_source_frame_count = 0;
    std::uint64_t expected_audible_source_start_frame = 0;
    std::uint64_t expected_audible_source_end_frame_exclusive = 0;
    std::uint64_t expected_delivery_frame_count = 0;
    std::uint64_t expected_public_seed = 0;

    friend bool operator==(const P18ExpectedCapture &,
                           const P18ExpectedCapture &) = default;
};

enum class P18ReferencePresentationMethod : std::uint8_t {
    reconstruction,
    conditioning,
    impulse_response_conversion,
    convolution,
    publication,
    audition_mix,
};

inline constexpr std::size_t kP18ReferencePresentationMethodCount = 6;

struct P18ExpectedPresentationMethod {
    P18ReferencePresentationMethod method =
        P18ReferencePresentationMethod::reconstruction;
    P18ExpectedSemanticMethod expected_semantic;

    friend bool operator==(const P18ExpectedPresentationMethod &,
                           const P18ExpectedPresentationMethod &) = default;
};

struct P18ExpectedBinary64 {
    std::uint64_t expected_ieee754_bits = 0;

    friend bool operator==(const P18ExpectedBinary64 &,
                           const P18ExpectedBinary64 &) = default;
};

struct P18ExpectedConfiguredIrMedia {
    contract::AudioAssetId expected_asset_id;
    std::string_view expected_semantic_id;
    std::string_view expected_evidence_source_id;
    contract::AudioSampleEncoding expected_encoding =
        contract::AudioSampleEncoding::pcm_s16le;
    contract::AudioChannelLayout expected_channel_layout =
        contract::AudioChannelLayout::mono;
    contract::RationalRateHz expected_sample_rate;
    std::uint64_t expected_frame_count = 0;
    std::uint64_t expected_meaningful_support_frame_count = 0;

    friend bool operator==(const P18ExpectedConfiguredIrMedia &,
                           const P18ExpectedConfiguredIrMedia &) = default;
};

struct P18ExpectedPresentationScalars {
    P18ExpectedBinary64 expected_jitter_scale;
    P18ExpectedBinary64 expected_jitter_modulation_cutoff_hz;
    P18ExpectedBinary64 expected_derivative_mix_01;
    P18ExpectedBinary64 expected_air_noise_mix_01;
    P18ExpectedBinary64 expected_air_noise_cutoff_hz;
    P18ExpectedBinary64 expected_impulse_response_gain_linear;
    P18ExpectedBinary64 expected_wet_mix_01;
    P18ExpectedBinary64 expected_publication_calibration_gain_linear;
    P18ExpectedBinary64 expected_audition_monitoring_gain_linear;
    P18ExpectedBinary64 expected_audition_fade_in_duration_s;
    P18ExpectedBinary64 expected_audition_fade_out_duration_s;

    friend bool operator==(const P18ExpectedPresentationScalars &,
                           const P18ExpectedPresentationScalars &) = default;
};

struct P18ExpectedPresentation {
    std::uint32_t expected_schema_version = 0;
    std::string_view expected_algorithm_record_semantic_id;
    std::string_view expected_algorithm_record_evidence_source_id;
    std::array<P18ExpectedPresentationMethod, kP18ReferencePresentationMethodCount>
        expected_methods;
    P18ExpectedConfiguredIrMedia expected_configured_ir_media;
    P18ExpectedPresentationScalars expected_scalars;
    std::array<P18ReferenceRoute, kP18ReferenceRouteCount>
        expected_audition_route_order;

    friend bool operator==(const P18ExpectedPresentation &,
                           const P18ExpectedPresentation &) = default;
};

enum class P18ReferenceAudioArtifact : std::uint8_t {
    exhaust_0_dry,
    exhaust_0_configured_ir,
    exhaust_0_selected,
    exhaust_1_dry,
    exhaust_1_configured_ir,
    exhaust_1_selected,
    master_raw,
    master_audition,
};

inline constexpr std::size_t kP18ReferenceAudioArtifactCount = 8;

struct P18ExpectedAudioComparator {
    P18ReferenceAudioArtifact audio = P18ReferenceAudioArtifact::exhaust_0_dry;
    std::string_view expected_role;
    std::string_view expected_relative_path;
    std::uint64_t expected_byte_count = 0;
    contract::Sha256Digest expected_sha256;
    bool expected_diagnostic = false;

    friend bool operator==(const P18ExpectedAudioComparator &,
                           const P18ExpectedAudioComparator &) = default;
};

enum class P18ReferenceMasteringPayload : std::uint8_t {
    raw_float32,
    monitoring_float32,
    faded_float32,
    s32le,
    pcm24le,
};

inline constexpr std::size_t kP18ReferenceMasteringPayloadCount = 5;

struct P18ExpectedMasteringComparator {
    P18ReferenceMasteringPayload payload = P18ReferenceMasteringPayload::raw_float32;
    contract::Sha256Digest expected_sha256;

    friend bool operator==(const P18ExpectedMasteringComparator &,
                           const P18ExpectedMasteringComparator &) = default;
};

struct P18ExpectedKernelComparators {
    std::uint64_t expected_coefficient_count = 0;
    contract::Sha256Digest expected_coefficient_f64le_sha256;
    contract::Sha256Digest expected_spectrum_f64le_sha256;

    friend bool operator==(const P18ExpectedKernelComparators &,
                           const P18ExpectedKernelComparators &) = default;
};

struct P18ExpectedRenderComparators {
    std::uint64_t expected_input_frame_count = 0;
    std::uint64_t expected_processed_block_count = 0;
    std::uint64_t expected_warmup_block_count = 0;
    std::uint64_t expected_published_block_count = 0;
    std::uint64_t expected_processed_source_frame_count = 0;
    std::uint64_t expected_warmup_source_frame_count = 0;
    std::uint64_t expected_published_source_frame_count = 0;
    std::uint64_t expected_saturation_count = 0;
    std::uint32_t expected_faded_absolute_peak_binary32_bits = 0;

    friend bool operator==(const P18ExpectedRenderComparators &,
                           const P18ExpectedRenderComparators &) = default;
};

// Private reference-session expectations. Authored configuration and publication
// policy may drive the reference session. Expected payload identities and comparator
// values may only validate or compare: observed fixture bytes, derived kernels,
// render statistics, and sealed artifacts must always be measured independently.
struct P18ReferenceCatalogV1 {
    std::uint32_t expected_reference_inputs_schema_version = 0;
    std::uint32_t expected_fixture_schema_version = 0;
    std::string_view expected_fixture_id;
    std::string_view expected_engine_id;
    std::string_view expected_engine_profile_id;
    std::string_view expected_presentation_calibration_id;
    std::array<P18ExpectedLineageFile, kP18ReferenceLineageFileCount>
        expected_lineage_files;

    P18ExpectedSemanticMethod expected_audit_reader;
    P18ExpectedSemanticMethod expected_excitation_adapter;
    std::string_view expected_audit_lane_semantic_id;
    P18ExpectedSemanticMethod expected_excitation_seam;

    std::array<P18ExpectedRoute, kP18ReferenceRouteCount> expected_routes;
    P18ExpectedSemanticMethod expected_random_generator;
    P18ExpectedSemanticMethod expected_seed_derivation;
    std::array<P18ExpectedExecutedSeed, kP18ReferenceExecutedSeedCount>
        expected_executed_seeds;
    P18ExpectedCapture expected_capture;
    P18ExpectedPresentation expected_presentation;

    std::array<P18ExpectedAudioComparator, kP18ReferenceAudioArtifactCount>
        expected_audio;
    std::array<P18ExpectedMasteringComparator, kP18ReferenceMasteringPayloadCount>
        expected_mastering;
    P18ExpectedKernelComparators expected_kernel;
    P18ExpectedRenderComparators expected_render;

    [[nodiscard]] const P18ExpectedAudioComparator *
    find_expected_audio(P18ReferenceAudioArtifact artifact) const noexcept {
        const auto index = static_cast<std::size_t>(artifact);
        return index < expected_audio.size() && expected_audio[index].audio == artifact
                   ? &expected_audio[index]
                   : nullptr;
    }

    [[nodiscard]] const P18ExpectedMasteringComparator *
    find_expected_mastering(P18ReferenceMasteringPayload payload) const noexcept {
        const auto index = static_cast<std::size_t>(payload);
        return index < expected_mastering.size() &&
                       expected_mastering[index].payload == payload
                   ? &expected_mastering[index]
                   : nullptr;
    }

    friend bool operator==(const P18ReferenceCatalogV1 &,
                           const P18ReferenceCatalogV1 &) = default;
};

[[nodiscard]] const P18ReferenceCatalogV1 &p18_reference_catalog_v1() noexcept;

} // namespace crankwave::reference
