#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/result.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline {

namespace session_detail {
class EngineSessionFactory;
} // namespace session_detail

inline constexpr std::uint32_t kEngineSessionPhysicsFramesPerBlock = 400U;
inline constexpr std::uint32_t kEngineSessionDeliveryFramesPerBlock = 3840U;
inline constexpr contract::RationalRateHz kEngineSessionPhysicsRateHz{20000U, 1U};
inline constexpr contract::RationalRateHz kEngineSessionDeliveryRateHz{192000U, 1U};

enum class EngineAudioBusKind : std::uint8_t {
    source_route_dry,
    source_route_configured_transfer,
    source_route_selected,
    engine_raw_master,
    engine_audition_master,
};

enum class EngineAudioSignalDisposition : std::uint8_t {
    active,
    declared_silent,
};

struct EngineAudioBusDescriptor {
    std::string_view id;
    EngineAudioBusKind kind = EngineAudioBusKind::source_route_selected;
    contract::SourceRouteKind source_route_kind =
        contract::SourceRouteKind::unspecified;
    std::optional<contract::RouteId> route_id;
    EngineAudioSignalDisposition signal_disposition =
        EngineAudioSignalDisposition::declared_silent;
    std::uint32_t channel_count = 1U;
    contract::RationalRateHz sample_rate = kEngineSessionDeliveryRateHz;
};

struct EngineAudioBusBlockView {
    EngineAudioBusDescriptor descriptor;
    std::span<const float> samples;
};

enum class EngineHeldDynoDisposition : std::uint8_t {
    tracking,
    absorbing_torque_limited,
    driving_torque_limited,
};

struct EngineHeldDynoTelemetry {
    double target_engine_speed_rpm = 0.0;
    double maximum_absorbing_torque_nm = 0.0;
    double maximum_driving_torque_nm = 0.0;
    double required_actuator_torque_nm = 0.0;
    double applied_actuator_torque_nm = 0.0;
    EngineHeldDynoDisposition disposition = EngineHeldDynoDisposition::tracking;
};

enum class EngineClutchDisposition : std::uint8_t {
    neutral,
    disengaged,
    engine_driving_torque_limited,
    vehicle_backdrive_torque_limited,
    tracking,
};

enum class EngineRoadLoadDisposition : std::uint8_t {
    moving,
    stopped_within_step,
    held_at_rest,
};

struct EngineFreeVehicleTelemetry {
    double vehicle_speed_m_s = 0.0;
    double vehicle_distance_m = 0.0;
    std::optional<std::uint32_t> selected_forward_gear_ordinal;
    double clutch_engagement_01 = 0.0;
    double service_brake_application_01 = 0.0;
    EngineClutchDisposition clutch_disposition = EngineClutchDisposition::neutral;
    double clutch_torque_capacity_nm = 0.0;
    double applied_average_clutch_torque_on_engine_nm = 0.0;
    std::optional<double> final_clutch_slip_rad_s;
    EngineRoadLoadDisposition road_load_disposition =
        EngineRoadLoadDisposition::held_at_rest;
    double requested_road_load_force_n = 0.0;
    double applied_average_road_load_force_n = 0.0;
};

struct EngineTelemetryFrame {
    std::uint64_t physics_step_end = 0;
    double mean_intake_manifold_pressure_pa_abs = 0.0;
    contract::EngineCaptureSample engine;
    std::optional<EngineHeldDynoTelemetry> held_dyno;
    std::optional<EngineFreeVehicleTelemetry> free_vehicle;
};

enum class EngineCycleStateFlag : std::uint32_t {
    ignition_enabled = 1U << 0U,
    fuel_enabled = 1U << 1U,
    starter_enabled = 1U << 2U,
    dyno_enabled = 1U << 3U,
    limiter_enabled = 1U << 4U,
    limiter_cut_active = 1U << 5U,
};

using EngineCycleStateFlagMask = std::uint32_t;

[[nodiscard]] constexpr EngineCycleStateFlagMask
engine_cycle_state_flag_mask(EngineCycleStateFlag flag) noexcept {
    return static_cast<EngineCycleStateFlagMask>(flag);
}

struct EngineCycleBoundaryEvidence {
    // Signed lattice ordinal n in crank_tdc_reference_rad + n * 4*pi.
    std::int64_t cycle_ordinal = 0;
    // Zero-based post-step physics-frame brackets. Their physical timestamp ticks
    // are frame + 1.
    std::uint64_t left_physics_frame = 0;
    std::uint64_t right_physics_frame = 0;
    double fraction_from_left_01 = 0.0;
    double theta_unwrapped_rad = 0.0;
    double time_s = 0.0;
    // Fractional by design: an exact crank-angle crossing generally falls between
    // delivery samples.
    double delivery_frame = 0.0;

