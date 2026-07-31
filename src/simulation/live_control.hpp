#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstdint>

namespace engine_sim_offline::simulation {

// Neutral per-physics-step overrides consumed by the simulation layer. Timeline
// ordering, delivery-frame projection, and command ownership remain session policy.
struct LiveControlOverrides {
    bool has_throttle = false;
    double throttle_01 = 0.0;
    bool has_ignition_enabled = false;
    bool ignition_enabled = false;
    bool has_fuel_enabled = false;
    bool fuel_enabled = false;
    bool has_limiter_enabled = false;
    bool limiter_enabled = false;
    bool has_external_resisting_torque_nm = false;
    double external_resisting_torque_nm = 0.0;
    bool has_starter_enabled = false;
    bool starter_enabled = false;
    bool has_dyno_target_engine_speed_rpm = false;
    double dyno_target_engine_speed_rpm = 0.0;
    bool has_dyno_maximum_absorbing_torque_nm = false;
    double dyno_maximum_absorbing_torque_nm = 0.0;
    bool has_dyno_maximum_driving_torque_nm = false;
    double dyno_maximum_driving_torque_nm = 0.0;
    bool has_vehicle_selected_forward_gear = false;
    // Zero is neutral; positive values are one-based authored gear ordinals.
    std::uint32_t vehicle_selected_forward_gear_ordinal = 0U;
    bool has_vehicle_clutch_engagement = false;
    double vehicle_clutch_engagement_01 = 0.0;
    bool has_vehicle_service_brake_application = false;
    double vehicle_service_brake_application_01 = 0.0;

    [[nodiscard]] bool any() const noexcept {
        return has_throttle || has_ignition_enabled || has_fuel_enabled ||
               has_limiter_enabled || has_external_resisting_torque_nm ||
               has_starter_enabled || has_dyno_target_engine_speed_rpm ||
               has_dyno_maximum_absorbing_torque_nm ||
               has_dyno_maximum_driving_torque_nm ||
               has_vehicle_selected_forward_gear ||
               has_vehicle_clutch_engagement ||
               has_vehicle_service_brake_application;
    }

    friend bool operator==(const LiveControlOverrides &,
                           const LiveControlOverrides &) = default;
};

namespace detail {

struct LowOrderLiveControlStep {
    bool valid = false;
    LiveControlOverrides overrides;
};

struct LowOrderLiveControlProvider {
    void *context = nullptr;
    contract::RationalRateHz physics_rate;
    LowOrderLiveControlStep (*drain_for_physics_step)(void *,
                                                      std::uint64_t) noexcept = nullptr;
};

} // namespace detail

} // namespace engine_sim_offline::simulation
