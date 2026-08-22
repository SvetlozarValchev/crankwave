#pragma once

#include "crankwave/authoring/engine_document.hpp"
#include "crankwave/authoring/quantity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace crankwave::authoring {

inline constexpr auto kAtlasBakeSchema = "crankwave/atlas-bake";

struct AtlasBakeTag;
struct AtlasBakeLoadLaneTag;
struct AtlasBakeLifecycleCaptureTag;

using AtlasBakeId = StableId<AtlasBakeTag>;
using AtlasBakeLoadLaneId = StableId<AtlasBakeLoadLaneTag>;
using AtlasBakeLoadLaneRef = StableRef<AtlasBakeLoadLaneTag>;
using AtlasBakeLifecycleCaptureId = StableId<AtlasBakeLifecycleCaptureTag>;

struct AtlasBakeAudio {
    RationalRate sample_rate;
    // Authored order is the immutable dry-source route order.
    std::vector<SourceRouteRef> routes;
    AudioBusRef output_bus;

    friend bool operator==(const AtlasBakeAudio &, const AtlasBakeAudio &) = default;
};

enum class AtlasBakeLoadCoordinate : std::uint8_t {
    measured_intake_manifold_pressure_pa_abs,
};

struct AtlasBakeDomain {
    double minimum_rpm = 0.0;
    double maximum_rpm = 0.0;
    AtlasBakeLoadCoordinate load_coordinate =
        AtlasBakeLoadCoordinate::measured_intake_manifold_pressure_pa_abs;
    double phase_cycle_revolutions = 0.0;
    std::uint32_t running_state_mask = 0;

    friend bool operator==(const AtlasBakeDomain &, const AtlasBakeDomain &) = default;
};

struct AtlasBakeCapture {
    RationalRate physics_rate;
    std::uint32_t samples_per_cycle = 0;
    std::uint32_t cycles_per_cell = 0;
    std::uint32_t guard_cycles_before = 0;
    std::uint32_t guard_cycles_after = 0;
    Quantity preparation_duration;
    double residual_taper_fraction_per_edge = 0.0;
    std::uint32_t maximum_concurrency = 0;
    // One declarative held-speed scenario supplies fuel, ambient, thermal, and
    // crankcase context. The baker clones it and overrides cell coordinates.
    std::string scenario_template_uri;

    friend bool operator==(const AtlasBakeCapture &,
                           const AtlasBakeCapture &) = default;
};

struct AtlasBakeLoadLane {
    AtlasBakeLoadLaneId id;
    double requested_throttle_01 = 0.0;

    friend bool operator==(const AtlasBakeLoadLane &,
                           const AtlasBakeLoadLane &) = default;
};

struct AtlasBakePhaseAlignmentReference {
    double rpm = 0.0;
    AtlasBakeLoadLaneRef load_lane;

    friend bool operator==(const AtlasBakePhaseAlignmentReference &,
                           const AtlasBakePhaseAlignmentReference &) = default;
};

struct AtlasBakePhaseAlignment {
    AtlasBakePhaseAlignmentReference reference;

    friend bool operator==(const AtlasBakePhaseAlignment &,
                           const AtlasBakePhaseAlignment &) = default;
};

enum class AtlasBakeTransientMotion : std::uint8_t {
    prescribed_exponential_speed,
};

struct AtlasBakeTransientCapture {
    AtlasBakeTransientMotion motion =
        AtlasBakeTransientMotion::prescribed_exponential_speed;
    std::uint32_t cycles_per_cell = 0;
    double normalized_rpm_slope_per_second = 0.0;
    double seam_closure_fraction_per_edge = 0.0;

    friend bool operator==(const AtlasBakeTransientCapture &,
                           const AtlasBakeTransientCapture &) = default;
};

struct AtlasBakeTransientEnvelope {
    double maximum_gain = 0.0;
    Quantity attack_duration;
    Quantity hold_duration;
    Quantity release_duration;

    friend bool operator==(const AtlasBakeTransientEnvelope &,
                           const AtlasBakeTransientEnvelope &) = default;
};

struct AtlasBakeTransientPolicy {
    Quantity detection_window;
    double onset_delta_01 = 0.0;
    double full_delta_01 = 0.0;
    double rearm_delta_01 = 0.0;
    Quantity refractory_duration;
    AtlasBakeTransientEnvelope rising;
    AtlasBakeTransientEnvelope falling;

    friend bool operator==(const AtlasBakeTransientPolicy &,
                           const AtlasBakeTransientPolicy &) = default;
};

enum class AtlasBakeLifecycleEvent : std::uint8_t {
    startup,
    shutdown,
    limiter,
};

struct AtlasBakeLifecycleCapture {
    AtlasBakeLifecycleCaptureId id;
    AtlasBakeLifecycleEvent event = AtlasBakeLifecycleEvent::startup;
    std::string scenario_uri;

    friend bool operator==(const AtlasBakeLifecycleCapture &,
                           const AtlasBakeLifecycleCapture &) = default;
};

struct AtlasBakeDocument {
    std::string schema = kAtlasBakeSchema;
    AtlasBakeId id;
    EngineRef engine;
    std::uint64_t public_seed = 0;
    AtlasBakeAudio audio;
    AtlasBakeDomain domain;
    AtlasBakeCapture capture;
    std::vector<double> rpm_anchors;
    std::vector<AtlasBakeLoadLane> load_lanes;
    AtlasBakePhaseAlignment phase_alignment;
    AtlasBakeTransientCapture transient_capture;
    AtlasBakeTransientPolicy transient_policy;
    std::vector<AtlasBakeLifecycleCapture> lifecycle_captures;

    friend bool operator==(const AtlasBakeDocument &,
                           const AtlasBakeDocument &) = default;
};

} // namespace crankwave::authoring
