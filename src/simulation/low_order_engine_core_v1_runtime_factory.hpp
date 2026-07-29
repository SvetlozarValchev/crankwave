#pragma once

#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"

#include <span>
#include <variant>

namespace engine_sim_offline::simulation::detail {

// Private construction seam for the composite runtime and focused leaf tests.
// Production code has no standalone mechanics or gas compiler entry point.
struct LowOrderEngineCoreV1RuntimeFactory {
    using MechanicsCompileResult =
        std::variant<LegacyLowOrderMechanicsSession, contract::ValidationReport>;
    using GasCompileResult =
        std::variant<LegacyLowOrderGasSession, contract::ValidationReport>;

    [[nodiscard]] static MechanicsCompileResult
    compile_mechanics(const contract::EngineSpec &engine,
                      const contract::LowOrderEngineCoreV1 &core,
                      const contract::RenderScenario &scenario,
                      const KinematicScenarioSchedule &schedule);

    [[nodiscard]] static MechanicsCompileResult
    compile_mechanics(const contract::EngineSpec &engine,
                      const contract::LowOrderEngineCoreV1 &core,
                      const contract::RenderScenario &scenario,
                      const ScenarioControlSchedule &schedule);

    [[nodiscard]] static GasCompileResult
    compile_gas(const contract::EngineSpec &engine,
                const contract::LowOrderEngineCoreV1 &core,
                const contract::RenderScenario &scenario,
                const ScenarioControlSchedule &schedule,
                std::span<const CenteredSliderCrankCylinder> cylinder_models);
};

} // namespace engine_sim_offline::simulation::detail
