#include "session/exact_cycle_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace crankwave::session {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;
constexpr double kMaximumExactInteger = 9007199254740991.0;

[[nodiscard]] EngineCycleStateFlagMask
state_flags(const contract::EngineCaptureSample &sample) noexcept {
    EngineCycleStateFlagMask flags = 0;
    const auto set = [&flags](const bool enabled, const EngineCycleStateFlag flag) {
        if (enabled) {
            flags |= engine_cycle_state_flag_mask(flag);
        }
    };
    set(sample.ignition_enabled, EngineCycleStateFlag::ignition_enabled);
    set(sample.fuel_enabled, EngineCycleStateFlag::fuel_enabled);
    set(sample.starter_enabled, EngineCycleStateFlag::starter_enabled);
    set(sample.dyno_enabled, EngineCycleStateFlag::dyno_enabled);
    set(sample.limiter_enabled, EngineCycleStateFlag::limiter_enabled);
    set(sample.limiter_cut_active, EngineCycleStateFlag::limiter_cut_active);
    return flags;
}

[[nodiscard]] bool finite_sample(const contract::EngineCaptureSample &sample) noexcept {
    const auto &torque = sample.torque.instantaneous_net_shaft;
    return std::isfinite(sample.theta_rad) && std::isfinite(sample.engine_speed_rpm) &&
           std::isfinite(sample.requested_throttle_01) &&
           std::isfinite(sample.resolved_engine_throttle_01) &&
           std::isfinite(sample.intake_plate_position_01) &&
           std::isfinite(torque.value_nm);
}

[[nodiscard]] bool valid_rate(const contract::RationalRateHz &rate) noexcept {
    return rate.numerator != 0U && rate.denominator != 0U;
}

} // namespace

ExactCycleEvidenceAccumulator::ExactCycleEvidenceAccumulator(
    const double crank_tdc_reference_rad,
    const contract::RationalRateHz delivery_rate) noexcept
    : crank_tdc_reference_rad_(crank_tdc_reference_rad), delivery_rate_(delivery_rate),
      configuration_valid_(std::isfinite(crank_tdc_reference_rad) &&
                           valid_rate(delivery_rate)) {}

std::optional<ExactCycleEvidenceError>
ExactCycleEvidenceAccumulator::fail(const ExactCycleEvidenceErrorCode code,
                                    const std::uint64_t physics_frame) noexcept {
    terminal_error_ = ExactCycleEvidenceError{code, physics_frame};
    return terminal_error_;
}

double ExactCycleEvidenceAccumulator::boundary_theta(
    const std::int64_t ordinal) const noexcept {
    return crank_tdc_reference_rad_ +
           static_cast<double>(ordinal) * kFourStrokeCycleRadians;
}

double ExactCycleEvidenceAccumulator::delivery_frame(
    const double physical_tick) const noexcept {
    const long double numerator =
        static_cast<long double>(physical_tick) *
        static_cast<long double>(active_capture_rate_.denominator) *
        static_cast<long double>(delivery_rate_.numerator);
    const long double denominator =
        static_cast<long double>(active_capture_rate_.numerator) *
        static_cast<long double>(delivery_rate_.denominator);
    return static_cast<double>(numerator / denominator);
}

ExactCycleEvidenceAccumulator::Point
ExactCycleEvidenceAccumulator::interpolate_boundary(
    const Point &left, const Point &right, const std::int64_t ordinal,
    const double theta_rad) const noexcept {
    if (theta_rad == right.theta_rad) {
        auto boundary = right;
        boundary.boundary = {
            ordinal,
            left.physics_frame,
            right.physics_frame,
            1.0,
            theta_rad,
            right.time_s,
            delivery_frame(right.physical_tick),
        };
        return boundary;
    }

    const double fraction =
        (theta_rad - left.theta_rad) / (right.theta_rad - left.theta_rad);
    const double physical_tick =
        left.physical_tick + fraction * (right.physical_tick - left.physical_tick);
    const double time_s = physical_tick *
                          static_cast<double>(active_capture_rate_.denominator) /
                          static_cast<double>(active_capture_rate_.numerator);
    auto boundary = right;
    boundary.physics_frame = right.physics_frame;
    boundary.physical_tick = physical_tick;
    boundary.time_s = time_s;
    boundary.theta_rad = theta_rad;
    boundary.boundary = {
        ordinal,
        left.physics_frame,
        right.physics_frame,
        fraction,
        theta_rad,
        time_s,
        delivery_frame(physical_tick),
    };
    return boundary;
}

