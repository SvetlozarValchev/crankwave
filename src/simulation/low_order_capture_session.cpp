#include "simulation/low_order_capture_session.hpp"

#include "contract/capture_block_admission.hpp"
#include "simulation/low_order_capture_buffer.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace engine_sim_offline::simulation {

LowOrderCaptureSession::LowOrderCaptureSession(
    LowOrderEngineCoreV1Runtime core, ProfilePolicy profile_policy,
    detail::LowOrderCaptureBuffer capture, contract::RationalRateHz rate,
    std::uint64_t expected_samples, std::string model_id, std::string profile_id,
    std::string scenario_id, contract::EngineId engine_id)
    : core_(std::move(core)), profile_policy_(std::move(profile_policy)),
      capture_(std::make_unique<detail::LowOrderCaptureBuffer>(std::move(capture))),
      rate_(rate), expected_samples_(expected_samples), model_id_(std::move(model_id)),
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
        static_cast<double>(step_end_index) * static_cast<double>(rate_.denominator) /
            static_cast<double>(rate_.numerator),
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
    return publish_next_block_impl(consumer, nullptr);
}

LowOrderCaptureAdvanceResult LowOrderCaptureSession::publish_next_block(
    const LowOrderCaptureBlockConsumer &consumer,
    const detail::LowOrderLiveControlProvider &live_controls) {
    return publish_next_block_impl(consumer, &live_controls);
}

