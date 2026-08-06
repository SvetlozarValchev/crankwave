#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::contract {

inline constexpr std::string_view kAudioAtlasSchema =
    "engine-sim-offline/audio-atlas";

struct AudioAtlasContentIdentity {
    std::string id;
    Sha256Digest sha256;

    friend bool operator==(const AudioAtlasContentIdentity &,
                           const AudioAtlasContentIdentity &) = default;
};

enum class AudioAtlasSampleEncoding : std::uint8_t {
    float32le,
};

struct AudioAtlasBus {
    std::string id;

    friend bool operator==(const AudioAtlasBus &,
                           const AudioAtlasBus &) = default;
};

struct AudioAtlasAudioFormat {
    std::uint32_t sample_rate_hz = 0;
    AudioAtlasSampleEncoding encoding = AudioAtlasSampleEncoding::float32le;
    // Authored order is the immutable runtime delivery order.
    std::vector<AudioAtlasBus> buses;

    friend bool operator==(const AudioAtlasAudioFormat &,
                           const AudioAtlasAudioFormat &) = default;
};

enum class AudioAtlasLoadCoordinate : std::uint8_t {
    measured_intake_manifold_pressure_pa_abs,
};

enum class AudioAtlasOutOfDomainBehavior : std::uint8_t {
    unavailable,
};

struct AudioAtlasDomain {
    double minimum_rpm = 0.0;
    double maximum_rpm = 0.0;
    AudioAtlasLoadCoordinate load_coordinate =
        AudioAtlasLoadCoordinate::measured_intake_manifold_pressure_pa_abs;
    double minimum_load_pa_abs = 0.0;
    double maximum_load_pa_abs = 0.0;
    double phase_cycle_revolutions = 0.0;
    std::vector<std::uint32_t> supported_state_masks;
    AudioAtlasOutOfDomainBehavior out_of_domain_behavior =
        AudioAtlasOutOfDomainBehavior::unavailable;

    friend bool operator==(const AudioAtlasDomain &,
                           const AudioAtlasDomain &) = default;
};

// Atlas state describes audible engine state, never the rig that happened to
// capture it. HeldDyno's motion-owner flag is deliberately absent so the same
// material can follow a free engine or a vehicle drivetrain.
enum class AudioAtlasEngineStateFlag : std::uint8_t {
    ignition_enabled = 0U,
    fuel_enabled = 1U,
    starter_enabled = 2U,
    limiter_enabled = 3U,
    limiter_cut_active = 4U,
};

[[nodiscard]] constexpr std::uint32_t audio_atlas_engine_state_flag_mask(
    const AudioAtlasEngineStateFlag flag) noexcept {
    return UINT32_C(1) << static_cast<std::uint8_t>(flag);
}

inline constexpr std::uint32_t kAudioAtlasKnownStateMask = (1U << 5U) - 1U;

enum class AudioAtlasResidualTaperMethod : std::uint8_t {
    boundary_zero_smoothstep_v1,
};

struct AudioAtlasResidualTaper {
    AudioAtlasResidualTaperMethod method =
        AudioAtlasResidualTaperMethod::boundary_zero_smoothstep_v1;
    // Boundary-zero residuals are an explicit runtime seam invariant.
    double boundary_value = 0.0;
    double fraction_per_edge = 0.0;
    std::uint32_t frames_per_edge = 0;

    friend bool operator==(const AudioAtlasResidualTaper &,
                           const AudioAtlasResidualTaper &) = default;
};

enum class AudioAtlasSelectorMethod : std::uint8_t {
    splitmix64_shuffled_bags_v1,
};

enum class AudioAtlasSelectorChangePhase : std::uint8_t {
    phase_cycle_boundary,
};

struct AudioAtlasSelectorContract {
    AudioAtlasSelectorMethod method =
        AudioAtlasSelectorMethod::splitmix64_shuffled_bags_v1;
    bool no_adjacent_repeat = true;
    AudioAtlasSelectorChangePhase change_phase =
        AudioAtlasSelectorChangePhase::phase_cycle_boundary;

