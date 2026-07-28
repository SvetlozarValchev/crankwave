#include "simulation/operating_cycle_accountant.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::simulation;

constexpr double kCycleRadians = 4.0 * std::numbers::pi_v<double>;
constexpr double kEngineSpeedRpm = 3000.0;
constexpr double kAngularSpeedRadS =
    kEngineSpeedRpm * 2.0 * std::numbers::pi_v<double> / 60.0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] OperatingCycleAccountingPlan plan() {
    return {
        {0.0, 0.003},
        {0.4, 0.005, 0.09, 0.0009},
        kEngineSpeedRpm,
        0.1,
        true,
        contract::indicated_gas_torque_term_mask(),
        contract::friction_pump_and_accessory_torque_term_mask(),
        contract::torque_term_mask(contract::TorqueTerm::starter),
        {
            {{1}, {10}, 0.001},
            {{2}, {20}, 0.002},
        },
        {{10}, {20}, {30}},
    };
}

[[nodiscard]] OperatingCycleAccountant
make_accountant(OperatingCycleAccountingPlan value = plan()) {
    auto compiled = compile_operating_cycle_accountant(std::move(value));
    const auto *error = std::get_if<OperatingCycleAccountingError>(&compiled);
    expect(error == nullptr, "valid operating-cycle plan was rejected");
    return std::get<OperatingCycleAccountant>(std::move(compiled));
}

struct SampleStorage {
    std::vector<OperatingGasVolumePressureSample> gas_volumes;

    [[nodiscard]] OperatingCycleSample
    view(std::uint64_t sample_index, double theta_rad, double time_s,
         double indicated_torque_nm = 100.0,
         double engine_speed_rpm = kEngineSpeedRpm) const noexcept {
        return {
            sample_index,        time_s,      theta_rad, engine_speed_rpm,
            indicated_torque_nm, gas_volumes,
        };
    }
};

[[nodiscard]] SampleStorage pressures(double cylinder_1_pa, double cylinder_2_pa,
                                      double other_volume_pa = 300000.0) {
    return {
        {
            {{10}, cylinder_1_pa},
            {{20}, cylinder_2_pa},
            {{30}, other_volume_pa},
        },
    };
}

[[nodiscard]] const OperatingCycleBoundaryCrossing &
require_crossing(const OperatingCycleAccountingAdvanceResult &result,
                 std::string_view message) {
    const auto *crossing = std::get_if<OperatingCycleBoundaryCrossing>(&result);
    expect(crossing != nullptr, message);
    return *crossing;
}

