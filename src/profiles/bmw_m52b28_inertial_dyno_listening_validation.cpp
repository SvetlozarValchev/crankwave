#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"

#include <utility>
#include <variant>

namespace engine_sim_offline::profiles {
namespace {

void append(contract::ValidationReport &destination,
            contract::ValidationReport source) {
    destination.append(std::move(source));
}

} // namespace

contract::ValidationReport validate_bmw_m52b28_inertial_dyno_listening_request(
    const BmwM52b28InertialDynoListeningRequest &request) {
    contract::ValidationReport report;
    append(report, contract::validate(request.engine, request.provenance));
    append(report, contract::validate(request.scenario, request.provenance));
    append(report, contract::validate_for_engine(request.scenario, request.engine));

    const auto *operating = std::get_if<contract::LowOrderOperatingPointV1Profile>(
        &request.engine.physics_profile);
    if (operating == nullptr) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "engine.physics_profile",
                   "BMW inertial-dyno listening request requires "
                   "low_order_operating_point_v1");
    } else {
        append(report, simulation::admit_implemented_cycle_accounting_methods(
                           request.engine, *operating));
    }

    const auto expected =
        detail::build_bmw_m52b28_inertial_dyno_listening_request_unvalidated();
    if (request != expected) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "bmw_m52b28_inertial_dyno_listening_request",
                   "request differs from the exact normative BMW M52B28 "
                   "inertial-dyno listening request");
    }
    return report;
}

} // namespace engine_sim_offline::profiles
