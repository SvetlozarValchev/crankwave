#pragma once

#include "engine_sim_offline/package_bake.hpp"

#include <memory>
#include <string>
#include <vector>

namespace engine_sim_offline::detail {

struct CompiledPackageBakeStorage {
    std::string id;
    compile::CompiledEngine engine;
    std::uint64_t public_seed = 0;
    compile::SiRate audio_sample_rate;
    std::vector<CompiledPackageBakeAudioBus> audio_buses;
    PackageBakeMethodGeometry geometry;
    CompiledPackageBakeRpmRange rpm_range;
    std::vector<CompiledPackageBakeScenarioSource> scenario_sources;
    std::vector<CompiledPackageBakeRunningPlane> running_planes;
    std::size_t idle_scenario_source_index = 0;
};

} // namespace engine_sim_offline::detail
