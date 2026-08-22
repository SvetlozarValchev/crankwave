#pragma once

#include "crankwave/compile.hpp"
#include "crankwave/contract/result.hpp"
#include "crankwave/publication.hpp"

namespace crankwave {

// Runs the portable EngineSession without wall-time pacing and publishes the
// resulting canonical buses through one native transaction.
[[nodiscard]] contract::RenderResult
bake(const compile::CompiledScenario &scenario, RenderSink &sink,
     RenderControl control = {});

// Rebinds a terminal bake result to the exact immutable compiled request.
[[nodiscard]] contract::ValidationReport
validate_bake_result(const contract::RenderResult &result,
                     const compile::CompiledScenario &scenario);

} // namespace crankwave
