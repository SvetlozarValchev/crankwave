#include "session/exact_cycle_evidence.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::session;

constexpr double kCycleRadians = 4.0 * std::numbers::pi_v<double>;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(const double actual, const double expected, const double tolerance,
                 const std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] contract::EngineCaptureSample
sample(const std::uint64_t step_end, const double theta_rad,
       const double requested_throttle, const double resolved_throttle,
       const double intake_plate, const double torque_nm,
       const EngineCycleStateFlagMask states = 0U,
       const contract::Availability availability = contract::Availability::available) {
    contract::EngineCaptureSample result;
    result.step_end_index = step_end;
    result.theta_rad = theta_rad;
    result.engine_speed_rpm = 600.0;
    result.requested_throttle_01 = requested_throttle;
    result.resolved_engine_throttle_01 = resolved_throttle;
    result.intake_plate_position_01 = intake_plate;
    result.ignition_enabled =
        (states &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled)) != 0U;
    result.fuel_enabled =
        (states & engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled)) !=
        0U;
    result.starter_enabled =
        (states &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::starter_enabled)) != 0U;
    result.dyno_enabled =
        (states & engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled)) !=
        0U;
    result.limiter_enabled =
        (states &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_enabled)) != 0U;
    result.limiter_cut_active =
        (states &
         engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active)) != 0U;
    const auto included =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas);
    const auto omitted = contract::torque_term_mask(contract::TorqueTerm::starter);
    result.torque.instantaneous_net_shaft = {
        availability == contract::Availability::available ? torque_nm : 0.0,
        availability,
        contract::Completeness::incomplete,
        availability == contract::Availability::available
            ? contract::QuantityUnavailableReason::none
            : contract::QuantityUnavailableReason::model_not_admitted,
        availability == contract::Availability::available ? included : 0U,
        availability == contract::Availability::available ? omitted : 0U,
    };
    return result;
}

[[nodiscard]] contract::CaptureClock clock(const std::uint64_t first_frame,
                                           const std::uint64_t rate_hz = 10U) {
    return {
        {rate_hz, 1U},
        first_frame,
        first_frame + 1U,
        contract::SamplePhase::post_step,
    };
}

void test_shifted_lattice_cross_block_zoh_evidence() {
    constexpr double reference = 0.3;
    ExactCycleEvidenceAccumulator accumulator(reference, {100U, 1U});
    std::vector<EngineCompletedCycleEvidence> completed;
    completed.reserve(8U);

    constexpr auto ignition =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled);
    constexpr auto fuel =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled);
    constexpr auto limiter_cut =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
    const std::array first_block{
        sample(1U, reference + 1.0, 0.0, 0.0, 0.0, 5.0),
        sample(2U, reference + kCycleRadians - 1.0, 0.1, 0.0, 0.0, 5.0),
        sample(3U, reference + kCycleRadians + 1.0, 0.2, 0.1, 0.0, 10.0, ignition),
    };
    const auto first_error = accumulator.consume(clock(0U), first_block, completed);
    expect(!first_error.has_value(), "first partial block was rejected");
    expect(completed.empty(), "initial partial cycle was published");

    const std::array second_block{
        sample(4U, reference + 2.0 * kCycleRadians - 1.0, 0.4, 0.3, 0.2, 20.0,
               ignition | fuel),
        sample(5U, reference + 2.0 * kCycleRadians + 1.0, 0.6, 0.5, 0.4, 30.0,
               ignition | fuel | limiter_cut),
    };
    const auto second_error = accumulator.consume(clock(3U), second_block, completed);
    expect(!second_error.has_value(), "cross-block full cycle was rejected");
    expect(completed.size() == 1U,
           "cross-block cycle was lost, duplicated, or partial was emitted");

    const auto &cycle = completed.front();
    expect(cycle.completed_cycle_ordinal == 0U &&
               cycle.start_boundary.cycle_ordinal == 1 &&
               cycle.end_boundary.cycle_ordinal == 2 &&
               cycle.start_boundary.left_physics_frame == 1U &&
               cycle.start_boundary.right_physics_frame == 2U &&
               cycle.end_boundary.left_physics_frame == 3U &&
               cycle.end_boundary.right_physics_frame == 4U,
           "shifted cycle lattice or physical brackets changed");
    expect_near(cycle.start_boundary.fraction_from_left_01, 0.5, 1e-14,
                "start-boundary fraction changed");
    expect_near(cycle.end_boundary.fraction_from_left_01, 0.5, 1e-14,
                "end-boundary fraction changed");
    expect_near(cycle.start_boundary.time_s, 0.25, 1e-14,
                "post-step start time changed");
    expect_near(cycle.end_boundary.time_s, 0.45, 1e-14, "post-step end time changed");
    expect_near(cycle.start_boundary.delivery_frame, 25.0, 1e-13,
                "start boundary did not project to delivery frames");
    expect_near(cycle.end_boundary.delivery_frame, 45.0, 1e-13,
                "end boundary did not project to delivery frames");
    expect_near(cycle.duration_s, 0.2, 1e-14, "cycle duration changed");
    expect_near(cycle.mean_engine_speed_rpm, 600.0, 1e-11,
                "duration-derived complete-cycle RPM changed");

    expect_near(cycle.requested_throttle.time_weighted_mean_01, 0.4, 1e-14,
                "requested-throttle ZOH mean changed");
    expect(cycle.requested_throttle.minimum_01 == 0.2 &&
               cycle.requested_throttle.maximum_01 == 0.6 &&
               cycle.requested_throttle.change_count == 2U,
           "requested-throttle range or changes changed");
    expect_near(cycle.resolved_engine_throttle.time_weighted_mean_01, 0.3, 1e-14,
                "resolved-throttle ZOH mean changed");
    expect_near(cycle.intake_plate_position.time_weighted_mean_01, 0.2, 1e-14,
                "intake-plate ZOH mean changed");

    expect(cycle.instantaneous_net_shaft.availability ==
                   contract::Availability::available &&
               cycle.instantaneous_net_shaft.completeness ==
                   contract::Completeness::incomplete &&
               cycle.instantaneous_net_shaft.included_terms ==
                   contract::torque_term_mask(contract::TorqueTerm::indicated_gas) &&
               cycle.instantaneous_net_shaft.omitted_terms ==
                   contract::torque_term_mask(contract::TorqueTerm::starter),
           "net-shaft availability, completeness, or term masks changed");
    expect_near(cycle.instantaneous_net_shaft.angular_work_j, 20.0 * kCycleRadians,
                1e-11, "interval-owned rectangular torque work changed");
    expect_near(cycle.instantaneous_net_shaft.cycle_mean_torque_nm, 20.0, 1e-12,
                "cycle-mean net-shaft torque changed");
    expect(cycle.start_state_flags == ignition &&
               cycle.end_state_flags == (ignition | fuel | limiter_cut) &&
               cycle.state_transition_flags == (fuel | limiter_cut),
           "half-open state ownership or transition mask changed");
}

