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

constexpr std::string_view kWarmRunningFreeEngineRigidCrankDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=warm-running-free-engine-rigid-crank-zoh-work-energy-v1
version=1
operation=positive-speed-warm-running-free-engine-rigid-one-degree-of-freedom-crank
state=finite-binary64-unwrapped-theta-rad-and-finite-positive-binary64-angular-speed-rad-s
inertia=finite-positive-binary64-total-crank-referred-equivalent-inertia-kg-m2
causal-engine-input=previous-committed-post-gas-indicated-torque-plus-applied-one-cycle-lagged-aggregate-loss-plus-mechanically-disengaged-starter-positive-zero
aggregate-loss-causality=completed-cycle-k-running-direction-cycle-mean-loss-torque-is-held-through-cycle-k-plus-1
aggregate-loss-initialization=fixed-held-preparation-must-supply-a-completed-cycle-no-guessed-value
external-resisting-input=finite-nonnegative-binary64-running-direction-resisting-magnitude-nm-from-authored-right-continuous-hold
external-resisting-sign=magnitude-is-subtracted-from-prior-committed-engine-torque
torque-hold=prior-committed-engine-torque-and-external-resisting-magnitude-are-zero-order-held-for-one-physics-step
alpha=(held-engine-torque-nm-minus-external-resisting-magnitude-nm)-divided-by-equivalent-inertia-kg-m2
omega-next=omega-plus-alpha-times-dt
theta-displacement=omega-times-dt-plus-binary64-0.5-times-alpha-times-dt-times-dt-in-written-order
theta-next=theta-plus-theta-displacement
work-energy=kinetic-energy-change-minus-held-net-torque-times-theta-displacement
preparation=exact-initial-rpm-held-through-fixed-sampling-horizon-equal-to-audible-start
release=first-physics-step-after-held-preparation-uses-carried-crank-gas-flame-randomness-pressure-history-and-lagged-loss-state
stall=unsupported-fail-before-publishing-zero-speed
reverse=unsupported
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(canonical_lf_descriptor(kWarmRunningFreeEngineRigidCrankDescriptor));

constexpr std::string_view kFreeEngineEquivalentInertiaSumDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=free-engine-equivalent-inertia-sum-v1
version=1
operation=resolve-total-constant-crank-referred-equivalent-inertia
engine-input=finite-positive-binary64-cycle-mean-crank-referred-engine-inertia-kg-m2
attachment-input=finite-nonnegative-binary64-attached-inertia-kg-m2
total=engine-input-plus-attachment-input-in-written-binary64-order
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
warm_running_free_engine_rigid_crank_zoh_work_energy_method_descriptor() noexcept {
    return kWarmRunningFreeEngineRigidCrankDescriptor;
}

const contract::MethodIdentity &
warm_running_free_engine_rigid_crank_zoh_work_energy_method_identity() {
    static const auto identity =
        make_identity(kWarmRunningFreeEngineRigidCrankZohWorkEnergyMethodId,
                      kWarmRunningFreeEngineRigidCrankZohWorkEnergyMethodVersion,
                      kWarmRunningFreeEngineRigidCrankDescriptor);
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
