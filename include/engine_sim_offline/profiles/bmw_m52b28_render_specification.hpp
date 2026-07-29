#pragma once

#include "engine_sim_offline/render.hpp"

namespace engine_sim_offline::profiles {

// Binds one already-resolved canonical BMW operating request to the product
// presentation path. The caller keeps scenario construction separate because its
// values and provenance belong to that specific render request. No fixture bytes,
// impulse responses, or reference-oracle material enter the returned specification.
[[nodiscard]] RenderSpecification
make_bmw_m52b28_render_specification(contract::EngineSpec engine,
                                     contract::ProvenanceLedger simulation_provenance);

} // namespace engine_sim_offline::profiles
