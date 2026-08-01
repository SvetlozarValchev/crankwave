#include "simulation/free_engine_method_registry.hpp"

#include <span>
#include <string>

namespace engine_sim_offline::simulation {
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

constexpr std::string_view kNonnegativeSpeedFreeEngineCenteredSliderCrankDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=nonnegative-speed-free-engine-centered-slider-crank-v1
version=1
operation=nonnegative-speed-free-engine-centered-slider-crank-one-degree-of-freedom
state=finite-binary64-unwrapped-theta-rad-and-finite-canonical-nonnegative-binary64-angular-speed-rad-s
engine-inertia=exact-centered-slider-crank-kinetic-energy-coefficient-M-of-theta-from-authored-crank-piston-and-connecting-rod-properties
attached-inertia=finite-nonnegative-binary64-constant-crank-referred-inertia-added-to-M-of-theta
inertia-boundary=analytic-M-of-theta-and-dM-dtheta-are-evaluated-at-the-current-left-boundary
causal-engine-input=previous-committed-post-gas-indicated-torque-plus-source-crank-friction-plus-source-piston-wall-friction-from-the-current-left-boundary-plus-the-separately-resolved-starter-torque
crank-friction=pristine-engine-sim-zero-speed-rotation-constraint-saturated-at-negative-authored-running-friction-torque-for-admitted-positive-speed-or-an-engaged-positive-forward-starter
starter=engine-sim-v1-unilateral-target-speed-starter-v1-consumes-the-signed-preconstraint-speed-predicted-with-gas-source-friction-and-external-resistance-then-adds-positive-forward-torque-up-to-the-authored-maximum
starter-source=ange-yaghi-engine-sim-85f7c3b959a908ed5232ede4f1a4ac7eafe6b630-starter-motor
starter-solver-source=ange-yaghi-simple-2d-constraint-solver-e009f4ff1c9c4c5874e865e893cdb62e208fb2b3-optimized-nsv-rigid-body-system
starter-required-torque=instantaneous-configuration-inertia-times-target-speed-minus-signed-preconstraint-speed-divided-by-step-duration-in-written-order
starter-unilateral-limit=applied-torque-is-positive-forward-and-the-lesser-of-required-torque-and-authored-maximum
starter-engagement=right-continuous-authored-or-live-level-state-with-no-automatic-release-and-no-dedicated-audio-source
piston-wall-friction=pristine-engine-sim-cpp-default-stribeck-coulomb-viscous-law-using-current-signed-slider-speed-and-the-retained-previous-step-wall-reaction-magnitude
piston-wall-reaction=ideal-centered-slider-crank-midpoint-rod-inverse-dynamics-from-current-left-boundary-phase-speed-acceleration-and-chamber-pressure
piston-wall-timing=step-n-friction-consumes-wall-reaction-n-minus-one-then-wall-reaction-n-is-committed-only-with-the-successful-post-gas-boundary-n-plus-one-transaction
aggregate-loss-accounting=held-speed-chen-flynn-evidence-is-observed-but-never-applied-to-free-engine-motion
external-resisting-input=finite-nonnegative-binary64-running-direction-resisting-magnitude-nm-from-authored-right-continuous-hold
external-resisting-sign=magnitude-is-subtracted-from-prior-committed-engine-torque
torque-hold=prior-committed-indicated-torque-current-left-boundary-piston-wall-friction-source-crank-friction-and-external-resisting-magnitude-are-zero-order-held-for-one-physics-step-while-M-and-dM-dtheta-use-that-same-left-boundary
velocity-inertia-torque=binary64-0.5-times-dM-dtheta-times-omega-times-omega-in-written-order
alpha=(held-engine-torque-nm-minus-external-resisting-magnitude-nm-minus-velocity-inertia-torque)-divided-by-M-of-theta
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-next-times-dt
theta-next=theta-plus-theta-displacement
positive-speed-arithmetic=when-left-boundary-omega-and-predicted-omega-next-are-positive-the-causal-input-grouping-written-order-alpha-omega-next-theta-displacement-and-theta-next-operations-are-byte-identical-to-the-accepted-warm-prepared-positive-speed-path-with-no-added-floating-point-operation
work-energy-evidence=none
warm-preparation=positive-initial-rpm-is-held-exactly-through-fixed-sampling-horizon
warm-release=first-physics-step-after-held-preparation-uses-carried-crank-gas-flame-randomness-pressure-history-fixed-crank-friction-plan-and-warm-lagged-piston-wall-state
warm-acquisition=dynamic-physics-continues-without-reset-from-release-through-an-equal-or-later-audible-start-boundary
cold-preparation=canonical-positive-zero-initial-rpm-requires-zero-preparation-frames-and-no-cycle-accountant-or-sampler
cold-bootstrap=fresh-core-state-canonical-positive-zero-angular-speed-and-causal-prior-committed-indicated-gas-torque-seeded-to-positive-zero
stall-detection=positive-left-boundary-omega-with-nonpositive-predicted-omega-next
stall-time=negative-left-boundary-omega-divided-by-alpha
stall-theta=left-boundary-theta-plus-left-boundary-omega-times-stall-time-plus-binary64-0.5-times-alpha-times-stall-time-times-stall-time-in-written-order
stall-commit=publish-one-full-physics-frame-at-canonical-positive-zero-speed-with-angular-displacement-equal-to-stall-theta-minus-left-boundary-theta
rest-constraint=with-starter-disabled-at-canonical-positive-zero-speed-source-crank-friction-cancels-the-net-applied-torque-up-to-the-authored-running-friction-magnitude
rest-motion=with-starter-disabled-nonpositive-effective-torque-holds-theta-and-publishes-canonical-positive-zero-acceleration-displacement-and-speed-while-positive-effective-torque-enters-forward-only-crank-motion
reverse=unsupported-and-never-published
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(
    canonical_lf_descriptor(kNonnegativeSpeedFreeEngineCenteredSliderCrankDescriptor));

constexpr std::string_view
    kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupDescriptor =
        R"method(engine-sim-offline.simulation-method-configuration.v1
method=nonnegative-speed-free-engine-centered-slider-crank-rigid-group-v1
version=1
operation=nonnegative-speed-free-engine-centered-slider-crank-one-degree-of-freedom-for-one-co-centered-co-phased-one-to-one-rigid-crank-group
crank-group-order=resolved-authored-crank-vector-order
crank-group-inertia=copy-first-authored-rotational-moment-of-inertia-kg-m2-then-add-each-subsequent-authored-value-in-written-binary64-order-with-finite-overflow-rejection
crank-group-running-friction=copy-first-authored-running-friction-torque-magnitude-nm-then-add-each-subsequent-authored-value-in-written-binary64-order-with-finite-overflow-rejection
crank-and-flywheel-mass=not-consumed-and-not-rederived-into-rotational-inertia-or-friction
crank-group-motion=one-common-finite-binary64-unwrapped-theta-rad-and-one-common-finite-canonical-nonnegative-binary64-angular-speed-rad-s
configuration-inertia=rigid-group-authored-rotational-inertia-plus-unchanged-centered-slider-crank-piston-and-connecting-rod-kinetic-energy-coefficient-M-of-theta
source-crank-friction=unchanged-pristine-engine-sim-positive-speed-and-rest-constraint-law-consuming-the-rigid-group-running-friction-magnitude
attached-inertia=finite-nonnegative-binary64-constant-crank-referred-inertia-added-to-M-of-theta
downstream-arithmetic=byte-identical-nonnegative-speed-free-engine-centered-slider-crank-v1-operation-order-after-rigid-group-property-resolution
stall-rest-starter-piston-wall-friction-external-resistance-and-vehicle-coupling=unchanged-from-nonnegative-speed-free-engine-centered-slider-crank-v1
work-energy-evidence=none
reverse=unsupported-and-never-published
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(canonical_lf_descriptor(
    kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupDescriptor));

constexpr std::string_view kFreeEngineEquivalentInertiaSumDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=free-engine-equivalent-inertia-sum-v1
version=1
operation=resolve-total-cycle-mean-crank-referred-inertia-reference
engine-input=finite-positive-binary64-cycle-mean-crank-referred-engine-inertia-kg-m2
attachment-input=finite-nonnegative-binary64-attached-inertia-kg-m2
total-cycle-mean-reference=engine-input-plus-attachment-input-in-written-binary64-order
negative-zero=inputs-and-result-must-be-positive-zero-or-positive
)method";

