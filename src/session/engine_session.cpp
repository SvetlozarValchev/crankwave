#include "engine_sim_offline/session.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "presentation/presentation_audio_session.hpp"
#include "session/control_timeline.hpp"
#include "session/session_build.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline {
namespace {

inline constexpr std::uint64_t kMaximumSessionBlockCount =
    std::numeric_limits<std::uint64_t>::max() / kEngineSessionDeliveryFramesPerBlock;

inline constexpr std::array kSourceRouteAudioBusKinds{
    EngineAudioBusKind::source_route_dry,
    EngineAudioBusKind::source_route_configured_transfer,
    EngineAudioBusKind::source_route_selected,
};

[[nodiscard]] EngineAudioSignalDisposition
source_route_signal_disposition(contract::RouteDisposition disposition) {
    switch (disposition) {
    case contract::RouteDisposition::rendered:
        return EngineAudioSignalDisposition::active;
    case contract::RouteDisposition::declared_silent:
        return EngineAudioSignalDisposition::declared_silent;
    case contract::RouteDisposition::unspecified:
    case contract::RouteDisposition::not_applicable:
        throw std::logic_error{
            "published source-route bus requires an executable signal disposition"};
    }
    throw std::logic_error{"source-route signal disposition is unknown"};
}

[[nodiscard]] EngineSessionError processing_error(
    std::string detail_code, std::string message,
    std::optional<contract::FailureContext> simulation_failure = std::nullopt) {
    return {
        EngineSessionErrorCode::processing_failed,
        std::move(detail_code),
        std::move(message),
        std::move(simulation_failure),
    };
}

[[nodiscard]] EngineControlRejectionCode
public_control_error(session::ControlTimelineError error) noexcept {
    using Internal = session::ControlTimelineError;
    switch (error) {
    case Internal::capacity_exceeded:
        return EngineControlRejectionCode::capacity_exceeded;
    case Internal::late_command:
        return EngineControlRejectionCode::late_command;
    case Internal::invalid_payload:
        return EngineControlRejectionCode::invalid_payload;
    case Internal::unordered_delivery_frame:
        return EngineControlRejectionCode::unordered_delivery_frame;
    case Internal::duplicate_sequence:
        return EngineControlRejectionCode::duplicate_sequence;
    case Internal::unordered_sequence:
        return EngineControlRejectionCode::unordered_sequence;
    case Internal::none:
    case Internal::invalid_rate:
    case Internal::clock_overflow:
    case Internal::delivery_cursor_regression:
    case Internal::noncontiguous_physics_step:
        return EngineControlRejectionCode::internal_clock_error;
    }
    return EngineControlRejectionCode::internal_clock_error;
}

[[nodiscard]] std::string control_error_message(session::ControlTimelineError error) {
    using Internal = session::ControlTimelineError;
    switch (error) {
    case Internal::capacity_exceeded:
        return "the bounded caller control-command queue is full";
    case Internal::late_command:
        return "the command targets audio or physics already generated";
    case Internal::invalid_payload:
        return "the command payload is outside the executable control range";
    case Internal::unordered_delivery_frame:
        return "command delivery-frame timestamps must be nondecreasing";
    case Internal::duplicate_sequence:
        return "command sequence numbers must be unique";
    case Internal::unordered_sequence:
        return "command sequence numbers must be strictly increasing";
    case Internal::none:
        return {};
    case Internal::invalid_rate:
    case Internal::clock_overflow:
    case Internal::delivery_cursor_regression:
    case Internal::noncontiguous_physics_step:
        return "the session control clock rejected an internal transition";
    }
    return "the session control clock rejected an unknown transition";
}

[[nodiscard]] EngineLiveControlCapabilityMask
live_control_capabilities(const contract::ScenarioMode &mode,
                          const contract::EngineSpec &engine) noexcept {
    const auto starter_capability = [&engine]() noexcept {
        const auto *profile = std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &engine.physics_profile);
        return profile != nullptr &&
               profile->starter.type.value == contract::StarterCapabilityType::cranking;
    };
    if (std::holds_alternative<contract::FreeEngine>(mode)) {
        auto result = kEngineLiveControlCapabilityThrottle |
                      kEngineLiveControlCapabilityIgnitionEnabled |
                      kEngineLiveControlCapabilityFuelEnabled |
                      kEngineLiveControlCapabilityLimiterEnabled |
                      kEngineLiveControlCapabilityExternalResistingTorque;
        if (starter_capability()) {
            result |= kEngineLiveControlCapabilityStarterEnabled;
        }
        return result;
    }
    if (std::holds_alternative<contract::InertialDyno>(mode)) {
        return kEngineLiveControlCapabilityThrottle |
               kEngineLiveControlCapabilityIgnitionEnabled |
               kEngineLiveControlCapabilityFuelEnabled;
    }
    if (std::holds_alternative<contract::HeldDyno>(mode)) {
        return kEngineLiveControlCapabilityThrottle |
               kEngineLiveControlCapabilityIgnitionEnabled |
               kEngineLiveControlCapabilityFuelEnabled |
               kEngineLiveControlCapabilityHeldDynoTargetEngineSpeed |
               kEngineLiveControlCapabilityHeldDynoMaximumAbsorbingTorque |
               kEngineLiveControlCapabilityHeldDynoMaximumDrivingTorque;
    }
    if (const auto *vehicle = std::get_if<contract::FreeVehicle>(&mode)) {
        auto result = kEngineLiveControlCapabilityThrottle |
                      kEngineLiveControlCapabilityIgnitionEnabled |
                      kEngineLiveControlCapabilityFuelEnabled |
                      kEngineLiveControlCapabilityLimiterEnabled |
                      kEngineLiveControlCapabilityVehicleSelectedForwardGear |
                      kEngineLiveControlCapabilityVehicleClutchEngagement;
        if (starter_capability()) {
            result |= kEngineLiveControlCapabilityStarterEnabled;
        }
        if (vehicle->rig.vehicle.maximum_service_brake_force_n.has_value() &&
            vehicle->rig.vehicle.maximum_service_brake_force_n->value > 0.0) {
            result |= kEngineLiveControlCapabilityVehicleServiceBrakeApplication;
        }
        return result;
    }
    return 0U;
}

