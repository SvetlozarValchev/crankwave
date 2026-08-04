#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/audio_atlas.hpp"
#include "engine_sim_offline/session.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline {

struct AtlasMovingLaneBusCapture {
    std::string id;
    EngineAudioBusKind kind = EngineAudioBusKind::source_route_selected;
    contract::SourceRouteKind source_route_kind =
        contract::SourceRouteKind::unspecified;
    std::optional<contract::RouteId> route_id;
    EngineAudioSignalDisposition signal_disposition =
        EngineAudioSignalDisposition::declared_silent;
    std::vector<float> samples;

    friend bool operator==(const AtlasMovingLaneBusCapture &,
                           const AtlasMovingLaneBusCapture &) = default;
};

// One finite scenario becomes one chronological moving performance. PCM and all
// frame addresses cover only the scenario's authored audible interval.
struct AtlasMovingLaneCapture {
    std::string engine_id;
    std::string scenario_id;
    contract::RationalRateHz sample_rate;
    std::uint64_t audible_source_frame = 0;
    std::uint64_t frame_count = 0;
    std::vector<AtlasMovingLaneBusCapture> buses;
    contract::AudioAtlasStateTimeline timeline;
    std::vector<contract::AudioAtlasCrankBoundary> crank_boundaries;

    friend bool operator==(const AtlasMovingLaneCapture &,
                           const AtlasMovingLaneCapture &) = default;
};

enum class AtlasMovingLaneCaptureErrorCode : std::uint8_t {
    invalid_request,
    invalid_session,
    session_failed,
    invalid_block,
    invalid_payload,
};

struct AtlasMovingLaneCaptureError {
    AtlasMovingLaneCaptureErrorCode code =
        AtlasMovingLaneCaptureErrorCode::invalid_session;
    std::string detail_code;
    std::string message;
    std::optional<EngineSessionError> session_error;
};

using AtlasMovingLaneCaptureResult =
    std::variant<AtlasMovingLaneCapture, AtlasMovingLaneCaptureError>;

// Executes the already compiled scenario exactly once. It performs no resampling,
// cycle extraction, signal processing, filesystem access, or threading.
[[nodiscard]] AtlasMovingLaneCaptureResult capture_atlas_moving_lane(
    const compile::CompiledScenario &scenario,
    std::span<const std::string_view> selected_bus_ids,
    double signed_load_coordinate);

} // namespace engine_sim_offline
