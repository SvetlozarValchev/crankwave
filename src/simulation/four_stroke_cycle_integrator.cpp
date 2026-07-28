#include "simulation/four_stroke_cycle_integrator.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

[[nodiscard]] bool finite_sample(const CycleTorqueSample &sample) noexcept {
    return std::isfinite(sample.time_s) && std::isfinite(sample.theta_unwrapped_rad) &&
           std::isfinite(sample.indicated_gas_torque_nm) &&
           std::isfinite(sample.friction_pump_and_accessory_torque_nm) &&
           std::isfinite(sample.starter_torque_nm);
}

[[nodiscard]] double stable_summed_torque(double indicated_gas_torque_nm,
                                          double friction_torque_nm,
                                          double starter_torque_nm) noexcept {
    return (indicated_gas_torque_nm + friction_torque_nm) + starter_torque_nm;
}

[[nodiscard]] double trapezoid(double left, double right,
                               double delta_theta_rad) noexcept {
    return ((left + right) * delta_theta_rad) * 0.5;
}

template <class Work> [[nodiscard]] bool finite_work(const Work &work) noexcept {
    return std::isfinite(work.indicated_gas_work_j) &&
           std::isfinite(work.friction_pump_and_accessory_work_j) &&
           std::isfinite(work.starter_work_j) &&
           std::isfinite(work.summed_torque_work_j);
}

} // namespace

double
interpolate_cycle_boundary_scalar(double left_value, double right_value,
                                  const CycleBoundaryEvidence &boundary) noexcept {
    if (boundary.left_bracket_sample_index == boundary.right_bracket_sample_index) {
        return right_value;
    }
    return left_value + boundary.fraction_from_left_01 * (right_value - left_value);
}

FourStrokeCycleIntegrator::FourStrokeCycleIntegrator(
    double cycle_reference_theta_rad, double total_displacement_m3) noexcept
    : cycle_reference_theta_rad_(cycle_reference_theta_rad),
      total_displacement_m3_(total_displacement_m3) {}

FourStrokeCycleIntegrator::FourStrokeCycleIntegrator(
    FourStrokeCycleIntegrator &&other) noexcept
    : cycle_reference_theta_rad_(other.cycle_reference_theta_rad_),
      total_displacement_m3_(other.total_displacement_m3_),
      next_boundary_theta_rad_(other.next_boundary_theta_rad_),
      next_boundary_cycle_index_(other.next_boundary_cycle_index_),
      cycle_start_theta_rad_(other.cycle_start_theta_rad_),
      cycle_start_time_s_(other.cycle_start_time_s_),
      cycle_start_boundary_(other.cycle_start_boundary_),
      completed_cycle_count_(other.completed_cycle_count_),
      accumulating_full_cycle_(other.accumulating_full_cycle_),
      previous_(other.previous_), work_(other.work_),
      terminal_error_(other.terminal_error_) {
    other.invalidate_after_move();
}

FourStrokeCycleIntegrator &
FourStrokeCycleIntegrator::operator=(FourStrokeCycleIntegrator &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    cycle_reference_theta_rad_ = other.cycle_reference_theta_rad_;
    total_displacement_m3_ = other.total_displacement_m3_;
    next_boundary_theta_rad_ = other.next_boundary_theta_rad_;
    next_boundary_cycle_index_ = other.next_boundary_cycle_index_;
    cycle_start_theta_rad_ = other.cycle_start_theta_rad_;
    cycle_start_time_s_ = other.cycle_start_time_s_;
    cycle_start_boundary_ = other.cycle_start_boundary_;
    completed_cycle_count_ = other.completed_cycle_count_;
    accumulating_full_cycle_ = other.accumulating_full_cycle_;
    previous_ = other.previous_;
    work_ = other.work_;
    terminal_error_ = other.terminal_error_;
    other.invalidate_after_move();
    return *this;
}

void FourStrokeCycleIntegrator::invalidate_after_move() noexcept {
    const std::uint64_t sample_index =
        previous_.has_value() ? previous_->sample_index : 0;
    previous_.reset();
    work_ = {};
    accumulating_full_cycle_ = false;
    completed_cycle_count_ = 0;
    terminal_error_ = FourStrokeCycleIntegrationError{
        FourStrokeCycleIntegrationErrorCode::moved_from,
        sample_index,
    };
}

FourStrokeCycleAdvanceResult
FourStrokeCycleIntegrator::fail(FourStrokeCycleIntegrationErrorCode code,
                                std::uint64_t sample_index) noexcept {
    terminal_error_ = FourStrokeCycleIntegrationError{code, sample_index};
    return *terminal_error_;
}

