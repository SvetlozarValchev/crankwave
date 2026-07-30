#include "session/control_timeline.hpp"

#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace engine_sim_offline::session {
namespace {

[[nodiscard]] bool valid_rate(contract::RationalRateHz rate) noexcept {
    return rate.numerator != 0 && rate.denominator != 0 &&
           std::gcd(rate.numerator, rate.denominator) == 1;
}

template <std::size_t NumeratorCount, std::size_t DenominatorCount>
void cancel_common_factors(std::array<std::uint64_t, NumeratorCount> &numerators,
                           std::array<std::uint64_t, DenominatorCount> &denominators)
    noexcept {
    for (auto &denominator : denominators) {
        for (auto &numerator : numerators) {
            const auto divisor = std::gcd(numerator, denominator);
            numerator /= divisor;
            denominator /= divisor;
        }
    }
}

#if defined(__SIZEOF_INT128__)
__extension__ using WideUnsigned = unsigned __int128;

[[nodiscard]] bool checked_multiply(WideUnsigned left, std::uint64_t right,
                                    WideUnsigned &result) noexcept {
    constexpr auto maximum = static_cast<WideUnsigned>(-1);
    if (right != 0 && left > maximum / right) {
        return false;
    }
    result = left * right;
    return true;
}
#else
using WideUnsigned = std::uint64_t;

[[nodiscard]] bool checked_multiply(WideUnsigned left, std::uint64_t right,
                                    WideUnsigned &result) noexcept {
    if (right != 0 && left > std::numeric_limits<WideUnsigned>::max() / right) {
        return false;
    }
    result = left * right;
    return true;
}
#endif

template <std::size_t Count>
[[nodiscard]] bool checked_product(const std::array<std::uint64_t, Count> &factors,
                                   WideUnsigned &result) noexcept {
    result = 1;
    for (const auto factor : factors) {
        if (!checked_multiply(result, factor, result)) {
            return false;
        }
    }
    return true;
}

template <class... Visitors> struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template <class... Visitors> Overloaded(Visitors...) -> Overloaded<Visitors...>;

} // namespace

PhysicsStepProjection project_delivery_frame_to_physics_step(
    std::uint64_t delivery_frame, contract::RationalRateHz physics_rate,
    contract::RationalRateHz delivery_rate) noexcept {
    if (!valid_rate(physics_rate) || !valid_rate(delivery_rate)) {
        return {ControlTimelineError::invalid_rate, 0};
    }
    if (delivery_frame == 0) {
        return {};
    }

    std::array<std::uint64_t, 3> numerator_factors{
        delivery_frame,
        physics_rate.numerator,
        delivery_rate.denominator,
    };
    std::array<std::uint64_t, 2> denominator_factors{
        physics_rate.denominator,
        delivery_rate.numerator,
    };
    cancel_common_factors(numerator_factors, denominator_factors);

    WideUnsigned numerator = 0;
    WideUnsigned denominator = 0;
    if (!checked_product(numerator_factors, numerator) ||
        !checked_product(denominator_factors, denominator) || denominator == 0) {
        return {ControlTimelineError::clock_overflow, 0};
    }

    auto quotient = numerator / denominator;
    if (numerator % denominator != 0) {
        ++quotient;
    }
    if (quotient > std::numeric_limits<std::uint64_t>::max()) {
        return {ControlTimelineError::clock_overflow, 0};
    }
    return {
        ControlTimelineError::none,
        static_cast<std::uint64_t>(quotient),
    };
}

ControlTimeline::ControlTimeline(std::size_t capacity,
                                 contract::RationalRateHz physics_rate,
                                 contract::RationalRateHz delivery_rate)
    : storage_(capacity), physics_rate_(physics_rate), delivery_rate_(delivery_rate) {
    if (!valid_rate(physics_rate_) || !valid_rate(delivery_rate_)) {
        throw std::invalid_argument{"control timeline requires reduced positive rates"};
    }
}

ControlTimelineResult
ControlTimeline::enqueue(std::span<const TimestampedControlCommand> commands) noexcept {
    if (commands.size() > storage_.size() - size_) {
        return {ControlTimelineError::capacity_exceeded, kNoCommandIndex};
    }

    bool have_previous = has_accepted_command_;
    std::uint64_t previous_delivery_frame = last_delivery_frame_;
    std::uint64_t previous_sequence = last_sequence_;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto &command = commands[index];
        if (command.delivery_frame < generated_delivery_frame_) {
            return {ControlTimelineError::late_command, index};
        }
        if (have_previous) {
            if (command.delivery_frame < previous_delivery_frame) {
                return {ControlTimelineError::unordered_delivery_frame, index};
            }
            if (command.sequence == previous_sequence) {
                return {ControlTimelineError::duplicate_sequence, index};
            }
            if (command.sequence < previous_sequence) {
                return {ControlTimelineError::unordered_sequence, index};
            }
        }
        if (!payload_is_valid(command.payload)) {
            return {ControlTimelineError::invalid_payload, index};
        }
        const auto projection = project_delivery_frame_to_physics_step(
            command.delivery_frame, physics_rate_, delivery_rate_);
        if (!projection) {
            return {projection.error, index};
        }
        if (projection.physics_step < next_physics_step_) {
            return {ControlTimelineError::late_command, index};
        }
        have_previous = true;
        previous_delivery_frame = command.delivery_frame;
        previous_sequence = command.sequence;
    }

    auto tail = tail_index();
    for (const auto &command : commands) {
        const auto projection = project_delivery_frame_to_physics_step(
            command.delivery_frame, physics_rate_, delivery_rate_);
        storage_[tail] = {command, projection.physics_step};
        ++tail;
        if (tail == storage_.size()) {
            tail = 0;
        }
    }
    size_ += commands.size();
    if (!commands.empty()) {
        has_accepted_command_ = true;
        last_delivery_frame_ = commands.back().delivery_frame;
        last_sequence_ = commands.back().sequence;
    }
    return {};
}

