#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <utility>

namespace engine_sim_offline::profiles {

BmwM52b28HeldSpeedListeningRequestResult
make_bmw_m52b28_held_speed_listening_request() {
    auto request = detail::build_bmw_m52b28_held_speed_listening_request_unvalidated();
    auto report = validate_bmw_m52b28_held_speed_listening_request(request);
    if (!report.ok()) {
        return report;
    }
    return BmwM52b28HeldSpeedListeningRequestResult{
        std::in_place_type<BmwM52b28HeldSpeedListeningRequest>,
        std::move(request),
    };
}

} // namespace engine_sim_offline::profiles

namespace engine_sim_offline::profiles::detail {

BmwM52b28HeldSpeedListeningRequest
build_bmw_m52b28_held_speed_listening_request_unvalidated() {
    BmwProvenanceBuilder builder{
        BmwProfileKind::low_order_operating_point_v1,
    };
    auto engine = build_bmw_m52b28_low_order_engine(builder);
    auto scenario = build_bmw_m52b28_held_speed_listening_scenario(builder, engine);
    auto provenance = builder.finish();
    return {
        std::move(engine),
        std::move(scenario),
        std::move(provenance),
    };
}

} // namespace engine_sim_offline::profiles::detail
