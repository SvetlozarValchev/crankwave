#include "contract_test_support.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_operating_profile.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;

constexpr double kHeldRpm = 3000.0;
constexpr double kCutoffTimeS = 0.22;
constexpr std::uint32_t kCyclesPerBlock = 2U;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

struct Fixture {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::Sha256Digest request_identity;
};

[[nodiscard]] Fixture fixture(double torque_tolerance_nm = 1.0e9,
                              double pressure_tolerance_pa = 1.0e12,
                              std::uint32_t cycles_per_block = kCyclesPerBlock) {
    auto profile_result = profiles::make_bmw_m52b28_operating_profile();
    const auto *canonical =
        std::get_if<profiles::BmwM52b28OperatingProfile>(&profile_result);
    expect(canonical != nullptr, "canonical BMW operating profile construction failed");

    auto engine = canonical->engine;
    const auto &profile =
        std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
    contract::test::InputBuilder builder;
    auto scenario = contract::test::make_scenario(builder, engine);
    scenario.scenario_id = "low-order-operating-runtime-test";
    scenario.fuel.fuel_id.value = profile.core.fuel.fuel_id.value;
    scenario.fuel.lower_heating_value_j_per_kg.value =
        profile.core.fuel.energy_density_j_per_kg.value;
    scenario.fuel.stoichiometric_air_fuel_mass_ratio.value =
        profile.core.fuel.molecular_afr.value;
    scenario.initial_thermal_state.oil_temperature_k.value =
        profile.aggregate_loss.required_oil_temperature_k.value;
    scenario.preparation = contract::ConvergenceSettling{
        builder.resolved(
            contract::adjacent_cycle_block_mean_convergence_method_identity(),
            "scenario.preparation.method"),
        builder.resolved(0.0, "scenario.preparation.minimum_warm_up_duration_s"),
        builder.resolved(0.0, "scenario.preparation.minimum_settling_duration_s"),
        builder.resolved(kCutoffTimeS,
                         "scenario.preparation.maximum_preparation_duration_s"),
        builder.resolved<std::uint32_t>(cycles_per_block,
                                        "scenario.preparation.comparison_cycle_count"),
        builder.resolved(torque_tolerance_nm,
                         "scenario.preparation.cycle_mean_torque_tolerance_nm"),
        builder.resolved(pressure_tolerance_pa,
                         "scenario.preparation.pressure_tolerance_pa"),
    };
    scenario.operating_state.value = {
        {
            "held-running",
            0.0,
            {true, true, false, true, false},
        },
    };
    scenario.total_duration_s.value = 0.3;
    scenario.audible_start_s.value = kCutoffTimeS;
    scenario.audible_duration_s.value = 0.08;
    scenario.rates.physics = {10000U, 1U};
    scenario.rates.capture = scenario.rates.physics;
    scenario.quality.value.capture_block_capacity_frames = 200U;
    scenario.quality.value.event_journal_capacity_records = 3800U;
    scenario.mode = contract::HeldSpeed{
        builder.resolved(kHeldRpm, "scenario.mode.engine_speed_rpm"),
        builder.resolved(profile.core.mechanism.crank.crank_tdc_reference_rad.value,
                         "scenario.mode.initial_theta_rad"),
        builder.resolved(0.85, "scenario.mode.throttle_01"),
    };

    contract::Sha256Digest request_identity;
    request_identity.bytes.front() = 1U;
    return {
        std::move(engine),
        std::move(scenario),
        request_identity,
    };
}

[[nodiscard]] simulation::LowOrderCapturePlan capture_plan(const Fixture &value) {
    auto result =
        simulation::compile_low_order_capture_plan(value.engine, value.scenario);
    const auto *plan = std::get_if<simulation::LowOrderCapturePlan>(&result);
    expect(plan != nullptr, "canonical operating capture plan was rejected");
    return *plan;
}