    friend bool operator==(const EngineCycleBoundaryEvidence &,
                           const EngineCycleBoundaryEvidence &) = default;
};

struct EngineCycleControlEvidence {
    double time_weighted_mean_01 = 0.0;
    double minimum_01 = 0.0;
    double maximum_01 = 0.0;
    std::uint32_t change_count = 0;

    friend bool operator==(const EngineCycleControlEvidence &,
                           const EngineCycleControlEvidence &) = default;
};

struct EngineCycleNetShaftEvidence {
    // Integral of the available instantaneous net-shaft torque with respect to
    // unwrapped crank angle over exactly one 720-degree cycle.
    double angular_work_j = 0.0;
    double cycle_mean_torque_nm = 0.0;
    contract::Availability availability = contract::Availability::unavailable;
    contract::Completeness completeness = contract::Completeness::incomplete;
    contract::QuantityUnavailableReason unavailable_reason =
        contract::QuantityUnavailableReason::cycle_integration_not_admitted;
    contract::TorqueTermMask included_terms = 0;
    contract::TorqueTermMask omitted_terms = 0;

    friend bool operator==(const EngineCycleNetShaftEvidence &,
                           const EngineCycleNetShaftEvidence &) = default;
};

// This evidence is non-acoustic: it observes the already committed physics capture
// and cannot alter excitation, presentation, or delivered PCM. The first partial
// 720-degree interval after session creation is intentionally omitted.
struct EngineCompletedCycleEvidence {
    std::uint64_t completed_cycle_ordinal = 0;
    EngineCycleBoundaryEvidence start_boundary;
    EngineCycleBoundaryEvidence end_boundary;
    double duration_s = 0.0;
    double mean_engine_speed_rpm = 0.0;
    EngineCycleControlEvidence requested_throttle;
    EngineCycleControlEvidence resolved_engine_throttle;
    EngineCycleControlEvidence intake_plate_position;
    EngineCycleNetShaftEvidence instantaneous_net_shaft;
    // State is half-open over (start, end]. Transition bits report any committed
    // state change in that interval; no discrete state is numerically averaged.
    EngineCycleStateFlagMask start_state_flags = 0;
    EngineCycleStateFlagMask end_state_flags = 0;
    EngineCycleStateFlagMask state_transition_flags = 0;

    friend bool operator==(const EngineCompletedCycleEvidence &,
                           const EngineCompletedCycleEvidence &) = default;
};

enum class EngineSessionBlockPhase : std::uint8_t {
    preparation,
    audible,
};

// The compiled scenario is always the authoritative finite recording recipe.
// Session creation must state whether to execute that exact recipe to completion or
// use it to initialize a continuous interactive FreeEngine runtime.
enum class EngineSessionExecutionKind : std::uint8_t {
    finite_scenario,
    open_ended,
};

// All spans borrow storage owned by the producing EngineSession. They remain valid
// until the next enqueue/process operation, move, or destruction of that session.
class EngineSessionBlockView final {
  public:
    [[nodiscard]] std::uint64_t block_ordinal() const noexcept;
    [[nodiscard]] EngineSessionBlockPhase phase() const noexcept;
    [[nodiscard]] std::uint64_t first_physics_frame() const noexcept;
    [[nodiscard]] std::uint32_t physics_frame_count() const noexcept;
    [[nodiscard]] std::uint64_t first_delivery_frame() const noexcept;
    [[nodiscard]] std::uint32_t delivery_frame_count() const noexcept;
    [[nodiscard]] std::span<const EngineAudioBusBlockView> audio_buses() const noexcept;
    [[nodiscard]] std::span<const EngineTelemetryFrame> telemetry() const noexcept;
    // Contains only complete cycles whose end boundary was crossed in this block.
    // A cycle spanning blocks is emitted exactly once, with the block that ends it.
    [[nodiscard]] std::span<const EngineCompletedCycleEvidence>
    cycle_evidence() const noexcept;

  private:
    EngineSessionBlockView(
        std::uint64_t block_ordinal, EngineSessionBlockPhase phase,
        std::uint64_t first_physics_frame, std::uint32_t physics_frame_count,
        std::uint64_t first_delivery_frame, std::uint32_t delivery_frame_count,
        std::span<const EngineAudioBusBlockView> audio_buses,
        std::span<const EngineTelemetryFrame> telemetry,
        std::span<const EngineCompletedCycleEvidence> cycle_evidence) noexcept;

