#include "simulation/rigid_crank_group.hpp"

#include <cmath>

namespace crankwave::simulation {
namespace {

[[nodiscard]] RigidCrankGroupError
error(const RigidCrankGroupIssue issue,
      const std::size_t crankshaft_index = kNoRigidCrankGroupCrankshaft) noexcept {
    return RigidCrankGroupError{issue, crankshaft_index};
}

} // namespace

RigidCrankGroupInertiaCalculation calculate_rigid_crank_group_authored_inertia(
    const contract::LegacyMechanismProfile &mechanism) noexcept {
    if (mechanism.cranks.empty()) {
        return error(RigidCrankGroupIssue::empty_group);
    }
    if (contract::find_output_crank(mechanism) == nullptr) {
        return error(RigidCrankGroupIssue::missing_output_crankshaft);
    }

    for (std::size_t index = 0; index < mechanism.cranks.size(); ++index) {
        const auto &crank = mechanism.cranks[index];
        if (!std::isfinite(crank.authored_crank_inertia_kg_m2.value)) {
            return error(RigidCrankGroupIssue::nonfinite_authored_crank_inertia, index);
        }
        if (!(crank.authored_crank_inertia_kg_m2.value > 0.0)) {
            return error(RigidCrankGroupIssue::nonpositive_authored_crank_inertia,
                         index);
        }
    }

    if (mechanism.cranks.size() == 1U) {
        return mechanism.cranks.front().authored_crank_inertia_kg_m2.value;
    }

    double result = mechanism.cranks.front().authored_crank_inertia_kg_m2.value;
    for (std::size_t index = 1U; index < mechanism.cranks.size(); ++index) {
        result += mechanism.cranks[index].authored_crank_inertia_kg_m2.value;
        if (!std::isfinite(result)) {
            return error(RigidCrankGroupIssue::nonfinite_aggregate_inertia);
        }
    }
    return result;
}

RigidCrankGroupCalculation calculate_rigid_crank_group_properties(
    const contract::LegacyMechanismProfile &mechanism) noexcept {
    const auto inertia_calculation =
        calculate_rigid_crank_group_authored_inertia(mechanism);
    const auto *inertia = std::get_if<double>(&inertia_calculation);
    if (inertia == nullptr) {
        return std::get<RigidCrankGroupError>(inertia_calculation);
    }

    for (std::size_t index = 0; index < mechanism.cranks.size(); ++index) {
        const auto &crank = mechanism.cranks[index];
        bool duplicate_identity = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            duplicate_identity =
                duplicate_identity ||
                mechanism.cranks[prior].crankshaft_id == crank.crankshaft_id;
        }
        if (!crank.crankshaft_id.valid() || duplicate_identity) {
            return error(RigidCrankGroupIssue::invalid_crankshaft_identity, index);
        }
        if (!std::isfinite(crank.running_friction_torque_magnitude_nm.value)) {
            return error(RigidCrankGroupIssue::nonfinite_running_friction, index);
        }
        if (crank.running_friction_torque_magnitude_nm.value < 0.0) {
            return error(RigidCrankGroupIssue::negative_running_friction, index);
        }
    }

    RigidCrankGroupProperties result;
    result.crankshaft_count = mechanism.cranks.size();
    result.authored_crank_inertia_kg_m2 = *inertia;
    if (mechanism.cranks.size() == 1U) {
        result.running_friction_torque_magnitude_nm =
            mechanism.cranks.front().running_friction_torque_magnitude_nm.value;
        return result;
    }

    result.running_friction_torque_magnitude_nm =
        mechanism.cranks.front().running_friction_torque_magnitude_nm.value;
    for (std::size_t index = 1U; index < mechanism.cranks.size(); ++index) {
        const auto &crank = mechanism.cranks[index];
        result.running_friction_torque_magnitude_nm +=
            crank.running_friction_torque_magnitude_nm.value;
        if (!std::isfinite(result.running_friction_torque_magnitude_nm)) {
            return error(RigidCrankGroupIssue::nonfinite_aggregate_friction);
        }
    }
    return result;
}

} // namespace crankwave::simulation
