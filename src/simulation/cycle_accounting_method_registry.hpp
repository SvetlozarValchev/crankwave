#pragma once

#include "engine_sim_offline/contract/engine.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view kFourStrokePiecewiseLinearCycleQuadratureMethodId =
    "four-stroke-piecewise-linear-cycle-quadrature-v1";
inline constexpr std::uint32_t kFourStrokePiecewiseLinearCycleQuadratureMethodVersion =
    1;

inline constexpr std::string_view kChenFlynnCycleMeanAggregateLossMethodId =
    "chen-flynn-cycle-mean-aggregate-loss-v1";
inline constexpr std::uint32_t kChenFlynnCycleMeanAggregateLossMethodVersion = 1;

struct CycleAccountingMethodIdentities {
    contract::MethodIdentity cycle_quadrature;
    contract::MethodIdentity aggregate_loss;

    friend bool operator==(const CycleAccountingMethodIdentities &,
                           const CycleAccountingMethodIdentities &) = default;
};

[[nodiscard]] std::string_view
four_stroke_piecewise_linear_cycle_quadrature_method_descriptor() noexcept;
[[nodiscard]] std::string_view
chen_flynn_cycle_mean_aggregate_loss_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
four_stroke_piecewise_linear_cycle_quadrature_method_identity();
[[nodiscard]] const contract::MethodIdentity &
chen_flynn_cycle_mean_aggregate_loss_method_identity();

[[nodiscard]] const CycleAccountingMethodIdentities &
implemented_cycle_accounting_method_identities();

// This admits only the two implemented cycle-accounting algorithms. It does not
// admit an engine profile, preparation policy, capture producer, or render route.
[[nodiscard]] bool exactly_matches_implemented_cycle_accounting_methods(
    const contract::EngineSpec &engine,
    const contract::LowOrderOperatingPointV1Profile &profile);

[[nodiscard]] contract::ValidationReport admit_implemented_cycle_accounting_methods(
    const contract::EngineSpec &engine,
    const contract::LowOrderOperatingPointV1Profile &profile);

} // namespace engine_sim_offline::simulation
