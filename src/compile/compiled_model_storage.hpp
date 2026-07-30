#pragma once

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/compile.hpp"

#include <memory>
#include <string>

namespace engine_sim_offline::compile::detail {

struct CompiledEngineStorage {
    std::string id;
    ResolvedEnginePackage resolved;
};

struct CompiledScenarioStorage {
    std::string id;
    std::shared_ptr<const CompiledEngineStorage> engine;
    ResolvedScenarioContracts resolved;
};

} // namespace engine_sim_offline::compile::detail
