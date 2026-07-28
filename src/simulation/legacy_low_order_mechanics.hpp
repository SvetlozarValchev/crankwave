#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/kinematic_scenario_schedule.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

namespace detail {
struct LowOrderEngineCoreV1RuntimeFactory;
}

struct MechanismCylinderSample {
    contract::CylinderId cylinder_id;
    contract::RouteId exhaust_route_id;
    double geometric_tdc_rad = 0.0;
    double ignition_wire_angle_rad = 0.0;
    double phase_rad = 0.0;
    double piston_travel_m = 0.0;
    double chamber_volume_m3 = 0.0;
    double dx_dtheta_m_per_rad = 0.0;
    double dvolume_dtheta_m3_per_rad = 0.0;
    double piston_speed_abs_m_s = 0.0;
    bool spark_crossed = false;

    friend bool operator==(const MechanismCylinderSample &,
                           const MechanismCylinderSample &) = default;
};

struct ScheduledMechanismEvent {
    std::uint8_t ordinal_within_step = 0;
    contract::EngineEventPayload payload;

    friend bool operator==(const ScheduledMechanismEvent &,
                           const ScheduledMechanismEvent &) = default;
};

struct LegacyMechanismStep {
    contract::RationalRateHz rate;
    std::uint64_t sample_index = 0;
    std::uint64_t step_end_index = 0;
    std::uint64_t timestamp_tick = 0;
    contract::OperatingState operating_state;
    double requested_throttle_01 = 0.0;
    double resolved_engine_throttle_01 = 0.0;
    double intake_plate_position_01 = 0.0;
    double main_flow_multiplier_01 = 0.0;
    double engine_speed_rpm = 0.0;
    double omega_legacy_rad_s = 0.0;
    double angular_speed_rad_s = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double body_angle_psi_rad = 0.0;
    double theta_cycle_rad = 0.0;
    double theta_unwrapped_rad = 0.0;
    double filtered_engine_speed_rpm = 0.0;
    double timing_advance_rad = 0.0;
    double limiter_timer_s = 0.0;
    bool limiter_cut_active = false;
    std::vector<MechanismCylinderSample> cylinders;
    std::vector<ScheduledMechanismEvent> events;

    friend bool operator==(const LegacyMechanismStep &,
                           const LegacyMechanismStep &) = default;
};

struct LegacyMechanicsCompleted {
    std::uint64_t sample_count = 0;

    friend bool operator==(const LegacyMechanicsCompleted &,
                           const LegacyMechanicsCompleted &) = default;
};

using LegacyMechanicsAdvanceResult =
    std::variant<std::reference_wrapper<const LegacyMechanismStep>,
                 LegacyMechanicsCompleted, contract::FailureContext>;

class LegacyLowOrderMechanicsSession final {
  public:
    LegacyLowOrderMechanicsSession(const LegacyLowOrderMechanicsSession &) = delete;
    LegacyLowOrderMechanicsSession &
    operator=(const LegacyLowOrderMechanicsSession &) = delete;
    LegacyLowOrderMechanicsSession(LegacyLowOrderMechanicsSession &&) noexcept =
        default;
    LegacyLowOrderMechanicsSession &
    operator=(LegacyLowOrderMechanicsSession &&) noexcept = default;

    // The returned reference is session-owned and remains valid only until the next
    // advance call. Completion and failure are terminal and stable.
    [[nodiscard]] LegacyMechanicsAdvanceResult advance();
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::span<const CenteredSliderCrankCylinder>
    cylinder_models() const noexcept;

  private:
    struct CylinderModel {
        CenteredSliderCrankCylinder crank;
        contract::RouteId exhaust_route_id;
    };

    LegacyLowOrderMechanicsSession(
        KinematicScenarioCursor scenario_cursor, contract::RationalRateHz rate,
        double crank_tdc_reference_rad, double initial_theta_cycle_rad,
        std::vector<CylinderModel> cylinders,
        std::vector<LegacyTrianglePoint> timing_curve, double timing_curve_radius_rad_s,
        double limiter_speed_rpm, double limiter_hold_s, bool limiter_enabled,
        std::string model_id, std::string profile_id, std::string scenario_id,
        contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          std::optional<contract::CylinderId> cylinder_id = std::nullopt,
          std::optional<contract::RouteId> route_id = std::nullopt) const;

    KinematicScenarioCursor scenario_cursor_;
    contract::RationalRateHz rate_;
    double crank_tdc_reference_rad_ = 0.0;
    double step_s_ = 0.0;
    double filter_alpha_ = 0.0;
    std::vector<CylinderModel> cylinders_;
    std::size_t maximum_event_count_ = 0;
    std::vector<CenteredSliderCrankCylinder> cylinder_model_view_;
    std::vector<LegacyTrianglePoint> timing_curve_;
    double timing_curve_radius_rad_s_ = 0.0;
    double limiter_speed_rpm_ = 0.0;
    double limiter_hold_s_ = 0.0;
    bool limiter_enabled_ = false;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;

    double body_angle_psi_rad_ = 0.0;
    double theta_cycle_rad_ = 0.0;
    double theta_unwrapped_rad_ = 0.0;
    double previous_angular_speed_rad_s_ = 0.0;
    double filtered_engine_speed_rpm_ = 0.0;
    double ignition_saved_angle_rad_ = 0.0;
    double limiter_timer_s_ = 0.0;
    std::uint64_t produced_sample_count_ = 0;
    LegacyMechanismStep step_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend struct detail::LowOrderEngineCoreV1RuntimeFactory;
};

} // namespace engine_sim_offline::simulation
