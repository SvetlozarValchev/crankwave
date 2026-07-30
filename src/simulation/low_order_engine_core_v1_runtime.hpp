#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/execution_extent.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace engine_sim_offline::simulation {

struct LowOrderEngineCoreV1StepView {
    std::reference_wrapper<const LegacyMechanismStep> mechanics;
    std::reference_wrapper<const LegacyLowOrderGasStep> gas;
};

struct LowOrderEngineCoreV1Completed {
    std::uint64_t sample_count = 0;

    friend bool operator==(const LowOrderEngineCoreV1Completed &,
                           const LowOrderEngineCoreV1Completed &) = default;
};

using LowOrderEngineCoreV1AdvanceResult =
    std::variant<LowOrderEngineCoreV1StepView, LowOrderEngineCoreV1Completed,
                 contract::FailureContext>;

// The sole owner of one coherent low-order mechanics+gas execution. A gas step can
// neither be paired with another mechanics session nor observed before both halves
// of the transaction have completed.
class LowOrderEngineCoreV1Runtime final {
  public:
    LowOrderEngineCoreV1Runtime(const LowOrderEngineCoreV1Runtime &) = delete;
    LowOrderEngineCoreV1Runtime &
    operator=(const LowOrderEngineCoreV1Runtime &) = delete;
    LowOrderEngineCoreV1Runtime(LowOrderEngineCoreV1Runtime &&) noexcept = default;
    LowOrderEngineCoreV1Runtime &
    operator=(LowOrderEngineCoreV1Runtime &&) noexcept = default;

    // Returned references are runtime-owned and remain valid only until the next
    // advance call or any move, assignment, or destruction of this runtime.
    // Completion and failure are terminal and stable.
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult advance();
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult
    advance(const LiveControlOverrides &overrides);
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult advance(PostStepCrankMotion motion);
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult
    advance(PostStepCrankMotion motion, const LiveControlOverrides &overrides);
    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::uint64_t produced_sample_count() const noexcept;
    [[nodiscard]] const LowOrderExecutionExtent &execution_extent() const noexcept;

  private:
    LowOrderEngineCoreV1Runtime(LegacyLowOrderMechanicsSession mechanics,
                                LegacyLowOrderGasSession gas,
                                LowOrderExecutionExtent execution_extent,
                                contract::RationalRateHz rate, std::string model_id,
                                std::string profile_id, std::string scenario_id,
                                contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(std::string detail_code, std::string state_summary,
          const LegacyMechanismStep *mechanics = nullptr) const;
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult
    fail(contract::FailureContext failure);
    [[nodiscard]] LowOrderEngineCoreV1AdvanceResult
    advance_with_motion(std::optional<PostStepCrankMotion> motion,
                        const LiveControlOverrides &overrides);

    LegacyLowOrderMechanicsSession mechanics_;
    LegacyLowOrderGasSession gas_;
    LowOrderExecutionExtent execution_extent_ =
        LowOrderExecutionExtent::finite_scenario(0U);
    contract::RationalRateHz rate_;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderEngineCoreV1Runtime, contract::ValidationReport>
    compile_low_order_engine_core_v1_runtime(const contract::EngineSpec &,
                                             const contract::RenderScenario &,
                                             const contract::LowOrderEngineCoreV1 &,
                                             const contract::RandomPlan &,
                                             LowOrderExecutionExtent);
};

using LowOrderEngineCoreV1CompileResult =
    std::variant<LowOrderEngineCoreV1Runtime, contract::ValidationReport>;

// Selects no executable profile. The caller owns that policy and passes the one
// exact shared core selected by its profile.
[[nodiscard]] LowOrderEngineCoreV1CompileResult
compile_low_order_engine_core_v1_runtime(const contract::EngineSpec &engine,
                                         const contract::RenderScenario &scenario,
                                         const contract::LowOrderEngineCoreV1 &core,
                                         const contract::RandomPlan &random_plan,
                                         LowOrderExecutionExtent execution_extent);

} // namespace engine_sim_offline::simulation
