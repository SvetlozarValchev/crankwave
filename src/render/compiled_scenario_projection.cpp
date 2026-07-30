#include "render/compiled_scenario_projection.hpp"

#include "compile/compiled_model_storage.hpp"

#include <ranges>
#include <utility>

namespace engine_sim_offline::render_detail {

CompiledScenarioProjection
CompiledScenarioAccess::project(const compile::CompiledScenario &scenario) {
    const auto &scenario_storage = *scenario.storage_;
    const auto &engine = scenario_storage.engine->resolved;
    const auto &resolved_scenario = scenario_storage.resolved;

    RenderSpecification specification{
        engine.engine,
        engine.presentation,
        engine.randomness,
        resolved_scenario.combined_provenance,
        resolved_scenario.source_matrix,
        {},
    };
    specification.asset_payloads.reserve(specification.presentation.assets.size());
    for (const auto &asset : specification.presentation.assets) {
        const auto payload =
            std::ranges::find_if(engine.assets, [&](const auto &candidate) {
                return candidate.kind == compile::AssetKind::audio &&
                       candidate.asset_id == asset.semantic_id.value;
            });
        if (payload != engine.assets.end()) {
            specification.asset_payloads.push_back({
                asset.id,
                payload->bytes,
            });
        }
    }

    return {
        std::move(specification),
        resolved_scenario.scenario,
    };
}

} // namespace engine_sim_offline::render_detail