void test_shared_boundary_pressure_peak_loss_and_brake_identities() {
    auto accountant = make_accountant();
    const auto start = pressures(100000.0, 200000.0, 300000.0);
    const auto interior = pressures(2000000.0, 4000000.0, 500000.0);
    const auto before = pressures(1000000.0, 2000000.0, 300000.0);
    const auto after = pressures(9000000.0, 400000.0, 700000.0);

    expect(std::holds_alternative<NoOperatingCycleBoundaryCrossing>(
               accountant.advance(start.view(0, 0.0, 0.0))),
           "exact cycle-start sample unexpectedly crossed a boundary");
    expect(std::holds_alternative<NoOperatingCycleBoundaryCrossing>(
               accountant.advance(interior.view(
                   1, 0.5 * kCycleRadians, 0.5 * kCycleRadians / kAngularSpeedRadS))),
           "interior sample unexpectedly crossed a boundary");
    expect(std::holds_alternative<NoOperatingCycleBoundaryCrossing>(
               accountant.advance(before.view(
                   2, kCycleRadians - 1.0, (kCycleRadians - 1.0) / kAngularSpeedRadS))),
           "pre-boundary sample unexpectedly crossed a boundary");

    const auto result = accountant.advance(
        after.view(3, kCycleRadians + 3.0, (kCycleRadians + 3.0) / kAngularSpeedRadS));
    const auto &crossing =
        require_crossing(result, "bracketed full cycle did not close");
    expect(crossing.completed_cycle.has_value(),
           "exact-start full cycle was discarded");
    expect_near(crossing.boundary.fraction_from_left_01, 0.25, 1e-15,
                "accountant did not reuse quadrature boundary fraction");
    expect(
        crossing.boundary_pressures.size() == 3 &&
            crossing.boundary_pressures[0].gas_volume_id == contract::GasVolumeId{10} &&
            crossing.boundary_pressures[1].gas_volume_id == contract::GasVolumeId{20} &&
            crossing.boundary_pressures[2].gas_volume_id == contract::GasVolumeId{30},
        "boundary pressure identity/order changed");
    expect_near(crossing.boundary_pressures[0].pressure_pa_abs, 3000000.0, 0.0,
                "chamber-1 boundary interpolation changed");
    expect_near(crossing.boundary_pressures[1].pressure_pa_abs, 1600000.0, 0.0,
                "chamber-2 boundary interpolation changed");
    expect_near(crossing.boundary_pressures[2].pressure_pa_abs, 400000.0, 0.0,
                "non-chamber boundary interpolation changed");

    const auto &cycle = *crossing.completed_cycle;
    expect(cycle.cylinder_peak_pressures.size() == 2,
           "cycle peak evidence shape changed");
    expect_near(cycle.cylinder_peak_pressures[0].peak_pressure_pa_abs, 3000000.0, 0.0,
                "after-boundary chamber-1 sample polluted prior-cycle peak");
    expect_near(cycle.cylinder_peak_pressures[1].peak_pressure_pa_abs, 4000000.0, 0.0,
                "interior chamber-2 peak was not retained");
    const double weighted_peak_pa = (0.001 * 3000000.0 + 0.002 * 4000000.0) / 0.003;
    expect_near(cycle.aggregate_loss.displacement_weighted_peak_pressure_pa_abs,
                weighted_peak_pa, 1e-9,
                "Chen-Flynn did not consume displacement-weighted cycle peaks");
    expect_near(cycle.aggregate_loss.mean_piston_speed_m_s, 10.0, 0.0,
                "accountant did not bind Chen-Flynn to held mechanics RPM");
    const double expected_fmep_bar =
        ((0.4 + 0.005 * (weighted_peak_pa / 100000.0)) + 0.09 * 10.0) + 0.0009 * 100.0;
    const double expected_loss_work = 100000.0 * expected_fmep_bar * 0.003;
    expect_near(cycle.aggregate_loss.positive_aggregate_loss_work_j, expected_loss_work,
                1e-13, "pressure-dependent aggregate loss work changed");

    const double expected_indicated_work = 100.0 * kCycleRadians;
    const double expected_brake_work = expected_indicated_work - expected_loss_work;
    expect_near(cycle.indicated_quadrature.indicated_gas_work_j,
                expected_indicated_work, 2e-12, "indicated work changed");
    expect(cycle.indicated_quadrature.friction_pump_and_accessory_work_j == 0.0 &&
               !std::signbit(
                   cycle.indicated_quadrature.friction_pump_and_accessory_work_j) &&
               cycle.indicated_quadrature.starter_work_j == 0.0 &&
               !std::signbit(cycle.indicated_quadrature.starter_work_j) &&
               cycle.starter_work_j == 0.0 && !std::signbit(cycle.starter_work_j),
           "canonical zero placeholder/starter work changed");
    expect_near(cycle.brake_work_j, expected_brake_work, 2e-12,
                "brake-work composition changed");
    expect_near(cycle.cycle_mean_brake_torque_nm, expected_brake_work / kCycleRadians,
                2e-13, "work-derived brake torque changed");
    expect_near(cycle.net_brake_mean_effective_pressure_pa, expected_brake_work / 0.003,
                1e-8, "work-derived net BMEP changed");
    expect_near(cycle.cycle_mean_brake_power_w,
                expected_brake_work / (kCycleRadians / kAngularSpeedRadS), 2e-10,
                "work-derived mean brake power changed");

    const auto next_end = pressures(100000.0, 100000.0, 100000.0);
    const auto next_result = accountant.advance(
        next_end.view(4, 2.0 * kCycleRadians, 2.0 * kCycleRadians / kAngularSpeedRadS));
    const auto &next_crossing =
        require_crossing(next_result, "post-boundary successor cycle did not close");
    expect(next_crossing.completed_cycle.has_value() &&
               next_crossing.completed_cycle->cylinder_peak_pressures[0]
                       .peak_pressure_pa_abs == 9000000.0 &&
               accountant.completed_cycle_count() == 2,
           "post-boundary sample did not enter exactly the successor cycle");
}

