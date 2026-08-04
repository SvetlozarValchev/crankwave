#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/session.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {

struct PackageSourceLaneBusCapture {
    std::string id;
    EngineAudioBusKind kind = EngineAudioBusKind::source_route_selected;
    contract::SourceRouteKind source_route_kind =
        contract::SourceRouteKind::unspecified;
    std::optional<contract::RouteId> route_id;
    EngineAudioSignalDisposition signal_disposition =
        EngineAudioSignalDisposition::declared_silent;
    std::uint32_t channel_count = 1U;
    contract::RationalRateHz sample_rate = kEngineSessionDeliveryRateHz;
    std::vector<float> samples;
};

struct PackageSourceLaneCycleBoundaryCapture {
    double lane_relative_start_delivery_frame = 0.0;
    double lane_relative_end_delivery_frame = 0.0;
};

struct PackageSourceLaneCapture {
    std::string engine_id;
    std::string scenario_id;
    contract::RationalRateHz sample_rate = kEngineSessionDeliveryRateHz;
    std::uint64_t audible_first_delivery_frame = 0U;
    std::uint64_t audible_delivery_frame_count = 0U;
    std::vector<PackageSourceLaneBusCapture> buses;
    // Observer evidence remains untouched and wall-clock based. In particular,
    // these are not presentation-latency-adjusted signal markers. The parallel
    // lane-boundary vector provides the same boundaries in tape coordinates.
    std::vector<EngineCompletedCycleEvidence> usable_cycles;
    std::vector<PackageSourceLaneCycleBoundaryCapture>
        usable_cycle_lane_boundaries;
    // Complete observer cycles that straddle or lie outside the audible tape are
    // deliberately not exposed as usable package units.
    std::uint64_t rejected_outside_audible_cycle_count = 0U;
};

enum class PackageSourceCaptureErrorCode : std::uint8_t {
    invalid_bus_selection,
    invalid_bus_format,
    session_failed,
    invalid_block_sequence,
    invalid_audio_payload,
    invalid_cycle_evidence,
    incomplete_session,
};

struct PackageSourceCaptureError {
    PackageSourceCaptureErrorCode code =
        PackageSourceCaptureErrorCode::incomplete_session;
    std::string detail_code;
    std::string message;
    std::optional<EngineSessionError> session_error;
};

using PackageSourceCaptureResult =
    std::variant<PackageSourceLaneCapture, PackageSourceCaptureError>;

// Runs exactly one finite authored scenario. Returned PCM is a direct owning copy
// of the selected EngineSession buses over only the audible interval: this layer
// performs no signal processing or publication.
[[nodiscard]] PackageSourceCaptureResult capture_package_source_lane(
    const compile::CompiledScenario &scenario,
    std::span<const std::string_view> selected_bus_ids);

} // namespace engine_sim_offline::package_detail