void ExactCycleEvidenceAccumulator::absorb_torque_metadata(
    const TorquePoint &torque) noexcept {
    if (!net_shaft_.has_metadata) {
        net_shaft_.availability = torque.availability;
        net_shaft_.completeness = torque.completeness;
        net_shaft_.unavailable_reason = torque.unavailable_reason;
        net_shaft_.included_terms = torque.included_terms;
        net_shaft_.omitted_terms = torque.omitted_terms;
        net_shaft_.has_metadata = true;
        return;
    }

    if (torque.availability == contract::Availability::unavailable) {
        net_shaft_.availability = contract::Availability::unavailable;
        if (net_shaft_.unavailable_reason ==
            contract::QuantityUnavailableReason::none) {
            net_shaft_.unavailable_reason = torque.unavailable_reason;
        }
    }
    if (torque.completeness == contract::Completeness::incomplete) {
        net_shaft_.completeness = contract::Completeness::incomplete;
    }
    net_shaft_.included_terms &= torque.included_terms;
    net_shaft_.omitted_terms |= torque.omitted_terms;
    net_shaft_.included_terms &= ~net_shaft_.omitted_terms;
}

void ExactCycleEvidenceAccumulator::begin_cycle(const Point &boundary) noexcept {
    accumulating_full_cycle_ = true;
    cycle_start_boundary_ = boundary.boundary;
    cycle_start_time_s_ = boundary.time_s;
    cycle_start_state_flags_ = 0;
    cycle_end_state_flags_ = 0;
    state_transition_flags_ = 0;
    has_interval_state_ = false;
    control_change_overflow_ = false;
    requested_throttle_ = {};
    resolved_throttle_ = {};
    intake_plate_position_ = {};
    net_shaft_ = {};
}

void ExactCycleEvidenceAccumulator::integrate_interval(const Point &left,
                                                       const Point &right) noexcept {
    const double duration_s = right.time_s - left.time_s;
    const double delta_theta_rad = right.theta_rad - left.theta_rad;

    const auto accumulate_control = [this, duration_s](ControlAccumulator &accumulator,
                                                       const double value) {
        accumulator.time_integral_s += value * duration_s;
        if (!accumulator.has_value) {
            accumulator.minimum_01 = value;
            accumulator.maximum_01 = value;
            accumulator.last_value_01 = value;
            accumulator.has_value = true;
            return;
        }
        accumulator.minimum_01 = std::min(accumulator.minimum_01, value);
        accumulator.maximum_01 = std::max(accumulator.maximum_01, value);
        if (value != accumulator.last_value_01) {
            if (accumulator.change_count == std::numeric_limits<std::uint32_t>::max()) {
                control_change_overflow_ = true;
            } else {
                ++accumulator.change_count;
            }
            accumulator.last_value_01 = value;
        }
    };
    accumulate_control(requested_throttle_, right.requested_throttle_01);
    accumulate_control(resolved_throttle_, right.resolved_throttle_01);
    accumulate_control(intake_plate_position_, right.intake_plate_position_01);

    if (!has_interval_state_) {
        cycle_start_state_flags_ = right.state_flags;
        cycle_end_state_flags_ = right.state_flags;
        has_interval_state_ = true;
    } else {
        state_transition_flags_ |= cycle_end_state_flags_ ^ right.state_flags;
        cycle_end_state_flags_ = right.state_flags;
    }

    absorb_torque_metadata(right.net_shaft);
    if (right.net_shaft.availability == contract::Availability::available) {
        net_shaft_.angular_work_j += right.net_shaft.value_nm * delta_theta_rad;
    }
}

