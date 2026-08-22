#pragma once

#include "crankwave/atlas_bake.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace crankwave::detail {

struct CompiledAtlasBakeStorage {
    std::string id;
    compile::CompiledEngine engine;
    std::uint64_t public_seed = 0;
    compile::SiRate audio_sample_rate;
    std::vector<CompiledAtlasBakeAudioBus> audio_buses;
    authoring::AtlasBakeDomain domain;
    std::vector<CompiledAtlasBakeScenarioSource> scenario_sources;
    std::vector<CompiledAtlasBakeMovingSegment> moving_segments;
};

} // namespace crankwave::detail
