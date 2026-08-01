#include "simulation/centered_slider_crank_equivalent_inertia.hpp"

#include <cmath>
#include <numbers>
#include <span>

namespace engine_sim_offline::simulation {
namespace {

constexpr std::string_view kMethodDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=centered-slider-crank-cycle-mean-equivalent-inertia-v1
version=1
operation=full-cycle-mean-crank-referred-kinetic-energy-equivalent-inertia
mechanism=one-authored-rigid-crank-plus-zero-or-more-centered-rigid-rod-and-translating-piston-assemblies
crank-contribution=authored-crank-inertia-kg-m2-without-readding-crank-or-flywheel-mass
rod-center=midpoint-between-crank-pin-and-wrist-pin
quadrature=4096-point-uniform-midpoint-over-binary64-two-times-pi
sample-angle-rad=(sample-index-plus-binary64-0.5)-times-two-pi-divided-by-4096
slider-position=r-times-cos-phi-plus-sqrt(l-times-l-minus-r-times-r-times-sin-phi-times-sin-phi)
equivalent-inertia=authored-crank-inertia-plus-cycle-mean-sum-of-piston-mass-times-piston-position-derivative-squared-plus-rod-mass-times-rod-center-velocity-derivative-norm-squared-plus-rod-inertia-times-rod-angle-derivative-squared
journal-phase=omitted-because-full-cycle-mean-is-invariant-under-periodic-phase-shift
summation=ascending-cylinder-vector-order-then-ascending-sample-index
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

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

static_assert(canonical_lf_descriptor(kMethodDescriptor));

[[nodiscard]] CenteredSliderCrankCycleMeanInertiaCalculation
error(CenteredSliderCrankCycleMeanInertiaIssue issue,
      std::size_t cylinder_index = kNoCenteredSliderCrankInertiaCylinder) noexcept {
    return CenteredSliderCrankCycleMeanInertiaError{issue, cylinder_index};
}

[[nodiscard]] CenteredSliderCrankConfigurationInertiaError configuration_error(
    CenteredSliderCrankConfigurationInertiaIssue issue,
    std::size_t cylinder_index = kNoCenteredSliderCrankInertiaCylinder) noexcept {
    return {issue, cylinder_index};
}

} // namespace

std::string_view centered_slider_crank_cycle_mean_inertia_method_descriptor() noexcept {
    return kMethodDescriptor;
}

const contract::MethodIdentity &
centered_slider_crank_cycle_mean_inertia_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{kCenteredSliderCrankCycleMeanEquivalentInertiaMethodId},
        kCenteredSliderCrankCycleMeanEquivalentInertiaMethodVersion,
        contract::sha256(std::as_bytes(
            std::span<const char>{kMethodDescriptor.data(), kMethodDescriptor.size()})),
    };
    return identity;
}