void test_discarded_partial_cycle_cannot_pollute_first_complete_peak() {
    auto accountant = make_accountant();
    const auto low = pressures(200000.0, 300000.0);
    const auto huge = pressures(90000000.0, 80000000.0);

    (void)accountant.advance(low.view(0, 1.0, 1.0 / kAngularSpeedRadS));
    (void)accountant.advance(
        huge.view(1, 0.5 * kCycleRadians, 0.5 * kCycleRadians / kAngularSpeedRadS));
    (void)accountant.advance(
        low.view(2, kCycleRadians - 1.0, (kCycleRadians - 1.0) / kAngularSpeedRadS));
    const auto discarded = accountant.advance(
        low.view(3, kCycleRadians, kCycleRadians / kAngularSpeedRadS));
    const auto &first_crossing =
        require_crossing(discarded, "discarded-partial boundary was hidden");
    expect(!first_crossing.completed_cycle.has_value(),
           "initial partial cycle was published");

    (void)accountant.advance(
        low.view(4, 1.5 * kCycleRadians, 1.5 * kCycleRadians / kAngularSpeedRadS));
    const auto complete = accountant.advance(
        low.view(5, 2.0 * kCycleRadians, 2.0 * kCycleRadians / kAngularSpeedRadS));
    const auto &second_crossing =
        require_crossing(complete, "first full cycle did not close");
    expect(second_crossing.completed_cycle.has_value(), "first full cycle was lost");
    expect(second_crossing.completed_cycle->cylinder_peak_pressures[0]
                       .peak_pressure_pa_abs == 200000.0 &&
               second_crossing.completed_cycle->cylinder_peak_pressures[1]
                       .peak_pressure_pa_abs == 300000.0,
           "discarded partial-cycle pressure polluted complete-cycle peaks");
}

void test_exact_start_boundary_is_included_in_cycle_peaks() {
    auto accountant = make_accountant();
    const auto high_start = pressures(7000000.0, 8000000.0, 9000000.0);
    const auto low_end = pressures(200000.0, 300000.0, 400000.0);

    (void)accountant.advance(high_start.view(0, 0.0, 0.0));
    const auto result = accountant.advance(
        low_end.view(1, kCycleRadians, kCycleRadians / kAngularSpeedRadS));
    const auto &crossing =
        require_crossing(result, "exact-boundary cycle did not close");
    expect(
        crossing.completed_cycle.has_value() &&
            crossing.completed_cycle->cylinder_peak_pressures[0].peak_pressure_pa_abs ==
                7000000.0 &&
            crossing.completed_cycle->cylinder_peak_pressures[1].peak_pressure_pa_abs ==
                8000000.0 &&
            crossing.boundary_pressures[0].pressure_pa_abs == 200000.0 &&
            crossing.boundary_pressures[1].pressure_pa_abs == 300000.0,
        "exact start/end boundary pressure ownership changed");
}

