#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/quantity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine_sim_offline::authoring {

inline constexpr auto kAtlasBakeSchema = "engine-sim-offline/atlas-bake";

struct AtlasBakeTag;
struct AtlasBakeScenarioSourceTag;
struct AtlasBakeMovingSegmentTag;

using AtlasBakeId = StableId<AtlasBakeTag>;
using AtlasBakeScenarioSourceId = StableId<AtlasBakeScenarioSourceTag>;
using AtlasBakeScenarioSourceRef = StableRef<AtlasBakeScenarioSourceTag>;
using AtlasBakeMovingSegmentId = StableId<AtlasBakeMovingSegmentTag>;

struct AtlasBakeAudio {
    RationalRate sample_rate;
    // Authored order is the immutable atlas delivery order.
    std::vector<AudioBusRef> buses;

    friend bool operator==(const AtlasBakeAudio &, const AtlasBakeAudio &) = default;
};

struct AtlasBakeDomain {
    double minimum_rpm = 0.0;
    double maximum_rpm = 0.0;
    double minimum_load_coordinate = 0.0;
    double maximum_load_coordinate = 0.0;

    friend bool operator==(const AtlasBakeDomain &, const AtlasBakeDomain &) = default;
};

struct AtlasBakeScenarioSource {
    AtlasBakeScenarioSourceId id;
    std::string uri;

    friend bool operator==(const AtlasBakeScenarioSource &,
                           const AtlasBakeScenarioSource &) = default;
};

struct AtlasBakeRpmRange {
    double minimum = 0.0;
    double maximum = 0.0;

    friend bool operator==(const AtlasBakeRpmRange &,
                           const AtlasBakeRpmRange &) = default;
};

struct AtlasBakeNormalizedRpmSlopeRange {
    double minimum_per_second = 0.0;
    double maximum_per_second = 0.0;

    friend bool operator==(const AtlasBakeNormalizedRpmSlopeRange &,
                           const AtlasBakeNormalizedRpmSlopeRange &) = default;
};

enum class AtlasBakeMovingDirection : std::uint8_t {
    rising,
    falling,
};

struct AtlasBakeHandoffEnvelope {
    std::uint32_t transition_frames = 0;
    double maximum_rpm_error = 0.0;
    double maximum_normalized_rpm_slope_error_per_second = 0.0;
    double maximum_load_error = 0.0;
    double maximum_crank_phase_error_revolutions = 0.0;

    friend bool operator==(const AtlasBakeHandoffEnvelope &,
                           const AtlasBakeHandoffEnvelope &) = default;
};

// One source scenario records one chronological performance. The captured range
// includes source context at both ends; the usable range is its strict interior.
struct AtlasBakeMovingSegment {
    AtlasBakeMovingSegmentId id;
    AtlasBakeMovingDirection direction = AtlasBakeMovingDirection::rising;
    double load_coordinate = 0.0;
    std::uint32_t state_mask = 0;
    AtlasBakeNormalizedRpmSlopeRange normalized_rpm_slope;
    AtlasBakeRpmRange captured_rpm;
    AtlasBakeRpmRange usable_rpm;
    AtlasBakeScenarioSourceRef scenario;
    AtlasBakeHandoffEnvelope handoff;

    friend bool operator==(const AtlasBakeMovingSegment &,
                           const AtlasBakeMovingSegment &) = default;
};

// Deliberate placeholders: the first sound-bearing slice admits moving material
// only. The corresponding authored arrays must be present and empty.
struct AtlasBakeStationaryTile {
    friend bool operator==(const AtlasBakeStationaryTile &,
                           const AtlasBakeStationaryTile &) = default;
};
struct AtlasBakeTransientPerformance {
    friend bool operator==(const AtlasBakeTransientPerformance &,
                           const AtlasBakeTransientPerformance &) = default;
};
struct AtlasBakeLifecyclePerformance {
    friend bool operator==(const AtlasBakeLifecyclePerformance &,
                           const AtlasBakeLifecyclePerformance &) = default;
};

struct AtlasBakeDocument {
    std::string schema = kAtlasBakeSchema;
    AtlasBakeId id;
    EngineRef engine;
    std::uint64_t public_seed = 0;
    AtlasBakeAudio audio;
    AtlasBakeDomain domain;
    std::vector<AtlasBakeScenarioSource> scenario_sources;
    std::vector<AtlasBakeMovingSegment> moving_segments;
    std::vector<AtlasBakeStationaryTile> stationary_tiles;
    std::vector<AtlasBakeTransientPerformance> transient_performances;
    std::vector<AtlasBakeLifecyclePerformance> lifecycle_performances;

    friend bool operator==(const AtlasBakeDocument &,
                           const AtlasBakeDocument &) = default;
};

} // namespace engine_sim_offline::authoring
