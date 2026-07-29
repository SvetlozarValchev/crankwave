#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view kRigidCrankZohWorkEnergyMethodId =
    "rigid-crank-zoh-work-energy-v1";
inline constexpr std::uint32_t kRigidCrankZohWorkEnergyMethodVersion = 1U;

inline constexpr std::string_view kPiecewiseLinearPositiveSpeedPassiveBrakeMethodId =
    "piecewise-linear-positive-speed-passive-brake-v1";
inline constexpr std::uint32_t
    kPiecewiseLinearPositiveSpeedPassiveBrakeMethodVersion = 1U;

[[nodiscard]] std::string_view
rigid_crank_zoh_work_energy_method_descriptor() noexcept;
[[nodiscard]] std::string_view
piecewise_linear_positive_speed_passive_brake_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
rigid_crank_zoh_work_energy_method_identity();
[[nodiscard]] const contract::MethodIdentity &
piecewise_linear_positive_speed_passive_brake_method_identity();

} // namespace engine_sim_offline::simulation
