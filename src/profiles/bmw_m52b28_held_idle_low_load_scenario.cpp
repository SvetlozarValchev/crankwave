#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <array>
#include <string_view>

namespace engine_sim_offline::profiles::detail {
namespace {

struct HeldIdleLowLoadPoint {
    std::string_view point_key;
    std::string_view scenario_id;
    double engine_speed_rpm;
    double throttle_01;
};

constexpr std::array<HeldIdleLowLoadPoint, kBmwM52b28HeldIdleLowLoadPointCount>
    kHeldIdleLowLoadPoints{
        HeldIdleLowLoadPoint{
            "rpm700-throttle0",
            "bmw-m52b28-held-idle-region-rpm700-throttle0",
            700.0,
            0.0,
        },
        HeldIdleLowLoadPoint{
            "rpm1500-throttle0p10",
            "bmw-m52b28-held-low-load-rpm1500-throttle0p10",
            1500.0,
            0.10,
        },
    };

} // namespace

std::string_view bmw_m52b28_held_idle_low_load_point_key(std::size_t point_index) {
    return kHeldIdleLowLoadPoints.at(point_index).point_key;
}

contract::RenderScenario
build_bmw_m52b28_held_idle_low_load_scenario(BmwProvenanceBuilder &builder,
                                             const contract::EngineSpec &engine,
                                             std::size_t point_index) {
    const auto &point = kHeldIdleLowLoadPoints.at(point_index);
    return build_bmw_m52b28_held_speed_scenario(
        builder, engine,
        {
            point.scenario_id,
            "held-idle-low-load-running",
            point.engine_speed_rpm,
            point.throttle_01,
            12.88,
            32U,
            15.0,
            12.88 + 15.0,
            "low-order-operating-point-listening-v1",
        });
}

} // namespace engine_sim_offline::profiles::detail