[[nodiscard]] simulation::LowOrderOperatingPointV1Runtime
operating_runtime(const Fixture &value, const simulation::LowOrderCapturePlan &plan) {
    auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    if (report != nullptr) {
        std::string message = "canonical operating runtime was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<simulation::LowOrderOperatingPointV1Runtime>(std::move(result));
}

[[nodiscard]] simulation::LowOrderEngineCoreV1Runtime
core_runtime(const Fixture &value) {
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        value.engine.physics_profile);
    auto result = simulation::compile_low_order_engine_core_v1_runtime(
        value.engine, value.scenario, profile.core);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report == nullptr, "canonical operating core was rejected");
    return std::get<simulation::LowOrderEngineCoreV1Runtime>(std::move(result));
}

void advance_to_cutoff(simulation::LowOrderEngineCoreV1Runtime &core,
                       simulation::LowOrderOperatingPointV1Runtime &operating) {
    while (operating.accepted_sample_count() < operating.fixed_cutoff_frame_count()) {
        auto core_result = core.advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&core_result);
        expect(step != nullptr, "operating core ended before the fixed cutoff");
        auto result = operating.advance(step->mechanics.get(), step->gas.get());
        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            throw std::runtime_error{
                "operating runtime faulted: " + failure->detail_code + "; " +
                failure->state_summary};
        }
    }
}

void test_complete_cycle_evidence_reaches_public_result() {
    auto value = fixture();
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);
    advance_to_cutoff(core, operating);

    expect(operating.finalized() && !operating.faulted() &&
               operating.operating_point_result().has_value(),
           "operating runtime did not finalize a settled result");
    const auto &convergence = operating.operating_point_result()->convergence;
    for (const auto *block : {&convergence.block_a, &convergence.block_b}) {
        expect(block->completed_cycles.size() == kCyclesPerBlock,
               "public block omitted complete-cycle work evidence");
        double indicated = 0.0;
        double loss = 0.0;
        double starter = 0.0;
        double brake = 0.0;
        std::vector<double> pressure_sums(block->mean_boundary_pressures.size(), 0.0);
        for (const auto &cycle : block->completed_cycles) {
            indicated += cycle.indicated_gas_work_j;
            loss += cycle.aggregate_loss_work_j;
            starter += cycle.starter_work_j;
            brake += cycle.brake_work_j;
            expect(cycle.end_boundary_pressures.size() ==
                       block->mean_boundary_pressures.size(),
                   "public completed cycle omitted end-boundary pressure lanes");
            for (std::size_t index = 0; index < cycle.end_boundary_pressures.size();
                 ++index) {
                expect(cycle.end_boundary_pressures[index].gas_volume_id ==
                           block->mean_boundary_pressures[index].gas_volume_id,
                       "public completed-cycle pressure identity/order changed");
                pressure_sums[index] +=
                    cycle.end_boundary_pressures[index].pressure_pa_abs;
            }
        }
        expect(std::bit_cast<std::uint64_t>(indicated) ==
                       std::bit_cast<std::uint64_t>(block->indicated_gas_work_j) &&
                   std::bit_cast<std::uint64_t>(loss) ==
                       std::bit_cast<std::uint64_t>(block->aggregate_loss_work_j) &&
                   std::bit_cast<std::uint64_t>(starter) ==
                       std::bit_cast<std::uint64_t>(block->starter_work_j) &&
                   std::bit_cast<std::uint64_t>(brake) ==
                       std::bit_cast<std::uint64_t>(block->brake_work_j),
               "public block totals changed the convergence reduction order");
        for (std::size_t index = 0; index < pressure_sums.size(); ++index) {
            const double mean =
                pressure_sums[index] / static_cast<double>(kCyclesPerBlock);
            expect(std::bit_cast<std::uint64_t>(mean) ==
                       std::bit_cast<std::uint64_t>(
                           block->mean_boundary_pressures[index].pressure_pa_abs),
                   "public pressure mean changed the convergence reduction order");
        }
    }
}

void test_capture_plan_transplants_are_rejected() {
    const auto value = fixture();
    auto plan = capture_plan(value);
    plan.scenario_id = "different-scenario";
    const auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(report->issues,
                                   [](const auto &issue) {
                                       return issue.path == "capture_plan.scenario_id";
                                   }),
           "runtime compiler admitted a capture plan from another scenario");
}

