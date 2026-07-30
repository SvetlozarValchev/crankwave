#pragma once

#include "engine_sim_offline/contract/parity_model.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view
    kCenteredSliderCrankCycleMeanEquivalentInertiaMethodId =
        "centered-slider-crank-cycle-mean-equivalent-inertia-v1";
inline constexpr std::uint32_t
    kCenteredSliderCrankCycleMeanEquivalentInertiaMethodVersion = 1U;
inline constexpr std::size_t kCenteredSliderCrankCycleMeanInertiaQuadraturePoints =
    4096U;
inline constexpr std::size_t kNoCenteredSliderCrankInertiaCylinder =
    std::numeric_limits<std::size_t>::max();

enum class CenteredSliderCrankCycleMeanInertiaIssue : std::uint8_t {
    nonfinite_authored_crank_inertia,
    nonpositive_authored_crank_inertia,
    nonfinite_crank_radius,
    nonpositive_crank_radius,
    nonfinite_connecting_rod_length,
    connecting_rod_not_longer_than_crank_radius,
    nonfinite_piston_mass,
    negative_piston_mass,
    nonfinite_connecting_rod_mass,
    negative_connecting_rod_mass,
    nonfinite_connecting_rod_inertia,
    negative_connecting_rod_inertia,
    nonfinite_derived_value,
    nonpositive_engine_equivalent_inertia,
};

struct CenteredSliderCrankCycleMeanInertiaError {
    CenteredSliderCrankCycleMeanInertiaIssue issue =
        CenteredSliderCrankCycleMeanInertiaIssue::nonfinite_derived_value;
    std::size_t cylinder_index = kNoCenteredSliderCrankInertiaCylinder;

    friend bool operator==(const CenteredSliderCrankCycleMeanInertiaError &,
                           const CenteredSliderCrankCycleMeanInertiaError &) = default;
};

struct CenteredSliderCrankCycleMeanInertia {
    double authored_crank_inertia_kg_m2 = 0.0;
    double piston_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_rotation_inertia_kg_m2 = 0.0;
    double engine_equivalent_inertia_kg_m2 = 0.0;

    friend bool operator==(const CenteredSliderCrankCycleMeanInertia &,
                           const CenteredSliderCrankCycleMeanInertia &) = default;
};

using CenteredSliderCrankCycleMeanInertiaCalculation =
    std::variant<CenteredSliderCrankCycleMeanInertia,
                 CenteredSliderCrankCycleMeanInertiaError>;

[[nodiscard]] std::string_view
centered_slider_crank_cycle_mean_inertia_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
centered_slider_crank_cycle_mean_inertia_method_identity();

// Returns the constant crank-referred inertia whose rotational kinetic energy is
// the full-cycle mean kinetic energy of the admitted centered slider-crank
// mechanism. Journal phase is immaterial to this full-cycle mean.
[[nodiscard]] CenteredSliderCrankCycleMeanInertiaCalculation
calculate_centered_slider_crank_cycle_mean_inertia(
    const contract::LegacyMechanismProfile &mechanism) noexcept;

} // namespace engine_sim_offline::simulation
