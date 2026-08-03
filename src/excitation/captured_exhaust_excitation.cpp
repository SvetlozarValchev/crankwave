#include "excitation/captured_exhaust_excitation.hpp"

#include "contract/capture_block_admission.hpp"
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

[[nodiscard]] contract::FailureContext
moved_from_failure(std::uint64_t sample_index = 0U) {
    return {
        contract::FailureKind::contract_violation,
        "captured-excitation-session-moved-from",
        "captured-exhaust-excitation",
        "unavailable",
        sample_index,
        sample_index,
        +0.0,
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
        static_cast<double>(state.next_frame_index) *
            static_cast<double>(state.sample_rate.denominator) /
            static_cast<double>(state.sample_rate.numerator),
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
    std::span<const contract::CylinderId> cylinder_ids,
    std::span<const contract::RouteId> route_ids, std::size_t frame_count,
    std::span<const double> pre_delay, std::span<const double> post_delay,
    std::span<const double> route_bus_values) noexcept
    : first_frame_index_(first_frame_index), sample_rate_(sample_rate),
      cylinder_ids_(cylinder_ids), route_ids_(route_ids), frame_count_(frame_count),
      pre_delay_(pre_delay), post_delay_(post_delay),
      route_bus_values_(route_bus_values) {}

std::uint64_t ExhaustExcitationDiagnosticBlockView::first_frame_index() const noexcept {
    return first_frame_index_;
}

contract::RationalRateHz
ExhaustExcitationDiagnosticBlockView::sample_rate() const noexcept {
    return sample_rate_;
}

std::span<const contract::CylinderId>
ExhaustExcitationDiagnosticBlockView::cylinder_ids() const noexcept {
    return cylinder_ids_;
}

std::span<const contract::RouteId>
ExhaustExcitationDiagnosticBlockView::route_ids() const noexcept {
    return route_ids_;
}

std::size_t ExhaustExcitationDiagnosticBlockView::cylinder_count() const noexcept {
    return cylinder_ids_.size();
}

std::size_t ExhaustExcitationDiagnosticBlockView::route_count() const noexcept {
    return route_ids_.size();
}

std::size_t ExhaustExcitationDiagnosticBlockView::frame_count() const noexcept {
    return frame_count_;
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

std::span<const double>
ExhaustExcitationDiagnosticBlockView::route_bus_values_engine_sim_source_unit()
    const noexcept {
    return route_bus_values_;
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

    if (!contract::detail::valid_capture_block_after_layout_admission(block)) {
        // Compilation freezes the exact admitted layout in state. The hot path
        // checks only block-varying invariants without allocating; an owning
        // diagnostic is built solely for a rejected block.
        const auto validation = contract::validate(block);
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-invalid",
                                 validation.ok()
                                     ? "allocation-free admitted-layout validation "
                                       "rejected the capture block without a public "
                                       "diagnostic"
                                     : first_issue_summary(validation)));
    }
    if (!exact_layout_matches(state, block.layout())) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-layout-mismatch",
                                 "capture engine, cylinder order, or route layout "
                                 "differs from the compiled excitation layout"));
    }
    if (block.frame_count() != state.block_capacity_frames ||
        block.declared_block_capacity_frames() != state.block_capacity_frames ||
        block.clock().rate != state.sample_rate ||
        block.clock().phase != contract::SamplePhase::post_step ||
        block.clock().first_sample_index != state.next_frame_index ||
        !block.reference_parity().has_value()) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-block-extent-mismatch",
                                 "excitation requires one contiguous full-capacity "
                                 "post-step block on its compiled capture clock with "
                                 "reference parity"));
    }
    if (state.next_frame_index >
        std::numeric_limits<std::uint64_t>::max() - state.block_capacity_frames) {
        return fail(state,
                    make_failure(state, contract::FailureKind::contract_violation,
                                 "captured-excitation-frame-counter-overflow",
                                 "excitation frame counter cannot represent the next "
                                 "complete block"));
    }

    const auto &parity = *block.reference_parity();
    const std::size_t cylinder_count = state.cylinders.size();
    const std::size_t route_count = state.routes.size();
    // Advance copies first. Thus every input-derived value and every accumulated
    // route term for the complete block is known finite before persistent delay
    // history changes.
    for (std::size_t cylinder = 0; cylinder < cylinder_count; ++cylinder) {
        state.prospective_delays[cylinder] = state.cylinders[cylinder].delay;
    }
    for (std::size_t route = 0; route < route_count; ++route) {
        state.prospective_route_delays[route] = state.routes[route].downstream_delay;
    }

    for (std::size_t frame = 0; frame < state.block_capacity_frames; ++frame) {
        const double filtered_speed = parity.filtered_engine_speed_rpm()[frame];
        const double activity =
            std::min(std::abs(filtered_speed), state.filtered_speed_threshold_rpm) /
            state.filtered_speed_threshold_rpm;
        for (std::size_t cylinder = 0; cylinder < cylinder_count; ++cylinder) {
            const auto &sample = parity.cylinders()[frame * cylinder_count + cylinder];
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
            state.pre_delay[frame * cylinder_count + cylinder] = value;
        }
    }

    // Preserve the delayed excitation as one identity-stable lane per capture
    // cylinder before any collector aggregation. This is the replacement seam for
    // later primary-transfer work; delay state and lane storage remain indexed by
    // canonical capture-cylinder order even when the authored accumulation order is
    // different.
    for (std::size_t frame = 0; frame < state.block_capacity_frames; ++frame) {
        for (const auto cylinder_index : state.accumulation_order) {
            const auto &cylinder = state.cylinders[cylinder_index];
            const double delayed = state.prospective_delays[cylinder_index].process(
                state.pre_delay[frame * cylinder_count +
                                cylinder.capture_cylinder_index]);
            state.post_delay[frame * cylinder_count + cylinder.capture_cylinder_index] =
                delayed;
        }
    }

    // The current parity collector is deliberately separate from lane production.
    // Fold the same terms, with the same parentheses and authored serial order, so
    // this architectural split cannot alter the accepted route samples.
    for (std::size_t frame = 0; frame < state.block_capacity_frames; ++frame) {
        std::fill_n(state.collector_bus_values.begin() + frame * route_count,
                    route_count, +0.0);
        for (const auto cylinder_index : state.accumulation_order) {
            const auto &cylinder = state.cylinders[cylinder_index];
            const double delayed = state.post_delay[frame * cylinder_count +
                                                    cylinder.capture_cylinder_index];
            const auto &route = state.routes[cylinder.route_index];
            const double route_term =
                cylinder.sound_attenuation_linear *
                ((route.audio_volume_linear * delayed) / state.cylinder_count_divisor) *
                (1.0 / (route.exhaust_system_length_m * route.exhaust_system_length_m));
            auto &bus =
                state.collector_bus_values[frame * route_count + cylinder.route_index];
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

    // Apply the shared route propagation after the collector. The compiler derives
    // each primary residual so primary + downstream delay is exactly the old rounded
    // total sample count; this topology change therefore preserves every arrival.
    for (std::size_t frame = 0; frame < state.block_capacity_frames; ++frame) {
        for (std::size_t route = 0; route < route_count; ++route) {
            const double delayed = state.prospective_route_delays[route].process(
                state.collector_bus_values[frame * route_count + route]);
            if (!std::isfinite(delayed)) {
                return fail(
                    state, make_failure(state, contract::FailureKind::numerical_failure,
                                        "captured-excitation-value-nonfinite",
                                        "route propagation produced a non-finite value",
                                        state.routes[route].route_id));
            }
            state.route_bus_values[frame * route_count + route] = delayed;
        }
    }

    for (std::size_t cylinder = 0; cylinder < cylinder_count; ++cylinder) {
        std::swap(state.cylinders[cylinder].delay, state.prospective_delays[cylinder]);
    }
    for (std::size_t route = 0; route < route_count; ++route) {
        std::swap(state.routes[route].downstream_delay,
                  state.prospective_route_delays[route]);
    }

    const auto output = presentation::ExhaustExcitationBlockView::borrow_for_callback(
        state.next_frame_index, state.sample_rate, state.route_ids,
        state.block_capacity_frames, state.route_bus_values);
    const auto diagnostics = ExhaustExcitationDiagnosticBlockView::borrow_for_callback(
        state.next_frame_index, state.sample_rate, state.cylinder_ids, state.route_ids,
        state.block_capacity_frames, state.pre_delay, state.post_delay,
        state.route_bus_values);

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
        state.block_capacity_frames,
        state.next_frame_index + state.block_capacity_frames,
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