void test_scenario_fuel_afr_is_exactly_bound_to_core() {
    auto value = fixture();
    const auto plan = capture_plan(value);
    value.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value =
        std::nextafter(value.scenario.fuel.stoichiometric_air_fuel_mass_ratio.value,
                       std::numeric_limits<double>::infinity());

    const auto result = simulation::compile_low_order_operating_point_v1_runtime(
        value.engine, value.scenario, plan, value.request_identity);
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(
                   report->issues,
                   [](const auto &issue) {
                       return issue.path ==
                              "scenario.fuel.stoichiometric_air_fuel_mass_ratio.value";
                   }),
           "runtime compiler admitted scenario AFR that was not bit-exact with the "
           "engine core");
}

void test_capture_reports_absent_instantaneous_models_truthfully() {
    const auto value = fixture();
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);
    auto core_result = core.advance();
    const auto &step = std::get<simulation::LowOrderEngineCoreV1StepView>(core_result);
    const auto result = operating.advance(step.mechanics.get(), step.gas.get());
    const auto *accepted =
        std::get_if<simulation::LowOrderOperatingPointV1Step>(&result);
    expect(
        accepted != nullptr &&
            accepted->capture_torque.pumping_partition.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted &&
            accepted->capture_torque.friction_pump_and_accessory.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted &&
            accepted->capture_torque.instantaneous_net_shaft.unavailable_reason ==
                contract::QuantityUnavailableReason::model_not_admitted,
        "capture mislabeled an absent instantaneous model as missing input");
}

void test_runtime_rejects_foreign_controls_and_shape() {
    const auto value = fixture();
    const auto plan = capture_plan(value);

    const auto expect_rejected = [&](const auto &mutate,
                                     std::string_view expected_detail,
                                     std::string_view message) {
        auto operating = operating_runtime(value, plan);
        auto core = core_runtime(value);
        auto result = core.advance();
        const auto &step = std::get<simulation::LowOrderEngineCoreV1StepView>(result);
        auto mechanics = step.mechanics.get();
        auto gas = step.gas.get();
        mutate(mechanics, gas);
        const auto rejected = operating.advance(mechanics, gas);
        const auto *failure = std::get_if<contract::FailureContext>(&rejected);
        expect(failure != nullptr && failure->detail_code == expected_detail, message);
    };

    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.requested_throttle_01 =
                std::nextafter(mechanics.requested_throttle_01, 1.0);
        },
        "operating-held-condition-disagreed",
        "runtime admitted a noncanonical held throttle");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.resolved_engine_throttle_01 =
                std::nextafter(mechanics.resolved_engine_throttle_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged resolved-engine throttle");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.intake_plate_position_01 =
                std::nextafter(mechanics.intake_plate_position_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged intake-plate position");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.main_flow_multiplier_01 =
                std::nextafter(mechanics.main_flow_multiplier_01, 1.0);
        },
        "operating-effective-throttle-disagreed",
        "runtime admitted a forged main-flow multiplier");
    expect_rejected([](auto &mechanics, auto &) { mechanics.limiter_timer_s = -0.0; },
                    "operating-disabled-limiter-disagreed",
                    "runtime admitted a noncanonical disabled-limiter timer");
    expect_rejected(
        [](auto &mechanics, auto &) { mechanics.limiter_cut_active = true; },
        "operating-disabled-limiter-disagreed",
        "runtime admitted an active cut from the disabled limiter");
    expect_rejected(
        [](auto &mechanics, auto &) {
            mechanics.events.push_back({
                static_cast<std::uint8_t>(mechanics.events.size()),
                contract::LimiterStateChanged{false, true, false, 0.0},
            });
        },
        "operating-disabled-limiter-disagreed",
        "runtime admitted a mechanics limiter transition while limiter was "
        "disabled");
    expect_rejected(
        [](auto &, auto &gas) {
            gas.events.push_back({
                static_cast<std::uint8_t>(gas.events.size()),
                contract::LimiterStateChanged{false, true, false, 0.0},
            });
        },
        "operating-disabled-limiter-disagreed",
        "runtime admitted a gas limiter transition while limiter was disabled");
    expect_rejected(
        [](auto &, auto &gas) {
            gas.indicated_gas_torque_nm = std::nextafter(
                gas.indicated_gas_torque_nm, std::numeric_limits<double>::infinity());
        },
        "operating-indicated-torque-reduction-disagreed",
        "runtime admitted aggregate torque that differed from its ordered "
        "cylinder reduction");
    expect_rejected([](auto &, auto &gas) { gas.gas_volumes.pop_back(); },
                    "operating-transaction-shape-disagreed",
                    "runtime admitted a foreign gas transaction shape");
}

