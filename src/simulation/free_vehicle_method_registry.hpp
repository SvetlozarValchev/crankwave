#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view kForwardVehicleRoadLoadMethodId =
    "forward-vehicle-road-load-v1";
inline constexpr std::uint32_t kForwardVehicleRoadLoadMethodVersion = 1U;
inline constexpr std::string_view kBoundedClutchCouplingMethodId =
    "bounded-forward-clutch-coupling-v1";
inline constexpr std::uint32_t kBoundedClutchCouplingMethodVersion = 1U;
inline constexpr std::string_view kBoundedForwardVehicleDrivetrainMethodId =
    "bounded-forward-vehicle-drivetrain-pgs-v1";
inline constexpr std::uint32_t kBoundedForwardVehicleDrivetrainMethodVersion = 1U;

[[nodiscard]] std::string_view forward_vehicle_road_load_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
forward_vehicle_road_load_method_identity();

[[nodiscard]] std::string_view bounded_clutch_coupling_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &bounded_clutch_coupling_method_identity();

[[nodiscard]] std::string_view
bounded_forward_vehicle_drivetrain_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
bounded_forward_vehicle_drivetrain_method_identity();

} // namespace engine_sim_offline::simulation
