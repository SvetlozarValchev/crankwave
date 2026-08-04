#include "package/uniform_cycle_bank.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::package_detail;

constexpr auto kRunningState =
    engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
    engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled);
constexpr std::uint64_t kFirstGlobalFrame = 10000U;
constexpr std::uint64_t kTapeFrameCount = 100000U;
constexpr contract::TorqueTermMask kBmwIncludedTorqueTerms =
    contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
    contract::torque_term_mask(contract::TorqueTerm::crank_friction) |
    contract::torque_term_mask(contract::TorqueTerm::piston_ring_friction) |
    contract::torque_term_mask(contract::TorqueTerm::starter);
constexpr contract::TorqueTermMask kBmwOmittedTorqueTerms =
    contract::known_torque_term_mask() & ~kBmwIncludedTorqueTerms;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const double left, const double right,
                        const double tolerance = 1.0e-12) noexcept {
    return std::isfinite(left) && std::abs(left - right) <= tolerance;
}

[[nodiscard]] EngineCompletedCycleEvidence
cycle(const std::uint64_t ordinal, const double rpm, const double local_start,
      const EngineCycleStateFlagMask transitions = 0U) {
    const auto global_start = static_cast<double>(kFirstGlobalFrame) + local_start;
    const auto global_end = global_start + 100.0;
    const auto lattice = static_cast<std::int64_t>(ordinal + 100U);
    const EngineCycleBoundaryEvidence start{
        lattice,     ordinal * 2U,           ordinal * 2U + 1U, 0.25,
        local_start, local_start / 192000.0, global_start,
    };
    const EngineCycleBoundaryEvidence end{
        lattice + 1, ordinal * 2U + 2U, ordinal * 2U + 3U,
        0.75,        local_start + 1.0, (local_start + 100.0) / 192000.0,
        global_end,
    };
    const EngineCycleControlEvidence requested{0.45, 0.45, 0.45, 0U};
    const EngineCycleControlEvidence resolved{0.42, 0.42, 0.42, 0U};
    const EngineCycleControlEvidence intake{0.40, 0.40, 0.40, 0U};
    const EngineCycleNetShaftEvidence torque{
        120.0,
        24.0,
        contract::Availability::available,
        contract::Completeness::incomplete,
        contract::QuantityUnavailableReason::none,
        kBmwIncludedTorqueTerms,
        kBmwOmittedTorqueTerms,
    };
    return {
        ordinal, start,         end,           end.time_s - start.time_s,
        rpm,     requested,     resolved,      intake,
        torque,  kRunningState, kRunningState, transitions,
    };
}

[[nodiscard]] UniformCycleBank require_bank(UniformCycleBankResult result) {
    if (const auto *failure = std::get_if<UniformCycleBankError>(&result)) {
        throw std::runtime_error{"unexpected cycle-bank failure at " + failure->path +
                                 ": " + failure->detail};
    }
    return std::get<UniformCycleBank>(std::move(result));
}

[[nodiscard]] const UniformCycleBankError &
require_error(const UniformCycleBankResult &result,
              const UniformCycleBankErrorCode code) {
    const auto *failure = std::get_if<UniformCycleBankError>(&result);
    expect(failure != nullptr && failure->code == code,
           "cycle-bank failure had the wrong typed code");
    return *failure;
}

[[nodiscard]] UniformRunningCycleBankRequest
running_request(const std::vector<EngineCompletedCycleEvidence> &cycles,
                const authoring::PackageBakeRunningDirection direction) {
    return {
        {cycles, kFirstGlobalFrame, kTapeFrameCount},
        kPackageBakeMethodGeometry,
        1000.0,
        1050.0,
        0.25,
        direction,
    };
}

void test_fractional_alignment_and_optimal_rising_assignment() {
    const std::vector cycles{
        cycle(10U, 995.0, 4000.0),  cycle(11U, 1007.0, 4100.0),
        cycle(12U, 1020.0, 4200.0), cycle(13U, 1033.0, 4300.0),
        cycle(14U, 1045.0, 4400.0),
    };
    const auto bank = require_bank(assign_uniform_running_cycle_bank(
        running_request(cycles, authoring::PackageBakeRunningDirection::rising)));
    expect(bank.units.size() == 3U && bank.units[0].completed_cycle_ordinal == 10U &&
               bank.units[1].completed_cycle_ordinal == 12U &&
               bank.units[2].completed_cycle_ordinal == 14U &&
               near(bank.total_squared_rpm_error, 75.0),
           "rising assignment was not the minimum-error unique ordered solution");
    expect(bank.load_calibration == contract::AudioPackageLoadCalibration{
                                        contract::Completeness::incomplete,
                                        kBmwIncludedTorqueTerms,
                                        kBmwOmittedTorqueTerms},
           "BMW partial modeled-torque calibration was not retained exactly");
    expect(bank.units[0].start.left_frame == 5228U &&
               bank.units[0].start.right_frame == 5229U &&
               near(bank.units[0].start.fraction_from_left_01, 0.8) &&
               near(bank.units[0].canonical_rpm, 1000.0) &&
               near(bank.units[0].average_signed_load, 0.25),
           "exact +1228.8 signal alignment was rounded or lost from the unit");
}