    friend bool operator==(const AudioAtlasSelectorContract &,
                           const AudioAtlasSelectorContract &) = default;
};

enum class AudioAtlasMeanInterpolationMethod : std::uint8_t {
    common_delay_phase_warp_v1,
};

enum class AudioAtlasMeanEnergyTarget : std::uint8_t {
    linear_anchor_rms,
};

enum class AudioAtlasResidualCorrelation : std::uint8_t {
    independent,
};

enum class AudioAtlasResidualEnergyTarget : std::uint8_t {
    linear_anchor_power,
};

enum class AudioAtlasResidualNormalization : std::uint8_t {
    sqrt_target_power_over_weighted_anchor_power,
};

struct AudioAtlasMeanInterpolationContract {
    AudioAtlasMeanInterpolationMethod method =
        AudioAtlasMeanInterpolationMethod::common_delay_phase_warp_v1;
    AudioAtlasMeanEnergyTarget energy_target =
        AudioAtlasMeanEnergyTarget::linear_anchor_rms;

    friend bool operator==(const AudioAtlasMeanInterpolationContract &,
                           const AudioAtlasMeanInterpolationContract &) = default;
};

struct AudioAtlasResidualInterpolationContract {
    AudioAtlasResidualCorrelation cross_cell_correlation =
        AudioAtlasResidualCorrelation::independent;
    AudioAtlasResidualEnergyTarget energy_target =
        AudioAtlasResidualEnergyTarget::linear_anchor_power;
    AudioAtlasResidualNormalization normalization =
        AudioAtlasResidualNormalization::
            sqrt_target_power_over_weighted_anchor_power;

    friend bool operator==(const AudioAtlasResidualInterpolationContract &,
                           const AudioAtlasResidualInterpolationContract &) =
        default;
};

struct AudioAtlasInterpolationContract {
    AudioAtlasMeanInterpolationContract mean;
    AudioAtlasResidualInterpolationContract residual;

    friend bool operator==(const AudioAtlasInterpolationContract &,
                           const AudioAtlasInterpolationContract &) = default;
};

struct AudioAtlasLoadLane {
    std::string id;
    double requested_throttle_01 = 0.0;
    std::uint32_t state_mask = 0;

    friend bool operator==(const AudioAtlasLoadLane &,
                           const AudioAtlasLoadLane &) = default;
};

struct AudioAtlasPhaseRouteRef {
    std::string route_id;
    std::string mean_artifact_id;
    std::string residual_artifact_id;
    double mean_rms = 0.0;
    double residual_power = 0.0;

    friend bool operator==(const AudioAtlasPhaseRouteRef &,
                           const AudioAtlasPhaseRouteRef &) = default;
};

struct AudioAtlasPhaseCell {
    std::string id;
    std::string load_lane_id;
    double rpm = 0.0;
    double load_coordinate_pa_abs = 0.0;
    double requested_throttle_01 = 0.0;
    std::uint32_t state_mask = 0;
    double shift_to_canonical_samples = 0.0;
    std::vector<AudioAtlasPhaseRouteRef> routes;

    friend bool operator==(const AudioAtlasPhaseCell &,
                           const AudioAtlasPhaseCell &) = default;
};

struct AudioAtlasPhaseTexture {
    std::uint32_t samples_per_cycle = 0;
    std::uint32_t residual_cycle_count = 0;
    AudioAtlasResidualTaper residual_taper;
    AudioAtlasSelectorContract selector;
    AudioAtlasInterpolationContract interpolation;
    std::vector<double> rpm_anchors;
    std::vector<AudioAtlasLoadLane> load_lanes;
    std::string reference_cell_id;
    std::vector<std::string> source_route_ids;
    std::vector<AudioAtlasPhaseCell> cells;

    friend bool operator==(const AudioAtlasPhaseTexture &,
                           const AudioAtlasPhaseTexture &) = default;
};

enum class AudioAtlasTransientDetectionMethod : std::uint8_t {
    causal_throttle_window_v1,
};