LowOrderCaptureAdvanceResult LowOrderCaptureSession::publish_next_block_impl(
    const LowOrderCaptureBlockConsumer &consumer,
    const detail::LowOrderLiveControlProvider *live_controls) {
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
    if (live_controls != nullptr &&
        (live_controls->context == nullptr ||
         live_controls->drain_for_physics_step == nullptr)) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-live-control-provider-invalid",
                          "live-control provider is incomplete"));
    }
    if (live_controls != nullptr &&
        live_controls->physics_rate != rate_) {
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-live-control-rate-mismatch",
                          "live-control physics rate differs from the admitted "
                          "capture physics rate"));
    }
    if (published_sample_count_ >= expected_samples_) {
        if (published_sample_count_ != expected_samples_ ||
            core_.produced_sample_count() != expected_samples_ || !core_.completed()) {
            return fail(fault(
                contract::FailureKind::contract_violation,
                "low-order-capture-completion-count-mismatch",
                "mechanics, gas, and published capture counts diverged at completion"));
        }
        std::optional<contract::HeldSpeedOperatingPointResult> operating_point;
        std::optional<contract::InertialDynoResult> inertial_dyno;
        if (const auto *policy =
                std::get_if<LowOrderOperatingPointV1Runtime>(&profile_policy_)) {
            if (policy->accepted_sample_count() != expected_samples_ ||
                !policy->finalized() || policy->faulted() ||
                !policy->operating_point_result().has_value()) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "low-order-operating-policy-completion-disagreed",
                    "held-speed policy did not finish the exact capture horizon with "
                    "one fixed-sample operating-point result"));
            }
            operating_point = *policy->operating_point_result();
        } else if (const auto *policy =
                       std::get_if<LowOrderInertialDynoV1Runtime>(&profile_policy_)) {
            if (policy->accepted_sample_count() != expected_samples_ ||
                !policy->finalized() || policy->faulted() ||
                !policy->inertial_dyno_result().has_value()) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "low-order-inertial-policy-completion-disagreed",
                    "inertial-dyno policy did not finish the exact capture horizon "
                    "with one typed result"));
            }
            inertial_dyno = *policy->inertial_dyno_result();
        } else if (const auto *policy =
                       std::get_if<LowOrderFreeEngineV1Runtime>(&profile_policy_)) {
            if (policy->accepted_sample_count() != expected_samples_ ||
                !policy->finalized() || policy->faulted()) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "low-order-free-engine-policy-completion-disagreed",
                    "free-engine policy did not finish the exact capture horizon"));
            }
        }
        terminal_completion_ = LowOrderCaptureCompleted{
            published_sample_count_,
            published_block_count_,
            std::move(operating_point),
            std::move(inertial_dyno),
        };
        return *terminal_completion_;
    }

    capture_->begin_block(published_sample_count_);
    const LegacyMechanismStep *last_mechanics = nullptr;
    for (std::uint32_t frame = 0; frame < capture_->block_capacity_frames(); ++frame) {
        LiveControlOverrides live_overrides;
        if (live_controls != nullptr) {
            const auto drained = live_controls->drain_for_physics_step(
                live_controls->context, core_.produced_sample_count());
            if (!drained.valid) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "low-order-live-control-step-mismatch",
                    "live-control timeline did not resolve the next contiguous "
                    "physics step"));
            }
            live_overrides = drained.overrides;
            if (live_overrides.any() &&
                std::holds_alternative<LowOrderOperatingPointV1Runtime>(
                    profile_policy_)) {
                return fail(fault(
                    contract::FailureKind::contract_violation,
                    "low-order-live-controls-not-admitted-for-held-evidence",
                    "held-speed operating-point evidence does not admit live "
                    "throttle, ignition, fuel, limiter, or external-resistance "
                    "overrides"));
            }
        }

        const LegacyMechanismStep *mechanics_pointer = nullptr;
        const LegacyLowOrderGasStep *gas_pointer = nullptr;
        std::optional<contract::TorqueTelemetry> motion_policy_capture_torque;
        if (auto *inertial =
                std::get_if<LowOrderInertialDynoV1Runtime>(&profile_policy_)) {
            auto result = inertial->advance(core_, live_overrides);
            if (const auto *failure =
                    std::get_if<contract::FailureContext>(&result)) {
                return fail(*failure);
            }
            if (const auto *completed =
                    std::get_if<LowOrderEngineCoreV1Completed>(&result)) {
                if (completed->sample_count != expected_samples_ ||
                    published_sample_count_ + capture_->frame_count() !=
                        expected_samples_) {
                    return fail(fault(
                        contract::FailureKind::contract_violation,
                        "low-order-core-premature-completion",
                        "inertial mechanics completed before the admitted capture "
                        "horizon"));
                }
                break;
            }
            const auto &step = std::get<LowOrderInertialDynoV1StepView>(result);
            mechanics_pointer = &step.mechanics.get();
            gas_pointer = &step.gas.get();
            motion_policy_capture_torque = step.capture_torque;
        } else if (auto *free_engine =
                       std::get_if<LowOrderFreeEngineV1Runtime>(&profile_policy_)) {
            auto result = free_engine->advance(core_, live_overrides);
            if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
                return fail(*failure);
            }
            if (const auto *completed =
                    std::get_if<LowOrderEngineCoreV1Completed>(&result)) {
                if (completed->sample_count != expected_samples_ ||
                    published_sample_count_ + capture_->frame_count() !=
                        expected_samples_) {
                    return fail(fault(
                        contract::FailureKind::contract_violation,
                        "low-order-core-premature-completion",
                        "free-engine mechanics completed before the admitted "
                        "capture horizon"));
                }
                break;
            }
            const auto &step = std::get<LowOrderFreeEngineV1StepView>(result);
            mechanics_pointer = &step.mechanics.get();
            gas_pointer = &step.gas.get();
            motion_policy_capture_torque = step.capture_torque;
        } else {
            auto core_result = core_.advance(live_overrides);
            if (const auto *failure =
                    std::get_if<contract::FailureContext>(&core_result)) {
                return fail(*failure);
            }
            if (const auto *completed =
                    std::get_if<LowOrderEngineCoreV1Completed>(&core_result)) {
                if (completed->sample_count != expected_samples_ ||
                    published_sample_count_ + capture_->frame_count() !=
                        expected_samples_) {
                    return fail(fault(
                        contract::FailureKind::contract_violation,
                        "low-order-core-premature-completion",
                        "mechanics completed before the admitted capture horizon"));
                }
                break;
            }
            const auto &core_step = std::get<LowOrderEngineCoreV1StepView>(core_result);
            mechanics_pointer = &core_step.mechanics.get();
            gas_pointer = &core_step.gas.get();
        }

        if (mechanics_pointer == nullptr || gas_pointer == nullptr) {
            return fail(fault(contract::FailureKind::contract_violation,
                              "low-order-policy-step-missing",
                              "active simulation policy produced no committed core "
                              "transaction"));
        }
        const auto &mechanics = *mechanics_pointer;
        const auto &gas = *gas_pointer;
        last_mechanics = &mechanics;
        std::variant<contract::TorqueTelemetry, contract::FailureContext>
            torque_evaluation;
        if (motion_policy_capture_torque.has_value()) {
            torque_evaluation = *motion_policy_capture_torque;
        } else {
            torque_evaluation = std::visit(
                [&](auto &policy) -> std::variant<contract::TorqueTelemetry,
                                                  contract::FailureContext> {
                    using Policy = std::decay_t<decltype(policy)>;
                    if constexpr (std::is_same_v<Policy,
                                                 LowOrderOperatingPointV1Runtime>) {
                        auto evaluated = policy.advance(mechanics, gas);
                        if (const auto *step =
                                std::get_if<LowOrderOperatingPointV1Step>(&evaluated)) {
                            return step->capture_torque;
                        }
                        return std::get<contract::FailureContext>(std::move(evaluated));
                    } else if constexpr (std::is_same_v<
                                             Policy,
                                             LowOrderInertialDynoV1Runtime>) {
                        return fault(contract::FailureKind::contract_violation,
                                     "low-order-inertial-policy-double-advanced",
                                     "inertial torque policy was invoked twice for "
                                     "one core transaction",
                                     &mechanics);
                    } else {
                        return fault(contract::FailureKind::contract_violation,
                                     "low-order-free-engine-policy-double-advanced",
                                     "free-engine torque policy was invoked twice for "
                                     "one core transaction",
                                     &mechanics);
                    }
                },
                profile_policy_);
        }
        if (auto *failure = std::get_if<contract::FailureContext>(&torque_evaluation)) {
            return fail(std::move(*failure));
        }
        const auto &torque = std::get<contract::TorqueTelemetry>(torque_evaluation);
        if (auto buffer_failure = capture_->append(mechanics, gas, torque);
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
    if (!contract::detail::valid_capture_block_after_layout_admission(block)) {
        // The buffer owns an immutable layout validated while the capture plan is
        // compiled. Keep the valid block path allocation-free, but construct the
        // public owning diagnostic after any block-varying invariant fails.
        const auto report = contract::validate(block);
        const auto state_summary =
            report.ok()
                ? std::string{
                      "allocation-free admitted-layout validation rejected the "
                      "capture block without a public diagnostic"}
                : "path=" + report.issues.front().path + "; " +
                      report.issues.front().message;
        return fail(fault(contract::FailureKind::contract_violation,
                          "low-order-capture-block-invalid",
                          state_summary, last_mechanics));
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