FourStrokeCycleIntegrator::TorquePoint FourStrokeCycleIntegrator::interpolate_boundary(
    const TorquePoint &left, const TorquePoint &right,
    double boundary_theta_rad) const noexcept {
    const bool exact_right_sample = boundary_theta_rad == right.theta_rad;
    const auto evidence =
        exact_right_sample
            ? CycleBoundaryEvidence{right.sample_index, right.sample_index, 0.0}
            : CycleBoundaryEvidence{
                  left.sample_index,
                  right.sample_index,
                  (boundary_theta_rad - left.theta_rad) /
                      (right.theta_rad - left.theta_rad),
              };
    const double indicated = interpolate_cycle_boundary_scalar(
        left.indicated_gas_torque_nm, right.indicated_gas_torque_nm, evidence);
    const double friction = interpolate_cycle_boundary_scalar(
        left.friction_pump_and_accessory_torque_nm,
        right.friction_pump_and_accessory_torque_nm, evidence);
    const double starter = interpolate_cycle_boundary_scalar(
        left.starter_torque_nm, right.starter_torque_nm, evidence);
    return {
        right.sample_index,
        interpolate_cycle_boundary_scalar(left.time_s, right.time_s, evidence),
        boundary_theta_rad,
        indicated,
        friction,
        starter,
        stable_summed_torque(indicated, friction, starter),
        evidence,
    };
}

void FourStrokeCycleIntegrator::integrate_segment(const TorquePoint &left,
                                                  const TorquePoint &right) noexcept {
    const double delta_theta = right.theta_rad - left.theta_rad;
    work_.indicated_gas_work_j += trapezoid(left.indicated_gas_torque_nm,
                                            right.indicated_gas_torque_nm, delta_theta);
    work_.friction_pump_and_accessory_work_j +=
        trapezoid(left.friction_pump_and_accessory_torque_nm,
                  right.friction_pump_and_accessory_torque_nm, delta_theta);
    work_.starter_work_j +=
        trapezoid(left.starter_torque_nm, right.starter_torque_nm, delta_theta);
    work_.summed_torque_work_j +=
        trapezoid(left.summed_torque_nm, right.summed_torque_nm, delta_theta);
}

void FourStrokeCycleIntegrator::begin_cycle(const TorquePoint &boundary) noexcept {
    accumulating_full_cycle_ = true;
    cycle_start_theta_rad_ = boundary.theta_rad;
    cycle_start_time_s_ = boundary.time_s;
    cycle_start_boundary_ = boundary.boundary_evidence;
    work_ = {};
}

CompletedFourStrokeCycle
FourStrokeCycleIntegrator::finish_cycle(const TorquePoint &boundary) noexcept {
    const double duration_s = boundary.time_s - cycle_start_time_s_;
    const auto result = CompletedFourStrokeCycle{
        completed_cycle_count_,
        cycle_start_boundary_,
        boundary.boundary_evidence,
        cycle_start_theta_rad_,
        boundary.theta_rad,
        cycle_start_time_s_,
        boundary.time_s,
        work_.indicated_gas_work_j,
        work_.friction_pump_and_accessory_work_j,
        work_.starter_work_j,
        work_.summed_torque_work_j,
        work_.summed_torque_work_j / kFourStrokeCycleRadians,
        work_.summed_torque_work_j / total_displacement_m3_,
        work_.summed_torque_work_j / duration_s,
    };
    return result;
}

double
FourStrokeCycleIntegrator::boundary_theta(std::int64_t cycle_index) const noexcept {
    return cycle_reference_theta_rad_ +
           static_cast<double>(cycle_index) * kFourStrokeCycleRadians;
}

