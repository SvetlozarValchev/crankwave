#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <variant>

namespace engine_sim_offline::simulation {

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

// Exact one-level planar position/volume equation used by pristine engine-sim's
// radial master/slave layout. This primitive owns no inertia, wall reaction, torque,
// gas, or audio behavior.
[[nodiscard]] OneLevelMasterRodSample evaluate_one_level_master_rod(
    const OneLevelMasterRodDriver &driver, const OneLevelMasterRodCylinder &cylinder,
    double body_angle_psi_rad, double angular_speed_rad_s) noexcept;

} // namespace engine_sim_offline::simulation
