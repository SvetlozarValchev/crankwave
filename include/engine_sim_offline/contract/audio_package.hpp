#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::contract {

inline constexpr std::string_view kAudioPackageSchema =
    "engine-sim-offline/audio-package";

// An immutable input identity. Package identity is intentionally only an authored
// semantic ID: embedding a digest of the package in its own manifest would be
// self-referential. All inputs and payload artifacts carry content digests.
struct AudioPackageContentIdentity {
    std::string id;
    Sha256Digest sha256;

    friend bool operator==(const AudioPackageContentIdentity &,
                           const AudioPackageContentIdentity &) = default;
};

struct AudioPackageIdentity {
    std::string package_id;
    AudioPackageContentIdentity engine;
    AudioPackageContentIdentity bake_plan;

    friend bool operator==(const AudioPackageIdentity &,
                           const AudioPackageIdentity &) = default;
};

struct AudioPackageProvenance {
    AudioPackageContentIdentity renderer_build;
    ProvenanceBundleRef source_inputs;

    friend bool operator==(const AudioPackageProvenance &,
                           const AudioPackageProvenance &) = default;
};

enum class AudioPackageAudioContainer : std::uint8_t {
    wav,
};

struct AudioPackageAudioFormat {
    RationalRateHz sample_rate;
    AudioPackageAudioContainer container = AudioPackageAudioContainer::wav;
    AudioSampleEncoding encoding = AudioSampleEncoding::float32le;
    AudioChannelLayout channel_layout = AudioChannelLayout::mono;

    friend bool operator==(const AudioPackageAudioFormat &,
                           const AudioPackageAudioFormat &) = default;
};

enum class AudioPackageBusKind : std::uint8_t {
    master_engine_audition,
    source_route,
};

enum class AudioPackageBusDisposition : std::uint8_t {
    monitor_mix,
    positional_emitter,
};

struct AudioPackageSourceRouteDescriptor {
    SourceRouteKind kind = SourceRouteKind::unspecified;
    std::string semantic_id;
    std::string emitter_anchor_id;

    friend bool operator==(const AudioPackageSourceRouteDescriptor &,
                           const AudioPackageSourceRouteDescriptor &) = default;
};

// Buses remain in authored delivery order. A master has no single source-route
// identity. A route stem carries enough physical metadata for a host to place it as
// an emitter without recovering information from the source engine document.
struct AudioPackageBusDescriptor {
    std::string id;
    AudioPackageBusKind kind = AudioPackageBusKind::master_engine_audition;
    AudioPackageBusDisposition disposition = AudioPackageBusDisposition::monitor_mix;
    std::optional<AudioPackageSourceRouteDescriptor> source_route;

    friend bool operator==(const AudioPackageBusDescriptor &,
                           const AudioPackageBusDescriptor &) = default;
};

struct AudioPackageArtifact {
    std::string id;
    std::string relative_path;
    std::uint64_t frame_count = 0;
    std::uint64_t byte_count = 0;
    Sha256Digest payload_sha256;

    friend bool operator==(const AudioPackageArtifact &,
                           const AudioPackageArtifact &) = default;
};

// Exact boundaries use an equal bracket and canonical +0. An interpolated boundary
// uses adjacent source frames and a fraction strictly between zero and one.
struct AudioPackageSourceBoundary {
    std::uint64_t left_frame = 0;
    std::uint64_t right_frame = 0;
    double fraction_from_left_01 = 0.0;

    friend bool operator==(const AudioPackageSourceBoundary &,
                           const AudioPackageSourceBoundary &) = default;
};

struct AudioPackageCycleUnit {
    std::uint64_t completed_cycle_ordinal = 0;
    AudioPackageSourceBoundary start;
    AudioPackageSourceBoundary end;
    double canonical_rpm = 0.0;
    double measured_rpm = 0.0;
    double average_signed_load = 0.0;
    // Held-speed idle sources may not expose the directional load-calibration
    // signal. Null is unavailable evidence, never an inferred zero torque.
    std::optional<double> average_net_torque_nm;
    double average_requested_throttle_01 = 0.0;
    double average_resolved_throttle_01 = 0.0;
    // One invariant discrete state over the half-open unit. The normal-running
    // contract rejects any nonzero transition mask instead of averaging booleans.
    std::uint32_t state_mask = 0;
    std::uint32_t transition_mask = 0;

