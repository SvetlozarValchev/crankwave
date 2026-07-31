#include "simulation/free_vehicle_method_registry.hpp"

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

constexpr std::string_view kForwardVehicleRoadLoadDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=forward-vehicle-road-load-v1
version=1
operation=forward-only-linear-vehicle-passive-road-load-and-service-brake-step
passive-source=ange-yaghi-engine-sim-85f7c3b959a908ed5232ede4f1a4ac7eafe6b630-vehicle-drag-constraint
air-density=28.97e-3-times-101325-divided-by-(8.31446261815324-times-298.15)
aerodynamic-force=0.5-times-air-density-times-speed-squared-times-drag-coefficient-times-frontal-area
passive-force=rolling-resistance-force-plus-aerodynamic-force
service-brake=declared-greenfield-extension-maximum-force-times-right-continuous-normalized-application
resisting-impulse=minimum-of-total-resisting-force-times-dt-and-initial-forward-momentum
state=finite-canonical-nonnegative-forward-linear-speed
reverse=unilateral-resistance-stops-at-positive-zero-and-never-drives-reverse
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

constexpr std::string_view kBoundedClutchCouplingDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=bounded-forward-clutch-coupling-v1
version=1
operation=two-inertia-forward-gear-relative-speed-constraint
source=ange-yaghi-engine-sim-85f7c3b959a908ed5232ede4f1a4ac7eafe6b630-transmission
solver-source=ange-yaghi-simple-2d-constraint-solver-e009f4ff1c9c4c5874e865e893cdb62e208fb2b3-clutch-constraint
neutral=null-selected-gear-transmits-zero-torque
reduction=gear-ratio-times-differential-ratio-divided-by-tire-radius
vehicle-inertia-at-crank=vehicle-mass-divided-by-reduction-squared
required-engine-torque=negative-predicted-slip-divided-by-(dt-times-(inverse-engine-inertia-plus-inverse-vehicle-inertia-at-crank))
torque-limit=plus-or-minus-maximum-clutch-torque-times-right-continuous-normalized-engagement
vehicle-reaction=equal-and-opposite-applied-engine-torque-times-reduction-as-wheel-force
shift=selected-ratio-changes-instantaneously-while-linear-vehicle-speed-is-preserved
reverse=unsupported
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

constexpr std::string_view kBoundedForwardVehicleDrivetrainDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=bounded-forward-vehicle-drivetrain-pgs-v1
version=1
operation=coupled-engine-clutch-forward-vehicle-road-load-step
row-sources=bounded-forward-clutch-coupling-v1-then-forward-vehicle-road-load-v1
left-boundary-road-capacity=rolling-resistance-force-plus-0.5-times-air-density-at-25c-times-vehicle-speed-squared-times-drag-coefficient-times-frontal-area-plus-maximum-service-brake-force-times-right-continuous-normalized-application
air-density-at-25c=28.97e-3-times-101325-divided-by-(8.31446261815324-times-298.15)
reduction-k=selected-forward-gear-ratio-times-differential-ratio-divided-by-tire-radius
clutch-impulse-j=impulse-on-engine
road-impulse-p=forward-resisting-impulse
clutch-bound=plus-or-minus-maximum-clutch-torque-times-right-continuous-normalized-engagement-times-dt
road-bound=zero-to-left-boundary-road-capacity-times-dt
initial-impulses=j-zero-and-p-zero
iteration-count=exactly-128
iteration-order=clutch-row-then-road-row
clutch-row=j-clamp(-(predicted-engine-angular-speed-k-times-predicted-vehicle-speed+p-times-k-divided-by-vehicle-mass)-divided-by-(inverse-engine-inertia+k-squared-divided-by-vehicle-mass),clutch-bounds)
road-row=p-clamp(vehicle-mass-times-predicted-vehicle-speed-j-times-k,zero,road-bound)
commit-engine-angular-speed=predicted-engine-angular-speed+j-divided-by-engine-inertia
commit-vehicle-speed=predicted-vehicle-speed-j-times-k-divided-by-vehicle-mass-p-divided-by-vehicle-mass
commit-crank-angle=previous-crank-angle-plus-committed-engine-angular-speed-times-dt
commit-vehicle-distance=previous-vehicle-distance-plus-committed-vehicle-speed-times-dt
neutral-or-disengaged=zero-clutch-impulse-and-unilateral-road-step
reverse=unsupported
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
)method";

static_assert(canonical_lf_descriptor(kForwardVehicleRoadLoadDescriptor));
static_assert(canonical_lf_descriptor(kBoundedClutchCouplingDescriptor));
static_assert(canonical_lf_descriptor(kBoundedForwardVehicleDrivetrainDescriptor));

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

std::string_view forward_vehicle_road_load_method_descriptor() noexcept {
    return kForwardVehicleRoadLoadDescriptor;
}

const contract::MethodIdentity &forward_vehicle_road_load_method_identity() {
    static const auto identity = make_identity(kForwardVehicleRoadLoadMethodId,
                                               kForwardVehicleRoadLoadMethodVersion,
                                               kForwardVehicleRoadLoadDescriptor);
    return identity;
}

std::string_view bounded_clutch_coupling_method_descriptor() noexcept {
    return kBoundedClutchCouplingDescriptor;
}

const contract::MethodIdentity &bounded_clutch_coupling_method_identity() {
    static const auto identity = make_identity(kBoundedClutchCouplingMethodId,
                                               kBoundedClutchCouplingMethodVersion,
                                               kBoundedClutchCouplingDescriptor);
    return identity;
}

std::string_view bounded_forward_vehicle_drivetrain_method_descriptor() noexcept {
    return kBoundedForwardVehicleDrivetrainDescriptor;
}

const contract::MethodIdentity &bounded_forward_vehicle_drivetrain_method_identity() {
    static const auto identity =
        make_identity(kBoundedForwardVehicleDrivetrainMethodId,
                      kBoundedForwardVehicleDrivetrainMethodVersion,
                      kBoundedForwardVehicleDrivetrainDescriptor);
    return identity;
}

} // namespace engine_sim_offline::simulation
