#include "simulation/inertial_dyno_method_registry.hpp"

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

constexpr std::string_view kRigidCrankDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=rigid-crank-zoh-work-energy-v1
version=1
operation=positive-speed-rigid-one-degree-of-freedom-crank-referred-inertial-dyno
state=finite-binary64-unwrapped-theta-rad-and-finite-positive-binary64-angular-speed-rad-s
inertia=finite-positive-binary64-total-crank-referred-equivalent-inertia-kg-m2
causal-input=previous-committed-post-gas-indicated-torque-plus-applied-one-cycle-lagged-aggregate-loss-plus-mechanically-disengaged-starter-positive-zero
aggregate-loss-causality=completed-cycle-k-chen-flynn-running-direction-cycle-mean-loss-torque-is-held-through-cycle-k-plus-1
aggregate-loss-initialization=fixed-horizon-preparation-must-supply-a-completed-cycle-no-guessed-value
variable-cycle-speed=cycle-mean-rpm-is-binary64-120-divided-by-represented-cycle-duration-s
passive-brake=input-positive-magnitude-evaluated-at-initial-step-angular-speed-and-subtracted-from-upstream-crank-torque
torque-hold=upstream-crank-torque-and-passive-brake-are-zero-order-held-for-one-physics-step
alpha=(held-upstream-crank-torque-nm-minus-passive-brake-torque-nm)-divided-by-equivalent-inertia-kg-m2
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-times-dt-plus-binary64-0.5-times-alpha-times-dt-times-dt-in-written-order
theta-next=theta-plus-theta-displacement
energy-evidence=kinetic-energy-change-minus-held-net-torque-times-theta-displacement
preparation=exact-initial-rpm-held-through-fixed-sampling-horizon-equal-to-audible-start
release=first-physics-step-after-horizon-uses-carried-crank-gas-flame-randomness-pressure-history-and-lagged-loss-state
stall=resolve-within-step-zero-speed-time-and-angle-and-fail-without-publishing-zero-or-reverse-state
reverse=unsupported
brake-domain=fail-without-extrapolation-at-initial-or-predicted-final-speed
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

constexpr std::string_view kPassiveBrakeDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=piecewise-linear-positive-speed-passive-brake-v1
version=1
operation=positive-speed-passive-resisting-torque-magnitude
curve=at-least-two-finite-binary64-points-in-strictly-increasing-nonnegative-angular-speed-rad-s-order
torque=finite-nonnegative-binary64-resisting-magnitude-nm
input=finite-positive-binary64-angular-speed-rad-s
lower-endpoint=return-exact-first-point-torque
upper-endpoint=return-exact-last-point-torque
interior-segment=first-point-pair-whose-lower-speed-is-less-than-or-equal-to-input-and-upper-speed-is-greater-than-input
fraction=(input-speed-minus-lower-speed)-divided-by-(upper-speed-minus-lower-speed)
interpolation=lower-torque-plus-(upper-torque-minus-lower-torque)-times-fraction-in-written-order
sign=returned-magnitude-is-subtracted-from-positive-running-direction-crank-torque
extrapolation=none-out-of-domain-input-or-predicted-output-fails
static-friction=none
reverse=unsupported
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(canonical_lf_descriptor(kRigidCrankDescriptor));
static_assert(canonical_lf_descriptor(kPassiveBrakeDescriptor));

[[nodiscard]] contract::MethodIdentity make_identity(
    std::string_view id, std::uint32_t version,
    std::string_view descriptor) noexcept {
    return {
        std::string{id},
        version,
        contract::sha256(std::as_bytes(
            std::span<const char>{descriptor.data(), descriptor.size()})),
    };
}

} // namespace

std::string_view rigid_crank_zoh_work_energy_method_descriptor() noexcept {
    return kRigidCrankDescriptor;
}

std::string_view
piecewise_linear_positive_speed_passive_brake_method_descriptor() noexcept {
    return kPassiveBrakeDescriptor;
}

const contract::MethodIdentity &rigid_crank_zoh_work_energy_method_identity() {
    static const auto identity = make_identity(
        kRigidCrankZohWorkEnergyMethodId, kRigidCrankZohWorkEnergyMethodVersion,
        kRigidCrankDescriptor);
    return identity;
}

const contract::MethodIdentity &
piecewise_linear_positive_speed_passive_brake_method_identity() {
    static const auto identity = make_identity(
        kPiecewiseLinearPositiveSpeedPassiveBrakeMethodId,
        kPiecewiseLinearPositiveSpeedPassiveBrakeMethodVersion,
        kPassiveBrakeDescriptor);
    return identity;
}

} // namespace crankwave::simulation
