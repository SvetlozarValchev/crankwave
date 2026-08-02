#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view
    kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodId =
        "nonnegative-speed-free-engine-centered-slider-crank-v1";
inline constexpr std::uint32_t
    kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodVersion = 1U;
inline constexpr std::string_view
    kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupMethodId =
        "nonnegative-speed-free-engine-centered-slider-crank-rigid-group-v1";
inline constexpr std::uint32_t
    kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupMethodVersion = 1U;
inline constexpr std::string_view kNonnegativeSpeedFreeEngineOneLevelMasterRodMethodId =
    "nonnegative-speed-free-engine-one-level-master-rod-v1";
inline constexpr std::uint32_t
    kNonnegativeSpeedFreeEngineOneLevelMasterRodMethodVersion = 1U;
inline constexpr std::string_view kFreeEngineEquivalentInertiaSumMethodId =
    "free-engine-equivalent-inertia-sum-v1";
inline constexpr std::uint32_t kFreeEngineEquivalentInertiaSumMethodVersion = 1U;

[[nodiscard]] std::string_view
nonnegative_speed_free_engine_centered_slider_crank_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
nonnegative_speed_free_engine_centered_slider_crank_method_identity();

[[nodiscard]] std::string_view
nonnegative_speed_free_engine_centered_slider_crank_rigid_group_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
nonnegative_speed_free_engine_centered_slider_crank_rigid_group_method_identity();

[[nodiscard]] std::string_view
nonnegative_speed_free_engine_one_level_master_rod_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
nonnegative_speed_free_engine_one_level_master_rod_method_identity();

[[nodiscard]] std::string_view
free_engine_equivalent_inertia_sum_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
free_engine_equivalent_inertia_sum_method_identity();

} // namespace engine_sim_offline::simulation
