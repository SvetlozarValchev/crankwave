#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <utility>

namespace engine_sim_offline::profiles {

BmwM52b28FullThrottleTorqueSweepRequestSetResult
make_bmw_m52b28_full_throttle_torque_sweep_request_set() {
    auto requests =
        detail::build_bmw_m52b28_full_throttle_torque_sweep_request_set_unvalidated();
    auto report = validate_bmw_m52b28_full_throttle_torque_sweep_request_set(requests);
    if (!report.ok()) {
        return report;
    }
    return BmwM52b28FullThrottleTorqueSweepRequestSetResult{
        std::in_place_type<BmwM52b28FullThrottleTorqueSweepRequestSet>,
        std::move(requests),
    };
}

} // namespace engine_sim_offline::profiles

namespace engine_sim_offline::profiles::detail {

BmwM52b28FullThrottleTorqueSweepRequest
build_bmw_m52b28_full_throttle_torque_sweep_request_unvalidated(
    std::size_t point_index) {
    BmwProvenanceBuilder builder{
        BmwProfileKind::low_order_operating_point_v1,
    };
    auto engine = build_bmw_m52b28_low_order_engine(builder);
    auto scenario = build_bmw_m52b28_full_throttle_torque_sweep_scenario(
        builder, engine, point_index);
    auto provenance = builder.finish();
    return {
        std::move(engine),
        std::move(scenario),
        std::move(provenance),
    };
}

BmwM52b28FullThrottleTorqueSweepRequestSet
build_bmw_m52b28_full_throttle_torque_sweep_request_set_unvalidated() {
    BmwM52b28FullThrottleTorqueSweepRequestSet requests;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        requests[index] =
            build_bmw_m52b28_full_throttle_torque_sweep_request_unvalidated(index);
    }
    return requests;
}

} // namespace engine_sim_offline::profiles::detail
