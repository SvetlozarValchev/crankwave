#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view
    kWarmRunningFreeEngineRigidCrankZohWorkEnergyMethodId =
        "warm-running-free-engine-rigid-crank-zoh-work-energy-v1";
inline constexpr std::uint32_t
    kWarmRunningFreeEngineRigidCrankZohWorkEnergyMethodVersion = 1U;
inline constexpr std::string_view kFreeEngineEquivalentInertiaSumMethodId =
    "free-engine-equivalent-inertia-sum-v1";
inline constexpr std::uint32_t kFreeEngineEquivalentInertiaSumMethodVersion = 1U;

[[nodiscard]] std::string_view
warm_running_free_engine_rigid_crank_zoh_work_energy_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
warm_running_free_engine_rigid_crank_zoh_work_energy_method_identity();

[[nodiscard]] std::string_view
free_engine_equivalent_inertia_sum_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
free_engine_equivalent_inertia_sum_method_identity();

} // namespace engine_sim_offline::simulation
