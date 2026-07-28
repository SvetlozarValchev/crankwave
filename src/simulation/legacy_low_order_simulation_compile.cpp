#include "simulation/legacy_low_order_simulation.hpp"

#include "simulation/low_order_capture_buffer.hpp"
#include "simulation/low_order_capture_plan.hpp"

#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

constexpr contract::Sha256Digest kM3FixedRateRpmConfigurationSha256{{
    0xc6, 0x4a, 0xb8, 0xb9, 0xc2, 0xf8, 0xc7, 0x8a, 0x15, 0x12, 0x22,
    0xd8, 0x89, 0x86, 0x52, 0x69, 0xbe, 0x19, 0xcc, 0x52, 0x1e, 0x68,
    0x52, 0xc4, 0x6d, 0xdf, 0x34, 0x50, 0x86, 0x9e, 0x75, 0xe4,
}};

[[nodiscard]] bool exact_m3_fixed_rate_rpm_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method) noexcept {
    return method.value.id == "fixed-rate-post-step-rpm-binary64-v1" &&
           method.value.version == 1U &&
           method.value.configuration_sha256 == kM3FixedRateRpmConfigurationSha256;
}

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

} // namespace

LegacySimulationCompileResult
compile_legacy_low_order_simulation_session(const contract::EngineSpec &engine,
                                            const contract::RenderScenario &scenario) {
    ValidationReport report;
    const auto *profile =
        std::get_if<contract::LegacyLowOrderV1Profile>(&engine.physics_profile);
    const auto *sweep = std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    require(report, profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "legacy capture requires a LegacyLowOrderV1Profile");
    require(report, sweep != nullptr, ContractIssueCode::unsupported_value,
            "scenario.mode", "M3 legacy capture requires a prescribed kinematic sweep");
    if (sweep != nullptr) {
        require(report,
                exact_m3_fixed_rate_rpm_method(sweep->trajectory.kinematic_resolution),
                ContractIssueCode::unsupported_value,
                "scenario.mode.trajectory.kinematic_resolution",
                "M3 legacy capture requires its exact fixed-rate RPM method "
                "configuration");
    }
    if (!report.ok() || profile == nullptr || sweep == nullptr) {
        return report;
    }

    std::optional<LegacyFixedCrankTorqueAccountingPlan> torque_accounting;
    auto accounting_result =
        compile_legacy_fixed_crank_torque_accounting(engine, profile->fixed_crank_loss);
    if (auto *accounting_report = std::get_if<ValidationReport>(&accounting_result)) {
        for (auto &issue : accounting_report->issues) {
            report.issues.push_back(std::move(issue));
        }
    } else {
        torque_accounting = std::get<LegacyFixedCrankTorqueAccountingPlan>(
            std::move(accounting_result));
    }

    require(report,
            scenario.rates.physics == contract::RationalRateHz{10000, 1} &&
                scenario.rates.capture == contract::RationalRateHz{10000, 1},
            ContractIssueCode::unsupported_value, "scenario.rates",
            "legacy capture requires exact 10000/1 Hz physics and capture clocks");
    if (!report.ok()) {
        return report;
    }

    auto capture_plan_result = compile_low_order_capture_plan(engine, scenario);
    if (auto *capture_report = std::get_if<ValidationReport>(&capture_plan_result)) {
        for (auto &issue : capture_report->issues) {
            report.issues.push_back(std::move(issue));
        }
    }
    if (!report.ok() || !torque_accounting.has_value()) {
        return report;
    }
    auto capture_plan = std::get<LowOrderCapturePlan>(std::move(capture_plan_result));

    auto core_result =
        compile_low_order_engine_core_v1_runtime(engine, scenario, profile->core);
    if (const auto *core_report = std::get_if<ValidationReport>(&core_result)) {
        return *core_report;
    }
    auto core_runtime = std::get<LowOrderEngineCoreV1Runtime>(std::move(core_result));

    detail::LowOrderCaptureBuffer capture{std::move(capture_plan.capture_buffer)};
    return LegacyLowOrderSimulationSession{
        std::move(core_runtime),
        *torque_accounting,
        std::move(capture),
        capture_plan.capture_horizon_frames,
        engine.methods.gas_exchange.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
