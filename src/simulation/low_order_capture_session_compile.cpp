#include "simulation/low_order_capture_session.hpp"

#include "simulation/low_order_capture_buffer.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <optional>
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

} // namespace

LowOrderCaptureCompileResult compile_low_order_capture_session(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const contract::RandomPlan &random_plan,
    const contract::Sha256Digest &simulation_request_identity_v7_sha256,
    LowOrderExecutionExtent execution_extent) {
    ValidationReport report;
    const auto *operating_profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    const auto *inertial = std::get_if<contract::InertialDyno>(&scenario.mode);
    const auto *prescribed =
        std::get_if<contract::PrescribedKinematicSweep>(&scenario.mode);
    const auto *free_engine = std::get_if<contract::FreeEngine>(&scenario.mode);
    const auto *held_dyno = std::get_if<contract::HeldDyno>(&scenario.mode);
    const auto *free_vehicle = std::get_if<contract::FreeVehicle>(&scenario.mode);
    require(report, operating_profile != nullptr, ContractIssueCode::unsupported_value,
            "engine.physics_profile",
            "low-order capture requires the operating-point physics profile");
    require(report, execution_extent.valid(), ContractIssueCode::invalid_value,
            "execution_extent",
            "low-order capture session requires a valid execution extent");
    require(report,
            !execution_extent.is_open_ended() || free_engine != nullptr ||
                held_dyno != nullptr || free_vehicle != nullptr,
            ContractIssueCode::unsupported_value, "execution_extent",
            "open-ended low-order capture sessions are admitted only for FreeEngine, "
            "HeldDyno, or FreeVehicle");
    if (!report.ok()) {
        return report;
    }

    auto capture_plan_result =
        compile_low_order_capture_plan(engine, scenario, execution_extent);
    if (auto *capture_report = std::get_if<ValidationReport>(&capture_plan_result)) {
        for (auto &issue : capture_report->issues) {
            report.issues.push_back(std::move(issue));
        }
    }
    if (!report.ok()) {
        return report;
    }
    auto capture_plan = std::get<LowOrderCapturePlan>(std::move(capture_plan_result));

    std::optional<LowOrderCaptureSession::ProfilePolicy> profile_policy;
    const contract::LowOrderEngineCoreV1 *core =
        operating_profile != nullptr ? &operating_profile->core : nullptr;
    if (core == nullptr) {
        report.add(ContractIssueCode::unsupported_value, "engine.physics_profile",
                   "low-order capture could not select one engine core");
        return report;
    }
    auto mechanism_plan_result = compile_mechanism_kinematics_plan(engine, *core);
    if (const auto *mechanism_report =
            std::get_if<ValidationReport>(&mechanism_plan_result)) {
        return *mechanism_report;
    }
    auto mechanism_plan =
        std::get<SharedMechanismKinematicsPlan>(std::move(mechanism_plan_result));
    if (operating_profile != nullptr) {
        if (prescribed != nullptr) {
            auto prescribed_result = compile_low_order_prescribed_kinematic_runtime(
                engine, scenario, execution_extent);
            if (auto *prescribed_report =
                    std::get_if<ValidationReport>(&prescribed_result)) {
                return std::move(*prescribed_report);
            }
            profile_policy.emplace(
                std::in_place_type<LowOrderPrescribedKinematicRuntime>,
                std::get<LowOrderPrescribedKinematicRuntime>(
                    std::move(prescribed_result)));
        } else if (inertial != nullptr) {
            auto inertial_result = compile_low_order_inertial_dyno_v1_runtime(
                engine, scenario, capture_plan, simulation_request_identity_v7_sha256);
            if (auto *inertial_report =
                    std::get_if<ValidationReport>(&inertial_result)) {
                return std::move(*inertial_report);
            }
            profile_policy.emplace(
                std::in_place_type<LowOrderInertialDynoV1Runtime>,
                std::get<LowOrderInertialDynoV1Runtime>(std::move(inertial_result)));
        } else if (free_engine != nullptr || held_dyno != nullptr ||
                   free_vehicle != nullptr) {
            auto dynamic_crank_result = compile_low_order_dynamic_crank_runtime(
                engine, scenario, capture_plan, simulation_request_identity_v7_sha256,
                mechanism_plan, execution_extent);
            if (auto *dynamic_crank_report =
                    std::get_if<ValidationReport>(&dynamic_crank_result)) {
                return std::move(*dynamic_crank_report);
            }
            profile_policy.emplace(
                std::in_place_type<LowOrderDynamicCrankRuntime>,
                std::get<LowOrderDynamicCrankRuntime>(std::move(dynamic_crank_result)));
        } else {
            auto operating_result = compile_low_order_operating_point_v1_runtime(
                engine, scenario, capture_plan, simulation_request_identity_v7_sha256);
            if (auto *operating_report =
                    std::get_if<ValidationReport>(&operating_result)) {
                return std::move(*operating_report);
            }
            profile_policy.emplace(
                std::in_place_type<LowOrderOperatingPointV1Runtime>,
                std::get<LowOrderOperatingPointV1Runtime>(std::move(operating_result)));
        }
    }
    if (!profile_policy.has_value()) {
        report.add(ContractIssueCode::unsupported_value, "engine.physics_profile",
                   "low-order capture could not select one exclusive profile "
                   "policy");
        return report;
    }

    auto core_result = compile_low_order_engine_core_v1_runtime(
        engine, scenario, *core, random_plan, std::move(mechanism_plan),
        execution_extent);
    if (const auto *core_report = std::get_if<ValidationReport>(&core_result)) {
        return *core_report;
    }
    auto core_runtime = std::get<LowOrderEngineCoreV1Runtime>(std::move(core_result));

    detail::LowOrderCaptureBuffer capture{std::move(capture_plan.capture_buffer)};
    return LowOrderCaptureSession{
        std::move(core_runtime),
        std::move(*profile_policy),
        std::move(capture),
        scenario.rates.capture,
        execution_extent,
        engine.methods.gas_exchange.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace crankwave::simulation
