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
           std::isfinite(step.limiter_timer_s);
}

} // namespace

LegacyLowOrderMechanicsSession::LegacyLowOrderMechanicsSession(
    KinematicScenarioCursor scenario_cursor, contract::RationalRateHz rate,
    double crank_tdc_reference_rad, double initial_theta_cycle_rad,
    std::vector<CylinderModel> cylinders, std::vector<LegacyTrianglePoint> timing_curve,
    double timing_curve_radius_rad_s, double limiter_speed_rpm, double limiter_hold_s,
    std::string model_id, std::string profile_id, std::string scenario_id,
    contract::EngineId engine_id)
    : scenario_cursor_(std::move(scenario_cursor)), rate_(rate),
      crank_tdc_reference_rad_(crank_tdc_reference_rad), step_s_(1.0 / 10000.0),
      filter_alpha_(step_s_ / (100.0 + step_s_)), cylinders_(std::move(cylinders)),
      maximum_event_count_(cylinders_.size() + 1U),
      timing_curve_(std::move(timing_curve)),
      timing_curve_radius_rad_s_(timing_curve_radius_rad_s),
      limiter_speed_rpm_(limiter_speed_rpm), limiter_hold_s_(limiter_hold_s),
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id),
      theta_cycle_rad_(initial_theta_cycle_rad),
      theta_unwrapped_rad_(initial_theta_cycle_rad),
      ignition_saved_angle_rad_(initial_theta_cycle_rad) {
    cylinder_model_view_.reserve(cylinders_.size());
    for (const auto &cylinder : cylinders_) {
        cylinder_model_view_.push_back(cylinder.crank);
    }
    step_.cylinders.resize(cylinders_.size());
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
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    const auto scheduled = scenario_cursor_.next();
    if (!scheduled.has_value()) {
        return LegacyMechanicsCompleted{produced_sample_count_};
    }

    step_.events.clear();
    step_.rate = rate_;
    step_.sample_index = scheduled->sample_index;
    step_.step_end_index = scheduled->step_end_index;
    step_.timestamp_tick = scheduled->step_end_index;
    step_.operating_state = scheduled->operating_state;
    step_.requested_throttle_01 = scheduled->requested_throttle;
    step_.engine_speed_rpm = scheduled->rpm;

    step_.omega_legacy_rad_s = -step_.engine_speed_rpm * kLegacyRpmScale;
    step_.angular_speed_rad_s = -step_.omega_legacy_rad_s;
    step_.angular_acceleration_rad_s2 =
        (step_.angular_speed_rad_s - previous_angular_speed_rad_s_) / step_s_;
    body_angle_psi_rad_ = std::fmod(
        body_angle_psi_rad_ + step_.omega_legacy_rad_s * step_s_, 4.0 * kLegacyPi);
    theta_cycle_rad_ = legacy_positive_mod(
        -(body_angle_psi_rad_ - crank_tdc_reference_rad_), 4.0 * kLegacyPi);
    theta_unwrapped_rad_ += step_.angular_speed_rad_s * step_s_;
    previous_angular_speed_rad_s_ = step_.angular_speed_rad_s;
    step_.body_angle_psi_rad = body_angle_psi_rad_;
    step_.theta_cycle_rad = theta_cycle_rad_;
    step_.theta_unwrapped_rad = theta_unwrapped_rad_;

    step_.resolved_engine_throttle_01 =
        1.0 - std::pow(step_.requested_throttle_01, 2.0);
    step_.intake_plate_position_01 = 0.994 * step_.resolved_engine_throttle_01;
    step_.main_flow_multiplier_01 =
        std::cos(kLegacyPi * step_.intake_plate_position_01 / 2.0);

    filtered_engine_speed_rpm_ = filter_alpha_ * filtered_engine_speed_rpm_ +
                                 (1.0 - filter_alpha_) * step_.engine_speed_rpm;
    step_.filtered_engine_speed_rpm = filtered_engine_speed_rpm_;
    step_.timing_advance_rad = legacy_triangle_sample(
        timing_curve_, -step_.omega_legacy_rad_s, timing_curve_radius_rad_s_);

    for (auto &cylinder : step_.cylinders) {
        cylinder.spark_crossed = false;
    }
    if (step_.operating_state.ignition_enabled && limiter_timer_s_ == 0.0) {
        for (std::size_t index = 0; index < cylinders_.size(); ++index) {
            const auto &model = cylinders_[index];
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
    ignition_saved_angle_rad_ = theta_cycle_rad_;

    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        const auto &model = cylinders_[index];
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
    return !terminal_fault_.has_value() && scenario_cursor_.completed();
}

std::span<const CenteredSliderCrankCylinder>
LegacyLowOrderMechanicsSession::cylinder_models() const noexcept {
    return cylinder_model_view_;
}

} // namespace engine_sim_offline::simulation
