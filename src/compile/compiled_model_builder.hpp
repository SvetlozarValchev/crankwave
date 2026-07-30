#pragma once

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/compile.hpp"

namespace engine_sim_offline::compile::detail {

class CompiledEngineBuilder final {
  public:
    [[nodiscard]] static EngineCompileResult
    build(ResolvedEnginePackage resolved) noexcept;
};

class CompiledScenarioBuilder final {
  public:
    [[nodiscard]] static ScenarioCompileResult
    compile(const CompiledEngine &engine,
            const authoring::ScenarioDocument &document) noexcept;

    [[nodiscard]] static ScenarioCompileResult
    build(const CompiledEngine &engine,
          ResolvedScenarioContracts resolved) noexcept;
};

} // namespace engine_sim_offline::compile::detail