CenteredSliderCrankCycleMeanInertiaCalculation
calculate_centered_slider_crank_cycle_mean_inertia(
    const contract::LegacyMechanismProfile &mechanism) noexcept {
    const auto *output_crank = contract::find_output_crank(mechanism);
    if (output_crank == nullptr) {
        return error(
            CenteredSliderCrankCycleMeanInertiaIssue::missing_output_crankshaft);
    }
    const double authored_crank_inertia_kg_m2 =
        output_crank->authored_crank_inertia_kg_m2.value;
    if (!std::isfinite(authored_crank_inertia_kg_m2)) {
        return error(
            CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_authored_crank_inertia);
    }
    if (!(authored_crank_inertia_kg_m2 > 0.0)) {
        return error(CenteredSliderCrankCycleMeanInertiaIssue::
                         nonpositive_authored_crank_inertia);
    }

    CenteredSliderCrankCycleMeanInertia result;
    result.authored_crank_inertia_kg_m2 = authored_crank_inertia_kg_m2;

    constexpr double sample_count =
        static_cast<double>(kCenteredSliderCrankCycleMeanInertiaQuadraturePoints);
    const double two_pi = 2.0 * std::numbers::pi_v<double>;

    for (std::size_t cylinder_index = 0; cylinder_index < mechanism.cylinders.size();
         ++cylinder_index) {
        const auto &cylinder = mechanism.cylinders[cylinder_index];
        const auto &parameters = cylinder.parameters;
        const auto *direct =
            std::get_if<contract::LegacyDirectJournalKinematics>(&cylinder.kinematics);
        if (direct == nullptr) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::
                             unsupported_cylinder_kinematics,
                         cylinder_index);
        }
        const double crank_radius_m = direct->crank_radius_m.value;
        const double connecting_rod_length_m = parameters.connecting_rod_length_m.value;
        const double piston_mass_kg = parameters.piston_mass_kg.value;
        const double connecting_rod_mass_kg = parameters.connecting_rod_mass_kg.value;
        const double connecting_rod_inertia_kg_m2 =
            parameters.connecting_rod_inertia_kg_m2.value;

        if (!std::isfinite(crank_radius_m)) {
            return error(
                CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_crank_radius,
                cylinder_index);
        }
        if (!(crank_radius_m > 0.0)) {
            return error(
                CenteredSliderCrankCycleMeanInertiaIssue::nonpositive_crank_radius,
                cylinder_index);
        }
        if (!std::isfinite(connecting_rod_length_m)) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::
                             nonfinite_connecting_rod_length,
                         cylinder_index);
        }
        if (!(connecting_rod_length_m > crank_radius_m)) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::
                             connecting_rod_not_longer_than_crank_radius,
                         cylinder_index);
        }
        if (!std::isfinite(piston_mass_kg)) {
            return error(
                CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_piston_mass,
                cylinder_index);
        }
        if (piston_mass_kg < 0.0) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::negative_piston_mass,
                         cylinder_index);
        }
        if (!std::isfinite(connecting_rod_mass_kg)) {
            return error(
                CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_connecting_rod_mass,
                cylinder_index);
        }
        if (connecting_rod_mass_kg < 0.0) {
            return error(
                CenteredSliderCrankCycleMeanInertiaIssue::negative_connecting_rod_mass,
                cylinder_index);
        }
        if (!std::isfinite(connecting_rod_inertia_kg_m2)) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::
                             nonfinite_connecting_rod_inertia,
                         cylinder_index);
        }
        if (connecting_rod_inertia_kg_m2 < 0.0) {
            return error(CenteredSliderCrankCycleMeanInertiaIssue::
                             negative_connecting_rod_inertia,
                         cylinder_index);
        }

        double piston_translation_sum_kg_m2 = 0.0;
        double rod_translation_sum_kg_m2 = 0.0;
        double rod_rotation_sum_kg_m2 = 0.0;
        for (std::size_t sample_index = 0;
             sample_index < kCenteredSliderCrankCycleMeanInertiaQuadraturePoints;
             ++sample_index) {
            const double phase_rad =
                (static_cast<double>(sample_index) + 0.5) * two_pi / sample_count;
            const double sine = std::sin(phase_rad);
            const double cosine = std::cos(phase_rad);
            const double sine_squared = sine * sine;
            const double crank_radius_squared = crank_radius_m * crank_radius_m;
            const double root =
                std::sqrt(connecting_rod_length_m * connecting_rod_length_m -
                          crank_radius_squared * sine_squared);

            const double crank_pin_dx_dtheta_m = crank_radius_m * cosine;
            const double crank_pin_dy_dtheta_m = -crank_radius_m * sine;
            const double wrist_pin_dy_dtheta_m =
                -crank_radius_m * sine - crank_radius_squared * sine * cosine / root;
            const double rod_center_dx_dtheta_m = 0.5 * crank_pin_dx_dtheta_m;
            const double rod_center_dy_dtheta_m =
                0.5 * (crank_pin_dy_dtheta_m + wrist_pin_dy_dtheta_m);
            const double rod_angle_derivative = crank_radius_m * cosine / root;

            piston_translation_sum_kg_m2 +=
                piston_mass_kg * wrist_pin_dy_dtheta_m * wrist_pin_dy_dtheta_m;
            rod_translation_sum_kg_m2 +=
                connecting_rod_mass_kg *
                (rod_center_dx_dtheta_m * rod_center_dx_dtheta_m +
                 rod_center_dy_dtheta_m * rod_center_dy_dtheta_m);
            rod_rotation_sum_kg_m2 += connecting_rod_inertia_kg_m2 *
                                      rod_angle_derivative * rod_angle_derivative;
        }

        result.piston_translation_inertia_kg_m2 +=
            piston_translation_sum_kg_m2 / sample_count;
        result.connecting_rod_translation_inertia_kg_m2 +=
            rod_translation_sum_kg_m2 / sample_count;
        result.connecting_rod_rotation_inertia_kg_m2 +=
            rod_rotation_sum_kg_m2 / sample_count;
    }

    result.engine_equivalent_inertia_kg_m2 =
        result.authored_crank_inertia_kg_m2 + result.piston_translation_inertia_kg_m2 +
        result.connecting_rod_translation_inertia_kg_m2 +
        result.connecting_rod_rotation_inertia_kg_m2;
    if (!std::isfinite(result.piston_translation_inertia_kg_m2) ||
        !std::isfinite(result.connecting_rod_translation_inertia_kg_m2) ||
        !std::isfinite(result.connecting_rod_rotation_inertia_kg_m2) ||
        !std::isfinite(result.engine_equivalent_inertia_kg_m2)) {
        return error(CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_derived_value);
    }
    if (!(result.engine_equivalent_inertia_kg_m2 > 0.0)) {
        return error(CenteredSliderCrankCycleMeanInertiaIssue::
                         nonpositive_engine_equivalent_inertia);
    }
    return result;
}

