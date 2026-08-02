#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/one_level_master_rod_kinematics.hpp"
#include "simulation/rigid_crank_group.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

// The immutable, executable geometry for one currently admitted direct-journal
// cylinder. All low-order consumers use these exact binary64 fields instead of
// reconstructing their own centered slider-crank view from the resolved profile.
struct DirectMechanismCylinderPlan {
    contract::CrankshaftId crankshaft_id;
    CenteredSliderCrankCylinder crank;
    contract::GasVolumeId chamber_volume_id;
    contract::RouteId exhaust_route_id;
    double bore_m = 0.0;
    double stroke_m = 0.0;
    double deck_height_m = 0.0;
    double piston_compression_height_m = 0.0;
    double piston_wrist_pin_position_m = 0.0;
    double head_chamber_volume_m3 = 0.0;
    double piston_displacement_term_m3 = 0.0;
    double authored_journal_angle_rad = 0.0;
    double fixed_geometry_volume_m3 = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;
    double connecting_rod_center_of_mass_from_crank_pin_m = 0.0;

    friend bool operator==(const DirectMechanismCylinderPlan &,
                           const DirectMechanismCylinderPlan &) = default;
};

struct DirectMechanismKinematicsPlan {
    contract::EngineId engine_id;
    std::string engine_profile_id;
    contract::CrankshaftId output_crankshaft_id;
    double crank_tdc_reference_rad = 0.0;
    RigidCrankGroupProperties rigid_crank_group;
    CenteredSliderCrankCycleMeanInertia cycle_mean_inertia;
    std::vector<DirectMechanismCylinderPlan> cylinders;

    friend bool operator==(const DirectMechanismKinematicsPlan &,
                           const DirectMechanismKinematicsPlan &) = default;
};

struct OneLevelMasterRodDirectRootPlan {
    OneLevelMasterRodDriver driver;
    OneLevelMasterRodCylinder cylinder;

    friend bool operator==(const OneLevelMasterRodDirectRootPlan &,
                           const OneLevelMasterRodDirectRootPlan &) = default;
};

struct OneLevelMasterRodSlaveAttachmentPlan {
    // Stable index into OneLevelMasterRodMechanismKinematicsPlan::cylinders.
    // The referenced entry is always a direct root and supplies the driver used
    // by the pure one-level master-rod evaluator.
    std::size_t master_cylinder_index = 0;
    OneLevelMasterRodCylinder cylinder;

    friend bool operator==(const OneLevelMasterRodSlaveAttachmentPlan &,
                           const OneLevelMasterRodSlaveAttachmentPlan &) = default;
};

using OneLevelMasterRodCylinderKinematicsPlan =
    std::variant<OneLevelMasterRodDirectRootPlan, OneLevelMasterRodSlaveAttachmentPlan>;

// One-level master/slave geometry and its authored rigid-body properties. The
// compiler releases it only after every root and slave passes the analytic
// full-cycle geometry certificate. Carrying the physical properties here does not
// itself grant equivalent-inertia, wall-reaction, or torque authority; those require
// a separately admitted articulated-dynamics reduction.
struct OneLevelMasterRodMechanismCylinderPlan {
    contract::CrankshaftId crankshaft_id;
    contract::BankId bank_id;
    contract::GasVolumeId chamber_volume_id;
    contract::RouteId exhaust_route_id;
    double bore_m = 0.0;
    double piston_area_m2 = 0.0;
    double fixed_geometry_volume_m3 = 0.0;
    OneLevelMasterRodFullCycleGeometry full_cycle_geometry;
    double ignition_wire_angle_rad = 0.0;
    double piston_mass_kg = 0.0;
    double connecting_rod_mass_kg = 0.0;
    double connecting_rod_inertia_kg_m2 = 0.0;
    double connecting_rod_center_of_mass_from_big_end_m = 0.0;
    OneLevelMasterRodCylinderKinematicsPlan kinematics;

    friend bool operator==(const OneLevelMasterRodMechanismCylinderPlan &,
                           const OneLevelMasterRodMechanismCylinderPlan &) = default;
};

struct OneLevelMasterRodMechanismKinematicsPlan {
    contract::EngineId engine_id;
    std::string engine_profile_id;
    contract::CrankshaftId output_crankshaft_id;
    double crank_tdc_reference_rad = 0.0;
    RigidCrankGroupProperties rigid_crank_group;
    std::vector<OneLevelMasterRodMechanismCylinderPlan> cylinders;

    friend bool operator==(const OneLevelMasterRodMechanismKinematicsPlan &,
                           const OneLevelMasterRodMechanismKinematicsPlan &) = default;
};

// This variant is deliberately the sole mechanism-plan type. Later mechanism
// families extend this closed execution choice rather than adding parallel
// compiler inputs to mechanics, gas, or crank dynamics.
using MechanismKinematicsPlan = std::variant<DirectMechanismKinematicsPlan,
                                             OneLevelMasterRodMechanismKinematicsPlan>;
using SharedMechanismKinematicsPlan = std::shared_ptr<const MechanismKinematicsPlan>;
using MechanismKinematicsPlanCompileResult =
    std::variant<SharedMechanismKinematicsPlan, contract::ValidationReport>;

[[nodiscard]] MechanismKinematicsPlanCompileResult
compile_mechanism_kinematics_plan(const contract::EngineSpec &engine,
                                  const contract::LowOrderEngineCoreV1 &core);

[[nodiscard]] const DirectMechanismKinematicsPlan *
direct_mechanism_kinematics_plan(const SharedMechanismKinematicsPlan &plan) noexcept;

[[nodiscard]] const OneLevelMasterRodMechanismKinematicsPlan *
one_level_master_rod_mechanism_kinematics_plan(
    const SharedMechanismKinematicsPlan &plan) noexcept;

// Point-evaluation helper. It resolves the stable slave-to-root index and delegates
// to the pure one-level master-rod primitive. Plan compilation already certified
// full-cycle geometry. The helper does not select a motion owner; callers still enforce
// the prescribed-only or direct-dynamic admission boundary.
[[nodiscard]] OneLevelMasterRodSample evaluate_one_level_master_rod_plan(
    const OneLevelMasterRodMechanismKinematicsPlan &plan, std::size_t cylinder_index,
    double body_angle_psi_rad, double angular_speed_rad_s) noexcept;

// Exact source binding for an already-compiled plan. This compares every authored
// or resolved source field consumed by its selected plan alternative.
[[nodiscard]] bool mechanism_kinematics_plan_matches_source(
    const SharedMechanismKinematicsPlan &plan, const contract::EngineSpec &engine,
    const contract::LowOrderEngineCoreV1 &core) noexcept;

} // namespace engine_sim_offline::simulation