void test_exact_right_crossing_retains_interval_bracket() {
    ExactCycleEvidenceAccumulator accumulator(0.0, {192000U, 1U});
    std::vector<EngineCompletedCycleEvidence> completed;
    completed.reserve(2U);
    const std::array samples{
        sample(1U, 0.0, 0.25, 0.25, 0.25, 12.0),
        sample(2U, kCycleRadians, 0.25, 0.25, 0.25, 12.0),
    };
    const auto error = accumulator.consume(clock(0U, 20000U), samples, completed);
    expect(!error.has_value() && completed.size() == 1U,
           "exact-right full cycle did not complete");
    const auto &boundary = completed.front().end_boundary;
    expect(boundary.left_physics_frame == 0U && boundary.right_physics_frame == 1U &&
               boundary.fraction_from_left_01 == 1.0 && boundary.delivery_frame == 19.2,
           "exact-right crossing lost its original interval bracket");
}

void test_stopped_and_reversed_crank_reset_without_fault() {
    ExactCycleEvidenceAccumulator accumulator(0.0, {100U, 1U});
    std::vector<EngineCompletedCycleEvidence> completed;
    completed.reserve(8U);
    const std::array samples{
        sample(1U, 0.0, 0.0, 0.0, 0.0, 0.0), sample(2U, 2.0, 0.0, 0.0, 0.0, 0.0),
        sample(3U, 2.0, 0.0, 0.0, 0.0, 0.0), sample(4U, 1.0, 0.0, 0.0, 0.0, 0.0),
        sample(5U, 3.0, 0.0, 0.0, 0.0, 0.0),
    };
    const auto error = accumulator.consume(clock(0U), samples, completed);
    expect(!error.has_value(),
           "stopped or reversed crank made observational evidence terminal");
    expect(completed.empty(), "cycle interrupted by reversal was published");
}

void test_noncontiguous_capture_is_rejected() {
    ExactCycleEvidenceAccumulator accumulator(0.0, {100U, 1U});
    std::vector<EngineCompletedCycleEvidence> completed;
    completed.reserve(2U);
    const std::array first{sample(1U, 0.0, 0.0, 0.0, 0.0, 0.0)};
    expect(!accumulator.consume(clock(0U), first, completed).has_value(),
           "valid seed sample was rejected");
    const std::array gap{sample(3U, 1.0, 0.0, 0.0, 0.0, 0.0)};
    const auto error = accumulator.consume(clock(2U), gap, completed);
    expect(error.has_value() &&
               error->code == ExactCycleEvidenceErrorCode::noncontiguous_sample,
           "noncontiguous post-step capture was accepted");
}

} // namespace

int main() {
    try {
        test_shifted_lattice_cross_block_zoh_evidence();
        test_exact_right_crossing_retains_interval_bracket();
        test_stopped_and_reversed_crank_reset_without_fault();
        test_noncontiguous_capture_is_rejected();
    } catch (const std::exception &error) {
        std::cerr << "Exact cycle evidence test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
