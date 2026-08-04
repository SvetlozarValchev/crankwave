#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/quantity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine_sim_offline::authoring {

struct PackageBakeTag;
struct PackageBakeScenarioSourceTag;
struct PackageBakeRunningPlaneTag;

using PackageBakeId = StableId<PackageBakeTag>;
using PackageBakeScenarioSourceId = StableId<PackageBakeScenarioSourceTag>;
using PackageBakeScenarioSourceRef = StableRef<PackageBakeScenarioSourceTag>;
using PackageBakeRunningPlaneId = StableId<PackageBakeRunningPlaneTag>;

struct PackageBakeAudio {
    RationalRate sample_rate;
    // Authored order is the package delivery order.
    std::vector<AudioBusRef> buses;

    friend bool operator==(const PackageBakeAudio &,
                           const PackageBakeAudio &) = default;
};

struct PackageBakeScenarioSource {
    PackageBakeScenarioSourceId id;
    std::string uri;

    friend bool operator==(const PackageBakeScenarioSource &,
                           const PackageBakeScenarioSource &) = default;
};

struct PackageBakeRpmRange {
    Quantity minimum;
    Quantity maximum;

    friend bool operator==(const PackageBakeRpmRange &,
                           const PackageBakeRpmRange &) = default;
};

enum class PackageBakeRunningDirection : std::uint8_t {
    rising,
    falling,
};

struct PackageBakeRunningPlane {
    PackageBakeRunningPlaneId id;
    double load_coordinate = 0.0;
    PackageBakeRunningDirection direction = PackageBakeRunningDirection::rising;
    PackageBakeScenarioSourceRef scenario;

    friend bool operator==(const PackageBakeRunningPlane &,
                           const PackageBakeRunningPlane &) = default;
};

struct PackageBakeIdle {
    PackageBakeScenarioSourceRef scenario;

    friend bool operator==(const PackageBakeIdle &, const PackageBakeIdle &) = default;
};

struct PackageBakeRunning {
    PackageBakeRpmRange rpm_range;
    // Planes are ordered by strictly increasing load_coordinate from -1 to +1.
    std::vector<PackageBakeRunningPlane> planes;
    PackageBakeIdle idle;

    friend bool operator==(const PackageBakeRunning &,
                           const PackageBakeRunning &) = default;
};

// Lifecycle performances are deliberately outside the first package gate. The
// required authored array is therefore empty; a future contract will replace this
// placeholder when the first event shape is admitted.
struct PackageBakeEvent {
    friend bool operator==(const PackageBakeEvent &,
                           const PackageBakeEvent &) = default;
};

struct PackageBakeDocument {
    std::string schema = "engine-sim-offline/package-bake";
    PackageBakeId id;
    EngineRef engine;
    std::uint64_t public_seed = 0;
    PackageBakeAudio audio;
    std::vector<PackageBakeScenarioSource> scenario_sources;
    PackageBakeRunning running;
    std::vector<PackageBakeEvent> events;

    friend bool operator==(const PackageBakeDocument &,
                           const PackageBakeDocument &) = default;
};

} // namespace engine_sim_offline::authoring
