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

contract::ValidationReport validate_bmw_m52b28_operating_profile(
    const BmwM52b28OperatingProfile &profile) {
    contract::ValidationReport report;
    append(report, contract::validate(profile.engine, profile.provenance));

    const auto *operating =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &profile.engine.physics_profile);
    if (operating == nullptr) {
        report.add(
            contract::ContractIssueCode::inconsistent_semantics,
            "engine.physics_profile",
            "BMW operating profile requires low_order_operating_point_v1");
    } else {
        append(
            report,
            simulation::admit_implemented_cycle_accounting_methods(
                profile.engine, *operating));
    }

    const auto expected =
        detail::build_bmw_m52b28_operating_profile_unvalidated();
    if (profile != expected) {
        report.add(
            contract::ContractIssueCode::inconsistent_semantics,
            "bmw_m52b28_operating_profile",
            "profile differs from the exact normative BMW M52B28 operating profile");
    }
    return report;
}

} // namespace engine_sim_offline::profiles
