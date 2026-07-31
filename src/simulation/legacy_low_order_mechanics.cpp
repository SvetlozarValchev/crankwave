#include "simulation/legacy_low_order_mechanics.hpp"

#include "simulation/legacy_ignition_schedule.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

bool finite_step_scalars(const LegacyMechanismStep &step) noexcept {
    return std::isfinite(step.requested_throttle_01) &&
           std::isfinite(step.resolved_engine_throttle_01) &&
           std::isfinite(step.intake_plate_position_01) &&
           std::isfinite(step.main_flow_multiplier_01) &&
           std::isfinite(step.engine_speed_rpm) &&
           std::isfinite(step.omega_legacy_rad_s) &&
           std::isfinite(step.angular_speed_rad_s) &&
           std::isfinite(step.angular_acceleration_rad_s2) &&
           std::isfinite(step.body_angle_psi_rad) &&
           std::isfinite(step.theta_cycle_rad) &&
           std::isfinite(step.theta_unwrapped_rad) &&
           std::isfinite(step.filtered_engine_speed_rpm) &&
           std::isfinite(step.timing_advance_rad) &&
           std::isfinite(step.external_resisting_torque_nm) &&
           std::isfinite(step.limiter_timer_s);
}

bool finite_canonical_nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && !std::signbit(value);
}

} // namespace

LegacyLowOrderMechanicsSession::LegacyLowOrderMechanicsSession(
    ScenarioControlCursor control_cursor,
    std::optional<KinematicScenarioCursor> kinematic_cursor,
    contract::RationalRateHz rate,
    SharedMechanismKinematicsPlan mechanism_plan,
    double initial_theta_cycle_rad,
    std::vector<LegacyTrianglePoint> timing_curve, double timing_curve_radius_rad_s,
    LegacyThrottleControllerParameters throttle_controller,
    double idle_throttle_plate_position_01,
    double limiter_speed_rpm, double limiter_hold_s, std::string model_id,
    std::string profile_id, std::string scenario_id, contract::EngineId engine_id)
    : control_cursor_(std::move(control_cursor)),
      kinematic_cursor_(std::move(kinematic_cursor)), rate_(rate),
      mechanism_plan_(std::move(mechanism_plan)),
      crank_tdc_reference_rad_(
          direct_mechanism_kinematics_plan(mechanism_plan_)
              ->crank_tdc_reference_rad),
      step_s_(1.0 / 10000.0), filter_alpha_(step_s_ / (100.0 + step_s_)),
      maximum_event_count_(
          direct_mechanism_kinematics_plan(mechanism_plan_)->cylinders.size() + 1U),
      timing_curve_(std::move(timing_curve)),
      timing_curve_radius_rad_s_(timing_curve_radius_rad_s),
      throttle_controller_(std::move(throttle_controller)),
      idle_throttle_plate_position_01_(idle_throttle_plate_position_01),
      limiter_speed_rpm_(limiter_speed_rpm), limiter_hold_s_(limiter_hold_s),
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id),
      theta_cycle_rad_(initial_theta_cycle_rad),
      theta_unwrapped_rad_(initial_theta_cycle_rad),
      ignition_saved_angle_rad_(initial_theta_cycle_rad) {
    step_.cylinders.resize(
        direct_mechanism_kinematics_plan(mechanism_plan_)->cylinders.size());
    step_.events.reserve(maximum_event_count_);
}

contract::FailureContext LegacyLowOrderMechanicsSession::fault(
    contract::FailureKind kind, std::string detail_code, std::string state_summary,
    std::optional<contract::CylinderId> cylinder_id,
    std::optional<contract::RouteId> route_id) const {
    return {
        kind,
        std::move(detail_code),
        model_id_,
        profile_id_,
        step_.sample_index,
        step_.step_end_index,
        static_cast<double>(step_.step_end_index) *
            static_cast<double>(rate_.denominator) /
            static_cast<double>(rate_.numerator),
        theta_unwrapped_rad_,
        engine_id_,
        cylinder_id,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        route_id,
        "scenario=" + scenario_id_ + "; " + std::move(state_summary),
        "none; simulation terminated without fallback",
        {},
    };
}

LegacyMechanicsAdvanceResult LegacyLowOrderMechanicsSession::advance() {
    return advance_with_motion(std::nullopt, {});
}

LegacyMechanicsAdvanceResult
LegacyLowOrderMechanicsSession::advance(const LiveControlOverrides &overrides) {
    return advance_with_motion(std::nullopt, overrides);
}