[[nodiscard]] EngineMotionMode
motion_mode(const contract::ScenarioMode &mode) noexcept {
    return std::visit(
        [](const auto &value) noexcept {
            using Mode = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Mode, contract::HeldSpeed>) {
                return EngineMotionMode::held_speed;
            } else if constexpr (std::is_same_v<Mode,
                                                contract::PrescribedKinematicSweep>) {
                return EngineMotionMode::prescribed_kinematic_sweep;
            } else if constexpr (std::is_same_v<Mode, contract::HeldDyno>) {
                return EngineMotionMode::held_dyno;
            } else if constexpr (std::is_same_v<Mode,
                                                contract::LoadTargetHeldCapture>) {
                return EngineMotionMode::load_target_held_capture;
            } else if constexpr (std::is_same_v<Mode, contract::InertialDyno>) {
                return EngineMotionMode::inertial_dyno;
            } else if constexpr (std::is_same_v<Mode, contract::FreeEngine>) {
                return EngineMotionMode::free_engine;
            } else {
                static_assert(std::is_same_v<Mode, contract::FreeVehicle>);
                return EngineMotionMode::free_vehicle;
            }
        },
        mode);
}

[[nodiscard]] EngineLiveControlCapabilityMask
required_capability(const EngineControlPayload &payload) noexcept {
    return std::visit(
        [](const auto &value) noexcept -> EngineLiveControlCapabilityMask {
            using Payload = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Payload, SetEngineThrottle>) {
                return kEngineLiveControlCapabilityThrottle;
            } else if constexpr (std::is_same_v<Payload, SetEngineIgnitionEnabled>) {
                return kEngineLiveControlCapabilityIgnitionEnabled;
            } else if constexpr (std::is_same_v<Payload, SetEngineFuelEnabled>) {
                return kEngineLiveControlCapabilityFuelEnabled;
            } else if constexpr (std::is_same_v<Payload, SetEngineStarterEnabled>) {
                return kEngineLiveControlCapabilityStarterEnabled;
            } else if constexpr (std::is_same_v<Payload, SetEngineLimiterEnabled>) {
                return kEngineLiveControlCapabilityLimiterEnabled;
            } else if constexpr (std::is_same_v<Payload,
                                                SetEngineExternalResistingTorque>) {
                return kEngineLiveControlCapabilityExternalResistingTorque;
            } else if constexpr (std::is_same_v<Payload,
                                                SetHeldDynoTargetEngineSpeed>) {
                return kEngineLiveControlCapabilityHeldDynoTargetEngineSpeed;
            } else if constexpr (std::is_same_v<Payload,
                                                SetHeldDynoMaximumAbsorbingTorque>) {
                return kEngineLiveControlCapabilityHeldDynoMaximumAbsorbingTorque;
            } else if constexpr (std::is_same_v<Payload,
                                                SetHeldDynoMaximumDrivingTorque>) {
                return kEngineLiveControlCapabilityHeldDynoMaximumDrivingTorque;
            } else if constexpr (std::is_same_v<Payload,
                                                SetVehicleSelectedForwardGear>) {
                return kEngineLiveControlCapabilityVehicleSelectedForwardGear;
            } else if constexpr (std::is_same_v<Payload, SetVehicleClutchEngagement>) {
                return kEngineLiveControlCapabilityVehicleClutchEngagement;
            } else {
                static_assert(
                    std::is_same_v<Payload, SetVehicleServiceBrakeApplication>);
                return kEngineLiveControlCapabilityVehicleServiceBrakeApplication;
            }
        },
        payload);
}

[[nodiscard]] EngineHeldDynoDisposition public_dyno_disposition(
    simulation::detail::BoundedDynoConstraintDisposition disposition) noexcept {
    using Internal = simulation::detail::BoundedDynoConstraintDisposition;
    switch (disposition) {
    case Internal::tracking:
        return EngineHeldDynoDisposition::tracking;
    case Internal::absorbing_torque_limited:
        return EngineHeldDynoDisposition::absorbing_torque_limited;
    case Internal::driving_torque_limited:
        return EngineHeldDynoDisposition::driving_torque_limited;
    }
    return EngineHeldDynoDisposition::tracking;
}

