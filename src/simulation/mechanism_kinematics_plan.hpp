#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

// The immutable, executable geometry for one currently admitted direct-journal
// cylinder. All low-order consumers use these exact binary64 fields instead of
// reconstructing their own centered slider-crank view from the resolved profile.
struct DirectMechanismCylinderPlan {
    CenteredSliderCrankCylinder crank;
    contract::GasVolumeId chamber_volume_id;
    contract::RouteId exhaust_route_id;
    double bore_m = 0.0;
    double deck_height_m = 0.0;
    double piston_compression_height_m = 0.0;
    double head_chamber_volume_m3 = 0.0;
    double piston_displacement_term_m3 = 0.0;
    double authored_journal_angle_rad = 0.0;
    double fixed_geometry_volume_m3 = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;

    friend bool operator==(const DirectMechanismCylinderPlan &,
                           const DirectMechanismCylinderPlan &) = default;
};

struct DirectMechanismKinematicsPlan {
    contract::EngineId engine_id;
    std::string engine_profile_id;
    double crank_tdc_reference_rad = 0.0;
    double authored_crank_inertia_kg_m2 = 0.0;
    CenteredSliderCrankCycleMeanInertia cycle_mean_inertia;
    std::vector<DirectMechanismCylinderPlan> cylinders;

    friend bool operator==(const DirectMechanismKinematicsPlan &,
                           const DirectMechanismKinematicsPlan &) = default;
};

// This variant is deliberately the sole mechanism-plan type. Later mechanism
// families extend this closed execution choice rather than adding parallel
// compiler inputs to mechanics, gas, or crank dynamics.
using MechanismKinematicsPlan = std::variant<DirectMechanismKinematicsPlan>;
using SharedMechanismKinematicsPlan =
    std::shared_ptr<const MechanismKinematicsPlan>;
using MechanismKinematicsPlanCompileResult =
    std::variant<SharedMechanismKinematicsPlan, contract::ValidationReport>;

[[nodiscard]] MechanismKinematicsPlanCompileResult
compile_mechanism_kinematics_plan(const contract::EngineSpec &engine,
                                  const contract::LowOrderEngineCoreV1 &core);

[[nodiscard]] const DirectMechanismKinematicsPlan *
direct_mechanism_kinematics_plan(
    const SharedMechanismKinematicsPlan &plan) noexcept;

// Exact source binding for an already-compiled plan. This compares every authored
// or resolved source field consumed by the direct plan without re-deriving any
// geometry or inertia.
[[nodiscard]] bool mechanism_kinematics_plan_matches_source(
    const SharedMechanismKinematicsPlan &plan,
    const contract::EngineSpec &engine,
    const contract::LowOrderEngineCoreV1 &core) noexcept;

} // namespace engine_sim_offline::simulation
