#include "simulation/low_order_prescribed_kinematic_runtime.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace crankwave::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] contract::TorqueValueNm
available_indicated_gas_torque(double value_nm) noexcept {
    return {
        value_nm,
        contract::Availability::available,
        contract::Completeness::complete,
        contract::QuantityUnavailableReason::none,
        contract::indicated_gas_torque_term_mask(),
        0,
    };
}

[[nodiscard]] contract::TorqueValueNm
unavailable_torque(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
        0,
        0,
    };
}

[[nodiscard]] contract::QuantityValue
unavailable_quantity(contract::QuantityUnavailableReason reason) noexcept {
    return {
        0.0,
        contract::Availability::unavailable,
        contract::Completeness::incomplete,
        reason,
    };
}

[[nodiscard]] contract::TorqueTelemetry
prescribed_capture_torque(double indicated_gas_torque_nm) noexcept {
    contract::TorqueTelemetry result;
    result.instantaneous_indicated_gas =
        available_indicated_gas_torque(indicated_gas_torque_nm);
    result.pumping_partition =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.friction_pump_and_accessory =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.starter =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.instantaneous_net_shaft =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.cycle_mean_net_shaft = unavailable_torque(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.actuator =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.dyno_reaction =
        unavailable_torque(contract::QuantityUnavailableReason::model_not_admitted);
    result.cycle_work_j = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.net_bmep_pa = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    result.instantaneous_power_w =
        unavailable_quantity(contract::QuantityUnavailableReason::model_not_admitted);
    result.cycle_mean_power_w = unavailable_quantity(
        contract::QuantityUnavailableReason::cycle_integration_not_admitted);
    return result;
}

} // namespace

LowOrderPrescribedKinematicRuntime::LowOrderPrescribedKinematicRuntime(
    contract::RationalRateHz rate, std::uint64_t expected_sample_count,
    std::string model_id, std::string profile_id, std::string scenario_id,
    contract::EngineId engine_id)
    : rate_(rate), expected_sample_count_(expected_sample_count),
      model_id_(std::move(model_id)), profile_id_(std::move(profile_id)),
      scenario_id_(std::move(scenario_id)), engine_id_(engine_id) {}

contract::FailureContext LowOrderPrescribedKinematicRuntime::fault(
    contract::FailureKind kind, std::string detail_code, std::string state_summary,
    const LegacyMechanismStep *mechanics) const {
    const auto sample_index =
        mechanics != nullptr ? mechanics->sample_index : accepted_sample_count_;
    const auto step_end_index =
        mechanics != nullptr ? mechanics->step_end_index : accepted_sample_count_;
    const double time_s = rate_.numerator == 0
                              ? 0.0
                              : static_cast<double>(step_end_index) *
                                    static_cast<double>(rate_.denominator) /
                                    static_cast<double>(rate_.numerator);
    return {
        kind,
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
        "none; prescribed-kinematic capture terminated without fallback",
        {},
    };
}

LowOrderPrescribedKinematicAdvanceResult
LowOrderPrescribedKinematicRuntime::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

std::optional<contract::FailureContext>
LowOrderPrescribedKinematicRuntime::reject_live_overrides(
    const LiveControlOverrides &overrides) {
    if (!overrides.any()) {
        return std::nullopt;
    }
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = fault(
            contract::FailureKind::contract_violation,
            "prescribed-kinematic-live-controls-not-admitted",
            "finite prescribed motion does not admit live throttle, ignition, fuel, "
            "limiter, starter, resistance, dyno, gear, clutch, or brake overrides");
    }
    return terminal_fault_;
}

LowOrderPrescribedKinematicAdvanceResult
LowOrderPrescribedKinematicRuntime::advance(const LegacyMechanismStep &mechanics,
                                            const LegacyLowOrderGasStep &gas) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (accepted_sample_count_ >= expected_sample_count_) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "prescribed-kinematic-step-after-horizon",
                          "prescribed policy received a transaction after its "
                          "finite capture horizon",
                          &mechanics));
    }
    if (mechanics.sample_index != accepted_sample_count_ ||
        mechanics.step_end_index != accepted_sample_count_ + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        gas.sample_index != mechanics.sample_index ||
        gas.step_end_index != mechanics.step_end_index ||
        gas.timestamp_tick != mechanics.timestamp_tick || mechanics.rate != rate_ ||
        gas.rate != rate_) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "prescribed-kinematic-step-clock-disagreed",
                          "prescribed policy received a noncontiguous or mismatched "
                          "mechanics+gas transaction",
                          &mechanics));
    }
    if (!std::isfinite(gas.indicated_gas_torque_nm)) {
        return fail(fault(contract::FailureKind::numerical_failure,
                          "prescribed-kinematic-indicated-torque-nonfinite",
                          "gas transaction contains nonfinite aggregate "
                          "indicated-gas torque",
                          &mechanics));
    }

    ++accepted_sample_count_;
    return LowOrderPrescribedKinematicStep{
        prescribed_capture_torque(gas.indicated_gas_torque_nm),
    };
}

bool LowOrderPrescribedKinematicRuntime::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderPrescribedKinematicRuntime::finalized() const noexcept {
    return !faulted() && expected_sample_count_ > 0U &&
           accepted_sample_count_ == expected_sample_count_;
}

std::uint64_t
LowOrderPrescribedKinematicRuntime::accepted_sample_count() const noexcept {
    return accepted_sample_count_;
}

LowOrderPrescribedKinematicCompileResult compile_low_order_prescribed_kinematic_runtime(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    LowOrderExecutionExtent execution_extent) {
    ValidationReport report;
    report.append(contract::validate_for_engine(scenario, engine));
    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *prescribed =
        std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    const auto finite_sample_count = execution_extent.finite_physics_frame_count();

    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "prescribed kinematic runtime requires the low-order physics profile");
    require(report, prescribed != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode",
            "prescribed kinematic runtime requires prescribed-kinematic-sweep mode");
    require(report, finite_sample_count.has_value() && *finite_sample_count > 0U,
            ContractIssueCode::unsupported_value, "execution_extent",
            "prescribed kinematic runtime requires a positive finite extent");
    require(report, scenario.rates.physics == scenario.rates.capture,
            ContractIssueCode::inconsistent_semantics, "scenario.rates",
            "prescribed kinematic runtime requires identical physics and capture "
            "clocks");
    if (!report.ok() || profile == nullptr || prescribed == nullptr ||
        !finite_sample_count.has_value()) {
        return report;
    }

    return LowOrderPrescribedKinematicRuntime{
        scenario.rates.physics,
        *finite_sample_count,
        engine.methods.gas_exchange.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace crankwave::simulation
