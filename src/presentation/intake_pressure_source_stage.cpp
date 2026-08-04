#include "presentation/intake_pressure_source_stage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace engine_sim_offline::presentation {
namespace {

constexpr double kIntakePressureDcRemovalTimeConstantS =
    1.0 / (2.0 * dsp::kSourceConditioningPi * kIntakePressureDcRemovalCutoffHz);

[[nodiscard]] bool canonical_nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 &&
           (value != 0.0 || !std::signbit(value));
}

} // namespace

std::vector<IntakePressureSourceRouteConfiguration>
IntakePressureSourceStage::validate_configuration(
    const IntakePressureSourceStageConfiguration &configuration) {
    const auto expected_input_frames =
        configuration.input_rate == kPreviewIntakePressureInputRateHz
            ? kPreviewExcitationFramesPerMethodBlock
        : configuration.input_rate == kIntakePressureInputRateHz
            ? kExcitationFramesPerMethodBlock
            : 0U;
    if (expected_input_frames == 0U ||
        configuration.source_rate != kIntakePressureSourceRateHz) {
        throw std::invalid_argument{
            "intake-pressure source stage admits only exact 10000/1 or 20000/1 "
            "input and 192000/1 output clocks"};
    }
    if (configuration.input_frames_per_block != expected_input_frames) {
        throw std::invalid_argument{
            "intake-pressure source stage requires one exact 20 ms input block"};
    }
    if (configuration.routes.empty()) {
        throw std::invalid_argument{
            "intake-pressure source stage requires at least one route"};
    }
    if (configuration.routes.size() > std::numeric_limits<std::size_t>::max() /
                                          configuration.input_frames_per_block ||
        configuration.routes.size() >
            std::numeric_limits<std::size_t>::max() / kSourceFramesPerMethodBlock) {
        throw std::overflow_error{
            "intake-pressure source-stage scratch size overflowed"};
    }

    for (std::size_t index = 0; index < configuration.routes.size(); ++index) {
        const auto &route = configuration.routes[index];
        if (!route.id.valid()) {
            throw std::invalid_argument{
                "intake-pressure source-stage route IDs must be valid nonzero "
                "identities"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (route.id == configuration.routes[prior].id) {
                throw std::invalid_argument{
                    "intake-pressure source-stage route IDs must be distinct"};
            }
        }
        if (!std::isfinite(route.reference_pressure_pa) ||
            route.reference_pressure_pa <= 0.0) {
            throw std::invalid_argument{
                "intake-pressure reference pressure must be positive and finite"};
        }
        if (!canonical_nonnegative(route.source_gain_linear)) {
            throw std::invalid_argument{
                "intake-pressure source gain must be finite canonical "
                "nonnegative"};
        }
    }
    return {configuration.routes.begin(), configuration.routes.end()};
}

IntakePressureSourceStage::IntakePressureSourceStage(
    IntakePressureSourceStageConfiguration configuration)
    : route_configurations_(validate_configuration(configuration)),
      input_rate_(configuration.input_rate), source_rate_(configuration.source_rate),
      input_frames_per_block_(configuration.input_frames_per_block),
      reconstruction_(route_configurations_.size(), input_rate_.numerator) {
    expected_route_ids_.reserve(route_configurations_.size());
    dc_removers_.reserve(route_configurations_.size());
    for (const auto &route : route_configurations_) {
        expected_route_ids_.push_back(route.id);
        dc_removers_.emplace_back(dsp::kConditionedSourceTimeStepS,
                                  kIntakePressureDcRemovalTimeConstantS);
    }
    gauge_input_scratch_.resize(input_frames_per_block_ * route_count());
    reconstructed_scratch_.resize(kSourceFramesPerMethodBlock * route_count());
}

IntakePressureSourceBlockView IntakePressureSourceStage::process(
    IntakePressureInputBlockView input,
    std::span<double> output_frame_major_pressure_pa_gauge) {
    if (terminal_failed_) {
        throw std::logic_error{
            "intake-pressure source stage cannot resume after an arithmetic "
            "failure"};
    }
    if (input.sample_rate() != input_rate_) {
        throw std::invalid_argument{
            "intake-pressure input rate differs from the configured input clock"};
    }
    if (!std::ranges::equal(input.route_ids(), expected_route_ids_)) {
        throw std::invalid_argument{
            "intake-pressure input route IDs must match the configured order"};
    }
    if (input.first_frame_index() != next_input_frame_index_) {
        throw std::invalid_argument{
            "intake-pressure input frames must be globally contiguous"};
    }
    if (input.route_count() != route_count() ||
        input.frame_count() != input_frames_per_block_) {
        throw std::invalid_argument{
            "intake-pressure input frame count differs from its configured "
            "method block"};
    }
    if (input.frame_count() >
            std::numeric_limits<std::size_t>::max() / input.route_count() ||
        input.absolute_pressure_pa().size() !=
            input.frame_count() * input.route_count()) {
        throw std::invalid_argument{
            "intake-pressure input must be a complete frame-major route matrix"};
    }
    if (kSourceFramesPerMethodBlock >
            std::numeric_limits<std::size_t>::max() / route_count() ||
        output_frame_major_pressure_pa_gauge.size() !=
            kSourceFramesPerMethodBlock * route_count()) {
        throw std::invalid_argument{
            "intake-pressure source stage requires exactly 3840 complete output "
            "frames per block"};
    }
    if (next_input_frame_index_ >
            std::numeric_limits<std::uint64_t>::max() - input_frames_per_block_ ||
        next_source_frame_index_ >
            std::numeric_limits<std::uint64_t>::max() - kSourceFramesPerMethodBlock) {
        throw std::overflow_error{
            "intake-pressure source-stage frame counter overflow"};
    }
    for (const auto pressure : input.absolute_pressure_pa()) {
        if (!std::isfinite(pressure)) {
            throw std::domain_error{
                "intake-pressure source-stage input was non-finite"};
        }
        if (pressure < 0.0 || (pressure == 0.0 && std::signbit(pressure))) {
            throw std::domain_error{
                "intake-pressure source-stage input was not canonical absolute "
                "pressure"};
        }
    }

    const auto exact_output_count =
        reconstruction_.expected_output_frame_count(input_frames_per_block_);
    if (exact_output_count != kSourceFramesPerMethodBlock ||
        reconstruction_.distance_to_next_output() != 0U) {
        terminal_failed_ = true;
        throw std::logic_error{
            "intake-pressure method block did not begin on its exact clock phase"};
    }

    const auto first_input_frame_index = next_input_frame_index_;
    const auto first_source_frame_index = next_source_frame_index_;
    try {
        for (std::size_t frame = 0; frame < input_frames_per_block_; ++frame) {
            for (std::size_t route = 0; route < route_count(); ++route) {
                const auto index = frame * route_count() + route;
                const double gauge = input.absolute_pressure_pa()[index] -
                                     route_configurations_[route].reference_pressure_pa;
                if (!std::isfinite(gauge)) {
                    throw std::domain_error{
                        "intake-pressure reference subtraction was non-finite"};
                }
                gauge_input_scratch_[index] = gauge;
            }
        }

        reconstruction_.process(gauge_input_scratch_, input_frames_per_block_,
                                reconstructed_scratch_);
        if (reconstruction_.distance_to_next_output() != 0U) {
            throw std::logic_error{
                "intake-pressure method block did not return to exact clock phase"};
        }

        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            for (std::size_t route = 0; route < route_count(); ++route) {
                const auto index = frame * route_count() + route;
                const double dc_removed =
                    dc_removers_[route].process(reconstructed_scratch_[index]);
                const double output =
                    dc_removed * route_configurations_[route].source_gain_linear;
                if (!std::isfinite(output)) {
                    throw std::domain_error{
                        "intake-pressure source gain produced non-finite output"};
                }
                output_frame_major_pressure_pa_gauge[index] = output;
            }
        }
    } catch (...) {
        terminal_failed_ = true;
        throw;
    }

    next_input_frame_index_ += input_frames_per_block_;
    next_source_frame_index_ += kSourceFramesPerMethodBlock;
    return {
        first_input_frame_index,
        first_source_frame_index,
        source_rate_,
        expected_route_ids_,
        input_frames_per_block_,
        kSourceFramesPerMethodBlock,
        output_frame_major_pressure_pa_gauge,
    };
}

std::span<const IntakePressureSourceRouteConfiguration>
IntakePressureSourceStage::route_configurations() const noexcept {
    return route_configurations_;
}

std::span<const contract::RouteId>
IntakePressureSourceStage::expected_route_ids() const noexcept {
    return expected_route_ids_;
}

std::size_t IntakePressureSourceStage::route_count() const noexcept {
    return route_configurations_.size();
}

contract::RationalRateHz IntakePressureSourceStage::input_rate() const noexcept {
    return input_rate_;
}

contract::RationalRateHz IntakePressureSourceStage::source_rate() const noexcept {
    return source_rate_;
}

std::size_t IntakePressureSourceStage::input_frames_per_block() const noexcept {
    return input_frames_per_block_;
}

std::uint64_t IntakePressureSourceStage::next_input_frame_index() const noexcept {
    return next_input_frame_index_;
}

std::uint64_t IntakePressureSourceStage::next_source_frame_index() const noexcept {
    return next_source_frame_index_;
}

bool IntakePressureSourceStage::terminal_failed() const noexcept {
    return terminal_failed_;
}

} // namespace engine_sim_offline::presentation
