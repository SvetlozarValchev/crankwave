#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <utility>

namespace engine_sim_offline::profiles {

BmwM52b28HeldIdleLowLoadRequestSetResult
make_bmw_m52b28_held_idle_low_load_request_set() {
    auto request_set =
        detail::build_bmw_m52b28_held_idle_low_load_request_set_unvalidated();
    auto report = validate_bmw_m52b28_held_idle_low_load_request_set(request_set);
    if (!report.ok()) {
        return report;
    }
    return BmwM52b28HeldIdleLowLoadRequestSetResult{
        std::in_place_type<BmwM52b28HeldIdleLowLoadRequestSet>,
        std::move(request_set),
    };
}

} // namespace engine_sim_offline::profiles

namespace engine_sim_offline::profiles::detail {

BmwM52b28HeldIdleLowLoadRequest
build_bmw_m52b28_held_idle_low_load_request_unvalidated(std::size_t point_index) {
    BmwProvenanceBuilder builder{
        BmwProfileKind::low_order_operating_point_v1,
    };
    auto engine = build_bmw_m52b28_low_order_engine(builder);
    auto scenario =
        build_bmw_m52b28_held_idle_low_load_scenario(builder, engine, point_index);
    auto provenance = builder.finish();
    return {
        std::string{bmw_m52b28_held_idle_low_load_point_key(point_index)},
        std::move(engine),
        std::move(scenario),
        std::move(provenance),
    };
}

BmwM52b28HeldIdleLowLoadRequestSet
build_bmw_m52b28_held_idle_low_load_request_set_unvalidated() {
    BmwM52b28HeldIdleLowLoadRequestSet request_set;
    for (std::size_t index = 0; index < request_set.size(); ++index) {
        request_set[index] =
            build_bmw_m52b28_held_idle_low_load_request_unvalidated(index);
    }
    return request_set;
}

} // namespace engine_sim_offline::profiles::detail
