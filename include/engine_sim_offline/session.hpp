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

inline constexpr std::uint32_t kEngineSessionPhysicsFramesPerBlock = 200U;
inline constexpr std::uint32_t kEngineSessionDeliveryFramesPerBlock = 3840U;
inline constexpr contract::RationalRateHz kEngineSessionPhysicsRateHz{10000U, 1U};
inline constexpr contract::RationalRateHz kEngineSessionDeliveryRateHz{192000U, 1U};

enum class EngineAudioBusKind : std::uint8_t {
    exhaust_route_dry,
    exhaust_route_configured_ir,
    exhaust_route_selected,
    engine_raw_master,
    engine_audition_master,
};

struct EngineAudioBusDescriptor {
    std::string_view id;
    EngineAudioBusKind kind = EngineAudioBusKind::exhaust_route_selected;
    std::optional<contract::RouteId> route_id;
    std::uint32_t channel_count = 1U;
    contract::RationalRateHz sample_rate = kEngineSessionDeliveryRateHz;
};

struct EngineAudioBusBlockView {
    EngineAudioBusDescriptor descriptor;
    std::span<const float> samples;
};

struct EngineTelemetryFrame {
    std::uint64_t physics_step_end = 0;
    contract::EngineCaptureSample engine;
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

  private:
    EngineSessionBlockView(std::uint64_t block_ordinal, EngineSessionBlockPhase phase,
                           std::uint64_t first_physics_frame,
                           std::uint64_t first_delivery_frame,
                           std::span<const EngineAudioBusBlockView> audio_buses,
                           std::span<const EngineTelemetryFrame> telemetry) noexcept;

    std::uint64_t block_ordinal_ = 0;
    EngineSessionBlockPhase phase_ = EngineSessionBlockPhase::preparation;
    std::uint64_t first_physics_frame_ = 0;
    std::uint64_t first_delivery_frame_ = 0;
    std::span<const EngineAudioBusBlockView> audio_buses_;
    std::span<const EngineTelemetryFrame> telemetry_;

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

using EngineControlPayload =
    std::variant<SetEngineThrottle, SetEngineIgnitionEnabled, SetEngineFuelEnabled,
                 SetEngineStarterEnabled, SetEngineLimiterEnabled,
                 SetEngineExternalResistingTorque>;

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