void test_nonconvergence_retains_exact_residual_diagnostics() {
    auto value = fixture(std::numeric_limits<double>::denorm_min(),
                         std::numeric_limits<double>::denorm_min());
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);

    contract::FailureContext failure;
    while (operating.accepted_sample_count() < operating.fixed_cutoff_frame_count()) {
        auto core_result = core.advance();
        const auto &step =
            std::get<simulation::LowOrderEngineCoreV1StepView>(core_result);
        auto result = operating.advance(step.mechanics.get(), step.gas.get());
        if (const auto *actual = std::get_if<contract::FailureContext>(&result)) {
            failure = *actual;
            break;
        }
    }
    const auto &preparation =
        std::get<contract::ConvergenceSettling>(value.scenario.preparation);
    expect(
        failure.kind == contract::FailureKind::preparation_not_converged &&
            failure.detail_code == contract::kPreparationNotConvergedDetailCode &&
            failure.tolerances.size() == 2U &&
            failure.tolerances[0].quantity_id ==
                contract::kCycleMeanTorqueResidualNmQuantityId &&
            failure.tolerances[1].quantity_id ==
                contract::kBoundaryPressureResidualPaQuantityId &&
            std::bit_cast<std::uint64_t>(failure.tolerances[0].tolerance) ==
                std::bit_cast<std::uint64_t>(
                    preparation.cycle_mean_torque_tolerance_nm.value) &&
            std::bit_cast<std::uint64_t>(failure.tolerances[1].tolerance) ==
                std::bit_cast<std::uint64_t>(preparation.pressure_tolerance_pa.value) &&
            failure.state_summary.find("torque-residual-binary64=") !=
                std::string::npos &&
            failure.state_summary.find("pressure-residual-binary64=") !=
                std::string::npos &&
            failure.state_summary.find("block-a-first-ordinal=") != std::string::npos,
        "nonconverged failure discarded stable residual/block evidence");
    expect(contract::validate(failure).ok(),
           "runtime produced an invalid typed nonconvergence failure");
}

void test_insufficient_cycles_have_distinct_failure_without_residuals() {
    auto value = fixture(1.0e9, 1.0e12, 100U);
    const auto plan = capture_plan(value);
    auto operating = operating_runtime(value, plan);
    auto core = core_runtime(value);

    contract::FailureContext failure;
    while (operating.accepted_sample_count() < operating.fixed_cutoff_frame_count()) {
        auto core_result = core.advance();
        const auto &step =
            std::get<simulation::LowOrderEngineCoreV1StepView>(core_result);
        auto result = operating.advance(step.mechanics.get(), step.gas.get());
        if (const auto *actual = std::get_if<contract::FailureContext>(&result)) {
            failure = *actual;
            break;
        }
    }
    expect(failure.kind == contract::FailureKind::preparation_not_converged &&
               failure.detail_code ==
                   contract::kPreparationInsufficientCyclesDetailCode &&
               failure.tolerances.empty() && contract::validate(failure).ok(),
           "insufficient-cycle cutoff did not produce the distinct auditable "
           "preparation failure");
}

void run_tests() {
    test_capture_plan_transplants_are_rejected();
    test_scenario_fuel_afr_is_exactly_bound_to_core();
    test_capture_reports_absent_instantaneous_models_truthfully();
    test_runtime_rejects_foreign_controls_and_shape();
    test_complete_cycle_evidence_reaches_public_result();
    test_nonconvergence_retains_exact_residual_diagnostics();
    test_insufficient_cycles_have_distinct_failure_without_residuals();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "low-order operating runtime test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
