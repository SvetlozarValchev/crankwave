#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/publication.hpp"

namespace engine_sim_offline {

// Runs the portable EngineSession without wall-time pacing and publishes the
// resulting canonical buses through one native transaction.
[[nodiscard]] contract::RenderResult
bake(const compile::CompiledScenario &scenario, RenderSink &sink,
     RenderControl control = {});

// Rebinds a terminal bake result to the exact immutable compiled request.
[[nodiscard]] contract::ValidationReport
validate_bake_result(const contract::RenderResult &result,
                     const compile::CompiledScenario &scenario);

} // namespace engine_sim_offline