[[nodiscard]] EngineClutchDisposition public_clutch_disposition(
    simulation::detail::BoundedClutchCouplingDisposition disposition) noexcept {
    using Internal = simulation::detail::BoundedClutchCouplingDisposition;
    switch (disposition) {
    case Internal::neutral:
        return EngineClutchDisposition::neutral;
    case Internal::disengaged:
        return EngineClutchDisposition::disengaged;
    case Internal::engine_driving_torque_limited:
        return EngineClutchDisposition::engine_driving_torque_limited;
    case Internal::vehicle_backdrive_torque_limited:
        return EngineClutchDisposition::vehicle_backdrive_torque_limited;
    case Internal::tracking:
        return EngineClutchDisposition::tracking;
    }
    return EngineClutchDisposition::neutral;
}

[[nodiscard]] EngineRoadLoadDisposition public_road_load_disposition(
    simulation::detail::ForwardVehicleRoadLoadDisposition disposition) noexcept {
    using Internal = simulation::detail::ForwardVehicleRoadLoadDisposition;
    switch (disposition) {
    case Internal::moving:
        return EngineRoadLoadDisposition::moving;
    case Internal::stopped_within_step:
        return EngineRoadLoadDisposition::stopped_within_step;
    case Internal::held_at_rest:
        return EngineRoadLoadDisposition::held_at_rest;
    }
    return EngineRoadLoadDisposition::held_at_rest;
}

} // namespace

EngineSessionBlockView::EngineSessionBlockView(
    const std::uint64_t block_ordinal, const EngineSessionBlockPhase phase,
    const std::uint64_t first_physics_frame, const std::uint32_t physics_frame_count,
    const std::uint64_t first_delivery_frame, const std::uint32_t delivery_frame_count,
    const std::span<const EngineAudioBusBlockView> audio_buses,
    const std::span<const EngineTelemetryFrame> telemetry) noexcept
    : block_ordinal_(block_ordinal), phase_(phase),
      first_physics_frame_(first_physics_frame),
      physics_frame_count_(physics_frame_count),
      first_delivery_frame_(first_delivery_frame),
      delivery_frame_count_(delivery_frame_count), audio_buses_(audio_buses),
      telemetry_(telemetry) {}

std::uint64_t EngineSessionBlockView::block_ordinal() const noexcept {
    return block_ordinal_;
}

EngineSessionBlockPhase EngineSessionBlockView::phase() const noexcept {
    return phase_;
}

std::uint64_t EngineSessionBlockView::first_physics_frame() const noexcept {
    return first_physics_frame_;
}

std::uint32_t EngineSessionBlockView::physics_frame_count() const noexcept {
    return physics_frame_count_;
}

std::uint64_t EngineSessionBlockView::first_delivery_frame() const noexcept {
    return first_delivery_frame_;
}

std::uint32_t EngineSessionBlockView::delivery_frame_count() const noexcept {
    return delivery_frame_count_;
}

std::span<const EngineAudioBusBlockView>
EngineSessionBlockView::audio_buses() const noexcept {
    return audio_buses_;
}

std::span<const EngineTelemetryFrame>
EngineSessionBlockView::telemetry() const noexcept {
    return telemetry_;
}

