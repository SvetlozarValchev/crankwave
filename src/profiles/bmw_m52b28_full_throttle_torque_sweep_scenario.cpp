#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <array>
#include <string_view>

namespace engine_sim_offline::profiles::detail {
namespace {

constexpr std::array<std::string_view, kBmwM52b28FullThrottleTorqueSweepPointCount>
    kScenarioIds{
        "bmw-m52b28-held-1500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3000rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3950rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-4500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-5300rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-6000rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-6500rpm-full-throttle-torque-sweep-v1",
    };

} // namespace

contract::RenderScenario
build_bmw_m52b28_full_throttle_torque_sweep_scenario(BmwProvenanceBuilder &builder,
                                                     const contract::EngineSpec &engine,
                                                     std::size_t point_index) {
    return build_bmw_m52b28_held_speed_scenario(
        builder, engine,
        {
            kScenarioIds.at(point_index),
            "torque-sweep-held-running",
            kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm.at(point_index),
            1.0,
            6.44,
            16U,
            0.75,
            1500.0,
            0.02,
            6.46,
            "low-order-operating-point-torque-sweep-v1",
        });
}

} // namespace engine_sim_offline::profiles::detail
