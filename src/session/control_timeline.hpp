#pragma once

#include "crankwave/contract/common.hpp"
#include "simulation/live_control.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>
#include <vector>

namespace crankwave::session {

struct SetThrottle {
    double throttle_01 = 0.0;

    friend bool operator==(const SetThrottle &, const SetThrottle &) = default;
};

struct SetIgnitionEnabled {
    bool enabled = false;

    friend bool operator==(const SetIgnitionEnabled &,
                           const SetIgnitionEnabled &) = default;
};

struct SetFuelEnabled {
    bool enabled = false;

    friend bool operator==(const SetFuelEnabled &, const SetFuelEnabled &) = default;
};

struct SetStarterEnabled {
    bool enabled = false;

    friend bool operator==(const SetStarterEnabled &,
                           const SetStarterEnabled &) = default;
};

struct SetLimiterEnabled {
    bool enabled = false;

    friend bool operator==(const SetLimiterEnabled &,
                           const SetLimiterEnabled &) = default;
};

struct SetExternalResistingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetExternalResistingTorque &,
                           const SetExternalResistingTorque &) = default;
};

struct SetDynoTargetEngineSpeed {
    double engine_speed_rpm = 0.0;

    friend bool operator==(const SetDynoTargetEngineSpeed &,
                           const SetDynoTargetEngineSpeed &) = default;
};

struct SetDynoMaximumAbsorbingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetDynoMaximumAbsorbingTorque &,
                           const SetDynoMaximumAbsorbingTorque &) = default;
};

struct SetDynoMaximumDrivingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetDynoMaximumDrivingTorque &,
                           const SetDynoMaximumDrivingTorque &) = default;
};

struct SetVehicleSelectedForwardGear {
    // Zero is neutral; positive values are one-based authored gear ordinals.
    std::uint32_t forward_gear_ordinal = 0U;

    friend bool operator==(const SetVehicleSelectedForwardGear &,
                           const SetVehicleSelectedForwardGear &) = default;
};

struct SetVehicleClutchEngagement {
    double engagement_01 = 0.0;

    friend bool operator==(const SetVehicleClutchEngagement &,
                           const SetVehicleClutchEngagement &) = default;
};

struct SetVehicleServiceBrakeApplication {
    double application_01 = 0.0;

    friend bool operator==(const SetVehicleServiceBrakeApplication &,
                           const SetVehicleServiceBrakeApplication &) = default;
};

using LiveControlPayload =
    std::variant<SetThrottle, SetIgnitionEnabled, SetFuelEnabled, SetStarterEnabled,
                 SetLimiterEnabled, SetExternalResistingTorque,
                 SetDynoTargetEngineSpeed, SetDynoMaximumAbsorbingTorque,
                 SetDynoMaximumDrivingTorque, SetVehicleSelectedForwardGear,
                 SetVehicleClutchEngagement, SetVehicleServiceBrakeApplication>;

struct TimestampedControlCommand {
    std::uint64_t delivery_frame = 0;
    std::uint64_t sequence = 0;
    LiveControlPayload payload;

    friend bool operator==(const TimestampedControlCommand &,
                           const TimestampedControlCommand &) = default;
};

enum class ControlTimelineError : std::uint8_t {
    none,
    invalid_rate,
    clock_overflow,
    capacity_exceeded,
    late_command,
    invalid_payload,
    unordered_delivery_frame,
    duplicate_sequence,
    unordered_sequence,
    delivery_cursor_regression,
    noncontiguous_physics_step,
};

inline constexpr std::size_t kNoCommandIndex = std::numeric_limits<std::size_t>::max();

struct ControlTimelineResult {
    ControlTimelineError error = ControlTimelineError::none;
    std::size_t command_index = kNoCommandIndex;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ControlTimelineError::none;
    }
};

struct PhysicsStepProjection {
    ControlTimelineError error = ControlTimelineError::none;
    std::uint64_t physics_step = 0;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ControlTimelineError::none;
    }
};

// Maps an absolute delivery-frame timestamp to the first physics interval that
// does not begin before that timestamp:
//
//   ceil(delivery_frame * physics_rate / delivery_rate)
//
// The calculation is exact integer arithmetic. Invalid rates and any result that
// cannot be represented by uint64_t are reported rather than saturated.
[[nodiscard]] PhysicsStepProjection
project_delivery_frame_to_physics_step(std::uint64_t delivery_frame,
                                       contract::RationalRateHz physics_rate,
                                       contract::RationalRateHz delivery_rate) noexcept;

struct PhysicsStepControls {
    std::uint64_t physics_step = 0;
    std::size_t applied_command_count = 0;
    simulation::LiveControlOverrides overrides;
};

struct PhysicsStepControlResult {
    ControlTimelineError error = ControlTimelineError::none;
    PhysicsStepControls controls;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ControlTimelineError::none;
    }
};

// Mutable, session-owned right-continuous live-control timeline. Storage is allocated
// exactly once by construction. Successful enqueue, cursor advancement, and
// per-physics-step draining do not allocate.
class ControlTimeline final {
  public:
    ControlTimeline(std::size_t capacity, contract::RationalRateHz physics_rate,
                    contract::RationalRateHz delivery_rate);

    ControlTimeline(const ControlTimeline &) = delete;
    ControlTimeline &operator=(const ControlTimeline &) = delete;
    ControlTimeline(ControlTimeline &&) noexcept = default;
    ControlTimeline &operator=(ControlTimeline &&) noexcept = default;

    // The complete batch is validated before any command or ordering state changes.
    // Commands must have nondecreasing delivery timestamps and strictly increasing
    // sequence numbers, both within the batch and across accepted batches.
    [[nodiscard]] ControlTimelineResult
    enqueue(std::span<const TimestampedControlCommand> commands) noexcept;

    // Records the exclusive end of PCM already returned to the caller. A later
    // enqueue targeting an earlier delivery frame is rejected as late.
    [[nodiscard]] ControlTimelineError
    advance_delivery_cursor(std::uint64_t generated_delivery_end) noexcept;

    // Called exactly once for each consecutive physics step, before mechanics
    // evaluates that step. All commands projected to the step are applied in caller
    // sequence order and the resulting right-continuous snapshot is returned.
    [[nodiscard]] PhysicsStepControlResult
    drain_for_physics_step(std::uint64_t physics_step) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t queued_command_count() const noexcept;
    [[nodiscard]] std::uint64_t generated_delivery_frame() const noexcept;
    [[nodiscard]] std::uint64_t next_physics_step() const noexcept;
    [[nodiscard]] const simulation::LiveControlOverrides &
    current_overrides() const noexcept;
    [[nodiscard]] contract::RationalRateHz physics_rate() const noexcept;
    [[nodiscard]] contract::RationalRateHz delivery_rate() const noexcept;

  private:
    struct QueuedCommand {
        TimestampedControlCommand command;
        std::uint64_t physics_step = 0;
    };

    [[nodiscard]] bool
    payload_is_valid(const LiveControlPayload &payload) const noexcept;
    void apply(const LiveControlPayload &payload) noexcept;
    [[nodiscard]] std::size_t tail_index() const noexcept;

    std::vector<QueuedCommand> storage_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    contract::RationalRateHz physics_rate_;
    contract::RationalRateHz delivery_rate_;
    simulation::LiveControlOverrides overrides_;
    std::uint64_t generated_delivery_frame_ = 0;
    std::uint64_t next_physics_step_ = 0;
    bool has_accepted_command_ = false;
    std::uint64_t last_delivery_frame_ = 0;
    std::uint64_t last_sequence_ = 0;
};

} // namespace crankwave::session