EngineCompletedCycleEvidence
ExactCycleEvidenceAccumulator::finish_cycle(const Point &boundary) const noexcept {
    const double duration_s = boundary.time_s - cycle_start_time_s_;
    const auto control = [duration_s](const ControlAccumulator &accumulator) {
        if (!accumulator.has_value) {
            return EngineCycleControlEvidence{};
        }
        return EngineCycleControlEvidence{
            accumulator.time_integral_s / duration_s,
            accumulator.minimum_01,
            accumulator.maximum_01,
            accumulator.change_count,
        };
    };

    EngineCycleNetShaftEvidence torque;
    torque.availability = net_shaft_.has_metadata ? net_shaft_.availability
                                                  : contract::Availability::unavailable;
    torque.completeness = net_shaft_.has_metadata ? net_shaft_.completeness
                                                  : contract::Completeness::incomplete;
    torque.unavailable_reason =
        net_shaft_.has_metadata
            ? net_shaft_.unavailable_reason
            : contract::QuantityUnavailableReason::required_input_missing;
    torque.included_terms = net_shaft_.included_terms;
    torque.omitted_terms = net_shaft_.omitted_terms;
    if (torque.availability == contract::Availability::available) {
        torque.angular_work_j = net_shaft_.angular_work_j;
        torque.cycle_mean_torque_nm =
            net_shaft_.angular_work_j / kFourStrokeCycleRadians;
        torque.unavailable_reason = contract::QuantityUnavailableReason::none;
    } else if (torque.unavailable_reason == contract::QuantityUnavailableReason::none) {
        torque.unavailable_reason =
            contract::QuantityUnavailableReason::required_input_missing;
    }

    return {
        completed_cycle_count_,
        cycle_start_boundary_,
        boundary.boundary,
        duration_s,
        120.0 / duration_s,
        control(requested_throttle_),
        control(resolved_throttle_),
        control(intake_plate_position_),
        torque,
        cycle_start_state_flags_,
        cycle_end_state_flags_,
        state_transition_flags_,
    };
}

