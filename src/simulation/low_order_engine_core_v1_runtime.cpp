#include "simulation/low_order_engine_core_v1_runtime.hpp"

#include "simulation/kinematic_scenario_schedule.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"

#include <string>
#include <utility>

namespace engine_sim_offline::simulation {

LowOrderEngineCoreV1Runtime::LowOrderEngineCoreV1Runtime(
    LegacyLowOrderMechanicsSession mechanics, LegacyLowOrderGasSession gas,
    std::uint64_t expected_sample_count, contract::RationalRateHz rate,
    std::string model_id, std::string profile_id, std::string scenario_id,
    contract::EngineId engine_id)
    : mechanics_(std::move(mechanics)), gas_(std::move(gas)),
      expected_sample_count_(expected_sample_count), rate_(rate),
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id) {}

contract::FailureContext
LowOrderEngineCoreV1Runtime::fault(std::string detail_code, std::string state_summary,
                                   const LegacyMechanismStep *mechanics) const {
    const auto sample_index =
        mechanics != nullptr ? mechanics->sample_index : gas_.produced_sample_count();
    const auto step_end_index =
        mechanics != nullptr ? mechanics->step_end_index : gas_.produced_sample_count();
    const double time_s = rate_.numerator == 0
                              ? 0.0
                              : static_cast<double>(step_end_index) *
                                    static_cast<double>(rate_.denominator) /
                                    static_cast<double>(rate_.numerator);
    return {
        contract::FailureKind::contract_violation,
        std::move(detail_code),
        model_id_,
        profile_id_,
        sample_index,
        step_end_index,
        time_s,
        mechanics != nullptr ? mechanics->theta_unwrapped_rad : 0.0,
        engine_id_,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "scenario=" + scenario_id_ + "; " + std::move(state_summary),
        "none; simulation terminated without fallback",
        {},
    };
}

LowOrderEngineCoreV1AdvanceResult
LowOrderEngineCoreV1Runtime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

LowOrderEngineCoreV1AdvanceResult LowOrderEngineCoreV1Runtime::advance() {
    return advance_with_motion(std::nullopt);
}

LowOrderEngineCoreV1AdvanceResult
LowOrderEngineCoreV1Runtime::advance(PostStepCrankMotion motion) {
    return advance_with_motion(motion);
}

LowOrderEngineCoreV1AdvanceResult LowOrderEngineCoreV1Runtime::advance_with_motion(
    std::optional<PostStepCrankMotion> motion) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    const auto committed_sample_count = gas_.produced_sample_count();
    if (committed_sample_count > expected_sample_count_) {
        return fail(fault("low-order-core-count-exceeds-horizon",
                          "gas count exceeded the compiled core horizon"));
    }
    if (committed_sample_count == expected_sample_count_) {
        if (!mechanics_.completed()) {
            return fail(fault(
                "low-order-core-completion-state-mismatch",
                "compiled horizon ended before mechanics reached stable completion"));
        }
        return LowOrderEngineCoreV1Completed{committed_sample_count};
    }

    auto mechanics_result =
        motion.has_value() ? mechanics_.advance(*motion) : mechanics_.advance();
    if (const auto *failure =
            std::get_if<contract::FailureContext>(&mechanics_result)) {
        return fail(*failure);
    }
    if (const auto *completion =
            std::get_if<LegacyMechanicsCompleted>(&mechanics_result)) {
        if (completion->sample_count != committed_sample_count ||
            committed_sample_count != expected_sample_count_) {
            return fail(fault("low-order-core-premature-completion",
                              "mechanics completed before the compiled core horizon"));
        }
        return LowOrderEngineCoreV1Completed{completion->sample_count};
    }

    const auto &mechanics =
        std::get<std::reference_wrapper<const LegacyMechanismStep>>(mechanics_result)
            .get();
    if (mechanics.sample_index != committed_sample_count ||
        mechanics.step_end_index != committed_sample_count + 1U) {
        return fail(fault("low-order-core-mechanics-count-mismatch",
                          "mechanics did not advance exactly one core transaction",
                          &mechanics));
    }
    auto gas_result = gas_.advance(mechanics);
    if (const auto *failure = std::get_if<contract::FailureContext>(&gas_result)) {
        return fail(*failure);
    }
    const auto &gas =
        std::get<std::reference_wrapper<const LegacyLowOrderGasStep>>(gas_result).get();
    if (mechanics.timestamp_tick != mechanics.step_end_index ||
        mechanics.rate != gas.rate || mechanics.sample_index != gas.sample_index ||
        mechanics.step_end_index != gas.step_end_index ||
        mechanics.timestamp_tick != gas.timestamp_tick ||
        gas_.produced_sample_count() != committed_sample_count + 1U) {
        return fail(fault(
            "low-order-core-step-pair-mismatch",
            "mechanics and gas did not publish one coherent post-step transaction",
            &mechanics));
    }
    return LowOrderEngineCoreV1StepView{
        std::cref(mechanics),
        std::cref(gas),
    };
}

bool LowOrderEngineCoreV1Runtime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderEngineCoreV1Runtime::completed() const noexcept {
    return !faulted() && mechanics_.completed() &&
           gas_.produced_sample_count() == expected_sample_count_;
}

std::uint64_t LowOrderEngineCoreV1Runtime::produced_sample_count() const noexcept {
    return gas_.produced_sample_count();
}

std::uint64_t LowOrderEngineCoreV1Runtime::expected_sample_count() const noexcept {
    return expected_sample_count_;
}

LowOrderEngineCoreV1CompileResult
compile_low_order_engine_core_v1_runtime(const contract::EngineSpec &engine,
                                         const contract::RenderScenario &scenario,
                                         const contract::LowOrderEngineCoreV1 &core,
                                         const contract::RandomPlan &random_plan) {
    std::optional<KinematicScenarioSchedule> kinematic_schedule;
    std::optional<ScenarioControlSchedule> control_schedule;
    if (std::holds_alternative<contract::InertialDyno>(scenario.mode)) {
        auto result = compile_scenario_control_schedule(scenario);
        if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
            return *report;
        }
        control_schedule.emplace(std::get<ScenarioControlSchedule>(std::move(result)));
    } else {
        auto result = compile_kinematic_scenario_schedule(scenario);
        if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
            return *report;
        }
        kinematic_schedule.emplace(
            std::get<KinematicScenarioSchedule>(std::move(result)));
        control_schedule.emplace(kinematic_schedule->control_schedule());
    }

    auto mechanics_result =
        kinematic_schedule.has_value()
            ? detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
                  engine, core, scenario, *kinematic_schedule)
            : detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
                  engine, core, scenario, *control_schedule);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&mechanics_result)) {
        return *report;
    }
    auto mechanics =
        std::get<LegacyLowOrderMechanicsSession>(std::move(mechanics_result));

    auto gas_result = detail::LowOrderEngineCoreV1RuntimeFactory::compile_gas(
        engine, core, scenario, random_plan, *control_schedule,
        mechanics.cylinder_models());
    if (const auto *report = std::get_if<contract::ValidationReport>(&gas_result)) {
        return *report;
    }
    auto gas = std::get<LegacyLowOrderGasSession>(std::move(gas_result));

    return LowOrderEngineCoreV1Runtime{
        std::move(mechanics),
        std::move(gas),
        control_schedule->sample_count(),
        control_schedule->rate(),
        engine.methods.gas_exchange.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
