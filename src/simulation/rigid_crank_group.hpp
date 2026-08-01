#pragma once

#include "engine_sim_offline/contract/parity_model.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>

namespace engine_sim_offline::simulation {

struct RigidCrankGroupProperties {
    std::size_t crankshaft_count = 0U;
    double authored_crank_inertia_kg_m2 = 0.0;
    double running_friction_torque_magnitude_nm = 0.0;

    friend bool operator==(const RigidCrankGroupProperties &,
                           const RigidCrankGroupProperties &) = default;
};

enum class RigidCrankGroupIssue : std::uint8_t {
    empty_group,
    missing_output_crankshaft,
    invalid_crankshaft_identity,
    nonfinite_authored_crank_inertia,
    nonpositive_authored_crank_inertia,
    nonfinite_running_friction,
    negative_running_friction,
    nonfinite_aggregate_inertia,
    nonfinite_aggregate_friction,
};

inline constexpr std::size_t kNoRigidCrankGroupCrankshaft =
    std::numeric_limits<std::size_t>::max();

struct RigidCrankGroupError {
    RigidCrankGroupIssue issue = RigidCrankGroupIssue::empty_group;
    std::size_t crankshaft_index = kNoRigidCrankGroupCrankshaft;

    friend bool operator==(const RigidCrankGroupError &,
                           const RigidCrankGroupError &) = default;
};

using RigidCrankGroupCalculation =
    std::variant<RigidCrankGroupProperties, RigidCrankGroupError>;
using RigidCrankGroupInertiaCalculation = std::variant<double, RigidCrankGroupError>;

// Resolves only the authored rotational-inertia aggregate. This narrower helper
// keeps the cycle-mean inertia primitive independent of the group's friction input.
[[nodiscard]] RigidCrankGroupInertiaCalculation
calculate_rigid_crank_group_authored_inertia(
    const contract::LegacyMechanismProfile &mechanism) noexcept;

// Resolves the fixed properties of the admitted co-phased 1:1 crank group. The
// resolved crank vector is the authored order. A single crank is copied without an
// added floating-point operation; multiple cranks are summed in that exact order.
// Crank and flywheel masses are deliberately not inputs to either aggregate.
[[nodiscard]] RigidCrankGroupCalculation calculate_rigid_crank_group_properties(
    const contract::LegacyMechanismProfile &mechanism) noexcept;

} // namespace engine_sim_offline::simulation
