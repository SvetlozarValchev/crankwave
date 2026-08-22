#pragma once

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "crankwave/compile.hpp"

#include <memory>
#include <string>

namespace crankwave::compile::detail {

struct CompiledEngineStorage {
    std::string id;
    ResolvedEnginePackage resolved;
};

struct CompiledScenarioStorage {
    std::string id;
    std::shared_ptr<const CompiledEngineStorage> engine;
    ResolvedScenarioContracts resolved;
};

} // namespace crankwave::compile::detail
