#pragma once

#include "engine_sim_offline/render.hpp"

#include <cstddef>
#include <vector>

namespace engine_sim_offline::reference {

// Extends one already resolved BMW simulation request with the accepted, local-only
// P1.8 exhaust presentation. The IR payload must be the content-pinned raw
// smooth_39.wav asset; this boundary verifies its catalog identity again before
// making it executable. No reference capture, parity lane, or audio oracle enters the
// returned production RenderSpecification.
[[nodiscard]] RenderSpecification make_bmw_p18_render_specification(
    contract::EngineSpec engine, contract::ProvenanceLedger simulation_provenance,
    std::vector<std::byte> verified_configured_ir_wave_bytes);

} // namespace engine_sim_offline::reference