class EngineSession::Implementation final {
  public:
    explicit Implementation(session_detail::BuiltSessionComponents components)
        : compiled_scenario_(std::move(components.compiled_scenario)),
          execution_kind_(components.execution_kind),
          simulation_request_identity_(components.simulation_request_identity),
          random_plan_(std::move(components.random_plan)),
          calibration_(std::move(components.calibration)),
          simulation_(std::move(components.simulation)),
          excitation_(std::move(components.excitation)),
          presentation_(std::move(components.presentation)),
          capacities_(compiled_scenario_.session_capacities()),
          physics_rate_(calibration_.capture_rate()),
          delivery_rate_(
              compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario_)
                  .scenario.scenario.rates.delivery),
          physics_frames_per_block_(
              static_cast<std::uint32_t>(calibration_.capture_frames_per_block())),
          control_timeline_(capacities_.control_command_queue_capacity, physics_rate_,
                            delivery_rate_),
          control_scratch_(capacities_.control_command_queue_capacity),
          total_block_count_(execution_kind_ ==
                                     EngineSessionExecutionKind::finite_scenario
                                 ? calibration_.total_block_count()
                                 : 0U),
          preparation_block_count_(calibration_.pre_audible_block_count()) {
        const auto inputs =
            compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario_);
        engine_id_ = inputs.engine.engine.engine_id.value;
        scenario_id_ = inputs.scenario.scenario.scenario_id;
        live_control_capabilities_ = live_control_capabilities(
            inputs.scenario.scenario.mode, inputs.engine.engine);
        motion_mode_ = motion_mode(inputs.scenario.scenario.mode);
        if (const auto *vehicle =
                std::get_if<contract::FreeVehicle>(&inputs.scenario.scenario.mode)) {
            forward_gear_descriptors_.reserve(vehicle->rig.transmission.gears.size());
            for (const auto &gear : vehicle->rig.transmission.gears) {
                forward_gear_descriptors_.push_back(
                    {gear.id, gear.authored_ordinal.value, gear.semantic_id.value,
                     gear.ratio.value});
            }
        }
        build_audio_bus_descriptors(inputs);
    }

    [[nodiscard]] EngineSessionDescriptor descriptor() const noexcept {
        return {
            engine_id_,
            scenario_id_,
            capacities_,
            physics_rate_,
            delivery_rate_,
            physics_frames_per_block_,
            delivery_frames_per_block_,
            total_block_count_,
            preparation_block_count_,
            audio_bus_descriptors_,
            live_control_capabilities_,
            execution_kind_,
            motion_mode_,
            forward_gear_descriptors_,
        };
    }

    [[nodiscard]] std::optional<EngineControlRejection>
    enqueue_controls(std::span<const EngineControlCommand> commands) {
        if (commands.empty()) {
            return EngineControlRejection{
                EngineControlRejectionCode::invalid_payload,
                0U,
                "a live-control batch must contain at least one command",
            };
        }
        if (terminal_error_.has_value() || terminal_completion_.has_value()) {
            return EngineControlRejection{
                EngineControlRejectionCode::session_terminal,
                0U,
                "live controls cannot be queued after the session reaches a "
                "terminal state",
            };
        }
        if (commands.size() > control_scratch_.size()) {
            return EngineControlRejection{
                EngineControlRejectionCode::capacity_exceeded,
                0U,
                "the submitted batch exceeds the complete bounded command queue",
            };
        }
        const auto first_live_delivery_frame =
            preparation_block_count_ * delivery_frames_per_block_;
        for (std::size_t index = 0; index < commands.size(); ++index) {
            const auto &source = commands[index];
            if ((live_control_capabilities_ & required_capability(source.payload)) ==
                0U) {
                return EngineControlRejection{
                    EngineControlRejectionCode::unsupported_for_operating_mode,
                    index,
                    "the requested live control is unavailable for this "
                    "operating mode",
                };
            }
            if (const auto *gear =
                    std::get_if<SetVehicleSelectedForwardGear>(&source.payload);
                gear != nullptr &&
                gear->forward_gear_ordinal > forward_gear_descriptors_.size()) {
                return EngineControlRejection{
                    EngineControlRejectionCode::invalid_payload,
                    index,
                    "the selected forward-gear ordinal is outside the session "
                    "inventory",
                };
            }
            if (source.delivery_frame < first_live_delivery_frame) {
                return EngineControlRejection{
                    EngineControlRejectionCode::unavailable_during_preparation,
                    index,
                    "live controls cannot replace the authored pre-audible state",
                };
            }
            const auto projection = session::project_delivery_frame_to_physics_step(
                source.delivery_frame, physics_rate_, delivery_rate_);
            if (!projection) {
                return EngineControlRejection{
                    EngineControlRejectionCode::internal_clock_error,
                    index,
                    "the live-control timestamp cannot be projected to the "
                    "session physics clock",
                };
            }
            const auto terminal_physics_frame =
                execution_kind_ == EngineSessionExecutionKind::finite_scenario
                    ? total_block_count_ * physics_frames_per_block_
                    : kMaximumSessionBlockCount * physics_frames_per_block_;
            if (projection.physics_step >= terminal_physics_frame) {
                return EngineControlRejection{
                    EngineControlRejectionCode::outside_session_horizon,
                    index,
                    "live controls must target a physics step the session can "
                    "generate",
                };
            }
            auto &destination = control_scratch_[index];
            destination.delivery_frame = source.delivery_frame;
            destination.sequence = source.sequence;
            destination.payload = std::visit(
                [](const auto &payload) -> session::LiveControlPayload {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<Payload, SetEngineThrottle>) {
                        return session::SetThrottle{payload.throttle_01};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetEngineIgnitionEnabled>) {
                        return session::SetIgnitionEnabled{payload.enabled};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetEngineFuelEnabled>) {
                        return session::SetFuelEnabled{payload.enabled};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetEngineStarterEnabled>) {
                        return session::SetStarterEnabled{payload.enabled};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetEngineLimiterEnabled>) {
                        return session::SetLimiterEnabled{payload.enabled};
                    } else if constexpr (std::is_same_v<
                                             Payload,
                                             SetEngineExternalResistingTorque>) {
                        return session::SetExternalResistingTorque{payload.torque_nm};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetHeldDynoTargetEngineSpeed>) {
                        return session::SetDynoTargetEngineSpeed{
                            payload.engine_speed_rpm};
                    } else if constexpr (std::is_same_v<
                                             Payload,
                                             SetHeldDynoMaximumAbsorbingTorque>) {
                        return session::SetDynoMaximumAbsorbingTorque{
                            payload.torque_nm};
                    } else if constexpr (std::is_same_v<
                                             Payload,
                                             SetHeldDynoMaximumDrivingTorque>) {
                        return session::SetDynoMaximumDrivingTorque{payload.torque_nm};
                    } else if constexpr (std::is_same_v<
                                             Payload, SetVehicleSelectedForwardGear>) {
                        return session::SetVehicleSelectedForwardGear{
                            payload.forward_gear_ordinal};
                    } else if constexpr (std::is_same_v<Payload,
                                                        SetVehicleClutchEngagement>) {
                        return session::SetVehicleClutchEngagement{
                            payload.engagement_01};
                    } else {
                        static_assert(
                            std::is_same_v<Payload, SetVehicleServiceBrakeApplication>);
                        return session::SetVehicleServiceBrakeApplication{
                            payload.application_01};
                    }
                },
                source.payload);
        }
        const auto result = control_timeline_.enqueue(
            std::span<const session::TimestampedControlCommand>{control_scratch_.data(),
                                                                commands.size()});
        if (result) {
            has_accepted_live_controls_ = true;
            return std::nullopt;
        }
        return EngineControlRejection{
            public_control_error(result.error),
            result.command_index == session::kNoCommandIndex ? 0U
                                                             : result.command_index,
            control_error_message(result.error),
        };
    }

    [[nodiscard]] EngineSessionProcessResult process_block() {
        if (terminal_error_.has_value()) {
            return *terminal_error_;
        }
        if (terminal_completion_.has_value()) {
            return *terminal_completion_;
        }

        try {
            const auto expected_block = simulation_.published_block_count();
            if (expected_block >= kMaximumSessionBlockCount) {
                return fail({
                    EngineSessionErrorCode::resource_exhausted,
                    "session-clock-exhausted",
                    "the open-ended session exhausted its delivery-frame clock",
                    std::nullopt,
                });
            }
            const auto expected_first_physics =
                expected_block * physics_frames_per_block_;
            const auto expected_first_delivery =
                expected_block * delivery_frames_per_block_;
            std::optional<contract::FailureContext> nested_failure;
            std::optional<presentation::PresentationAudioBlockView> audio;
            const simulation::detail::LowOrderLiveControlProvider live_controls{
                &control_timeline_,
                control_timeline_.physics_rate(),
                [](void *context, std::uint64_t physics_step) noexcept {
                    const auto drained =
                        static_cast<session::ControlTimeline *>(context)
                            ->drain_for_physics_step(physics_step);
                    return simulation::detail::LowOrderLiveControlStep{
                        static_cast<bool>(drained),
                        drained.controls.overrides,
                    };
                },
            };

            auto simulation_result = simulation_.publish_next_block(
                [&](const contract::CaptureBlockView &capture) -> bool {
                    if (capture.frame_count() != physics_frames_per_block_ ||
                        capture.clock().first_sample_index != expected_first_physics ||
                        capture.engine().empty()) {
                        return false;
                    }
                    const auto &last = capture.engine().back();
                    telemetry_[0] = {
                        last.step_end_index,
                        last,
                        std::nullopt,
                        std::nullopt,
                    };

                    auto excitation_result = excitation_.process_block(
                        capture,
                        [&](const presentation::ExhaustExcitationBlockView
                                &excitation_block,
                            const excitation::IntakePressureBlockView &intake_pressure,
                            const excitation::ExhaustExcitationDiagnosticBlockView &)
                            -> bool {
                            const auto intake_route_ids = intake_pressure.route_ids();
                            const auto intake_values =
                                intake_pressure.pressure_pa_abs();
                            const auto presentation_intake = presentation::
                                IntakePressureInputBlockView::borrow_for_callback(
                                    intake_pressure.first_frame_index(),
                                    intake_pressure.sample_rate(), intake_route_ids,
                                    intake_pressure.frame_count(), intake_values);
                            audio.emplace(presentation_->process(excitation_block,
                                                                 presentation_intake));
                            return true;
                        });
                    if (auto *failure =
                            std::get_if<contract::FailureContext>(&excitation_result)) {
                        nested_failure = std::move(*failure);
                        return false;
                    }
                    return true;
                },
                live_controls);

            if (nested_failure.has_value()) {
                return fail(processing_error(
                    "session-audio-pipeline-failed",
                    "excitation or presentation rejected the current physics block",
                    std::move(nested_failure)));
            }
            if (auto *failure =
                    std::get_if<contract::FailureContext>(&simulation_result)) {
                return fail(processing_error(
                    "session-simulation-failed",
                    "simulation rejected the current physics block", *failure));
            }
            if (auto *completed = std::get_if<simulation::LowOrderCaptureCompleted>(
                    &simulation_result)) {
                if (execution_kind_ == EngineSessionExecutionKind::open_ended) {
                    return fail(processing_error(
                        "open-session-completed-unexpectedly",
                        "an open-ended simulation reported finite completion"));
                }
                return complete(*completed);
            }

            const auto &published =
                std::get<simulation::LowOrderCaptureBlockPublished>(simulation_result);
            if (!audio.has_value() || published.block_ordinal != expected_block ||
                published.first_sample_index != expected_first_physics ||
                published.frame_count != physics_frames_per_block_ ||
                published.published_sample_count !=
                    expected_first_physics + physics_frames_per_block_ ||
                audio->first_input_frame_index() != expected_first_physics ||
                audio->first_source_frame_index() != expected_first_delivery ||
                audio->frame_count() != delivery_frames_per_block_) {
                return fail(processing_error(
                    "session-block-extent-disagreed",
                    "simulation, excitation, and presentation block clocks "
                    "diverged"));
            }

            telemetry_[0].held_dyno.reset();
            telemetry_[0].free_vehicle.reset();
            if (const auto dyno = simulation_.held_dyno_state(); dyno.has_value()) {
                const auto &torque = telemetry_[0].engine.torque;
                if (torque.actuator.availability != contract::Availability::available ||
                    torque.dyno_reaction.availability !=
                        contract::Availability::available ||
                    std::bit_cast<std::uint64_t>(torque.actuator.value_nm) !=
                        std::bit_cast<std::uint64_t>(
                            dyno->applied_actuator_torque_nm) ||
                    std::bit_cast<std::uint64_t>(torque.dyno_reaction.value_nm) !=
                        std::bit_cast<std::uint64_t>(
                            -dyno->applied_actuator_torque_nm)) {
                    return fail(processing_error(
                        "session-held-dyno-telemetry-disagreed",
                        "held-dyno sidecar and common torque telemetry describe "
                        "different committed actuator values"));
                }
                telemetry_[0].held_dyno = EngineHeldDynoTelemetry{
                    dyno->target_engine_speed_rpm,
                    dyno->maximum_absorbing_torque_nm,
                    dyno->maximum_driving_torque_nm,
                    dyno->required_actuator_torque_nm,
                    dyno->applied_actuator_torque_nm,
                    public_dyno_disposition(dyno->disposition),
                };
            }
            if (const auto vehicle = simulation_.free_vehicle_state();
                vehicle.has_value()) {
                if (std::bit_cast<std::uint64_t>(vehicle->engine_speed_rpm) !=
                    std::bit_cast<std::uint64_t>(
                        telemetry_[0].engine.engine_speed_rpm)) {
                    return fail(processing_error(
                        "session-free-vehicle-telemetry-disagreed",
                        "free-vehicle sidecar and common engine telemetry describe "
                        "different committed crank speeds"));
                }
                telemetry_[0].free_vehicle = EngineFreeVehicleTelemetry{
                    vehicle->vehicle_speed_m_s,
                    vehicle->vehicle_distance_m,
                    vehicle->selected_forward_gear_ordinal,
                    vehicle->clutch_engagement_01,
                    vehicle->service_brake_application_01,
                    public_clutch_disposition(vehicle->clutch_disposition),
                    vehicle->clutch_torque_capacity_nm,
                    vehicle->applied_average_clutch_torque_on_engine_nm,
                    vehicle->final_clutch_slip_rad_s,
                    public_road_load_disposition(vehicle->road_load_disposition),
                    vehicle->requested_road_load_force_n,
                    vehicle->applied_average_road_load_force_n,
                };
            }

            bind_audio_bus_views(*audio);
            const auto cursor_error = control_timeline_.advance_delivery_cursor(
                expected_first_delivery + delivery_frames_per_block_);
            if (cursor_error != session::ControlTimelineError::none) {
                return fail(processing_error("session-control-cursor-failed",
                                             control_error_message(cursor_error)));
            }

            const auto phase = expected_block < preparation_block_count_
                                   ? EngineSessionBlockPhase::preparation
                                   : EngineSessionBlockPhase::audible;
            return EngineSessionBlockView{
                expected_block,          phase,
                expected_first_physics,  physics_frames_per_block_,
                expected_first_delivery, delivery_frames_per_block_,
                audio_bus_views_,        telemetry_,
            };
        } catch (const std::bad_alloc &) {
            return fail({
                EngineSessionErrorCode::resource_exhausted,
                "session-process-resource-exhausted",
                "session processing exhausted memory",
                std::nullopt,
            });
        } catch (const std::exception &error) {
            return fail(processing_error("session-process-threw", error.what()));
        } catch (...) {
            return fail(
                processing_error("session-process-threw",
                                 "session processing threw a non-standard exception"));
        }
    }

  private:
    void build_audio_bus_descriptors(
        const compile::detail::CompiledScenarioInputsView &inputs) {
        const auto route_count = calibration_.route_count();
        const auto bus_count = route_count * 3U + 2U;
        audio_bus_ids_.reserve(bus_count);

        for (const auto &route : calibration_.routes()) {
            const auto engine_route =
                std::ranges::find(inputs.engine.engine.routes, route.route_id(),
                                  &contract::RouteSpec::id);
            if (engine_route == inputs.engine.engine.routes.end()) {
                throw std::logic_error{
                    "presentation route is absent from the compiled engine"};
            }
            const auto requirement =
                std::ranges::find(inputs.scenario.source_matrix.required_source_routes,
                                  engine_route->semantic_id.value,
                                  &contract::SourceRouteRequirement::semantic_id);
            if (requirement ==
                    inputs.scenario.source_matrix.required_source_routes.end() ||
                requirement->kind != engine_route->kind.value ||
                requirement->artifact_roles.size() != 3U) {
                throw std::logic_error{
                    "presentation route lacks three ordered public signal roles"};
            }
            for (const auto &role : requirement->artifact_roles) {
                audio_bus_ids_.push_back(role);
            }
        }

        const contract::OutputBusRequirement *raw = nullptr;
        const contract::OutputBusRequirement *audition = nullptr;
        for (const auto &bus : inputs.scenario.source_matrix.required_output_buses) {
            if (bus.kind == contract::OutputBusKind::master_engine_raw) {
                raw = &bus;
            } else if (bus.kind == contract::OutputBusKind::master_engine_audition) {
                audition = &bus;
            }
        }
        if (raw == nullptr || audition == nullptr) {
            throw std::logic_error{
                "compiled source policy lacks raw and audition master buses"};
        }
        audio_bus_ids_.push_back(raw->semantic_id);
        audio_bus_ids_.push_back(audition->semantic_id);

        audio_bus_descriptors_.reserve(bus_count);
        for (std::size_t route = 0; route < route_count; ++route) {
            const auto route_id = calibration_.routes()[route].route_id();
            const auto engine_route = std::ranges::find(
                inputs.engine.engine.routes, route_id, &contract::RouteSpec::id);
            if (engine_route == inputs.engine.engine.routes.end()) {
                throw std::logic_error{
                    "presentation route is absent from the compiled engine"};
            }
            const auto requirement =
                std::ranges::find(inputs.scenario.source_matrix.required_source_routes,
                                  engine_route->semantic_id.value,
                                  &contract::SourceRouteRequirement::semantic_id);
            if (requirement ==
                inputs.scenario.source_matrix.required_source_routes.end()) {
                throw std::logic_error{
                    "presentation route lacks a public source requirement"};
            }
            const auto base = route * 3U;
            for (std::size_t stem = 0; stem < kSourceRouteAudioBusKinds.size();
                 ++stem) {
                audio_bus_descriptors_.push_back(
                    {audio_bus_ids_[base + stem], kSourceRouteAudioBusKinds[stem],
                     engine_route->kind.value, route_id,
                     source_route_signal_disposition(requirement->disposition)});
            }
        }
        audio_bus_descriptors_.push_back(
            {audio_bus_ids_[bus_count - 2U], EngineAudioBusKind::engine_raw_master,
             contract::SourceRouteKind::unspecified, std::nullopt,
             EngineAudioSignalDisposition::active});
        audio_bus_descriptors_.push_back(
            {audio_bus_ids_[bus_count - 1U], EngineAudioBusKind::engine_audition_master,
             contract::SourceRouteKind::unspecified, std::nullopt,
             EngineAudioSignalDisposition::active});
        audio_bus_views_.resize(bus_count);
    }

    void bind_audio_bus_views(const presentation::PresentationAudioBlockView &audio) {
        std::size_t bus = 0;
        for (std::size_t route = 0; route < audio.route_count(); ++route) {
            using Role = presentation::PresentationAudioStemRole;
            for (const auto role :
                 std::array{Role::dry, Role::configured_transfer, Role::selected}) {
                audio_bus_views_[bus] = {
                    audio_bus_descriptors_[bus],
                    audio.route_stem(route, role),
                };
                ++bus;
            }
        }
        audio_bus_views_[bus] = {
            audio_bus_descriptors_[bus],
            audio.raw_master(),
        };
        ++bus;
        audio_bus_views_[bus] = {
            audio_bus_descriptors_[bus],
            audio.audition_master(),
        };
    }

    [[nodiscard]] EngineSessionProcessResult fail(EngineSessionError error) {
        if (!terminal_error_.has_value()) {
            terminal_error_ = std::move(error);
        }
        return *terminal_error_;
    }

    [[nodiscard]] EngineSessionProcessResult
    complete(const simulation::LowOrderCaptureCompleted &completed) {
        if (execution_kind_ != EngineSessionExecutionKind::finite_scenario) {
            return fail(processing_error(
                "open-session-completion-invalid",
                "only a finite-scenario session may publish completion evidence"));
        }
        const auto expected_physics = total_block_count_ * physics_frames_per_block_;
        if (completed.sample_count != expected_physics ||
            completed.block_count != total_block_count_ ||
            simulation_.published_sample_count() != expected_physics ||
            excitation_.next_frame_index() != expected_physics ||
            presentation_->next_input_frame_index() != expected_physics ||
            presentation_->next_source_frame_index() !=
                total_block_count_ * delivery_frames_per_block_) {
            return fail(processing_error(
                "session-completion-count-disagreed",
                "pipeline stages did not complete one common fixed horizon"));
        }

        const auto inputs =
            compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario_);
        if (!has_accepted_live_controls_ &&
            completed.held_speed_operating_point.has_value()) {
            const auto report = contract::validate(
                *completed.held_speed_operating_point, inputs.scenario.scenario,
                inputs.engine.engine, simulation_request_identity_);
            if (!report.ok()) {
                return fail(processing_error(
                    "session-held-result-invalid",
                    "held-speed completion evidence failed request validation"));
            }
        }
        if (!has_accepted_live_controls_ && completed.inertial_dyno.has_value()) {
            const auto report =
                contract::validate(*completed.inertial_dyno, inputs.scenario.scenario,
                                   simulation_request_identity_);
            if (!report.ok()) {
                return fail(processing_error(
                    "session-inertial-result-invalid",
                    "inertial-dyno completion evidence failed request validation"));
            }
        }

        terminal_completion_ = EngineSessionCompleted{
            expected_physics,
            total_block_count_ * delivery_frames_per_block_,
            total_block_count_,
            has_accepted_live_controls_,
            has_accepted_live_controls_
                ? std::optional<contract::HeldSpeedOperatingPointResult>{}
                : completed.held_speed_operating_point,
            has_accepted_live_controls_ ? std::optional<contract::InertialDynoResult>{}
                                        : completed.inertial_dyno,
        };
        return *terminal_completion_;
    }

    compile::CompiledScenario compiled_scenario_;
    EngineSessionExecutionKind execution_kind_ =
        EngineSessionExecutionKind::finite_scenario;
    contract::Sha256Digest simulation_request_identity_;
    contract::RandomPlan random_plan_;
    presentation::AdmittedPresentationCalibration calibration_;
    simulation::LowOrderCaptureSession simulation_;
    excitation::CapturedGasSourceExcitationSession excitation_;
    std::unique_ptr<presentation::PresentationAudioSession> presentation_;
    compile::CompiledSessionCapacities capacities_;
    contract::RationalRateHz physics_rate_ = kEngineSessionPhysicsRateHz;
    contract::RationalRateHz delivery_rate_ = kEngineSessionDeliveryRateHz;
    std::uint32_t physics_frames_per_block_ = kEngineSessionPhysicsFramesPerBlock;
    std::uint32_t delivery_frames_per_block_ = kEngineSessionDeliveryFramesPerBlock;
    session::ControlTimeline control_timeline_;
    std::vector<session::TimestampedControlCommand> control_scratch_;
    std::string engine_id_;
    std::string scenario_id_;
    std::vector<std::string> audio_bus_ids_;
    std::vector<EngineAudioBusDescriptor> audio_bus_descriptors_;
    std::vector<EngineAudioBusBlockView> audio_bus_views_;
    std::vector<EngineForwardGearDescriptor> forward_gear_descriptors_;
    std::array<EngineTelemetryFrame, 1> telemetry_{};
    std::uint64_t total_block_count_ = 0;
    std::uint64_t preparation_block_count_ = 0;
    EngineLiveControlCapabilityMask live_control_capabilities_ = 0U;
    EngineMotionMode motion_mode_ = EngineMotionMode::held_speed;
    bool has_accepted_live_controls_ = false;
    std::optional<EngineSessionCompleted> terminal_completion_;
    std::optional<EngineSessionError> terminal_error_;
};

