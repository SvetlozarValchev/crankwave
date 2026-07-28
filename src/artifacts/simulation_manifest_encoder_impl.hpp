#pragma once

#include "manifest_encoder_impl.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

namespace engine_sim_offline::artifacts::detail {

// Canonical simulation-v1 input writers. These deliberately write only their
// respective typed values so the same field enumeration can be embedded in both a
// request-identity document and a completed simulation manifest.
[[nodiscard]] bool write_engine_spec(CanonicalJsonWriter &writer,
                                     const contract::EngineSpec &engine);
[[nodiscard]] bool write_render_scenario(CanonicalJsonWriter &writer,
                                         const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::artifacts::detail