FourStrokeCycleAdvanceResult
FourStrokeCycleIntegrator::advance(const CycleTorqueSample &sample) noexcept {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (!finite_sample(sample)) {
        return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_sample,
                    sample.sample_index);
    }

    const double summed_torque = stable_summed_torque(
        sample.indicated_gas_torque_nm, sample.friction_pump_and_accessory_torque_nm,
        sample.starter_torque_nm);
    if (!std::isfinite(summed_torque)) {
        return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_sample,
                    sample.sample_index);
    }
    const TorquePoint current{
        sample.sample_index,
        sample.time_s,
        sample.theta_unwrapped_rad,
        sample.indicated_gas_torque_nm,
        sample.friction_pump_and_accessory_torque_nm,
        sample.starter_torque_nm,
        summed_torque,
        {
            sample.sample_index,
            sample.sample_index,
            0.0,
        },
    };

    if (!previous_.has_value()) {
        const double cycle_index_value = std::floor(
            (current.theta_rad - cycle_reference_theta_rad_) / kFourStrokeCycleRadians);
        // Binary64 represents every integer exactly only through 2^53. Keeping the
        // boundary ordinal in that domain also makes the checked conversion exact.
        constexpr double kMaximumExactInteger = 9007199254740991.0;
        constexpr double kMinimumCycleIndex = -kMaximumExactInteger;
        constexpr double kMaximumPrecedingCycleIndex = kMaximumExactInteger - 1.0;
        if (!std::isfinite(cycle_index_value) ||
            cycle_index_value < kMinimumCycleIndex ||
            cycle_index_value > kMaximumPrecedingCycleIndex) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        const auto preceding_cycle_index = static_cast<std::int64_t>(cycle_index_value);
        const double preceding_boundary = boundary_theta(preceding_cycle_index);
        if (!std::isfinite(preceding_boundary)) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        next_boundary_cycle_index_ = preceding_cycle_index + 1;
        next_boundary_theta_rad_ = boundary_theta(next_boundary_cycle_index_);
        if (current.theta_rad == preceding_boundary) {
            begin_cycle(current);
        } else {
            accumulating_full_cycle_ = false;
        }
        if (!std::isfinite(next_boundary_theta_rad_) ||
            next_boundary_theta_rad_ <= current.theta_rad) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        previous_ = current;
        return NoFourStrokeCycleBoundaryCrossing{};
    }

    const auto &previous = *previous_;
    if (current.sample_index <= previous.sample_index ||
        !(current.time_s > previous.time_s) ||
        !(current.theta_rad > previous.theta_rad)) {
        return fail(FourStrokeCycleIntegrationErrorCode::nonmonotonic_sample,
                    sample.sample_index);
    }
    const double delta_theta = current.theta_rad - previous.theta_rad;
    if (!std::isfinite(delta_theta)) {
        return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                    sample.sample_index);
    }

    std::optional<CompletedFourStrokeCycle> completed;
    std::optional<FourStrokeCycleBoundaryCrossing> crossing;
    if (current.theta_rad >= next_boundary_theta_rad_) {
        if (next_boundary_cycle_index_ == std::numeric_limits<std::int64_t>::max()) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        const auto following_boundary_cycle_index = next_boundary_cycle_index_ + 1;
        const double following_boundary_theta =
            boundary_theta(following_boundary_cycle_index);
        if (!std::isfinite(following_boundary_theta) ||
            following_boundary_theta <= next_boundary_theta_rad_) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }
        if (current.theta_rad >= following_boundary_theta) {
            return fail(
                FourStrokeCycleIntegrationErrorCode::multiple_boundaries_in_segment,
                sample.sample_index);
        }

        const auto boundary =
            interpolate_boundary(previous, current, next_boundary_theta_rad_);
        if (!std::isfinite(boundary.time_s) ||
            !std::isfinite(boundary.indicated_gas_torque_nm) ||
            !std::isfinite(boundary.friction_pump_and_accessory_torque_nm) ||
            !std::isfinite(boundary.starter_torque_nm) ||
            !std::isfinite(boundary.summed_torque_nm)) {
            return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                        sample.sample_index);
        }

        if (accumulating_full_cycle_) {
            integrate_segment(previous, boundary);
            const double cycle_duration_s = boundary.time_s - cycle_start_time_s_;
            if (!finite_work(work_) || !std::isfinite(cycle_duration_s) ||
                !(cycle_duration_s > 0.0)) {
                return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                            sample.sample_index);
            }
            if (completed_cycle_count_ == std::numeric_limits<std::uint64_t>::max()) {
                return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                            sample.sample_index);
            }
            completed = finish_cycle(boundary);
            if (!std::isfinite(completed->cycle_mean_summed_torque_nm) ||
                !std::isfinite(completed->summed_torque_mean_effective_pressure_pa) ||
                !std::isfinite(completed->cycle_mean_summed_power_w)) {
                return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                            sample.sample_index);
            }
        }

        crossing = FourStrokeCycleBoundaryCrossing{
            boundary.boundary_evidence,
            boundary.theta_rad,
            boundary.time_s,
            completed,
        };
        begin_cycle(boundary);
        next_boundary_cycle_index_ = following_boundary_cycle_index;
        next_boundary_theta_rad_ = following_boundary_theta;
        if (current.theta_rad > boundary.theta_rad) {
            integrate_segment(boundary, current);
        }
    } else if (accumulating_full_cycle_) {
        integrate_segment(previous, current);
    }

    if (!finite_work(work_)) {
        return fail(FourStrokeCycleIntegrationErrorCode::nonfinite_result,
                    sample.sample_index);
    }
    previous_ = current;
    if (crossing.has_value()) {
        if (crossing->completed_cycle.has_value()) {
            ++completed_cycle_count_;
        }
        return *crossing;
    }
    return NoFourStrokeCycleBoundaryCrossing{};
}

bool FourStrokeCycleIntegrator::faulted() const noexcept {
    return terminal_error_.has_value();
}

std::uint64_t FourStrokeCycleIntegrator::completed_cycle_count() const noexcept {
    return completed_cycle_count_;
}

FourStrokeCycleIntegratorCompileResult compile_four_stroke_cycle_integrator(
    const FourStrokeCycleIntegrationPlan &plan) noexcept {
    if (!std::isfinite(plan.cycle_reference_theta_rad) ||
        !std::isfinite(plan.total_displacement_m3) ||
        !(plan.total_displacement_m3 > 0.0)) {
        return FourStrokeCycleIntegrationError{
            FourStrokeCycleIntegrationErrorCode::invalid_plan,
            0,
        };
    }
    return FourStrokeCycleIntegrator{
        plan.cycle_reference_theta_rad,
        plan.total_displacement_m3,
    };
}

} // namespace engine_sim_offline::simulation