EngineSession::EngineSession(
    std::unique_ptr<EngineSession::Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}

EngineSession::EngineSession(EngineSession &&) noexcept = default;
EngineSession &EngineSession::operator=(EngineSession &&) noexcept = default;
EngineSession::~EngineSession() = default;

EngineSessionDescriptor EngineSession::descriptor() const noexcept {
    if (!implementation_) {
        return {};
    }
    return implementation_->descriptor();
}

std::optional<EngineControlRejection>
EngineSession::enqueue_controls(const std::span<const EngineControlCommand> commands) {
    if (!implementation_) {
        return EngineControlRejection{
            EngineControlRejectionCode::internal_clock_error,
            0U,
            "the engine session has been moved from",
        };
    }
    return implementation_->enqueue_controls(commands);
}

EngineSessionProcessResult EngineSession::process_block() {
    if (!implementation_) {
        return EngineSessionError{
            EngineSessionErrorCode::consumer_state_invalid,
            "engine-session-moved-from",
            "the engine session has been moved from",
            std::nullopt,
        };
    }
    return implementation_->process_block();
}

namespace session_detail {

class EngineSessionFactory final {
  public:
    [[nodiscard]] static EngineSession make(BuiltSessionComponents components) {
        return EngineSession{
            std::make_unique<EngineSession::Implementation>(std::move(components))};
    }
};

} // namespace session_detail

