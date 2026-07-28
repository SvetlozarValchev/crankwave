#include "excitation/captured_exhaust_excitation.hpp"

#include "excitation/captured_exhaust_excitation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::excitation {
namespace {

inline constexpr contract::RationalRateHz kExcitationRateHz{10000, 1};

[[nodiscard]] contract::FailureContext
moved_from_failure(std::uint64_t sample_index = 0U) {
    return {
        contract::FailureKind::contract_violation,
        "captured-excitation-session-moved-from",
        "captured-exhaust-excitation",
        "unavailable",
        sample_index,
        sample_index,
        static_cast<double>(sample_index) / 10000.0,
        +0.0,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "captured exhaust excitation session has no owned state",
        "none; excitation processing terminated without fallback",
        {},
    };
}

[[nodiscard]] contract::FailureContext
make_failure(const detail::CapturedExhaustExcitationState &state,
             contract::FailureKind kind, std::string detail_code,
             std::string state_summary,
             std::optional<contract::RouteId> route_id = std::nullopt) {
    return {
        kind,
        std::move(detail_code),
        state.model_id,
        state.profile_id,
        state.next_frame_index,
        state.next_frame_index,
        static_cast<double>(state.next_frame_index) / 10000.0,
        +0.0,
        state.engine_id,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        route_id,
        std::move(state_summary),
        "none; excitation processing terminated without fallback",
        {},
    };
}

[[nodiscard]] CapturedExhaustExcitationProcessResult
fail(detail::CapturedExhaustExcitationState &state, contract::FailureContext failure) {
    if (!state.terminal_fault.has_value()) {
        state.terminal_fault = std::move(failure);
    }
    return *state.terminal_fault;
}

[[nodiscard]] std::string
first_issue_summary(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return "capture validation failed without a diagnostic";
    }
    return "path=" + report.issues.front().path + "; " + report.issues.front().message;
}

[[nodiscard]] bool
exact_layout_matches(const detail::CapturedExhaustExcitationState &state,
                     const contract::CaptureLayoutView &layout) {
    return layout.engine_id() == state.engine_id &&
           std::ranges::equal(layout.cylinders(), state.cylinder_ids) &&
           std::ranges::equal(layout.routes(), state.route_layout);
}

} // namespace

double detail::CapturedExcitationDelayState::process(double input) noexcept {
    if (history.empty()) {
        ++accepted_input_count;
        return input;
    }

    double output = +0.0;
    if (accepted_input_count >= history.size()) {
        output = history[next_write];
    }
    history[next_write] = input;
    ++next_write;
    if (next_write == history.size()) {
        next_write = 0U;
    }
    ++accepted_input_count;
    return output;
}

ExhaustExcitationDiagnosticBlockView::ExhaustExcitationDiagnosticBlockView(
    std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
    std::array<contract::CylinderId, kCapturedExcitationCylinderCount> cylinder_ids,
    std::array<contract::RouteId, kCapturedExcitationRouteCount> route_ids,
    std::span<const double> pre_delay, std::span<const double> post_delay,
    std::span<const presentation::ExhaustExcitationFrame> route_bus_frames) noexcept
    : first_frame_index_(first_frame_index), sample_rate_(sample_rate),
      cylinder_ids_(cylinder_ids), route_ids_(route_ids), pre_delay_(pre_delay),
      post_delay_(post_delay), route_bus_frames_(route_bus_frames) {}

std::uint64_t ExhaustExcitationDiagnosticBlockView::first_frame_index() const noexcept {
    return first_frame_index_;
}

contract::RationalRateHz
ExhaustExcitationDiagnosticBlockView::sample_rate() const noexcept {
    return sample_rate_;
}

const std::array<contract::CylinderId, kCapturedExcitationCylinderCount> &
ExhaustExcitationDiagnosticBlockView::cylinder_ids() const noexcept {
    return cylinder_ids_;
}

const std::array<contract::RouteId, kCapturedExcitationRouteCount> &
ExhaustExcitationDiagnosticBlockView::route_ids() const noexcept {
    return route_ids_;
}

std::span<const double>
ExhaustExcitationDiagnosticBlockView::pre_delay_cylinder_values_engine_sim_source_unit()
    const noexcept {
    return pre_delay_;
}

