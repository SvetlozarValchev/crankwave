#include "simulation/bounded_dyno_constraint.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace crankwave::simulation {
namespace {

[[nodiscard]] consteval bool
canonical_lf_descriptor(std::string_view descriptor) noexcept {
    if (descriptor.empty() || descriptor.back() != '\n') {
        return false;
    }
    for (const char character : descriptor) {
        if (character == '\r' || character == '\0') {
            return false;
        }
    }
    return true;
}

constexpr std::string_view kBoundedHeldDynoConstraintDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=bounded-held-dyno-speed-constraint
version=1
operation=positive-speed-one-degree-of-freedom-bounded-dynamometer-velocity-constraint
source=ange-yaghi-engine-sim-85f7c3b959a908ed5232ede4f1a4ac7eafe6b630-dynamometer
solver-source=ange-yaghi-simple-2d-constraint-solver-e009f4ff1c9c4c5874e865e893cdb62e208fb2b3-optimized-nsv-rigid-body-system
state=finite-binary64-unwrapped-theta-rad-and-positive-binary64-angular-speed-rad-s
engine-inertia=exact-centered-slider-crank-M-of-theta-and-dM-dtheta-at-current-left-boundary
causal-engine-input=previous-committed-indicated-torque-plus-current-source-crank-and-piston-wall-friction
target=input-finite-nonnegative-binary64-post-step-angular-speed-rad-s
required-actuator=M-times-target-minus-current-omega-divided-by-dt-plus-binary64-0.5-times-dM-dtheta-times-current-omega-squared-minus-held-upstream-engine-torque
limits=required-actuator-clamped-between-negative-maximum-absorbing-torque-and-positive-maximum-driving-torque
reaction=dyno-reaction-is-exact-negative-of-applied-actuator-torque
alpha=held-upstream-engine-torque-plus-applied-actuator-torque-minus-velocity-inertia-torque-divided-by-M
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-next-times-dt
theta-next=theta-plus-theta-displacement
saturation=publish-achieved-speed-and-absorbing-or-driving-limit-disposition
reverse=unsupported-and-reported-as-a-typed-within-step-stall
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

static_assert(canonical_lf_descriptor(kBoundedHeldDynoConstraintDescriptor));

constexpr std::string_view kBoundedHeldDynoOneLevelMasterRodConstraintDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=bounded-held-dyno-speed-constraint-one-level-master-rod-v1
version=1
operation=positive-speed-one-degree-of-freedom-bounded-dynamometer-velocity-constraint-with-one-level-master-rod-articulated-mechanism
source=ange-yaghi-engine-sim-85f7c3b959a908ed5232ede4f1a4ac7eafe6b630-dynamometer-and-one-level-master-rod-rigid-body-mechanism
solver-source=ange-yaghi-simple-2d-constraint-solver-e009f4ff1c9c4c5874e865e893cdb62e208fb2b3-optimized-nsv-rigid-body-system
topology=one-authored-rigid-crank-plus-one-level-master-rod-root-and-slave-rigid-rods-and-translating-pistons
state=finite-binary64-unwrapped-theta-rad-and-positive-binary64-angular-speed-rad-s
engine-inertia=exact-articulated-kinetic-energy-coefficient-M-of-theta-and-dM-dtheta-at-current-left-boundary
causal-engine-input=previous-committed-indicated-torque-plus-current-source-crank-friction-and-per-cylinder-piston-wall-friction
piston-wall-friction=pristine-engine-sim-cpp-default-stribeck-coulomb-viscous-law-using-each-current-signed-piston-axis-speed-and-that-piston-retained-previous-step-wall-reaction-magnitude
piston-wall-reaction=leaf-first-coupled-articulated-inverse-dynamics-from-current-left-boundary-phase-speed-acceleration-and-per-cylinder-chamber-pressure
piston-wall-reaction-reduction=slave-branch-forces-and-moments-accumulate-into-the-root-before-the-root-bearing-and-master-piston-wall-reaction-are-solved
piston-wall-timing=step-n-friction-consumes-wall-reaction-n-minus-one-then-the-coupled-wall-reaction-vector-n-is-committed-only-with-the-successful-post-gas-boundary-n-plus-one-transaction
warm-preparation=per-cylinder-piston-travel-chen-flynn-evidence-under-the-fixed-held-preparation-boundary
warm-release=first-post-preparation-step-consumes-carried-articulated-gas-flame-randomness-pressure-and-wall-reaction-history
target=input-finite-nonnegative-binary64-post-step-angular-speed-rad-s
required-actuator=M-times-target-minus-current-omega-divided-by-dt-plus-binary64-0.5-times-dM-dtheta-times-current-omega-squared-minus-held-upstream-engine-torque
limits=required-actuator-clamped-between-negative-maximum-absorbing-torque-and-positive-maximum-driving-torque
reaction=dyno-reaction-is-exact-negative-of-applied-actuator-torque
alpha=held-upstream-engine-torque-plus-applied-actuator-torque-minus-velocity-inertia-torque-divided-by-M
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-next-times-dt
theta-next=theta-plus-theta-displacement
saturation=publish-achieved-speed-and-absorbing-or-driving-limit-disposition
reverse=unsupported-and-reported-as-a-typed-within-step-stall
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

static_assert(
    canonical_lf_descriptor(kBoundedHeldDynoOneLevelMasterRodConstraintDescriptor));

[[nodiscard]] contract::MethodIdentity make_identity() noexcept {
    return {
        std::string{kBoundedHeldDynoConstraintMethodId},
        kBoundedHeldDynoConstraintMethodVersion,
        contract::sha256(std::as_bytes(
            std::span<const char>{kBoundedHeldDynoConstraintDescriptor.data(),
                                  kBoundedHeldDynoConstraintDescriptor.size()})),
    };
}

[[nodiscard]] contract::MethodIdentity make_one_level_master_rod_identity() noexcept {
    return {
        std::string{kBoundedHeldDynoOneLevelMasterRodConstraintMethodId},
        kBoundedHeldDynoOneLevelMasterRodConstraintMethodVersion,
        contract::sha256(std::as_bytes(std::span<const char>{
            kBoundedHeldDynoOneLevelMasterRodConstraintDescriptor.data(),
            kBoundedHeldDynoOneLevelMasterRodConstraintDescriptor.size()})),
    };
}

} // namespace