EngineSessionCreateResult
create_engine_session(const compile::CompiledScenario &scenario,
                      const EngineSessionExecutionKind execution_kind) {
    try {
        if (execution_kind != EngineSessionExecutionKind::finite_scenario &&
            execution_kind != EngineSessionExecutionKind::open_ended) {
            return EngineSessionError{
                EngineSessionErrorCode::unsupported_configuration,
                "engine-session-execution-kind-invalid",
                "engine session creation requires a known execution kind",
                std::nullopt,
            };
        }
        auto built = session_detail::build_session_components(scenario, execution_kind);
        if (auto *error = std::get_if<EngineSessionError>(&built)) {
            return std::move(*error);
        }
        return session_detail::EngineSessionFactory::make(
            std::get<session_detail::BuiltSessionComponents>(std::move(built)));
    } catch (const std::bad_alloc &) {
        return EngineSessionError{
            EngineSessionErrorCode::resource_exhausted,
            "engine-session-creation-resource-exhausted",
            "engine session creation exhausted memory",
            std::nullopt,
        };
    } catch (const std::exception &error) {
        return EngineSessionError{
            EngineSessionErrorCode::internal_error,
            "engine-session-creation-threw",
            error.what(),
            std::nullopt,
        };
    } catch (...) {
        return EngineSessionError{
            EngineSessionErrorCode::internal_error,
            "engine-session-creation-threw",
            "engine session creation threw a non-standard exception",
            std::nullopt,
        };
    }
}

} // namespace engine_sim_offline
