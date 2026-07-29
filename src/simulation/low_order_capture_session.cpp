#include "simulation/low_order_capture_session.hpp"

#include "simulation/low_order_capture_buffer.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {

LowOrderCaptureSession::LowOrderCaptureSession(
    LowOrderEngineCoreV1Runtime core,
    LegacyFixedCrankTorqueAccountingPlan torque_accounting,
    detail::LowOrderCaptureBuffer capture, std::uint64_t expected_samples,
    std::string model_id, std::string profile_id, std::string scenario_id,
    contract::EngineId engine_id)
    : core_(std::move(core)), torque_accounting_(torque_accounting),
      capture_(std::make_unique<detail::LowOrderCaptureBuffer>(std::move(capture))),
      expected_samples_(expected_samples), model_id_(std::move(model_id)),
      profile_id_(std::move(profile_id)), scenario_id_(std::move(scenario_id)),
      engine_id_(engine_id) {}

LowOrderCaptureSession::LowOrderCaptureSession(LowOrderCaptureSession &&) noexcept =
    default;

LowOrderCaptureSession &
LowOrderCaptureSession::operator=(LowOrderCaptureSession &&) noexcept = default;

LowOrderCaptureSession::~LowOrderCaptureSession() = default;

contract::FailureContext
LowOrderCaptureSession::fault(contract::FailureKind kind, std::string detail_code,
                              std::string state_summary,
                              const LegacyMechanismStep *mechanics) const {
    const std::uint64_t sample_index =
        mechanics != nullptr ? mechanics->sample_index : published_sample_count_;
    const std::uint64_t step_end_index =
        mechanics != nullptr ? mechanics->step_end_index : published_sample_count_;
    return {
        kind,
        std::move(detail_code),
        model_id_,
        profile_id_,
        sample_index,
        step_end_index,
        static_cast<double>(step_end_index) / 10000.0,
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

LowOrderCaptureAdvanceResult
LowOrderCaptureSession::fail(contract::FailureContext failure) {
    if (!terminal_fault_.has_value()) {
        terminal_fault_ = std::move(failure);
    }
    return *terminal_fault_;
}

LowOrderCaptureAdvanceResult LowOrderCaptureSession::publish_next_block(
    const LowOrderCaptureBlockConsumer &consumer) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (terminal_completion_.has_value()) {
        return *terminal_completion_;
    }
    if (consumer_callback_active_) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-consumer-reentrant",
                          "capture consumer re-entered its session while a borrowed "
                          "view was active"));
    }
    if (!consumer) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-consumer-missing",
                          "capture publication requires a synchronous consumer"));
    }
    if (published_sample_count_ >= expected_samples_) {
        if (published_sample_count_ != expected_samples_ ||
            core_.produced_sample_count() != expected_samples_ || !core_.completed()) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "low-order-capture-completion-count-mismatch",
                "mechanics, gas, and published capture counts diverged at completion"));
        }
        terminal_completion_ = LowOrderCaptureCompleted{
            published_sample_count_,
            published_block_count_,
        };
        return *terminal_completion_;
    }

    capture_->begin_block(published_sample_count_);
    const LegacyMechanismStep *last_mechanics = nullptr;
    for (std::uint32_t frame = 0; frame < capture_->block_capacity_frames(); ++frame) {
        auto core_result = core_.advance();
        if (const auto *failure = std::get_if<contract::FailureContext>(&core_result)) {
            return fail(*failure);
        }
        if (const auto *completed =
                std::get_if<LowOrderEngineCoreV1Completed>(&core_result)) {
            if (completed->sample_count != expected_samples_ ||
                published_sample_count_ + capture_->frame_count() !=
                    expected_samples_) {
                return fail(
                    fault(contract::FailureKind::contract_violation,
                          "low-order-core-premature-completion",
                          "mechanics completed before the admitted capture horizon"));
            }
            break;
        }

        const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
        const auto &mechanics = core_step.mechanics.get();
        const auto &gas = core_step.gas.get();
        last_mechanics = &mechanics;
        const auto torque_evaluation = evaluate_legacy_fixed_crank_torque_accounting(
            torque_accounting_, mechanics.angular_speed_rad_s,
            gas.indicated_gas_torque_nm);
        const auto *torque = std::get_if<contract::TorqueTelemetry>(&torque_evaluation);
        if (torque == nullptr) {
            return fail(fault(contract::FailureKind::numerical_failure,
                              "legacy-fixed-crank-torque-accounting-failed",
                              "M3 torque accountant produced no finite telemetry",
                              &mechanics));
        }
        if (auto buffer_failure = capture_->append(mechanics, gas, *torque);
            buffer_failure.has_value()) {
            auto failure =
                fault(buffer_failure->kind, std::move(buffer_failure->detail_code),
                      std::move(buffer_failure->state_summary), &mechanics);
            failure.cylinder_id = buffer_failure->cylinder_id;
            failure.port_id = buffer_failure->port_id;
            failure.gas_volume_id = buffer_failure->gas_volume_id;
            failure.flow_edge_id = buffer_failure->flow_edge_id;
            failure.route_id = buffer_failure->route_id;
            return fail(std::move(failure));
        }

        if (published_sample_count_ + capture_->frame_count() == expected_samples_) {
            break;
        }
    }

    if (capture_->frame_count() == 0U) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-empty-block",
                          "active simulation produced no capture frames"));
    }

    const auto block = capture_->view();
    const auto report = contract::validate(block);
    if (!report.ok()) {
        const auto &issue = report.issues.front();
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-block-invalid",
                          "path=" + issue.path + "; " + issue.message, last_mechanics));
    }

    bool accepted = false;
    consumer_callback_active_ = true;
    try {
        accepted = consumer(block);
    } catch (const std::exception &exception) {
        consumer_callback_active_ = false;
        if (terminal_fault_.has_value()) {
            return *terminal_fault_;
        }
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-consumer-threw",
                          "capture consumer threw: " + std::string{exception.what()},
                          last_mechanics));
    } catch (...) {
        consumer_callback_active_ = false;
        if (terminal_fault_.has_value()) {
            return *terminal_fault_;
        }
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-consumer-threw",
                          "capture consumer threw a non-standard exception",
                          last_mechanics));
    }
    consumer_callback_active_ = false;
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (!accepted) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-consumer-rejected",
                          "capture consumer rejected a complete validated block",
                          last_mechanics));
    }

    const LowOrderCaptureBlockPublished published{
        published_block_count_,
        capture_->first_sample_index(),
        capture_->frame_count(),
        published_sample_count_ + capture_->frame_count(),
    };
    published_sample_count_ = published.published_sample_count;
    ++published_block_count_;
    return published;
}

bool LowOrderCaptureSession::faulted() const noexcept {
    return terminal_fault_.has_value();
}

bool LowOrderCaptureSession::completed() const noexcept {
    return terminal_completion_.has_value();
}

std::uint64_t LowOrderCaptureSession::published_sample_count() const noexcept {
    return published_sample_count_;
}

std::uint64_t LowOrderCaptureSession::published_block_count() const noexcept {
    return published_block_count_;
}

} // namespace engine_sim_offline::simulation
