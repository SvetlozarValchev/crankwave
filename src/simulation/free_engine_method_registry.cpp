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

constexpr std::string_view kWarmRunningFreeEngineCenteredSliderCrankDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=warm-running-free-engine-centered-slider-crank-v1
version=1
operation=positive-speed-warm-running-free-engine-centered-slider-crank-one-degree-of-freedom
state=finite-binary64-unwrapped-theta-rad-and-finite-positive-binary64-angular-speed-rad-s
engine-inertia=exact-centered-slider-crank-kinetic-energy-coefficient-M-of-theta-from-authored-crank-piston-and-connecting-rod-properties
attached-inertia=finite-nonnegative-binary64-constant-crank-referred-inertia-added-to-M-of-theta
inertia-boundary=analytic-M-of-theta-and-dM-dtheta-are-evaluated-at-the-current-left-boundary
causal-engine-input=previous-committed-post-gas-indicated-torque-plus-fixed-positive-speed-source-crank-friction-plus-source-piston-wall-friction-from-the-current-left-boundary-plus-mechanically-disengaged-starter-positive-zero
crank-friction=pristine-engine-sim-zero-speed-rotation-constraint-saturated-at-negative-authored-running-friction-torque-for-admitted-positive-speed
piston-wall-friction=pristine-engine-sim-cpp-default-stribeck-coulomb-viscous-law-using-current-signed-slider-speed-and-the-retained-previous-step-wall-reaction-magnitude
piston-wall-reaction=ideal-centered-slider-crank-midpoint-rod-inverse-dynamics-from-current-left-boundary-phase-speed-acceleration-and-chamber-pressure
piston-wall-timing=step-n-friction-consumes-wall-reaction-n-minus-one-then-wall-reaction-n-is-committed-only-with-the-successful-post-gas-boundary-n-plus-one-transaction
aggregate-loss-accounting=held-speed-chen-flynn-evidence-is-observed-but-never-applied-to-free-engine-motion
external-resisting-input=finite-nonnegative-binary64-running-direction-resisting-magnitude-nm-from-authored-right-continuous-hold
external-resisting-sign=magnitude-is-subtracted-from-prior-committed-engine-torque
torque-hold=prior-committed-indicated-torque-current-left-boundary-piston-wall-friction-fixed-crank-friction-and-external-resisting-magnitude-are-zero-order-held-for-one-physics-step-while-M-and-dM-dtheta-use-that-same-left-boundary
velocity-inertia-torque=binary64-0.5-times-dM-dtheta-times-omega-times-omega-in-written-order
alpha=(held-engine-torque-nm-minus-external-resisting-magnitude-nm-minus-velocity-inertia-torque)-divided-by-M-of-theta
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-times-dt-plus-binary64-0.5-times-alpha-times-dt-times-dt-in-written-order
theta-next=theta-plus-theta-displacement
work-energy-evidence=none
preparation=exact-initial-rpm-held-through-fixed-sampling-horizon-equal-to-audible-start
release=first-physics-step-after-held-preparation-uses-carried-crank-gas-flame-randomness-pressure-history-fixed-crank-friction-plan-and-warm-lagged-piston-wall-state
stall=unsupported-fail-before-publishing-zero-speed
reverse=unsupported
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(
    canonical_lf_descriptor(kWarmRunningFreeEngineCenteredSliderCrankDescriptor));

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
warm_running_free_engine_centered_slider_crank_method_descriptor() noexcept {
    return kWarmRunningFreeEngineCenteredSliderCrankDescriptor;
}

const contract::MethodIdentity &
warm_running_free_engine_centered_slider_crank_method_identity() {
    static const auto identity =
        make_identity(kWarmRunningFreeEngineCenteredSliderCrankMethodId,
                      kWarmRunningFreeEngineCenteredSliderCrankMethodVersion,
                      kWarmRunningFreeEngineCenteredSliderCrankDescriptor);
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