struct AudioAtlasTransientEnvelope {
    std::uint32_t attack_frames = 0;
    std::uint32_t hold_frames = 0;
    std::uint32_t release_frames = 0;
    double maximum_gain_linear = 0.0;

    friend bool operator==(const AudioAtlasTransientEnvelope &,
                           const AudioAtlasTransientEnvelope &) = default;
};

struct AudioAtlasTransientPolicy {
    AudioAtlasTransientDetectionMethod detection_method =
        AudioAtlasTransientDetectionMethod::causal_throttle_window_v1;
    std::uint32_t throttle_window_frames = 0;
    std::uint32_t minimum_history_frames = 0;
    double throttle_delta_onset_01 = 0.0;
    double throttle_delta_full_01 = 0.0;
    double throttle_delta_rearm_01 = 0.0;
    std::uint32_t refractory_frames = 0;
    std::uint32_t opposite_return_frames = 0;
    AudioAtlasTransientEnvelope rising;
    AudioAtlasTransientEnvelope falling;

    friend bool operator==(const AudioAtlasTransientPolicy &,
                           const AudioAtlasTransientPolicy &) = default;
};

enum class AudioAtlasTransientDirection : std::uint8_t {
    rising,
    falling,
};

struct AudioAtlasTransientRouteRef {
    std::string route_id;
    std::string artifact_id;

    friend bool operator==(const AudioAtlasTransientRouteRef &,
                           const AudioAtlasTransientRouteRef &) = default;
};

struct AudioAtlasTransientCell {
    std::string id;
    double rpm = 0.0;
    double load_coordinate_pa_abs = 0.0;
    double requested_throttle_01 = 0.0;
    std::uint32_t state_mask = 0;
    std::uint32_t cycle_count = 0;
    std::uint32_t samples_per_cycle = 0;
    double phase_origin_revolutions = 0.0;
    // Maps payload slot zero back onto the absolute host phase-cycle ordinal.
    std::uint32_t source_cycle_origin_ordinal_mod_cycle_count = 0;
    enum class SeamClosureMethod : std::uint8_t {
        phase_aligned_boundary_smoothstep_v1,
    } seam_closure_method =
        SeamClosureMethod::phase_aligned_boundary_smoothstep_v1;
    std::uint32_t seam_closure_frames_per_side = 0;
    std::vector<AudioAtlasTransientRouteRef> routes;

    friend bool operator==(const AudioAtlasTransientCell &,
                           const AudioAtlasTransientCell &) = default;
};

struct AudioAtlasTransientLayer {
    std::string id;
    AudioAtlasTransientDirection direction =
        AudioAtlasTransientDirection::rising;
    std::vector<std::string> source_route_ids;
    std::vector<AudioAtlasTransientCell> cells;

    friend bool operator==(const AudioAtlasTransientLayer &,
                           const AudioAtlasTransientLayer &) = default;
};

enum class AudioAtlasLifecycleEvent : std::uint8_t {
    startup,
    shutdown,
    limiter,
};

struct AudioAtlasBusArtifactRef {
    std::string bus_id;
    std::string artifact_id;

    friend bool operator==(const AudioAtlasBusArtifactRef &,
                           const AudioAtlasBusArtifactRef &) = default;
};

// The final knot is the exclusive end sentinel for each lifecycle artifact.
struct AudioAtlasLifecycleKnot {
    std::uint64_t frame = 0;
    double rpm = 0.0;
    double load_coordinate_pa_abs = 0.0;
    double requested_throttle_01 = 0.0;
    std::uint32_t state_mask = 0;

    friend bool operator==(const AudioAtlasLifecycleKnot &,
                           const AudioAtlasLifecycleKnot &) = default;
};

struct AudioAtlasLifecyclePerformance {
    std::string id;
    AudioAtlasLifecycleEvent event = AudioAtlasLifecycleEvent::startup;
    std::uint32_t entry_state_mask = 0;
    std::uint32_t exit_state_mask = 0;
    std::vector<AudioAtlasBusArtifactRef> artifacts;
    std::vector<AudioAtlasLifecycleKnot> timeline;

