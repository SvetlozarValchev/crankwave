#pragma once

#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/result.hpp"
#include "crankwave/contract/scenario.hpp"
#include "simulation/execution_extent.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/live_control.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace crankwave::simulation {

struct LowOrderPrescribedKinematicStep {
    contract::TorqueTelemetry capture_torque;

    friend bool operator==(const LowOrderPrescribedKinematicStep &,
                           const LowOrderPrescribedKinematicStep &) = default;
};

using LowOrderPrescribedKinematicAdvanceResult =
    std::variant<LowOrderPrescribedKinematicStep, contract::FailureContext>;

// Finite prescribed motion owns no crank dynamics or cycle accounting. It checks
// the committed mechanics+gas clock and exposes only the aggregate instantaneous
// indicated-gas torque that the gas transaction actually computed.
class LowOrderPrescribedKinematicRuntime final {
  public:
    LowOrderPrescribedKinematicRuntime(const LowOrderPrescribedKinematicRuntime &) =
        delete;
    LowOrderPrescribedKinematicRuntime &
    operator=(const LowOrderPrescribedKinematicRuntime &) = delete;
    LowOrderPrescribedKinematicRuntime(LowOrderPrescribedKinematicRuntime &&) noexcept =
        default;
    LowOrderPrescribedKinematicRuntime &
    operator=(LowOrderPrescribedKinematicRuntime &&) noexcept = default;

    [[nodiscard]] std::optional<contract::FailureContext>
    reject_live_overrides(const LiveControlOverrides &overrides);
    [[nodiscard]] LowOrderPrescribedKinematicAdvanceResult
    advance(const LegacyMechanismStep &mechanics, const LegacyLowOrderGasStep &gas);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] std::uint64_t accepted_sample_count() const noexcept;

  private:
    LowOrderPrescribedKinematicRuntime(contract::RationalRateHz rate,
                                       std::uint64_t expected_sample_count,
                                       std::string model_id, std::string profile_id,
                                       std::string scenario_id,
                                       contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          const LegacyMechanismStep *mechanics = nullptr) const;
    [[nodiscard]] LowOrderPrescribedKinematicAdvanceResult
    fail(contract::FailureContext failure);

    contract::RationalRateHz rate_;
    std::uint64_t expected_sample_count_ = 0;
    std::uint64_t accepted_sample_count_ = 0;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderPrescribedKinematicRuntime, contract::ValidationReport>
    compile_low_order_prescribed_kinematic_runtime(const contract::EngineSpec &,
                                                   const contract::RenderScenario &,
                                                   LowOrderExecutionExtent);
};

using LowOrderPrescribedKinematicCompileResult =
    std::variant<LowOrderPrescribedKinematicRuntime, contract::ValidationReport>;

[[nodiscard]] LowOrderPrescribedKinematicCompileResult
compile_low_order_prescribed_kinematic_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    LowOrderExecutionExtent execution_extent);

} // namespace crankwave::simulation
