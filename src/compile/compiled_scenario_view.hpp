#pragma once

#include "compile/compiled_model_storage.hpp"
#include "crankwave/compile.hpp"

namespace crankwave::compile::detail {

struct CompiledScenarioInputsView {
    const ResolvedEnginePackage &engine;
    const ResolvedScenarioContracts &scenario;
};

// Shared read-only bridge for portable session construction and native publication
// planning. It exposes the immutable compiler result without creating a second
// executable projection or copying verified asset payloads.
class CompiledScenarioViewAccess final {
  public:
    [[nodiscard]] static CompiledScenarioInputsView
    inputs(const CompiledScenario &scenario) noexcept {
        return {
            scenario.storage_->engine->resolved,
            scenario.storage_->resolved,
        };
    }
};

} // namespace crankwave::compile::detail