void test_falling_rows_reverse_source_order_without_reuse() {
    const std::vector cycles{
        cycle(20U, 1045.0, 4000.0), cycle(21U, 1033.0, 4100.0),
        cycle(22U, 1020.0, 4200.0), cycle(23U, 1007.0, 4300.0),
        cycle(24U, 995.0, 4400.0),
    };
    const auto bank = require_bank(assign_uniform_running_cycle_bank(
        running_request(cycles, authoring::PackageBakeRunningDirection::falling)));
    expect(bank.units.size() == 3U && bank.units[0].canonical_rpm == 1000.0 &&
               bank.units[2].canonical_rpm == 1050.0 &&
               bank.units[0].completed_cycle_ordinal == 24U &&
               bank.units[1].completed_cycle_ordinal == 22U &&
               bank.units[2].completed_cycle_ordinal == 20U &&
               bank.units[1].end.left_frame <= bank.units[0].start.left_frame,
           "falling canonical rows did not preserve decreasing unique source order");
}

void test_unsafe_cycles_and_impossible_coverage_are_rejected() {
    const std::vector edge_cycle{cycle(1U, 1000.0, 0.0)};
    const auto edge = assign_uniform_running_cycle_bank(
        running_request(edge_cycle, authoring::PackageBakeRunningDirection::rising));
    expect(require_error(edge, UniformCycleBankErrorCode::cycle_rejected).path ==
               "lane.cycles[0].start_boundary.delivery_frame",
           "edge-unsafe aligned cycle lacked a precise rejection path");

    const std::vector transitioning{
        cycle(2U, 1000.0, 4000.0,
              engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active)),
    };
    const auto transition = assign_uniform_running_cycle_bank(
        running_request(transitioning, authoring::PackageBakeRunningDirection::rising));
    expect(require_error(transition, UniformCycleBankErrorCode::cycle_rejected).path ==
               "lane.cycles[0].state_transition_flags",
           "transitioning normal-running cycle lacked a precise rejection path");

    const std::vector uncovered{
        cycle(3U, 1000.0, 4000.0),
        cycle(4U, 1001.0, 4100.0),
        cycle(5U, 1002.0, 4200.0),
    };
    const auto impossible = assign_uniform_running_cycle_bank(
        running_request(uncovered, authoring::PackageBakeRunningDirection::rising));
    (void)require_error(impossible, UniformCycleBankErrorCode::impossible_coverage);
}

void test_idle_retains_every_safe_cycle_chronologically() {
    std::vector cycles{
        cycle(30U, 698.0, 4000.0),
        cycle(31U, 702.0, 4100.0),
        cycle(32U, 700.0, 4200.0),
    };
    for (auto &item : cycles) {
        item.instantaneous_net_shaft.availability =
            contract::Availability::unavailable;
        item.instantaneous_net_shaft.completeness =
            contract::Completeness::incomplete;
        item.instantaneous_net_shaft.unavailable_reason =
            contract::QuantityUnavailableReason::model_not_admitted;
        item.instantaneous_net_shaft.included_terms = 0U;
        item.instantaneous_net_shaft.omitted_terms = 0U;
    }
    const auto bank = require_bank(retain_uniform_idle_cycle_pool({
        {cycles, kFirstGlobalFrame, kTapeFrameCount},
        kPackageBakeMethodGeometry,
        700.0,
        0.0,
    }));
    expect(bank.units.size() == cycles.size() &&
               bank.units[0].completed_cycle_ordinal == 30U &&
               bank.units[2].completed_cycle_ordinal == 32U &&
               bank.units[0].canonical_rpm == 698.0 &&
               bank.units[1].canonical_rpm == 702.0 &&
               bank.units[2].canonical_rpm == 700.0 &&
               !bank.units[0].average_net_torque_nm.has_value() &&
               !bank.units[2].average_net_torque_nm.has_value(),
           "idle pool did not retain every safe cycle in chronological order");
}

void test_torque_availability_and_term_partition_fail_closed() {
    auto unavailable = cycle(40U, 1000.0, 4000.0);
    unavailable.instantaneous_net_shaft.availability =
        contract::Availability::unavailable;
    unavailable.instantaneous_net_shaft.completeness =
        contract::Completeness::incomplete;
    unavailable.instantaneous_net_shaft.unavailable_reason =
        contract::QuantityUnavailableReason::cycle_integration_not_admitted;
    unavailable.instantaneous_net_shaft.included_terms = 0U;
    unavailable.instantaneous_net_shaft.omitted_terms = 0U;
    const std::vector unavailable_cycles{unavailable};
    const auto unavailable_result = assign_uniform_running_cycle_bank(
        running_request(unavailable_cycles,
                        authoring::PackageBakeRunningDirection::rising));
    expect(require_error(unavailable_result,
                         UniformCycleBankErrorCode::cycle_rejected)
                   .path == "lane.cycles[0].instantaneous_net_shaft",
           "unavailable modeled torque did not reject the running cycle precisely");

    std::vector mixed{
        cycle(50U, 1000.0, 4000.0),
        cycle(51U, 1025.0, 4100.0),
        cycle(52U, 1050.0, 4200.0),
    };
    mixed[1].instantaneous_net_shaft.completeness =
        contract::Completeness::complete;
    mixed[1].instantaneous_net_shaft.included_terms =
        contract::known_torque_term_mask();
    mixed[1].instantaneous_net_shaft.omitted_terms = 0U;
    const auto mixed_result = assign_uniform_running_cycle_bank(
        running_request(mixed, authoring::PackageBakeRunningDirection::rising));
    (void)require_error(mixed_result,
                        UniformCycleBankErrorCode::impossible_coverage);
}

} // namespace

int main() {
    try {
        test_fractional_alignment_and_optimal_rising_assignment();
        test_falling_rows_reverse_source_order_without_reuse();
        test_unsafe_cycles_and_impossible_coverage_are_rejected();
        test_idle_retains_every_safe_cycle_chronologically();
        test_torque_availability_and_term_partition_fail_closed();
        std::cout << "uniform cycle-bank tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "uniform cycle-bank tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
