#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"

#include <string>
#include <utility>
#include <variant>

namespace engine_sim_offline::profiles {
namespace {

void append(contract::ValidationReport &destination,
            contract::ValidationReport source) {
    destination.append(std::move(source));
}

void validate_point_contract(contract::ValidationReport &report,
                             const BmwM52b28HeldIdleLowLoadRequest &request) {
    append(report, contract::validate(request.engine, request.provenance));
    append(report, contract::validate(request.scenario, request.provenance));
    append(report, contract::validate_for_engine(request.scenario, request.engine));

    const auto *operating = std::get_if<contract::LowOrderOperatingPointV1Profile>(
        &request.engine.physics_profile);
    if (operating == nullptr) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "engine.physics_profile",
                   "BMW held idle/low-load request requires "
                   "low_order_operating_point_v1");
    } else {
        append(report, simulation::admit_implemented_cycle_accounting_methods(
                           request.engine, *operating));
    }
}

} // namespace

contract::ValidationReport validate_bmw_m52b28_held_idle_low_load_request_set(
    const BmwM52b28HeldIdleLowLoadRequestSet &request_set) {
    contract::ValidationReport report;
    for (const auto &request : request_set) {
        validate_point_contract(report, request);
    }

    const auto expected =
        detail::build_bmw_m52b28_held_idle_low_load_request_set_unvalidated();
    for (std::size_t index = 0; index < request_set.size(); ++index) {
        if (request_set[index] != expected[index]) {
            report.add(contract::ContractIssueCode::inconsistent_semantics,
                       "bmw_m52b28_held_idle_low_load_request_set[" +
                           std::to_string(index) + "]",
                       "request differs from the exact normative BMW M52B28 held "
                       "idle-region/low-load point at this frozen set index");
        }
    }
    return report;
}

} // namespace engine_sim_offline::profiles
