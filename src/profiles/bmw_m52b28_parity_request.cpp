#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <utility>

namespace engine_sim_offline::profiles {

BmwM52b28ParityRequestResult
make_bmw_m52b28_parity_request(std::vector<double> post_step_rpm) {
    auto input_report = detail::validate_bmw_m52b28_parity_rpm_input(post_step_rpm);
    if (!input_report.ok()) {
        return input_report;
    }

    auto request =
        detail::build_bmw_m52b28_parity_request_unvalidated(std::move(post_step_rpm));
    auto report = validate_bmw_m52b28_parity_request(request);
    if (!report.ok()) {
        return report;
    }
    return request;
}

} // namespace engine_sim_offline::profiles

namespace engine_sim_offline::profiles::detail {

BmwM52b28ParityRequest
build_bmw_m52b28_parity_request_unvalidated(std::vector<double> post_step_rpm) {
    BmwProvenanceBuilder builder{BmwProfileKind::parity_request_v1};
    auto engine = build_bmw_m52b28_low_order_engine(builder);
    auto scenario = build_bmw_m52b28_parity_scenario(builder, std::move(post_step_rpm));
    auto provenance = builder.finish();
    return {
        std::move(engine),
        std::move(scenario),
        std::move(provenance),
    };
}

} // namespace engine_sim_offline::profiles::detail