    std::uint64_t block_ordinal_ = 0;
    EngineSessionBlockPhase phase_ = EngineSessionBlockPhase::preparation;
    std::uint64_t first_physics_frame_ = 0;
    std::uint32_t physics_frame_count_ = 0;
    std::uint64_t first_delivery_frame_ = 0;
    std::uint32_t delivery_frame_count_ = 0;
    std::span<const EngineAudioBusBlockView> audio_buses_;
    std::span<const EngineTelemetryFrame> telemetry_;
    std::span<const EngineCompletedCycleEvidence> cycle_evidence_;

    friend class EngineSession;
};

struct SetEngineThrottle {
    double throttle_01 = 0.0;

    friend bool operator==(const SetEngineThrottle &,
                           const SetEngineThrottle &) = default;
};

struct SetEngineIgnitionEnabled {
    bool enabled = false;

    friend bool operator==(const SetEngineIgnitionEnabled &,
                           const SetEngineIgnitionEnabled &) = default;
};

struct SetEngineFuelEnabled {
    bool enabled = false;

    friend bool operator==(const SetEngineFuelEnabled &,
                           const SetEngineFuelEnabled &) = default;
};

struct SetEngineStarterEnabled {
    bool enabled = false;

    friend bool operator==(const SetEngineStarterEnabled &,
                           const SetEngineStarterEnabled &) = default;
};

struct SetEngineLimiterEnabled {
    bool enabled = false;

    friend bool operator==(const SetEngineLimiterEnabled &,
                           const SetEngineLimiterEnabled &) = default;
};

struct SetEngineExternalResistingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetEngineExternalResistingTorque &,
                           const SetEngineExternalResistingTorque &) = default;
};

struct SetHeldDynoTargetEngineSpeed {
    double engine_speed_rpm = 0.0;

    friend bool operator==(const SetHeldDynoTargetEngineSpeed &,
                           const SetHeldDynoTargetEngineSpeed &) = default;
};

struct SetHeldDynoMaximumAbsorbingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetHeldDynoMaximumAbsorbingTorque &,
                           const SetHeldDynoMaximumAbsorbingTorque &) = default;
};

struct SetHeldDynoMaximumDrivingTorque {
    double torque_nm = 0.0;

    friend bool operator==(const SetHeldDynoMaximumDrivingTorque &,
                           const SetHeldDynoMaximumDrivingTorque &) = default;
};

struct SetVehicleSelectedForwardGear {
    // Zero selects neutral. Positive values are one-based authored gear ordinals.
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

using EngineControlPayload =
    std::variant<SetEngineThrottle, SetEngineIgnitionEnabled, SetEngineFuelEnabled,
                 SetEngineStarterEnabled, SetEngineLimiterEnabled,
                 SetEngineExternalResistingTorque, SetHeldDynoTargetEngineSpeed,
                 SetHeldDynoMaximumAbsorbingTorque, SetHeldDynoMaximumDrivingTorque,
                 SetVehicleSelectedForwardGear, SetVehicleClutchEngagement,
                 SetVehicleServiceBrakeApplication>;

using EngineLiveControlCapabilityMask = std::uint32_t;

inline constexpr EngineLiveControlCapabilityMask kEngineLiveControlCapabilityThrottle =
    UINT32_C(1) << 0U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityIgnitionEnabled = UINT32_C(1) << 1U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityFuelEnabled = UINT32_C(1) << 2U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityLimiterEnabled = UINT32_C(1) << 3U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityExternalResistingTorque = UINT32_C(1) << 4U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityStarterEnabled = UINT32_C(1) << 5U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityHeldDynoTargetEngineSpeed = UINT32_C(1) << 6U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityHeldDynoMaximumAbsorbingTorque = UINT32_C(1) << 7U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityHeldDynoMaximumDrivingTorque = UINT32_C(1) << 8U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityVehicleSelectedForwardGear = UINT32_C(1) << 9U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityVehicleClutchEngagement = UINT32_C(1) << 10U;
inline constexpr EngineLiveControlCapabilityMask
    kEngineLiveControlCapabilityVehicleServiceBrakeApplication = UINT32_C(1) << 11U;

struct EngineControlCommand {
    std::uint64_t delivery_frame = 0;
    std::uint64_t sequence = 0;
    EngineControlPayload payload;