CenteredSliderCrankConfigurationInertiaCalculation
evaluate_centered_slider_crank_configuration_inertia(
    const CenteredSliderCrankConfigurationInertiaPlan &plan,
    const double crank_angle_rad) noexcept {
    if (!std::isfinite(crank_angle_rad)) {
        return configuration_error(
            CenteredSliderCrankConfigurationInertiaIssue::nonfinite_crank_angle);
    }
    if (!std::isfinite(plan.authored_crank_inertia_kg_m2) ||
        !std::isfinite(plan.attached_inertia_kg_m2)) {
        return configuration_error(
            CenteredSliderCrankConfigurationInertiaIssue::nonfinite_plan_value);
    }
    if (!(plan.authored_crank_inertia_kg_m2 > 0.0)) {
        return configuration_error(CenteredSliderCrankConfigurationInertiaIssue::
                                       nonpositive_authored_crank_inertia);
    }
    if (plan.attached_inertia_kg_m2 < 0.0 ||
        std::signbit(plan.attached_inertia_kg_m2)) {
        return configuration_error(
            CenteredSliderCrankConfigurationInertiaIssue::negative_attached_inertia);
    }

    CenteredSliderCrankConfigurationInertia result;
    result.authored_crank_inertia_kg_m2 = plan.authored_crank_inertia_kg_m2;
    result.attached_inertia_kg_m2 = plan.attached_inertia_kg_m2;

    for (std::size_t cylinder_index = 0; cylinder_index < plan.cylinders.size();
         ++cylinder_index) {
        const auto &cylinder = plan.cylinders[cylinder_index];
        if (!std::isfinite(cylinder.geometric_tdc_rad) ||
            !std::isfinite(cylinder.crank_radius_m) ||
            !std::isfinite(cylinder.connecting_rod_length_m) ||
            !std::isfinite(cylinder.piston_mass_kg) ||
            !std::isfinite(cylinder.connecting_rod_mass_kg) ||
            !std::isfinite(cylinder.connecting_rod_inertia_kg_m2)) {
            return configuration_error(
                CenteredSliderCrankConfigurationInertiaIssue::nonfinite_plan_value,
                cylinder_index);
        }
        if (!(cylinder.crank_radius_m > 0.0) ||
            !(cylinder.connecting_rod_length_m > cylinder.crank_radius_m)) {
            return configuration_error(CenteredSliderCrankConfigurationInertiaIssue::
                                           invalid_slider_crank_geometry,
                                       cylinder_index);
        }
        if (cylinder.piston_mass_kg < 0.0 || cylinder.connecting_rod_mass_kg < 0.0 ||
            cylinder.connecting_rod_inertia_kg_m2 < 0.0) {
            return configuration_error(
                CenteredSliderCrankConfigurationInertiaIssue::negative_mass_property,
                cylinder_index);
        }

        const double phase_rad = crank_angle_rad - cylinder.geometric_tdc_rad;
        const double sine = std::sin(phase_rad);
        const double cosine = std::cos(phase_rad);
        const double crank_radius_squared =
            cylinder.crank_radius_m * cylinder.crank_radius_m;
        const double rod_length_squared =
            cylinder.connecting_rod_length_m * cylinder.connecting_rod_length_m;
        const double root =
            std::sqrt(rod_length_squared - crank_radius_squared * sine * sine);
        if (!std::isfinite(phase_rad) || !std::isfinite(sine) ||
            !std::isfinite(cosine) || !std::isfinite(root) || !(root > 0.0)) {
            return configuration_error(
                CenteredSliderCrankConfigurationInertiaIssue::nonfinite_derived_value,
                cylinder_index);
        }

        const double piston_axis_first = -cylinder.crank_radius_m * sine -
                                         crank_radius_squared * sine * cosine / root;
        const double root_cubed = root * root * root;
        const double piston_axis_second =
            -cylinder.crank_radius_m * cosine -
            crank_radius_squared * (cosine * cosine - sine * sine) / root -
            crank_radius_squared * crank_radius_squared * sine * sine * cosine *
                cosine / root_cubed;

        const double crank_pin_normal_first = cylinder.crank_radius_m * cosine;
        const double crank_pin_axis_first = -cylinder.crank_radius_m * sine;
        const double crank_pin_normal_second = -cylinder.crank_radius_m * sine;
        const double crank_pin_axis_second = -cylinder.crank_radius_m * cosine;
        const double rod_center_normal_first = 0.5 * crank_pin_normal_first;
        const double rod_center_axis_first =
            0.5 * (crank_pin_axis_first + piston_axis_first);
        const double rod_center_normal_second = 0.5 * crank_pin_normal_second;
        const double rod_center_axis_second =
            0.5 * (crank_pin_axis_second + piston_axis_second);
        const double rod_angle_first = cylinder.crank_radius_m * cosine / root;
        const double rod_angle_second = -cylinder.crank_radius_m *
                                        (rod_length_squared - crank_radius_squared) *
                                        sine / root_cubed;

        const double piston_translation_inertia =
            cylinder.piston_mass_kg * piston_axis_first * piston_axis_first;
        const double rod_translation_inertia =
            cylinder.connecting_rod_mass_kg *
            (rod_center_normal_first * rod_center_normal_first +
             rod_center_axis_first * rod_center_axis_first);
        const double rod_rotation_inertia =
            cylinder.connecting_rod_inertia_kg_m2 * rod_angle_first * rod_angle_first;
        const double piston_translation_derivative =
            2.0 * cylinder.piston_mass_kg * piston_axis_first * piston_axis_second;
        const double rod_translation_derivative =
            2.0 * cylinder.connecting_rod_mass_kg *
            (rod_center_normal_first * rod_center_normal_second +
             rod_center_axis_first * rod_center_axis_second);
        const double rod_rotation_derivative = 2.0 *
                                               cylinder.connecting_rod_inertia_kg_m2 *
                                               rod_angle_first * rod_angle_second;

        if (!std::isfinite(piston_translation_inertia) ||
            !std::isfinite(rod_translation_inertia) ||
            !std::isfinite(rod_rotation_inertia) ||
            !std::isfinite(piston_translation_derivative) ||
            !std::isfinite(rod_translation_derivative) ||
            !std::isfinite(rod_rotation_derivative)) {
            return configuration_error(
                CenteredSliderCrankConfigurationInertiaIssue::nonfinite_derived_value,
                cylinder_index);
        }
        result.piston_translation_inertia_kg_m2 += piston_translation_inertia;
        result.connecting_rod_translation_inertia_kg_m2 += rod_translation_inertia;
        result.connecting_rod_rotation_inertia_kg_m2 += rod_rotation_inertia;
        result.piston_translation_derivative_kg_m2_per_rad +=
            piston_translation_derivative;
        result.connecting_rod_translation_derivative_kg_m2_per_rad +=
            rod_translation_derivative;
        result.connecting_rod_rotation_derivative_kg_m2_per_rad +=
            rod_rotation_derivative;
    }

    result.total_inertia_kg_m2 = result.authored_crank_inertia_kg_m2 +
                                 result.attached_inertia_kg_m2 +
                                 result.piston_translation_inertia_kg_m2 +
                                 result.connecting_rod_translation_inertia_kg_m2 +
                                 result.connecting_rod_rotation_inertia_kg_m2;
    result.total_derivative_kg_m2_per_rad =
        result.piston_translation_derivative_kg_m2_per_rad +
        result.connecting_rod_translation_derivative_kg_m2_per_rad +
        result.connecting_rod_rotation_derivative_kg_m2_per_rad;
    if (!std::isfinite(result.total_inertia_kg_m2) ||
        !std::isfinite(result.total_derivative_kg_m2_per_rad)) {
        return configuration_error(
            CenteredSliderCrankConfigurationInertiaIssue::nonfinite_derived_value);
    }
    return result;
}

} // namespace engine_sim_offline::simulation