    friend bool operator==(const AudioAtlasLifecyclePerformance &,
                           const AudioAtlasLifecyclePerformance &) = default;
};

enum class AudioAtlasTransferKind : std::uint8_t {
    direct,
    fixed_spectrum_convolution,
};

struct AudioAtlasRouteTransfer {
    AudioAtlasTransferKind kind = AudioAtlasTransferKind::direct;
    // These are zero/empty for direct transfer and mandatory for convolution.
    std::uint32_t fft_size = 0;
    std::uint32_t coefficient_count = 0;
    std::string spectrum_artifact_id;

    friend bool operator==(const AudioAtlasRouteTransfer &,
                           const AudioAtlasRouteTransfer &) = default;
};

struct AudioAtlasPresentationRoute {
    std::string route_id;
    std::string output_bus_id;
    // The route transfer is evaluated once; this is wet*transfer(dry) +
    // (1-wet)*dry before the route enters its output bus.
    double wet_mix_01 = 0.0;
    AudioAtlasRouteTransfer transfer;

    friend bool operator==(const AudioAtlasPresentationRoute &,
                           const AudioAtlasPresentationRoute &) = default;
};

enum class AudioAtlasMasterMethod : std::uint8_t {
    canonical_adaptive_v1,
};

struct AudioAtlasMasterPresentation {
    AudioAtlasMasterMethod method =
        AudioAtlasMasterMethod::canonical_adaptive_v1;
    double volume_linear = 0.0;

    friend bool operator==(const AudioAtlasMasterPresentation &,
                           const AudioAtlasMasterPresentation &) = default;
};

struct AudioAtlasPresentation {
    AudioAtlasContentIdentity method_identity;
    AudioAtlasContentIdentity build_identity;
    std::uint32_t batch_frames = 0;
    // source_unit = captured_dry_sample * captured_to_source_scale.
    double captured_to_source_scale = 0.0;
    std::vector<AudioAtlasPresentationRoute> source_routes;
    AudioAtlasMasterPresentation master;

    friend bool operator==(const AudioAtlasPresentation &,
                           const AudioAtlasPresentation &) = default;
};

enum class AudioAtlasArtifactEncoding : std::uint8_t {
    float32le,
    complex_float64le,
};

// element_count counts scalar Float32 samples or interleaved-complex Float64
// values (one real/imaginary pair is one complex element), according to encoding.
struct AudioAtlasArtifact {
    std::string id;
    std::string path;
    AudioAtlasArtifactEncoding encoding = AudioAtlasArtifactEncoding::float32le;
    std::uint64_t element_count = 0;
    std::uint64_t byte_count = 0;
    Sha256Digest sha256;

    friend bool operator==(const AudioAtlasArtifact &,
                           const AudioAtlasArtifact &) = default;
};

struct AudioAtlasProvenance {
    AudioAtlasContentIdentity engine;
    AudioAtlasContentIdentity bake_document;
    AudioAtlasContentIdentity renderer_build;
    ProvenanceBundleRef source_inputs;

    friend bool operator==(const AudioAtlasProvenance &,
                           const AudioAtlasProvenance &) = default;
};

struct AudioAtlasManifest {
    std::string schema = std::string{kAudioAtlasSchema};
    std::string id;
    std::string engine;
    std::uint64_t public_seed = 0;
    AudioAtlasAudioFormat audio;
    AudioAtlasDomain domain;
    AudioAtlasPhaseTexture phase_texture;
    AudioAtlasTransientPolicy transient_policy;
    std::vector<AudioAtlasTransientLayer> transient_layers;
    std::vector<AudioAtlasLifecyclePerformance> lifecycle_performances;
    AudioAtlasPresentation presentation;
    std::vector<AudioAtlasArtifact> artifacts;
    AudioAtlasProvenance provenance;

    friend bool operator==(const AudioAtlasManifest &,
                           const AudioAtlasManifest &) = default;
};

[[nodiscard]] ValidationReport validate(const AudioAtlasManifest &manifest);

} // namespace engine_sim_offline::contract