ControlTimelineError ControlTimeline::advance_delivery_cursor(
    std::uint64_t generated_delivery_end) noexcept {
    if (generated_delivery_end < generated_delivery_frame_) {
        return ControlTimelineError::delivery_cursor_regression;
    }
    generated_delivery_frame_ = generated_delivery_end;
    return ControlTimelineError::none;
}

PhysicsStepControlResult
ControlTimeline::drain_for_physics_step(std::uint64_t physics_step) noexcept {
    if (physics_step != next_physics_step_) {
        return {
            ControlTimelineError::noncontiguous_physics_step,
            {physics_step, 0, overrides_},
        };
    }
    if (physics_step == std::numeric_limits<std::uint64_t>::max()) {
        return {
            ControlTimelineError::clock_overflow,
            {physics_step, 0, overrides_},
        };
    }

    std::size_t applied = 0;
    while (size_ != 0 && storage_[head_].physics_step == physics_step) {
        apply(storage_[head_].command.payload);
        ++head_;
        if (head_ == storage_.size()) {
            head_ = 0;
        }
        --size_;
        ++applied;
    }
    ++next_physics_step_;
    return {
        ControlTimelineError::none,
        {physics_step, applied, overrides_},
    };
}

std::size_t ControlTimeline::capacity() const noexcept {
    return storage_.size();
}

std::size_t ControlTimeline::queued_command_count() const noexcept {
    return size_;
}

std::uint64_t ControlTimeline::generated_delivery_frame() const noexcept {
    return generated_delivery_frame_;
}

std::uint64_t ControlTimeline::next_physics_step() const noexcept {
    return next_physics_step_;
}

const simulation::LiveControlOverrides &
ControlTimeline::current_overrides() const noexcept {
    return overrides_;
}

contract::RationalRateHz ControlTimeline::physics_rate() const noexcept {
    return physics_rate_;
}

contract::RationalRateHz ControlTimeline::delivery_rate() const noexcept {
    return delivery_rate_;
}

bool ControlTimeline::payload_is_valid(const LiveControlPayload &payload) const noexcept {
    if (const auto *throttle = std::get_if<SetThrottle>(&payload)) {
        return std::isfinite(throttle->throttle_01) &&
               throttle->throttle_01 >= 0.0 && throttle->throttle_01 <= 1.0;
    }
    if (const auto *resistance = std::get_if<SetExternalResistingTorque>(&payload)) {
        return std::isfinite(resistance->torque_nm) && resistance->torque_nm >= 0.0;
    }
    return true;
}

void ControlTimeline::apply(const LiveControlPayload &payload) noexcept {
    std::visit(
        Overloaded{
            [this](const SetThrottle &command) {
                overrides_.has_throttle = true;
                overrides_.throttle_01 = command.throttle_01;
            },
            [this](const SetIgnitionEnabled &command) {
                overrides_.has_ignition_enabled = true;
                overrides_.ignition_enabled = command.enabled;
            },
            [this](const SetFuelEnabled &command) {
                overrides_.has_fuel_enabled = true;
                overrides_.fuel_enabled = command.enabled;
            },
            [this](const SetLimiterEnabled &command) {
                overrides_.has_limiter_enabled = true;
                overrides_.limiter_enabled = command.enabled;
            },
            [this](const SetExternalResistingTorque &command) {
                overrides_.has_external_resisting_torque_nm = true;
                overrides_.external_resisting_torque_nm = command.torque_nm;
            },
        },
        payload);
}

std::size_t ControlTimeline::tail_index() const noexcept {
    if (storage_.empty()) {
        return 0;
    }
    const auto distance_to_end = storage_.size() - head_;
    return size_ < distance_to_end ? head_ + size_ : size_ - distance_to_end;
}

static_assert(std::is_nothrow_copy_assignable_v<TimestampedControlCommand>);
static_assert(std::is_nothrow_move_assignable_v<TimestampedControlCommand>);

} // namespace engine_sim_offline::session