    friend bool operator==(const AudioPackageCycleUnit &,
                           const AudioPackageCycleUnit &) = default;
};

// Each lane has exactly one artifact per package bus, in the same order as the bus
// registry. The unit table is consequently shared by every bus in that lane.
struct AudioPackageLaneArtifactRef {
    std::string bus_id;
    std::string artifact_id;

    friend bool operator==(const AudioPackageLaneArtifactRef &,
                           const AudioPackageLaneArtifactRef &) = default;
};

enum class AudioPackageRunningDirection : std::uint8_t {
    rising,
    falling,
};

struct AudioPackageRunningPlane {
    std::string id;
    double load_coordinate = 0.0;
    AudioPackageRunningDirection direction = AudioPackageRunningDirection::rising;
    AudioPackageContentIdentity source_scenario;
    std::vector<AudioPackageLaneArtifactRef> artifacts;
    std::vector<AudioPackageCycleUnit> units;

    friend bool operator==(const AudioPackageRunningPlane &,
                           const AudioPackageRunningPlane &) = default;
};

struct AudioPackageRpmGrid {
    // Full bank extrema, including the same number of selector-padding rows on
    // either side of the requested playback domain.
    double minimum_rpm = 0.0;
    double playback_minimum_rpm = 0.0;
    double playback_maximum_rpm = 0.0;
    double maximum_rpm = 0.0;
    double spacing_rpm = 0.0;
    std::uint32_t padding_rows_per_side = 0;
    std::uint32_t neighbor_radius_rows = 0;
    std::uint32_t edge_guard_frames = 0;
    double maximum_assignment_error_rpm = 0.0;

    friend bool operator==(const AudioPackageRpmGrid &,
                           const AudioPackageRpmGrid &) = default;
};

struct AudioPackageIdle {
    AudioPackageContentIdentity source_scenario;
    std::vector<AudioPackageLaneArtifactRef> artifacts;
    std::vector<AudioPackageCycleUnit> units;

    friend bool operator==(const AudioPackageIdle &,
                           const AudioPackageIdle &) = default;
};

struct AudioPackageLoadCalibration {
    // This is the exact modeled net-shaft subset used to compare coast, part,
    // and power captures. It is not promoted to complete physical brake torque.
    Completeness completeness = Completeness::incomplete;
    TorqueTermMask included_terms = 0;
    TorqueTermMask omitted_terms = 0;

    friend bool operator==(const AudioPackageLoadCalibration &,
                           const AudioPackageLoadCalibration &) = default;
};

struct AudioPackageRunning {
    std::uint32_t cycle_revolutions = 2;
    std::uint64_t selector_seed = 0;
    // Added to exact physical crank-boundary delivery coordinates before they
    // become source-tape unit boundaries. This is the common reconstruction
    // delay only; modeled route propagation and transfer phase remain in PCM.
    double cycle_signal_alignment_frames = 0.0;
    AudioPackageLoadCalibration load_calibration;
    AudioPackageRpmGrid rpm_grid;
    std::vector<AudioPackageRunningPlane> planes;
    AudioPackageIdle idle;

    friend bool operator==(const AudioPackageRunning &,
                           const AudioPackageRunning &) = default;
};

// Lifecycle performances are intentionally outside the first normal-running gate.
// The required array must remain empty until the event contract replaces this type.
struct AudioPackageEvent {
    friend bool operator==(const AudioPackageEvent &,
                           const AudioPackageEvent &) = default;
};

struct AudioPackageManifest {
    std::string schema = std::string{kAudioPackageSchema};
    AudioPackageIdentity identity;
    AudioPackageProvenance provenance;
    AudioPackageAudioFormat audio;
    std::vector<AudioPackageBusDescriptor> buses;
    AudioPackageRunning running;
    std::vector<AudioPackageEvent> events;
    std::vector<AudioPackageArtifact> artifacts;

    friend bool operator==(const AudioPackageManifest &,
                           const AudioPackageManifest &) = default;
};

[[nodiscard]] ValidationReport validate(const AudioPackageManifest &manifest);

} // namespace engine_sim_offline::contract
