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

enum class AudioAtlasChannelLayout : std::uint8_t {
    mono,
};

struct AudioAtlasBus {
    std::string id;

    friend bool operator==(const AudioAtlasBus &,
                           const AudioAtlasBus &) = default;
};

struct AudioAtlasAudioFormat {
    std::uint32_t sample_rate_hz = 0;
    AudioAtlasSampleEncoding encoding = AudioAtlasSampleEncoding::float32le;
    AudioAtlasChannelLayout channel_layout = AudioAtlasChannelLayout::mono;
    // Authored order is the immutable runtime delivery order.
    std::vector<AudioAtlasBus> buses;

    friend bool operator==(const AudioAtlasAudioFormat &,
                           const AudioAtlasAudioFormat &) = default;
};

struct AudioAtlasDomain {
    double minimum_rpm = 0.0;
    double maximum_rpm = 0.0;
    double minimum_load_coordinate = 0.0;
    double maximum_load_coordinate = 0.0;

    friend bool operator==(const AudioAtlasDomain &,
                           const AudioAtlasDomain &) = default;
};

struct AudioAtlasArtifact {
    std::string id;
    std::string relative_path;
    std::uint64_t frame_count = 0;
    std::uint64_t byte_count = 0;
    Sha256Digest payload_sha256;

    friend bool operator==(const AudioAtlasArtifact &,
                           const AudioAtlasArtifact &) = default;
};

struct AudioAtlasBusArtifactRef {
    std::string bus_id;
    std::string artifact_id;

    friend bool operator==(const AudioAtlasBusArtifactRef &,
                           const AudioAtlasBusArtifactRef &) = default;
};

struct AudioAtlasFrameRange {
    // Half-open delivery-frame range [begin, end).
    std::uint64_t begin = 0;
    std::uint64_t end = 0;

    friend bool operator==(const AudioAtlasFrameRange &,
                           const AudioAtlasFrameRange &) = default;
};

struct AudioAtlasRpmRange {
    double minimum = 0.0;
    double maximum = 0.0;

    friend bool operator==(const AudioAtlasRpmRange &,
                           const AudioAtlasRpmRange &) = default;
};

struct AudioAtlasNormalizedRpmSlopeRange {
    double minimum_per_second = 0.0;
    double maximum_per_second = 0.0;

    friend bool operator==(const AudioAtlasNormalizedRpmSlopeRange &,
                           const AudioAtlasNormalizedRpmSlopeRange &) = default;
};

// Atlas state describes audible engine state, never the rig that happened to
// capture it. In particular, HeldDyno's motion-owner flag is deliberately absent
// so the same material can follow a free engine or vehicle drivetrain.
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

enum class AudioAtlasMovingDirection : std::uint8_t {
    rising,
    falling,
};

// A knot is the exact operating state at one PCM-frame address. Consecutive knots
// define a half-open interval [knots[i].frame, knots[i + 1].frame). Continuous
// coordinates are linearly reconstructed over that interval; state_mask and
// transition_mask belong to its left boundary. The final knot is the exclusive end
// sentinel and is never rendered as a frame.
struct AudioAtlasTimelineKnot {
    std::uint64_t frame = 0;
    double rpm = 0.0;
    double rpm_slope_rpm_per_second = 0.0;
    double requested_throttle_01 = 0.0;
    double signed_load_coordinate = 0.0;
    double manifold_pressure_pa_abs = 0.0;
    // Unwrapped physical crank position. It may seed phase, but it never owns a
    // normal-running runtime seam.
    double unwrapped_crank_revolutions = 0.0;
    std::uint32_t state_mask = 0;
    std::uint32_t transition_mask = 0;

    friend bool operator==(const AudioAtlasTimelineKnot &,
                           const AudioAtlasTimelineKnot &) = default;
};

struct AudioAtlasStateTimeline {
    std::vector<AudioAtlasTimelineKnot> knots;

    friend bool operator==(const AudioAtlasStateTimeline &,
                           const AudioAtlasStateTimeline &) = default;
};

// Exact frames use an equal bracket and canonical +0. Fractional coordinates use
// adjacent frames and a fraction strictly between zero and one.
struct AudioAtlasFractionalFrame {
    std::uint64_t left_frame = 0;
    std::uint64_t right_frame = 0;
    double fraction_from_left_01 = 0.0;

    friend bool operator==(const AudioAtlasFractionalFrame &,
                           const AudioAtlasFractionalFrame &) = default;
};

struct AudioAtlasCrankBoundary {
    std::uint64_t completed_cycle_ordinal = 0;
    AudioAtlasFractionalFrame position;

    friend bool operator==(const AudioAtlasCrankBoundary &,
                           const AudioAtlasCrankBoundary &) = default;
};

struct AudioAtlasHandoffEnvelope {
    std::uint32_t transition_frames = 0;
    double maximum_rpm_error = 0.0;
    double maximum_normalized_rpm_slope_error_per_second = 0.0;
    double maximum_load_error = 0.0;
    double maximum_crank_phase_error_revolutions = 0.0;

    friend bool operator==(const AudioAtlasHandoffEnvelope &,
                           const AudioAtlasHandoffEnvelope &) = default;
};

// PCM remains chronological from captured_frames.begin to captured_frames.end.
// usable_frames is a strict interior range, leaving source context at both ends for
// a single bounded handoff. It is never divided into independently repeated cycles.
struct AudioAtlasMovingSegment {
    std::string id;
    AudioAtlasMovingDirection direction = AudioAtlasMovingDirection::rising;
    double load_coordinate = 0.0;
    std::uint32_t state_mask = 0;
    AudioAtlasNormalizedRpmSlopeRange normalized_rpm_slope;
    AudioAtlasFrameRange captured_frames;
    AudioAtlasFrameRange usable_frames;
    AudioAtlasRpmRange captured_rpm;
    AudioAtlasRpmRange usable_rpm;
    AudioAtlasContentIdentity source_scenario;
    AudioAtlasContentIdentity capture_configuration;
    std::vector<AudioAtlasBusArtifactRef> artifacts;
    AudioAtlasStateTimeline timeline;
    std::vector<AudioAtlasCrankBoundary> crank_boundaries;
    AudioAtlasHandoffEnvelope handoff;

    friend bool operator==(const AudioAtlasMovingSegment &,
                           const AudioAtlasMovingSegment &) = default;
};

// These types deliberately have no provisional fields. Their arrays must remain
// empty until a sound-bearing slice replaces each placeholder with an admitted
// current contract.
struct AudioAtlasStationaryTile {
    friend bool operator==(const AudioAtlasStationaryTile &,
                           const AudioAtlasStationaryTile &) = default;
};

struct AudioAtlasTransientPerformance {
    friend bool operator==(const AudioAtlasTransientPerformance &,
                           const AudioAtlasTransientPerformance &) = default;
};

struct AudioAtlasLifecyclePerformance {
    friend bool operator==(const AudioAtlasLifecyclePerformance &,
                           const AudioAtlasLifecyclePerformance &) = default;
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
    std::vector<AudioAtlasMovingSegment> moving_segments;
    std::vector<AudioAtlasStationaryTile> stationary_tiles;
    std::vector<AudioAtlasTransientPerformance> transient_performances;
    std::vector<AudioAtlasLifecyclePerformance> lifecycle_performances;
    std::vector<AudioAtlasArtifact> artifacts;
    AudioAtlasProvenance provenance;

    friend bool operator==(const AudioAtlasManifest &,
                           const AudioAtlasManifest &) = default;
};

[[nodiscard]] ValidationReport validate(const AudioAtlasManifest &manifest);

} // namespace engine_sim_offline::contract