std::optional<ExactCycleEvidenceError> ExactCycleEvidenceAccumulator::advance(
    const Point &current,
    std::vector<EngineCompletedCycleEvidence> &completed) noexcept {
    if (!previous_.has_value()) {
        const double ordinal_value = std::floor(
            (current.theta_rad - crank_tdc_reference_rad_) / kFourStrokeCycleRadians);
        if (!std::isfinite(ordinal_value) || ordinal_value < -kMaximumExactInteger ||
            ordinal_value > kMaximumExactInteger - 1.0) {
            return fail(ExactCycleEvidenceErrorCode::ordinal_overflow,
                        current.physics_frame);
        }
        const auto preceding_ordinal = static_cast<std::int64_t>(ordinal_value);
        const double preceding_boundary = boundary_theta(preceding_ordinal);
        next_boundary_ordinal_ = preceding_ordinal + 1;
        next_boundary_theta_rad_ = boundary_theta(next_boundary_ordinal_);
        if (!std::isfinite(preceding_boundary) ||
            !std::isfinite(next_boundary_theta_rad_) ||
            !(next_boundary_theta_rad_ > current.theta_rad)) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                        current.physics_frame);
        }
        if (current.theta_rad == preceding_boundary) {
            auto boundary = current;
            boundary.boundary = {
                preceding_ordinal,
                current.physics_frame,
                current.physics_frame,
                0.0,
                current.theta_rad,
                current.time_s,
                delivery_frame(current.physical_tick),
            };
            begin_cycle(boundary);
        }
        previous_ = current;
        return std::nullopt;
    }

    const auto &previous = *previous_;
    if (current.physics_frame != previous.physics_frame + 1U) {
        return fail(ExactCycleEvidenceErrorCode::noncontiguous_sample,
                    current.physics_frame);
    }
    if (!(current.time_s > previous.time_s)) {
        return fail(ExactCycleEvidenceErrorCode::malformed_capture_clock,
                    current.physics_frame);
    }
    if (current.theta_rad < previous.theta_rad) {
        // Reversal is a valid operating transition (notably during shutdown or
        // vehicle backdrive), but a forward 720-degree interval cannot span it.
        // Discard the interrupted partial cycle and seed a new lattice search from
        // this post-step sample without making the session terminal.
        previous_.reset();
        accumulating_full_cycle_ = false;
        return advance(current, completed);
    }

    if (current.theta_rad >= next_boundary_theta_rad_) {
        if (next_boundary_ordinal_ == std::numeric_limits<std::int64_t>::max()) {
            return fail(ExactCycleEvidenceErrorCode::ordinal_overflow,
                        current.physics_frame);
        }
        const auto following_ordinal = next_boundary_ordinal_ + 1;
        const double following_boundary = boundary_theta(following_ordinal);
        if (!std::isfinite(following_boundary) ||
            !(following_boundary > next_boundary_theta_rad_)) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                        current.physics_frame);
        }
        if (current.theta_rad >= following_boundary) {
            return fail(ExactCycleEvidenceErrorCode::multiple_boundaries_in_interval,
                        current.physics_frame);
        }

        const auto boundary = interpolate_boundary(
            previous, current, next_boundary_ordinal_, next_boundary_theta_rad_);
        if (!std::isfinite(boundary.time_s) ||
            !std::isfinite(boundary.boundary.delivery_frame) ||
            boundary.time_s < previous.time_s || boundary.time_s > current.time_s) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                        current.physics_frame);
        }

        if (accumulating_full_cycle_) {
            integrate_interval(previous, boundary);
            if (control_change_overflow_) {
                return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                            current.physics_frame);
            }
            const auto cycle = finish_cycle(boundary);
            if (!std::isfinite(cycle.duration_s) || !(cycle.duration_s > 0.0) ||
                !std::isfinite(cycle.mean_engine_speed_rpm) ||
                !std::isfinite(cycle.requested_throttle.time_weighted_mean_01) ||
                !std::isfinite(cycle.resolved_engine_throttle.time_weighted_mean_01) ||
                !std::isfinite(cycle.intake_plate_position.time_weighted_mean_01) ||
                (cycle.instantaneous_net_shaft.availability ==
                     contract::Availability::available &&
                 (!std::isfinite(cycle.instantaneous_net_shaft.angular_work_j) ||
                  !std::isfinite(
                      cycle.instantaneous_net_shaft.cycle_mean_torque_nm)))) {
                return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                            current.physics_frame);
            }
            if (completed.size() >= completed.capacity()) {
                return fail(ExactCycleEvidenceErrorCode::output_capacity_exceeded,
                            current.physics_frame);
            }
            if (completed_cycle_count_ == std::numeric_limits<std::uint64_t>::max()) {
                return fail(ExactCycleEvidenceErrorCode::ordinal_overflow,
                            current.physics_frame);
            }
            completed.push_back(cycle);
            ++completed_cycle_count_;
        }

        begin_cycle(boundary);
        next_boundary_ordinal_ = following_ordinal;
        next_boundary_theta_rad_ = following_boundary;
        if (current.theta_rad > boundary.theta_rad) {
            integrate_interval(boundary, current);
            if (control_change_overflow_) {
                return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                            current.physics_frame);
            }
        }
    } else if (accumulating_full_cycle_) {
        integrate_interval(previous, current);
        if (control_change_overflow_) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_result,
                        current.physics_frame);
        }
    }

    previous_ = current;
    return std::nullopt;
}