std::string_view bounded_held_dyno_constraint_method_descriptor() noexcept {
    return kBoundedHeldDynoConstraintDescriptor;
}

const contract::MethodIdentity &bounded_held_dyno_constraint_method_identity() {
    static const auto identity = make_identity();
    return identity;
}

std::string_view
bounded_held_dyno_one_level_master_rod_constraint_method_descriptor() noexcept {
    return kBoundedHeldDynoOneLevelMasterRodConstraintDescriptor;
}

const contract::MethodIdentity &
bounded_held_dyno_one_level_master_rod_constraint_method_identity() {
    static const auto identity = make_one_level_master_rod_identity();
    return identity;
}

namespace detail {
namespace {

[[nodiscard]] BoundedDynoConstraintInputError
input_error(BoundedDynoConstraintInputIssue issue) noexcept {
    return {issue};
}

} // namespace

BoundedDynoConstraintCalculation
advance_bounded_dyno_constraint(const BoundedDynoConstraintInput &input) noexcept {
    using Issue = BoundedDynoConstraintInputIssue;
    if (!std::isfinite(input.instantaneous_inertia_kg_m2)) {
        return input_error(Issue::nonfinite_instantaneous_inertia);
    }
    if (!(input.instantaneous_inertia_kg_m2 > 0.0)) {
        return input_error(Issue::nonpositive_instantaneous_inertia);
    }
    if (!std::isfinite(input.inertia_derivative_kg_m2_per_rad)) {
        return input_error(Issue::nonfinite_inertia_derivative);
    }
    if (!std::isfinite(input.initial_state.theta_rad)) {
        return input_error(Issue::nonfinite_theta);
    }
    if (!std::isfinite(input.initial_state.angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_initial_angular_speed);
    }
    if (!(input.initial_state.angular_speed_rad_s > 0.0)) {
        return input_error(Issue::nonpositive_initial_angular_speed);
    }
    if (!std::isfinite(input.held_upstream_engine_torque_nm)) {
        return input_error(Issue::nonfinite_upstream_engine_torque);
    }
    if (!std::isfinite(input.target_angular_speed_rad_s)) {
        return input_error(Issue::nonfinite_target_angular_speed);
    }
    if (input.target_angular_speed_rad_s < 0.0) {
        return input_error(Issue::negative_target_angular_speed);
    }
    if (!std::isfinite(input.maximum_absorbing_torque_nm)) {
        return input_error(Issue::nonfinite_maximum_absorbing_torque);
    }
    if (input.maximum_absorbing_torque_nm < 0.0) {
        return input_error(Issue::negative_maximum_absorbing_torque);
    }
    if (!std::isfinite(input.maximum_driving_torque_nm)) {
        return input_error(Issue::nonfinite_maximum_driving_torque);
    }
    if (input.maximum_driving_torque_nm < 0.0) {
        return input_error(Issue::negative_maximum_driving_torque);
    }
    if (!std::isfinite(input.duration_s)) {
        return input_error(Issue::nonfinite_duration);
    }
    if (!(input.duration_s > 0.0)) {
        return input_error(Issue::nonpositive_duration);
    }

    const double omega0 = input.initial_state.angular_speed_rad_s;
    const double velocity_inertia_torque_nm =
        0.5 * input.inertia_derivative_kg_m2_per_rad * omega0 * omega0;
    const double required_acceleration_rad_s2 =
        (input.target_angular_speed_rad_s - omega0) / input.duration_s;
    const double required_actuator_torque_nm =
        input.instantaneous_inertia_kg_m2 * required_acceleration_rad_s2 +
        velocity_inertia_torque_nm - input.held_upstream_engine_torque_nm;
    const double applied_actuator_torque_nm =
        std::clamp(required_actuator_torque_nm, -input.maximum_absorbing_torque_nm,
                   input.maximum_driving_torque_nm);
    const double angular_acceleration_rad_s2 =
        (input.held_upstream_engine_torque_nm + applied_actuator_torque_nm -
         velocity_inertia_torque_nm) /
        input.instantaneous_inertia_kg_m2;
    const double omega1 = omega0 + angular_acceleration_rad_s2 * input.duration_s;
    const double angular_displacement_rad = omega1 * input.duration_s;
    const double theta1 = input.initial_state.theta_rad + angular_displacement_rad;

    if (!std::isfinite(velocity_inertia_torque_nm) ||
        !std::isfinite(required_acceleration_rad_s2) ||
        !std::isfinite(required_actuator_torque_nm) ||
        !std::isfinite(applied_actuator_torque_nm) ||
        !std::isfinite(angular_acceleration_rad_s2) || !std::isfinite(omega1) ||
        !std::isfinite(angular_displacement_rad) || !std::isfinite(theta1)) {
        return input_error(Issue::nonfinite_derived_value);
    }

    if (!(omega1 > 0.0)) {
        const double stall_time_s = -omega0 / angular_acceleration_rad_s2;
        const double stall_theta_rad =
            input.initial_state.theta_rad + omega0 * stall_time_s +
            0.5 * angular_acceleration_rad_s2 * stall_time_s * stall_time_s;
        if (!std::isfinite(stall_time_s) || !(stall_time_s > 0.0) ||
            stall_time_s > input.duration_s || !std::isfinite(stall_theta_rad)) {
            return input_error(Issue::nonfinite_derived_value);
        }
        return BoundedDynoConstraintStall{
            input,
            required_actuator_torque_nm,
            applied_actuator_torque_nm,
            velocity_inertia_torque_nm,
            angular_acceleration_rad_s2,
            omega1,
            stall_time_s,
            stall_theta_rad,
        };
    }

    BoundedDynoConstraintDisposition disposition =
        BoundedDynoConstraintDisposition::tracking;
    if (applied_actuator_torque_nm > required_actuator_torque_nm) {
        disposition = BoundedDynoConstraintDisposition::absorbing_torque_limited;
    } else if (applied_actuator_torque_nm < required_actuator_torque_nm) {
        disposition = BoundedDynoConstraintDisposition::driving_torque_limited;
    }

    return BoundedDynoConstraintStep{
        input,
        {theta1, omega1},
        disposition,
        required_actuator_torque_nm,
        applied_actuator_torque_nm,
        velocity_inertia_torque_nm,
        angular_acceleration_rad_s2,
        angular_displacement_rad,
    };
}

} // namespace detail
} // namespace crankwave::simulation
