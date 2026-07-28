#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <utility>

namespace engine_sim_offline::profiles {

BmwM52b28OperatingProfileResult make_bmw_m52b28_operating_profile() {
    auto profile = detail::build_bmw_m52b28_operating_profile_unvalidated();
    auto report = validate_bmw_m52b28_operating_profile(profile);
    if (!report.ok()) {
        return report;
    }
    return profile;
}

} // namespace engine_sim_offline::profiles

namespace engine_sim_offline::profiles::detail {

BmwM52b28OperatingProfile
build_bmw_m52b28_operating_profile_unvalidated() {
    BmwProvenanceBuilder builder{
        BmwProfileKind::low_order_operating_point_v1,
    };
    auto engine = build_bmw_m52b28_low_order_engine(builder);
    auto provenance = builder.finish();
    return {
        std::move(engine),
        std::move(provenance),
    };
}

} // namespace engine_sim_offline::profiles::detail