std::optional<ExactCycleEvidenceError> ExactCycleEvidenceAccumulator::consume(
    const contract::CaptureClock &clock,
    const std::span<const contract::EngineCaptureSample> samples,
    std::vector<EngineCompletedCycleEvidence> &completed) noexcept {
    if (terminal_error_.has_value()) {
        return terminal_error_;
    }
    if (!configuration_valid_) {
        return fail(ExactCycleEvidenceErrorCode::invalid_configuration,
                    clock.first_sample_index);
    }
    if (!valid_rate(clock.rate) || clock.phase != contract::SamplePhase::post_step ||
        clock.first_sample_index == std::numeric_limits<std::uint64_t>::max() ||
        clock.first_timestamp_tick != clock.first_sample_index + 1U) {
        return fail(ExactCycleEvidenceErrorCode::malformed_capture_clock,
                    clock.first_sample_index);
    }
    if (active_capture_rate_.numerator == 0U) {
        active_capture_rate_ = clock.rate;
    } else if (clock.rate != active_capture_rate_) {
        return fail(ExactCycleEvidenceErrorCode::malformed_capture_clock,
                    clock.first_sample_index);
    }

    for (std::size_t offset = 0; offset < samples.size(); ++offset) {
        if (offset >
            std::numeric_limits<std::uint64_t>::max() - clock.first_sample_index) {
            return fail(ExactCycleEvidenceErrorCode::malformed_capture_clock,
                        clock.first_sample_index);
        }
        const auto physics_frame =
            clock.first_sample_index + static_cast<std::uint64_t>(offset);
        if (physics_frame == std::numeric_limits<std::uint64_t>::max()) {
            return fail(ExactCycleEvidenceErrorCode::malformed_capture_clock,
                        physics_frame);
        }
        const auto &sample = samples[offset];
        if (sample.step_end_index != physics_frame + 1U) {
            return fail(ExactCycleEvidenceErrorCode::noncontiguous_sample,
                        physics_frame);
        }
        if (!finite_sample(sample)) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_sample, physics_frame);
        }
        const double physical_tick = static_cast<double>(physics_frame + 1U);
        const double time_s = physical_tick *
                              static_cast<double>(clock.rate.denominator) /
                              static_cast<double>(clock.rate.numerator);
        if (!std::isfinite(time_s)) {
            return fail(ExactCycleEvidenceErrorCode::nonfinite_result, physics_frame);
        }
        const auto &source_torque = sample.torque.instantaneous_net_shaft;
        const Point current{
            physics_frame,
            physical_tick,
            time_s,
            sample.theta_rad,
            sample.requested_throttle_01,
            sample.resolved_engine_throttle_01,
            sample.intake_plate_position_01,
            {
                source_torque.value_nm,
                source_torque.availability,
                source_torque.completeness,
                source_torque.unavailable_reason,
                source_torque.included_terms,
                source_torque.omitted_terms,
            },
            state_flags(sample),
            {},
        };
        if (auto error = advance(current, completed); error.has_value()) {
            return error;
        }
    }
    return std::nullopt;
}

const char *
exact_cycle_evidence_error_message(const ExactCycleEvidenceErrorCode code) noexcept {
    switch (code) {
    case ExactCycleEvidenceErrorCode::invalid_configuration:
        return "exact cycle evidence has an invalid crank reference or delivery rate";
    case ExactCycleEvidenceErrorCode::malformed_capture_clock:
        return "post-step capture clock is malformed or changed rate";
    case ExactCycleEvidenceErrorCode::noncontiguous_sample:
        return "post-step engine capture is not contiguous";
    case ExactCycleEvidenceErrorCode::nonfinite_sample:
        return "post-step engine capture contains a nonfinite cycle input";
    case ExactCycleEvidenceErrorCode::multiple_boundaries_in_interval:
        return "one physics interval crossed multiple 720-degree boundaries";
    case ExactCycleEvidenceErrorCode::ordinal_overflow:
        return "exact cycle boundary ordinal exceeded its checked domain";
    case ExactCycleEvidenceErrorCode::output_capacity_exceeded:
        return "completed cycle evidence exceeded its preallocated block capacity";
    case ExactCycleEvidenceErrorCode::nonfinite_result:
        return "exact cycle reduction produced a nonfinite result";
    }
    return "exact cycle evidence failed for an unknown reason";
}

} // namespace crankwave::session