void test_plan_and_sample_shapes_fail_closed() {
    {
        auto invalid = plan();
        invalid.aggregate_loss_terms =
            contract::torque_term_mask(contract::TorqueTerm::crank_friction);
        const auto compiled = compile_operating_cycle_accountant(std::move(invalid));
        const auto *error = std::get_if<OperatingCycleAccountingError>(&compiled);
        expect(error != nullptr &&
                   error->code ==
                       OperatingCycleAccountingErrorCode::invalid_term_partition,
               "incomplete torque partition was admitted");
    }
    {
        auto invalid = plan();
        invalid.physically_resolved_gas_volumes = {{20}, {30}};
        const auto compiled = compile_operating_cycle_accountant(std::move(invalid));
        const auto *error = std::get_if<OperatingCycleAccountingError>(&compiled);
        expect(error != nullptr &&
                   error->code ==
                       OperatingCycleAccountingErrorCode::invalid_gas_volume_plan,
               "plan omitting a chamber pressure was admitted");
    }
    {
        auto accountant = make_accountant();
        const auto valid_pressures = pressures(200000.0, 300000.0);
        const auto mismatched_rpm =
            std::nextafter(kEngineSpeedRpm, std::numeric_limits<double>::infinity());
        const auto result = accountant.advance(
            valid_pressures.view(17, 0.0, 0.0, 100.0, mismatched_rpm));
        const auto *error = std::get_if<OperatingCycleAccountingError>(&result);
        expect(error != nullptr &&
                   error->code ==
                       OperatingCycleAccountingErrorCode::engine_speed_mismatch &&
                   accountant.faulted(),
               "Chen-Flynn plan RPM drifted from transactional mechanics RPM");
        expect(accountant.advance(pressures(1.0, 1.0).view(18, 1.0, 1.0)) == result,
               "terminal accounting error was not stable");
    }
    {
        auto accountant = make_accountant();
        auto nonfinite = pressures(200000.0, 300000.0);
        nonfinite.gas_volumes[1].pressure_pa_abs =
            std::numeric_limits<double>::quiet_NaN();
        const auto result = accountant.advance(nonfinite.view(3, 0.0, 0.0));
        const auto *error = std::get_if<OperatingCycleAccountingError>(&result);
        expect(error != nullptr &&
                   error->code ==
                       OperatingCycleAccountingErrorCode::nonfinite_pressure &&
                   error->element_index == 1,
               "nonfinite absolute pressure was admitted");
    }
}

void test_post_quadrature_loss_failure_does_not_count_an_operating_cycle() {
    auto overflow_plan = plan();
    overflow_plan.aggregate_loss = {0.0, 1.0e6, 0.0, 0.0};
    auto accountant = make_accountant(std::move(overflow_plan));
    const auto extreme = pressures(std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max());

    (void)accountant.advance(extreme.view(0, 0.0, 0.0));
    const auto result = accountant.advance(
        extreme.view(1, kCycleRadians, kCycleRadians / kAngularSpeedRadS));
    const auto *error = std::get_if<OperatingCycleAccountingError>(&result);
    expect(error != nullptr &&
               error->code ==
                   OperatingCycleAccountingErrorCode::aggregate_loss_failure &&
               accountant.completed_cycle_count() == 0,
           "post-quadrature loss failure published a nonexistent operating cycle");
}

void test_move_leaves_one_working_accountant() {
    auto source = make_accountant();
    const auto start = pressures(200000.0, 300000.0);
    (void)source.advance(start.view(0, 0.0, 0.0));
    auto destination = std::move(source);

    const auto source_result =
        source.advance(start.view(1, kCycleRadians, kCycleRadians / kAngularSpeedRadS));
    const auto *source_error =
        std::get_if<OperatingCycleAccountingError>(&source_result);
    expect(source_error != nullptr &&
               source_error->code == OperatingCycleAccountingErrorCode::moved_from &&
               source.faulted(),
           "move construction left a second working accountant");

    const auto destination_result = destination.advance(
        start.view(1, kCycleRadians, kCycleRadians / kAngularSpeedRadS));
    const auto &crossing = require_crossing(
        destination_result, "moved destination did not retain cycle state");
    expect(crossing.completed_cycle.has_value() &&
               destination.completed_cycle_count() == 1,
           "moved destination did not complete its cycle");
}

} // namespace

int main() {
    test_shared_boundary_pressure_peak_loss_and_brake_identities();
    test_discarded_partial_cycle_cannot_pollute_first_complete_peak();
    test_exact_start_boundary_is_included_in_cycle_peaks();
    test_plan_and_sample_shapes_fail_closed();
    test_post_quadrature_loss_failure_does_not_count_an_operating_cycle();
    test_move_leaves_one_working_accountant();
    return 0;
}