static_assert(canonical_lf_descriptor(kFreeEngineEquivalentInertiaSumDescriptor));

[[nodiscard]] contract::MethodIdentity
make_identity(std::string_view id, std::uint32_t version,
              std::string_view descriptor) noexcept {
    return {
        std::string{id},
        version,
        contract::sha256(
            std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()})),
    };
}

} // namespace

std::string_view
nonnegative_speed_free_engine_centered_slider_crank_method_descriptor() noexcept {
    return kNonnegativeSpeedFreeEngineCenteredSliderCrankDescriptor;
}

const contract::MethodIdentity &
nonnegative_speed_free_engine_centered_slider_crank_method_identity() {
    static const auto identity =
        make_identity(kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodId,
                      kNonnegativeSpeedFreeEngineCenteredSliderCrankMethodVersion,
                      kNonnegativeSpeedFreeEngineCenteredSliderCrankDescriptor);
    return identity;
}

std::string_view
nonnegative_speed_free_engine_centered_slider_crank_rigid_group_method_descriptor() noexcept {
    return kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupDescriptor;
}

const contract::MethodIdentity &
nonnegative_speed_free_engine_centered_slider_crank_rigid_group_method_identity() {
    static const auto identity = make_identity(
        kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupMethodId,
        kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupMethodVersion,
        kNonnegativeSpeedFreeEngineCenteredSliderCrankRigidGroupDescriptor);
    return identity;
}

std::string_view free_engine_equivalent_inertia_sum_method_descriptor() noexcept {
    return kFreeEngineEquivalentInertiaSumDescriptor;
}

const contract::MethodIdentity &free_engine_equivalent_inertia_sum_method_identity() {
    static const auto identity =
        make_identity(kFreeEngineEquivalentInertiaSumMethodId,
                      kFreeEngineEquivalentInertiaSumMethodVersion,
                      kFreeEngineEquivalentInertiaSumDescriptor);
    return identity;
}

} // namespace engine_sim_offline::simulation
