#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/render.hpp"

namespace engine_sim_offline::render_detail {

struct CompiledScenarioProjection {
    RenderSpecification specification;
    contract::RenderScenario scenario;
};

// The sole private bridge through CompiledScenario's immutable ownership boundary.
// Compile remains independent of render; only the render target sees this type.
class CompiledScenarioAccess final {
  public:
    [[nodiscard]] static CompiledScenarioProjection
    project(const compile::CompiledScenario &scenario);
};

} // namespace engine_sim_offline::render_detail
