#include "engine_sim_offline/compile.hpp"

#include "compile/compiled_model_builder.hpp"
#include "compile/engine_resolver.hpp"

#include <utility>
#include <variant>

namespace engine_sim_offline::compile {

EngineCompileResult
compile_engine(const authoring::EnginePackageDocument &document,
               const std::span<const AssetPayloadView> assets) noexcept {
    auto result = detail::resolve_engine_package(document, assets);
    if (auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        return std::move(*report);
    }
    return detail::CompiledEngineBuilder::build(
        std::get<detail::ResolvedEnginePackage>(std::move(result)));
}

ScenarioCompileResult
compile_scenario(const CompiledEngine &engine,
                 const authoring::ScenarioDocument &document) noexcept {
    return detail::CompiledScenarioBuilder::compile(engine, document);
}

} // namespace engine_sim_offline::compile
