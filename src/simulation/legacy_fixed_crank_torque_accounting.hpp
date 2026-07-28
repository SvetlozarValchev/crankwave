#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "engine_sim_offline/contract/torque.hpp"

#include <cstdint>
#include <variant>

namespace engine_sim_offline::simulation {

struct LegacyFixedCrankTorqueAccountingPlan {
    double magnitude_nm = 0.0;

    friend bool operator==(const LegacyFixedCrankTorqueAccountingPlan &,
                           const LegacyFixedCrankTorqueAccountingPlan &) = default;
};

enum class LegacyFixedCrankTorqueAccountingErrorCode : std::uint8_t {
    nonfinite_angular_speed,
    nonfinite_indicated_gas_torque,
    nonfinite_torque_sum,
};

struct LegacyFixedCrankTorqueAccountingError {
    LegacyFixedCrankTorqueAccountingErrorCode code =
        LegacyFixedCrankTorqueAccountingErrorCode::nonfinite_torque_sum;

    friend bool operator==(const LegacyFixedCrankTorqueAccountingError &,
                           const LegacyFixedCrankTorqueAccountingError &) = default;
};

using LegacyFixedCrankTorqueAccountingEvaluation =
    std::variant<contract::TorqueTelemetry, LegacyFixedCrankTorqueAccountingError>;
using LegacyFixedCrankTorqueAccountingCompileResult =
    std::variant<LegacyFixedCrankTorqueAccountingPlan, contract::ValidationReport>;

// Compiles the complete M3 torque-accounting policy. The shared gas solver neither
// receives nor owns this plan, its loss, or its incomplete capability.
[[nodiscard]] LegacyFixedCrankTorqueAccountingCompileResult
compile_legacy_fixed_crank_torque_accounting(
    const contract::EngineSpec &engine,
    const contract::LegacyFixedCrankLossV1 &profile);

// Reproduces the complete M3 telemetry classification, running-direction sign, and
// indicated-plus-loss/power operation order.
[[nodiscard]] LegacyFixedCrankTorqueAccountingEvaluation
evaluate_legacy_fixed_crank_torque_accounting(
    const LegacyFixedCrankTorqueAccountingPlan &plan, double angular_speed_rad_s,
    double indicated_gas_torque_nm) noexcept;

} // namespace engine_sim_offline::simulation