LegacyMechanicsAdvanceResult
LegacyLowOrderMechanicsSession::advance(PostStepCrankMotion motion) {
    return advance_with_motion(motion, {});
}

LegacyMechanicsAdvanceResult
LegacyLowOrderMechanicsSession::advance(PostStepCrankMotion motion,
                                        const LiveControlOverrides &overrides) {
    return advance_with_motion(motion, overrides);
}

LegacyMechanicsAdvanceResult LegacyLowOrderMechanicsSession::advance_with_motion(
    std::optional<PostStepCrankMotion> motion, const LiveControlOverrides &overrides) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    const auto *direct_plan =
        direct_mechanism_kinematics_plan(mechanism_plan_);
    if (direct_plan == nullptr) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  "legacy-mechanics-mechanism-plan-unavailable",
                  "mechanics session has no retained direct mechanism plan");
        return *terminal_fault_;
    }
    if (control_cursor_.completed()) {
        if (kinematic_cursor_.has_value() && !kinematic_cursor_->completed()) {
            terminal_fault_ =
                fault(contract::FailureKind::contract_violation,
                      "legacy-mechanics-control-motion-horizon-mismatch",
                      "control horizon ended before the kinematic motion lane");
            return *terminal_fault_;
        }
        return LegacyMechanicsCompleted{produced_sample_count_};
    }
    if (!motion.has_value() && !kinematic_cursor_.has_value()) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  "legacy-mechanics-external-motion-required",
                  "dynamic mechanics requires one finite post-step crank-motion input");
        return *terminal_fault_;
    }
    if (overrides.has_throttle &&
        (!std::isfinite(overrides.throttle_01) || overrides.throttle_01 < 0.0 ||
         overrides.throttle_01 > 1.0)) {
        terminal_fault_ = fault(contract::FailureKind::contract_violation,
                                "legacy-mechanics-invalid-live-throttle",
                                "live throttle override must be finite and in [0, 1]");
        return *terminal_fault_;
    }
    if (overrides.has_external_resisting_torque_nm &&
        (!std::isfinite(overrides.external_resisting_torque_nm) ||
         overrides.external_resisting_torque_nm < 0.0)) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  "legacy-mechanics-invalid-live-external-resisting-torque",
                  "live external resisting torque override must be finite and "
                  "nonnegative");
        return *terminal_fault_;
    }
    if (motion.has_value()) {
        if (!finite_canonical_nonnegative(motion->engine_speed_rpm) ||
            !finite_canonical_nonnegative(motion->angular_displacement_rad) ||
            !(motion->angular_displacement_rad < 4.0 * kLegacyPi)) {
            terminal_fault_ =
                fault(contract::FailureKind::contract_violation,
                      "legacy-mechanics-invalid-post-step-motion",
                      "external post-step RPM and angular displacement must be finite "
                      "canonical nonnegative values; angular displacement must be "
                      "less than one engine cycle per physics step");
            return *terminal_fault_;
        }
    }

    const auto controls = control_cursor_.next();
    if (!controls.has_value()) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  control_cursor_.clock_overflowed()
                      ? "legacy-mechanics-frame-counter-overflow"
                      : "legacy-mechanics-control-horizon-mismatch",
                  control_cursor_.clock_overflowed()
                      ? "open-ended mechanics exhausted its uint64 physics clock"
                      : "control cursor lost an expected physics step");
        return *terminal_fault_;
    }
    std::optional<ScheduledScenarioStep> kinematic;
    if (kinematic_cursor_.has_value()) {
        kinematic = kinematic_cursor_->next();
        if (!kinematic.has_value() ||
            kinematic->sample_index != controls->sample_index ||
            kinematic->step_end_index != controls->step_end_index ||
            kinematic->requested_throttle != controls->requested_throttle ||
            kinematic->operating_state != controls->operating_state) {
            terminal_fault_ = fault(
                contract::FailureKind::contract_violation,
                "legacy-mechanics-control-motion-step-mismatch",
                "compiled control and kinematic motion cursors lost step alignment");
            return *terminal_fault_;
        }
    }

    step_.events.clear();
    step_.rate = rate_;
    step_.sample_index = controls->sample_index;
    step_.step_end_index = controls->step_end_index;
    step_.timestamp_tick = controls->step_end_index;
    step_.operating_state = controls->operating_state;
    if (overrides.has_ignition_enabled) {
        step_.operating_state.ignition_enabled = overrides.ignition_enabled;
    }
    if (overrides.has_fuel_enabled) {
        step_.operating_state.fuel_enabled = overrides.fuel_enabled;
    }
    if (overrides.has_starter_enabled) {
        step_.operating_state.starter_enabled = overrides.starter_enabled;
    }
    if (overrides.has_limiter_enabled) {
        step_.operating_state.limiter_enabled = overrides.limiter_enabled;
    }
    step_.requested_throttle_01 =
        overrides.has_throttle ? overrides.throttle_01 : controls->requested_throttle;
    step_.external_resisting_torque_nm = overrides.has_external_resisting_torque_nm
                                             ? overrides.external_resisting_torque_nm
                                             : 0.0;
    step_.engine_speed_rpm =
        motion.has_value() ? motion->engine_speed_rpm : kinematic->rpm;

    step_.omega_legacy_rad_s = -step_.engine_speed_rpm * kLegacyRpmScale;
    step_.angular_speed_rad_s = -step_.omega_legacy_rad_s;
    const double angular_displacement_rad = motion.has_value()
                                                ? motion->angular_displacement_rad
                                                : step_.angular_speed_rad_s * step_s_;
    step_.angular_acceleration_rad_s2 =
        (step_.angular_speed_rad_s - previous_angular_speed_rad_s_) / step_s_;
    if (angular_displacement_rad > 0.0) {
        body_angle_psi_rad_ =
            std::fmod(body_angle_psi_rad_ - angular_displacement_rad, 4.0 * kLegacyPi);
        theta_cycle_rad_ = legacy_positive_mod(
            -(body_angle_psi_rad_ - crank_tdc_reference_rad_), 4.0 * kLegacyPi);
        theta_unwrapped_rad_ += angular_displacement_rad;
    }
    previous_angular_speed_rad_s_ = step_.angular_speed_rad_s;
    step_.body_angle_psi_rad = body_angle_psi_rad_;
    step_.theta_cycle_rad = theta_cycle_rad_;
    step_.theta_unwrapped_rad = theta_unwrapped_rad_;

    LegacyDirectThrottleState throttle;
    if (const auto *direct =
            std::get_if<LegacyDirectThrottleControllerParameters>(
                &throttle_controller_)) {
        throttle = evaluate_legacy_direct_throttle(
            step_.requested_throttle_01, direct->gamma,
            idle_throttle_plate_position_01_);
    } else {
        const auto governed = evaluate_legacy_governor_throttle(
            governor_state_,
            std::get<LegacyGovernorControllerParameters>(throttle_controller_),
            step_.requested_throttle_01, step_.angular_speed_rad_s, step_s_,
            idle_throttle_plate_position_01_);
        governor_state_ = governed.controller;
        throttle = governed.throttle;
    }
    step_.resolved_engine_throttle_01 = throttle.resolved_engine_throttle_01;
    step_.intake_plate_position_01 = throttle.intake_plate_position_01;
    step_.main_flow_multiplier_01 = throttle.main_flow_multiplier_01;

    filtered_engine_speed_rpm_ = filter_alpha_ * filtered_engine_speed_rpm_ +
                                 (1.0 - filter_alpha_) * step_.engine_speed_rpm;
    step_.filtered_engine_speed_rpm = filtered_engine_speed_rpm_;
    step_.timing_advance_rad = legacy_triangle_sample(
        timing_curve_, -step_.omega_legacy_rad_s, timing_curve_radius_rad_s_);

    for (auto &cylinder : step_.cylinders) {
        cylinder.spark_crossed = false;
    }
    if (step_.operating_state.ignition_enabled && limiter_timer_s_ == 0.0) {
        const auto &cylinders = direct_plan->cylinders;
        for (std::size_t index = 0; index < cylinders.size(); ++index) {
            const auto &model = cylinders[index];
            const double spark_angle_rad = legacy_wrap_4pi(
                model.crank.ignition_wire_angle_rad - step_.timing_advance_rad);
            const auto crossing = evaluate_legacy_ignition_crossing(
                ignition_saved_angle_rad_, theta_cycle_rad_, spark_angle_rad,
                step_.omega_legacy_rad_s);
            if (!crossing.crossed) {
                continue;
            }
            if (step_.events.size() == maximum_event_count_) {
                terminal_fault_ =
                    fault(contract::FailureKind::event_schedule_violation,
                          "legacy-mechanics-event-capacity-exceeded",
                          "spark crossing exceeded the compiled per-step event "
                          "capacity",
                          model.crank.cylinder_id, model.exhaust_route_id);
                return *terminal_fault_;
            }
            step_.cylinders[index].spark_crossed = true;
            step_.events.push_back({
                static_cast<std::uint8_t>(step_.events.size()),
                contract::SparkCrossing{
                    model.crank.cylinder_id,
                    ignition_saved_angle_rad_,
                    theta_cycle_rad_,
                    crossing.adjusted_current_angle_rad,
                    crossing.adjusted_spark_angle_rad,
                    step_.timing_advance_rad,
                },
            });
        }
    }

    if (step_.operating_state.limiter_enabled) {
        const auto limiter =
            update_legacy_limiter(limiter_timer_s_, step_s_, step_.omega_legacy_rad_s,
                                  limiter_speed_rpm_, limiter_hold_s_);
        limiter_timer_s_ = limiter.timer_s;
        step_.limiter_timer_s = limiter_timer_s_;
        step_.limiter_cut_active = limiter.new_active;
        if (limiter.old_active != limiter.new_active) {
            if (step_.events.size() == maximum_event_count_) {
                terminal_fault_ =
                    fault(contract::FailureKind::event_schedule_violation,
                          "legacy-mechanics-event-capacity-exceeded",
                          "limiter transition exceeded the compiled per-step event "
                          "capacity");
                return *terminal_fault_;
            }
            step_.events.push_back({
                static_cast<std::uint8_t>(step_.events.size()),
                contract::LimiterStateChanged{
                    limiter.old_active,
                    limiter.new_active,
                    limiter.overspeed_refreshed,
                    limiter.timer_s,
                },
            });
        }
    } else {
        const bool old_active = limiter_timer_s_ != 0.0;
        limiter_timer_s_ = 0.0;
        step_.limiter_timer_s = 0.0;
        step_.limiter_cut_active = false;
        if (old_active) {
            if (step_.events.size() == maximum_event_count_) {
                terminal_fault_ =
                    fault(contract::FailureKind::event_schedule_violation,
                          "legacy-mechanics-event-capacity-exceeded",
                          "limiter transition exceeded the compiled per-step event "
                          "capacity");
                return *terminal_fault_;
            }
            step_.events.push_back({
                static_cast<std::uint8_t>(step_.events.size()),
                contract::LimiterStateChanged{
                    true,
                    false,
                    false,
                    0.0,
                },
            });
        }
    }
    ignition_saved_angle_rad_ = theta_cycle_rad_;

    const auto &cylinders = direct_plan->cylinders;
    for (std::size_t index = 0; index < cylinders.size(); ++index) {
        const auto &model = cylinders[index];
        const auto evaluated = evaluate_centered_slider_crank(
            model.crank, theta_cycle_rad_, step_.angular_speed_rad_s);
        if (!evaluated.valid) {
            terminal_fault_ =
                fault(contract::FailureKind::nonphysical_state,
                      "legacy-slider-crank-state-invalid",
                      "analytic centered slider-crank produced an invalid state",
                      model.crank.cylinder_id, model.exhaust_route_id);
            return *terminal_fault_;
        }
        auto &output = step_.cylinders[index];
        output.cylinder_id = model.crank.cylinder_id;
        output.exhaust_route_id = model.exhaust_route_id;
        output.geometric_tdc_rad = model.crank.geometric_tdc_rad;
        output.ignition_wire_angle_rad = model.crank.ignition_wire_angle_rad;
        output.phase_rad = evaluated.phase_rad;
        output.piston_travel_m = evaluated.piston_travel_m;
        output.chamber_volume_m3 = evaluated.chamber_volume_m3;
        output.dx_dtheta_m_per_rad = evaluated.dx_dtheta_m_per_rad;
        output.dvolume_dtheta_m3_per_rad = evaluated.dvolume_dtheta_m3_per_rad;
        output.piston_speed_abs_m_s = evaluated.piston_speed_abs_m_s;
    }

    if (!finite_step_scalars(step_)) {
        terminal_fault_ = fault(contract::FailureKind::numerical_failure,
                                "legacy-mechanics-nonfinite-step",
                                "mechanics produced a nonfinite scalar");
        return *terminal_fault_;
    }

    ++produced_sample_count_;
    return std::cref(step_);
}

bool LegacyLowOrderMechanicsSession::completed() const noexcept {
    return !terminal_fault_.has_value() && control_cursor_.completed() &&
           (!kinematic_cursor_.has_value() || kinematic_cursor_->completed());
}

} // namespace engine_sim_offline::simulation