std::span<const double> ExhaustExcitationDiagnosticBlockView::
    post_delay_cylinder_values_engine_sim_source_unit() const noexcept {
    return post_delay_;
}

std::span<const presentation::ExhaustExcitationFrame>
ExhaustExcitationDiagnosticBlockView::route_bus_frames() const noexcept {
    return route_bus_frames_;
}

CapturedExhaustExcitationSession::CapturedExhaustExcitationSession(
    std::unique_ptr<detail::CapturedExhaustExcitationState> state) noexcept
    : state_(std::move(state)) {}

CapturedExhaustExcitationSession::CapturedExhaustExcitationSession(
    CapturedExhaustExcitationSession &&) noexcept = default;

CapturedExhaustExcitationSession &CapturedExhaustExcitationSession::operator=(
    CapturedExhaustExcitationSession &&) noexcept = default;

CapturedExhaustExcitationSession::~CapturedExhaustExcitationSession() = default;

CapturedExhaustExcitationProcessResult CapturedExhaustExcitationSession::process_block(
    const contract::CaptureBlockView &block,
    const ExhaustExcitationConsumer &consumer) {
    if (state_ == nullptr) {
        return moved_from_failure();
    }
    auto &state = *state_;
    if (state.terminal_fault.has_value()) {
        return *state.terminal_fault;
    }
    if (state.consumer_callback_active) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-consumer-reentrant",
                                 "excitation consumer re-entered its session while "
                                 "borrowed output views were active"));
    }
    if (!consumer) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-consumer-missing",
                                 "excitation publication requires a synchronous "
                                 "consumer"));
    }

    const auto validation = contract::validate(block);
    if (!validation.ok()) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-invalid",
                                 first_issue_summary(validation)));
    }
    if (!exact_layout_matches(state, block.layout())) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-layout-mismatch",
                                 "capture engine, cylinder order, or route layout "
                                 "differs from the compiled excitation layout"));
    }
    if (block.frame_count() != kCapturedExcitationFramesPerBlock ||
        block.declared_block_capacity_frames() != kCapturedExcitationFramesPerBlock ||
        block.clock().rate != kExcitationRateHz ||
        block.clock().phase != contract::SamplePhase::post_step ||
        block.clock().first_sample_index != state.next_frame_index ||
        !block.reference_parity().has_value()) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-extent-mismatch",
                                 "excitation requires one contiguous 200-frame, "
                                 "10000/1 Hz post-step block with reference parity"));
    }
    if (state.next_frame_index >
        std::numeric_limits<std::uint64_t>::max() - kCapturedExcitationFramesPerBlock) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-frame-counter-overflow",
                                 "excitation frame counter cannot represent the next "
                                 "complete block"));
    }

    const auto &parity = *block.reference_parity();
    // Advance copies first. Thus every input-derived value and every accumulated
    // route term for the complete block is known finite before persistent delay
    // history changes.
    for (std::size_t cylinder = 0; cylinder < kCapturedExcitationCylinderCount;
         ++cylinder) {
        state.prospective_delays[cylinder] = state.cylinders[cylinder].delay;
    }

    for (std::size_t frame = 0; frame < kCapturedExcitationFramesPerBlock; ++frame) {
        const double filtered_speed = parity.filtered_engine_speed_rpm()[frame];
        const double activity =
            std::min(std::abs(filtered_speed), state.filtered_speed_threshold_rpm) /
            state.filtered_speed_threshold_rpm;
        for (std::size_t cylinder = 0; cylinder < kCapturedExcitationCylinderCount;
             ++cylinder) {
            const auto &sample =
                parity.cylinders()[frame * kCapturedExcitationCylinderCount + cylinder];
            const double activity_squared = activity * activity;
            const double activity_cubed = activity_squared * activity;
            const double speed_scale = activity_cubed * state.excitation_scale;
            const double gauge_static = sample.exhaust_primary_static_pressure_pa_abs -
                                        state.reference_atmosphere_pa_abs;
            const double static_component = state.gauge_static_gain * gauge_static;
            const double forward_component =
                state.dynamic_forward_gain * sample.dynamic_pressure_forward_pa;
            const double reverse_component =
                state.dynamic_reverse_gain * sample.dynamic_pressure_reverse_pa;
            const double pressure_term =
                (static_component + forward_component) + reverse_component;
            const double value = speed_scale * pressure_term;
            if (!std::isfinite(value)) {
                return fail(
                    state, make_failure(state, contract::FailureKind::numerical_failure,
                                        "captured-excitation-value-nonfinite",
                                        "pre-delay excitation arithmetic produced a "
                                        "non-finite value"));
            }
            state.pre_delay[frame * kCapturedExcitationCylinderCount + cylinder] =
                value;
        }
    }

    for (std::size_t frame = 0; frame < kCapturedExcitationFramesPerBlock; ++frame) {
        state.route_bus_frames[frame].route_values_engine_sim_source_unit = {
            +0.0,
            +0.0,
        };
        for (const auto cylinder_index : state.accumulation_order) {
            const auto &cylinder = state.cylinders[cylinder_index];
            const double delayed = state.prospective_delays[cylinder_index].process(
                state.pre_delay[frame * kCapturedExcitationCylinderCount +
                                cylinder.capture_cylinder_index]);
            state.post_delay[frame * kCapturedExcitationCylinderCount +
                             cylinder.capture_cylinder_index] = delayed;

            const auto &route = state.routes[cylinder.route_index];
            const double route_term =
                cylinder.sound_attenuation_linear *
                ((route.audio_volume_linear * delayed) / state.cylinder_count_divisor) *
                (1.0 / (route.exhaust_system_length_m * route.exhaust_system_length_m));
            auto &bus = state.route_bus_frames[frame]
                            .route_values_engine_sim_source_unit[cylinder.route_index];
            bus += route_term;
            if (!std::isfinite(delayed) || !std::isfinite(route_term) ||
                !std::isfinite(bus)) {
                return fail(
                    state, make_failure(state, contract::FailureKind::numerical_failure,
                                        "captured-excitation-value-nonfinite",
                                        "delay or route-bus arithmetic produced a "
                                        "non-finite value",
                                        route.route_id));
            }
        }
    }

    for (std::size_t cylinder = 0; cylinder < kCapturedExcitationCylinderCount;
         ++cylinder) {
        std::swap(state.cylinders[cylinder].delay, state.prospective_delays[cylinder]);
    }

    const auto output = presentation::ExhaustExcitationBlockView::borrow_for_callback(
        state.next_frame_index, kExcitationRateHz, state.route_ids,
        state.route_bus_frames);
    const auto diagnostics = ExhaustExcitationDiagnosticBlockView::borrow_for_callback(
        state.next_frame_index, kExcitationRateHz, state.cylinder_ids, state.route_ids,
        state.pre_delay, state.post_delay, state.route_bus_frames);

    bool accepted = false;
    state.consumer_callback_active = true;
    try {
        accepted = consumer(output, diagnostics);
    } catch (const std::exception &exception) {
        state.consumer_callback_active = false;
        if (state.terminal_fault.has_value()) {
            return *state.terminal_fault;
        }
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-consumer-threw",
                                 "excitation consumer threw: " +
                                     std::string{exception.what()}));
    } catch (...) {
        state.consumer_callback_active = false;
        if (state.terminal_fault.has_value()) {
            return *state.terminal_fault;
        }
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-consumer-threw",
                                 "excitation consumer threw a non-standard "
                                 "exception"));
    }
    state.consumer_callback_active = false;
    if (state.terminal_fault.has_value()) {
        return *state.terminal_fault;
    }
    if (!accepted) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-consumer-rejected",
                                 "excitation consumer rejected a complete validated "
                                 "block"));
    }

    const ExhaustExcitationBlockPublished published{
        state.published_block_count,
        state.next_frame_index,
        kCapturedExcitationFramesPerBlock,
        state.next_frame_index + kCapturedExcitationFramesPerBlock,
    };
    state.next_frame_index = published.published_frame_count;
    ++state.published_block_count;
    return published;
}

std::uint64_t CapturedExhaustExcitationSession::next_frame_index() const noexcept {
    return state_ != nullptr ? state_->next_frame_index : 0U;
}

std::uint64_t CapturedExhaustExcitationSession::published_block_count() const noexcept {
    return state_ != nullptr ? state_->published_block_count : 0U;
}

bool CapturedExhaustExcitationSession::faulted() const noexcept {
    return state_ == nullptr || state_->terminal_fault.has_value();
}

} // namespace engine_sim_offline::excitation
