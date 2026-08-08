#pragma once

#include "engine_sim_offline/session.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

enum class FiniteResponsiveCaptureErrorCode : std::uint8_t {
    invalid_request,
    cancelled,
    session_failed,
    invalid_session,
    invalid_block,
    invalid_payload,
    resource_exhausted,
    internal_error,
};

struct FiniteResponsiveCaptureError {
    FiniteResponsiveCaptureErrorCode code =
        FiniteResponsiveCaptureErrorCode::internal_error;
    std::string detail_code;
    std::string message;
    std::optional<EngineSessionError> session_error;
};

// Owning counterpart of EngineAudioBusDescriptor. Session descriptor views expire
// with the session; responsive captures are deliberately self-contained values.
struct ResponsiveCaptureBusDescriptor {
    std::string id;
    EngineAudioBusKind kind = EngineAudioBusKind::source_route_selected;
    contract::SourceRouteKind source_route_kind =
        contract::SourceRouteKind::unspecified;
    std::optional<contract::RouteId> route_id;
    EngineAudioSignalDisposition signal_disposition =
        EngineAudioSignalDisposition::declared_silent;
    std::uint32_t channel_count = 1U;
    contract::RationalRateHz sample_rate = kEngineSessionDeliveryRateHz;

    friend bool operator==(const ResponsiveCaptureBusDescriptor &,
                           const ResponsiveCaptureBusDescriptor &) = default;
};

struct ResponsiveCaptureBus {
    ResponsiveCaptureBusDescriptor descriptor;
    // Chronological, channel-interleaved PCM for audible blocks only. Preparation
    // output is intentionally discarded; its exact state evidence remains below.
    std::vector<float> audible_interleaved_samples;

    friend bool operator==(const ResponsiveCaptureBus &,
                           const ResponsiveCaptureBus &) = default;
};

// Common low-dimensional endpoint consumed by held, directional, and lifecycle
// responsive transforms. It is derived losslessly from the retained full endpoint
// telemetry. delivery_frame is the exclusive end of the source block. state_flags
// uses the full EngineCycleStateFlagMask diagnostic layout (including dyno at bit 3,
// with limiter enabled/cut at bits 4/5). It is NOT the responsive playback package's
// presentation state_mask layout, which omits dyno and places limiter at bits 3/4;
// package transforms must map the named flags explicitly.
struct ResponsiveCaptureEndpoint {
    std::uint64_t delivery_frame = 0;
    double engine_speed_rpm = 0.0;
    double mean_intake_manifold_pressure_pa_abs = 0.0;
    double requested_throttle_01 = 0.0;
    double resolved_engine_throttle_01 = 0.0;
    double unwrapped_crank_revolutions = 0.0;
    EngineCycleStateFlagMask state_flags = 0U;

    friend bool operator==(const ResponsiveCaptureEndpoint &,
                           const ResponsiveCaptureEndpoint &) = default;
};

struct ResponsiveCaptureBlock {
    std::uint64_t block_ordinal = 0;
    EngineSessionBlockPhase phase = EngineSessionBlockPhase::preparation;
    std::uint64_t first_physics_frame = 0;
    std::uint32_t physics_frame_count = 0;
    std::uint64_t first_delivery_frame = 0;
    std::uint32_t delivery_frame_count = 0;
    ResponsiveCaptureEndpoint endpoint;
    EngineTelemetryFrame telemetry;
    std::vector<EngineCompletedCycleEvidence> completed_cycles;
    EngineEventCounters event_counters;
};

struct ResponsiveCaptureForwardGear {
    contract::GearId id;
    std::uint32_t authored_ordinal = 0U;
    std::string semantic_id;
    double ratio = 0.0;

    friend bool operator==(const ResponsiveCaptureForwardGear &,
                           const ResponsiveCaptureForwardGear &) = default;
};

struct FiniteResponsiveCapture {
    std::string engine_id;
    std::string scenario_id;
    compile::CompiledSessionCapacities capacities;
    contract::RationalRateHz physics_rate = kEngineSessionPhysicsRateHz;
    contract::RationalRateHz delivery_rate = kEngineSessionDeliveryRateHz;
    std::uint32_t physics_frames_per_block = kEngineSessionPhysicsFramesPerBlock;
    std::uint32_t delivery_frames_per_block = kEngineSessionDeliveryFramesPerBlock;
    std::uint64_t total_block_count = 0;
    std::uint64_t preparation_block_count = 0;
    std::uint64_t total_physics_frame_count = 0;
    std::uint64_t total_delivery_frame_count = 0;
    std::uint64_t audible_first_delivery_frame = 0;
    std::uint64_t audible_delivery_frame_count = 0;
    EngineLiveControlCapabilityMask live_control_capabilities = 0U;
    EngineMotionMode motion_mode = EngineMotionMode::held_speed;
    std::vector<ResponsiveCaptureForwardGear> forward_gears;

    // This separate list makes request order explicit even when consumers inspect
    // only identity metadata. buses uses the exact same order.
    std::vector<std::string> selected_bus_ids;
    std::vector<ResponsiveCaptureBus> buses;
    // Contains every preparation and audible block in source order.
    std::vector<ResponsiveCaptureBlock> blocks;
    EngineSessionCompleted completion;

    // Both ledgers are copied from the compiler authority. Native executable and
    // installed-release identity belongs to the package layer, not this capture.
    contract::ProvenanceLedger engine_provenance;
    contract::ProvenanceLedger scenario_provenance;
};

using FiniteResponsiveCaptureResult =
    std::variant<FiniteResponsiveCapture, FiniteResponsiveCaptureError>;

// Drives one authoritative finite session and captures all requested buses in that
// single execution. Cancellation is observed before session creation and between
// native process blocks; no partially populated capture is returned.
[[nodiscard]] FiniteResponsiveCaptureResult capture_finite_responsive_session(
    const compile::CompiledScenario &scenario,
    std::span<const std::string_view> selected_bus_ids,
    std::stop_token cancellation = {});

// Revalidates an owning capture at an adapter/serialization boundary. The capture
// function applies the same validator before publishing success.
[[nodiscard]] std::optional<FiniteResponsiveCaptureError>
validate_finite_responsive_capture(const FiniteResponsiveCapture &capture);

} // namespace engine_sim_offline::responsive
