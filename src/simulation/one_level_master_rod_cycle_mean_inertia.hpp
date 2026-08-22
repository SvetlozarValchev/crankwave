#pragma once

#include "crankwave/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>

namespace crankwave::simulation {

struct OneLevelMasterRodMechanismKinematicsPlan;

inline constexpr std::string_view kOneLevelMasterRodCycleMeanEquivalentInertiaMethodId =
    "one-level-master-rod-cycle-mean-equivalent-inertia-v1";
inline constexpr std::uint32_t
    kOneLevelMasterRodCycleMeanEquivalentInertiaMethodVersion = 1U;
inline constexpr std::size_t kOneLevelMasterRodCycleMeanInertiaQuadraturePoints = 4096U;
inline constexpr std::size_t kNoOneLevelMasterRodCycleMeanInertiaCylinder =
    std::numeric_limits<std::size_t>::max();

enum class OneLevelMasterRodCycleMeanInertiaIssue : std::uint8_t {
    invalid_articulated_mechanism,
    invalid_quadrature_sample,
    nonfinite_accumulation,
    nonpositive_engine_equivalent_inertia,
};

struct OneLevelMasterRodCycleMeanInertiaError {
    OneLevelMasterRodCycleMeanInertiaIssue issue =
        OneLevelMasterRodCycleMeanInertiaIssue::nonfinite_accumulation;
    std::size_t cylinder_index = kNoOneLevelMasterRodCycleMeanInertiaCylinder;

    friend bool operator==(const OneLevelMasterRodCycleMeanInertiaError &,
                           const OneLevelMasterRodCycleMeanInertiaError &) = default;
};

// The constant crank-referred inertia whose rotational kinetic energy is the
// 4096-point full-cycle mean of the compiled one-level articulated mechanism.
// The authored crank is retained once; each other field is a separately averaged
// kinetic-energy component.
struct OneLevelMasterRodCycleMeanInertia {
    double authored_crank_inertia_kg_m2 = 0.0;
    double piston_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_translation_inertia_kg_m2 = 0.0;
    double connecting_rod_rotation_inertia_kg_m2 = 0.0;
    double engine_equivalent_inertia_kg_m2 = 0.0;

    friend bool operator==(const OneLevelMasterRodCycleMeanInertia &,
                           const OneLevelMasterRodCycleMeanInertia &) = default;
};

using OneLevelMasterRodCycleMeanInertiaCalculation =
    std::variant<OneLevelMasterRodCycleMeanInertia,
                 OneLevelMasterRodCycleMeanInertiaError>;

[[nodiscard]] std::string_view
one_level_master_rod_cycle_mean_inertia_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
one_level_master_rod_cycle_mean_inertia_method_identity();

// Compiles the articulated mechanism once, allocates one exact-size scratch, and
// averages its three moving-body inertia components over a canonical body-angle
// revolution. Scenario attachment inertia is deliberately outside this engine
// baseline calculation.
[[nodiscard]] OneLevelMasterRodCycleMeanInertiaCalculation
calculate_one_level_master_rod_cycle_mean_inertia(
    const OneLevelMasterRodMechanismKinematicsPlan &plan) noexcept;

} // namespace crankwave::simulation
