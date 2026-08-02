#include "simulation/one_level_master_rod_cycle_mean_inertia.hpp"

#include "simulation/one_level_master_rod_configuration_inertia.hpp"

#include <bit>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

constexpr std::string_view kMethodDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=one-level-master-rod-cycle-mean-equivalent-inertia-v1
version=1
operation=full-cycle-mean-crank-referred-kinetic-energy-equivalent-inertia
mechanism=one-authored-rigid-crank-plus-one-level-master-rod-root-and-slave-rigid-rods-and-translating-pistons
crank-contribution=authored-rigid-crank-group-inertia-kg-m2-included-exactly-once-after-moving-component-averages
attached-inertia=excluded-engine-baseline-only
quadrature=4096-point-uniform-midpoint-over-binary64-two-times-pi
sample-coordinate=canonical-pristine-body-angle-psi-rad
sample-angle-rad=(sample-index-plus-binary64-0.5)-times-two-pi-divided-by-4096
articulated-state=one-compiled-mechanism-plus-one-exact-size-caller-owned-scratch-reused-for-every-sample
instantaneous-components=sum-in-stable-plan-cylinder-order-of-piston-mass-times-wrist-derivative-norm-squared-rod-mass-times-center-derivative-norm-squared-and-rod-inertia-times-rod-angle-derivative-squared
quadrature-summation=ascending-sample-index-with-three-separate-binary64-component-accumulators
component-means=each-component-accumulator-divided-once-by-binary64-4096
equivalent-inertia=authored-crank-inertia-plus-piston-mean-plus-rod-translation-mean-plus-rod-rotation-mean-in-written-order
configuration-inertia-total=not-accumulated
configuration-inertia-derivative=not-accumulated
body-angle-reference=crank-tdc-reference-does-not-shift-the-canonical-quadrature-lattice
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

[[nodiscard]] consteval bool
canonical_lf_descriptor(const std::string_view descriptor) noexcept {
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

static_assert(canonical_lf_descriptor(kMethodDescriptor));

[[nodiscard]] OneLevelMasterRodCycleMeanInertiaError
error(const OneLevelMasterRodCycleMeanInertiaIssue issue,
      const std::size_t cylinder_index =
          kNoOneLevelMasterRodCycleMeanInertiaCylinder) noexcept {
    return {issue, cylinder_index};
}

[[nodiscard]] bool same_binary64(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

} // namespace

std::string_view one_level_master_rod_cycle_mean_inertia_method_descriptor() noexcept {
    return kMethodDescriptor;
}

const contract::MethodIdentity &
one_level_master_rod_cycle_mean_inertia_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{kOneLevelMasterRodCycleMeanEquivalentInertiaMethodId},
        kOneLevelMasterRodCycleMeanEquivalentInertiaMethodVersion,
        contract::sha256(std::as_bytes(
            std::span<const char>{kMethodDescriptor.data(), kMethodDescriptor.size()})),
    };
    return identity;
}

OneLevelMasterRodCycleMeanInertiaCalculation
calculate_one_level_master_rod_cycle_mean_inertia(
    const OneLevelMasterRodMechanismKinematicsPlan &plan) noexcept {
    try {
        auto compilation = compile_one_level_master_rod_articulated_mechanism(plan);
        if (const auto *compilation_error =
                std::get_if<OneLevelMasterRodConfigurationInertiaError>(&compilation)) {
            return error(
                OneLevelMasterRodCycleMeanInertiaIssue::invalid_articulated_mechanism,
                compilation_error->cylinder_index);
        }
        auto compiled = std::get<CompiledOneLevelMasterRodArticulatedMechanism>(
            std::move(compilation));
        auto scratch = compiled.make_state_scratch();

        OneLevelMasterRodCycleMeanInertia result;
        result.authored_crank_inertia_kg_m2 =
            plan.rigid_crank_group.authored_crank_inertia_kg_m2;
        double piston_sum_kg_m2 = 0.0;
        double rod_translation_sum_kg_m2 = 0.0;
        double rod_rotation_sum_kg_m2 = 0.0;
        constexpr double sample_count =
            static_cast<double>(kOneLevelMasterRodCycleMeanInertiaQuadraturePoints);
        const double two_pi = 2.0 * std::numbers::pi_v<double>;

        for (std::size_t sample_index = 0;
             sample_index < kOneLevelMasterRodCycleMeanInertiaQuadraturePoints;
             ++sample_index) {
            const double body_angle_psi_rad =
                (static_cast<double>(sample_index) + 0.5) * two_pi / sample_count;
            const auto calculation =
                compiled.evaluate_configuration_inertia_at_body_angle_psi(
                    0.0, body_angle_psi_rad, scratch);
            if (const auto *sample_error =
                    std::get_if<OneLevelMasterRodConfigurationInertiaError>(
                        &calculation)) {
                return error(
                    OneLevelMasterRodCycleMeanInertiaIssue::invalid_quadrature_sample,
                    sample_error->cylinder_index);
            }
            const auto &sample =
                std::get<OneLevelMasterRodConfigurationInertia>(calculation);
            if (!same_binary64(sample.authored_crank_inertia_kg_m2,
                               result.authored_crank_inertia_kg_m2) ||
                !same_binary64(sample.attached_inertia_kg_m2, 0.0)) {
                return error(
                    OneLevelMasterRodCycleMeanInertiaIssue::invalid_quadrature_sample);
            }

            piston_sum_kg_m2 += sample.piston_translation_inertia_kg_m2;
            rod_translation_sum_kg_m2 +=
                sample.connecting_rod_translation_inertia_kg_m2;
            rod_rotation_sum_kg_m2 += sample.connecting_rod_rotation_inertia_kg_m2;
            if (!std::isfinite(piston_sum_kg_m2) ||
                !std::isfinite(rod_translation_sum_kg_m2) ||
                !std::isfinite(rod_rotation_sum_kg_m2)) {
                return error(
                    OneLevelMasterRodCycleMeanInertiaIssue::nonfinite_accumulation);
            }
        }

        result.piston_translation_inertia_kg_m2 = piston_sum_kg_m2 / sample_count;
        result.connecting_rod_translation_inertia_kg_m2 =
            rod_translation_sum_kg_m2 / sample_count;
        result.connecting_rod_rotation_inertia_kg_m2 =
            rod_rotation_sum_kg_m2 / sample_count;
        result.engine_equivalent_inertia_kg_m2 =
            result.authored_crank_inertia_kg_m2 +
            result.piston_translation_inertia_kg_m2 +
            result.connecting_rod_translation_inertia_kg_m2 +
            result.connecting_rod_rotation_inertia_kg_m2;
        if (!std::isfinite(result.authored_crank_inertia_kg_m2) ||
            !std::isfinite(result.piston_translation_inertia_kg_m2) ||
            !std::isfinite(result.connecting_rod_translation_inertia_kg_m2) ||
            !std::isfinite(result.connecting_rod_rotation_inertia_kg_m2) ||
            !std::isfinite(result.engine_equivalent_inertia_kg_m2)) {
            return error(
                OneLevelMasterRodCycleMeanInertiaIssue::nonfinite_accumulation);
        }
        if (!(result.engine_equivalent_inertia_kg_m2 > 0.0)) {
            return error(OneLevelMasterRodCycleMeanInertiaIssue::
                             nonpositive_engine_equivalent_inertia);
        }
        return result;
    } catch (...) {
        return error(
            OneLevelMasterRodCycleMeanInertiaIssue::invalid_articulated_mechanism);
    }
}

} // namespace engine_sim_offline::simulation