    friend bool operator==(const EngineControlCommand &,
                           const EngineControlCommand &) = default;
};

enum class EngineControlRejectionCode : std::uint8_t {
    capacity_exceeded,
    late_command,
    invalid_payload,
    unordered_delivery_frame,
    duplicate_sequence,
    unordered_sequence,
    unsupported_for_operating_mode,
    unavailable_during_preparation,
    outside_session_horizon,
    session_terminal,
    internal_clock_error,
};

struct EngineControlRejection {
    EngineControlRejectionCode code = EngineControlRejectionCode::internal_clock_error;
    std::size_t command_index = 0;
    std::string message;
};

enum class EngineSessionErrorCode : std::uint8_t {
    invalid_compiled_scenario,
    unsupported_configuration,
    resource_exhausted,
    processing_failed,
    consumer_state_invalid,
    internal_error,
};

struct EngineSessionError {
    EngineSessionErrorCode code = EngineSessionErrorCode::internal_error;
    std::string detail_code;
    std::string message;
    std::optional<contract::FailureContext> simulation_failure;
};

enum class EngineMotionMode : std::uint8_t {
    held_speed,
    prescribed_kinematic_sweep,
    held_dyno,
    load_target_held_capture,
    inertial_dyno,
    free_engine,
    free_vehicle,
};

struct EngineForwardGearDescriptor {
    contract::GearId id;
    std::uint32_t authored_ordinal = 0U;
    std::string_view semantic_id;
    double ratio = 0.0;
};

// String and bus views borrow storage from the producing EngineSession. They remain
// valid until that session is moved, assigned, or destroyed.
struct EngineSessionDescriptor {
    std::string_view engine_id;
    std::string_view scenario_id;
    compile::CompiledSessionCapacities capacities;
    contract::RationalRateHz physics_rate = kEngineSessionPhysicsRateHz;
    contract::RationalRateHz delivery_rate = kEngineSessionDeliveryRateHz;
    std::uint32_t physics_frames_per_block = kEngineSessionPhysicsFramesPerBlock;
    std::uint32_t delivery_frames_per_block = kEngineSessionDeliveryFramesPerBlock;
    std::uint64_t total_block_count = 0;
    std::uint64_t preparation_block_count = 0;
    std::span<const EngineAudioBusDescriptor> audio_buses;
    EngineLiveControlCapabilityMask live_control_capabilities = 0U;
    EngineSessionExecutionKind execution_kind =
        EngineSessionExecutionKind::finite_scenario;
    EngineMotionMode motion_mode = EngineMotionMode::held_speed;
    std::span<const EngineForwardGearDescriptor> forward_gears;
};

struct EngineSessionCompleted {
    std::uint64_t physics_frame_count = 0;
    std::uint64_t delivery_frame_count = 0;
    std::uint64_t block_count = 0;
    // Authoritative scenario-bound result evidence is withheld when true because
    // the authored request identity does not include the accepted live command
    // journal.
    bool live_controls_accepted = false;
    std::optional<contract::HeldSpeedOperatingPointResult> held_speed_operating_point;
    std::optional<contract::InertialDynoResult> inertial_dyno;
};

using EngineSessionProcessResult =
    std::variant<EngineSessionBlockView, EngineSessionCompleted, EngineSessionError>;

class EngineSession final {
  public:
    EngineSession(const EngineSession &) = delete;
    EngineSession &operator=(const EngineSession &) = delete;
    EngineSession(EngineSession &&) noexcept;
    EngineSession &operator=(EngineSession &&) noexcept;
    ~EngineSession();

    [[nodiscard]] EngineSessionDescriptor descriptor() const noexcept;

    [[nodiscard]] std::optional<EngineControlRejection>
    enqueue_controls(std::span<const EngineControlCommand> commands);

    [[nodiscard]] EngineSessionProcessResult process_block();

  private:
    class Implementation;
    explicit EngineSession(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;

    friend class session_detail::EngineSessionFactory;
};

using EngineSessionCreateResult = std::variant<EngineSession, EngineSessionError>;

// Session creation may allocate and compile immutable IR/kernel data. Finite sessions
// execute the authored scenario horizon exactly. Open-ended sessions are admitted only
// for FreeEngine scenarios and retain crank, gas, random, filter, and convolution state
// until destroyed; they do not complete merely because the authored recording horizon
// elapsed. Returned PCM and telemetry views borrow session-owned storage. Terminal
// results and diagnostics are owning values.
[[nodiscard]] EngineSessionCreateResult
create_engine_session(const compile::CompiledScenario &scenario,
                      EngineSessionExecutionKind execution_kind);

} // namespace engine_sim_offline
