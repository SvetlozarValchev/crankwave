#pragma once

#include "crankwave/contract/common.hpp"

#include <cstddef>
#include <limits>
#include <variant>

namespace crankwave::simulation {

// Geometry shared by a direct master cylinder and each one-level slave attached to
// that master rod. The journal phase is global/body-local, matching engine-sim's
// crankshaft rod-journal angle rather than a bank-relative direct-cylinder phase.
struct OneLevelMasterRodDriver {
    double crank_radius_m = 0.0;
    double crank_journal_global_phase_rad = 0.0;
    double master_bank_angle_rad = 0.0;
    double master_connecting_rod_length_m = 0.0;

    friend bool operator==(const OneLevelMasterRodDriver &,
                           const OneLevelMasterRodDriver &) = default;
};

struct OneLevelMasterRodRootJournal {
    friend bool operator==(const OneLevelMasterRodRootJournal &,
                           const OneLevelMasterRodRootJournal &) = default;
};

struct OneLevelMasterRodSlavePin {
    double throw_radius_m = 0.0;
    // Zero points from the master big end toward its wrist pin; positive phase is
    // counter-clockwise in the engine plane.
    double local_phase_rad = 0.0;

    friend bool operator==(const OneLevelMasterRodSlavePin &,
                           const OneLevelMasterRodSlavePin &) = default;
};

using OneLevelMasterRodJournal =
    std::variant<OneLevelMasterRodRootJournal, OneLevelMasterRodSlavePin>;

struct OneLevelMasterRodCylinder {
    contract::CylinderId cylinder_id;
    // engine-sim's authored cylinder-bank angle. The slider axis is angle + pi/2.
    double bank_angle_rad = 0.0;
    double connecting_rod_length_m = 0.0;
    double piston_area_m2 = 0.0;
    double deck_height_m = 0.0;
    double piston_compression_height_m = 0.0;
    // Axial piston-datum offset: positive places the datum deckward of the pin.
    double piston_wrist_pin_position_m = 0.0;
    double head_chamber_volume_m3 = 0.0;
    double piston_displacement_term_m3 = 0.0;
    OneLevelMasterRodJournal journal;

    friend bool operator==(const OneLevelMasterRodCylinder &,
                           const OneLevelMasterRodCylinder &) = default;
};

struct OneLevelMasterRodSample {
    // Absolute position along the cylinder-bank axis. A radial cylinder has no
    // generally valid centered-slider/TDC-relative travel identity.
    double piston_axis_position_m = 0.0;
    // Derivative of the absolute axis position with respect to increasing engine
    // cycle angle. engine-sim's body angle moves in the opposite direction.
    double piston_axis_derivative_m_per_rad = 0.0;
    double chamber_volume_m3 = 0.0;
    double dvolume_dtheta_m3_per_rad = 0.0;
    double piston_speed_abs_m_s = 0.0;
    bool valid = false;

    friend bool operator==(const OneLevelMasterRodSample &,
                           const OneLevelMasterRodSample &) = default;
};

// A full-cycle proof for the one-level geometry, kept separate from point
// evaluation. Reachability is exact for a root journal and deliberately
// sufficient (not claimed exact) for a slave pin.
enum class OneLevelMasterRodFullCycleReason {
    admitted,
    invalid_geometry,
    reachability_not_certified,
    chamber_volume_not_certified,
};

struct OneLevelMasterRodFullCycleCheck {
    OneLevelMasterRodFullCycleReason reason =
        OneLevelMasterRodFullCycleReason::invalid_geometry;
    // Minimum forward reach margin over every linkage needed by this cylinder.
    // A nonpositive value cannot certify a forward slider solution for the cycle.
    double forward_reach_margin_m = std::numeric_limits<double>::quiet_NaN();
    // Exact root minimum or a conservative slave lower bound. This is NaN when
    // invalid or unreachable geometry prevents a meaningful volume certificate.
    double minimum_chamber_volume_m3 = std::numeric_limits<double>::quiet_NaN();

    [[nodiscard]] bool admitted() const noexcept {
        return reason == OneLevelMasterRodFullCycleReason::admitted;
    }

    friend bool operator==(const OneLevelMasterRodFullCycleCheck &,
                           const OneLevelMasterRodFullCycleCheck &) = default;
};

// Deterministic full-revolution extrema derived from the exact point evaluator.
// The currently admitted slave subset has exactly two simple stationary points;
// geometries with a more complicated piston path fail closed instead of being
// collapsed to a nominal twice-crank-throw stroke.
struct OneLevelMasterRodFullCycleGeometry {
    std::size_t stationary_point_count = 0;
    double minimum_piston_axis_position_m = 0.0;
    double maximum_piston_axis_position_m = 0.0;
    double swept_stroke_m = 0.0;
    double minimum_chamber_volume_m3 = 0.0;
    double maximum_chamber_volume_m3 = 0.0;
    double swept_displacement_m3 = 0.0;
    double piston_axis_path_length_m_per_crank_revolution = 0.0;

    friend bool operator==(const OneLevelMasterRodFullCycleGeometry &,
                           const OneLevelMasterRodFullCycleGeometry &) = default;
};

enum class OneLevelMasterRodFullCycleGeometryIssue {
    full_cycle_not_certified,
    stationary_point_isolation_ambiguous,
    invalid_evaluator_sample,
    insufficient_stationary_points,
    nonfinite_derived_geometry,
};

using OneLevelMasterRodFullCycleGeometryCalculation =
    std::variant<OneLevelMasterRodFullCycleGeometry,
                 OneLevelMasterRodFullCycleGeometryIssue>;

// Exact one-level planar position/volume equation used by pristine engine-sim's
// radial master/slave layout. This primitive owns no inertia, wall reaction, torque,
// gas, or audio behavior.
[[nodiscard]] OneLevelMasterRodSample evaluate_one_level_master_rod(
    const OneLevelMasterRodDriver &driver, const OneLevelMasterRodCylinder &cylinder,
    double body_angle_psi_rad, double angular_speed_rad_s) noexcept;

// Analytically certifies ideal linkage reachability and strictly positive chamber
// volume over a complete crank cycle, with a scale-aware guard that refuses
// binary64-ambiguous reach and volume margins. The point evaluator still validates
// every evaluated state and fails closed on numerical degeneracy. This performs no
// sampling and grants no inertia, reaction, torque, gas, or runtime authority.
[[nodiscard]] OneLevelMasterRodFullCycleCheck certify_one_level_master_rod_full_cycle(
    const OneLevelMasterRodDriver &driver,
    const OneLevelMasterRodCylinder &cylinder) noexcept;

// Resolves the admitted two-stationary-point slave path with independent 4096- and
// 8192-node binary64 probe lattices and safeguarded bracket refinement. Any invalid
// evaluation, lattice disagreement, extra/missing/tangent stationary point, or
// numerically ambiguous unbracketed stationary point fails closed. Direct roots use
// exact analytic dead centers. Chamber extrema are retained from the exact evaluator
// arithmetic at the resolved piston extrema.
[[nodiscard]] OneLevelMasterRodFullCycleGeometryCalculation
calculate_one_level_master_rod_full_cycle_geometry(
    const OneLevelMasterRodDriver &driver,
    const OneLevelMasterRodCylinder &cylinder) noexcept;

} // namespace crankwave::simulation
