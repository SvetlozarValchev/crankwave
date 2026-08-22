#pragma once

#include "crankwave/authoring/scenario_document.hpp"
#include "crankwave/contract/common.hpp"
#include "crankwave/responsive/directional_texture.hpp"
#include "crankwave/responsive/finite_capture.hpp"
#include "crankwave/responsive/profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::responsive {

inline constexpr std::string_view kDirectionalCaptureMethodId =
    "prescribed-exponential-rpm-one-third-per-second-three-cycle-dry-routes-v3";
inline constexpr std::string_view kDirectionalSeamAlgorithmId =
    "phase-aligned-three-cycle-template-boundary-smoothstep-1of8-v1";
inline constexpr std::string_view kDirectionalScenarioIdentityMethodId =
    "crankwave.responsive-directional-scenario.v1";
inline constexpr std::string_view kDirectionalCellIdentityMethodId =
    "crankwave.responsive-directional-cell.v1";
inline constexpr std::string_view kDirectionalCaptureIdentityMethodId =
    "crankwave.responsive-directional-capture.v1";
inline constexpr std::string_view kDirectionalModelIdentityMethodId =
    "crankwave.responsive-directional-model.v1";
inline constexpr std::string_view kDirectionalLoadCoalescingMethodId =
    "coalesce-only-identical-coordinate-and-byte-identical-source-and-closed-payload";

inline constexpr double kDirectionalPreparationSeconds = 4.5;
inline constexpr double kDirectionalPostSweepHoldSeconds = 1.0;
inline constexpr double kDirectionalLogarithmicRpmRatePerSecond = 1.0 / 3.0;
inline constexpr double kDirectionalTrajectoryPointPeriodSeconds = 0.25;
inline constexpr std::uint32_t kDirectionalDurationAlignmentRateHz = 50U;
inline constexpr std::uint64_t kDirectionalPublicSeed = 12'648'430U;
inline constexpr double kDirectionalMaximumSeamOverDerivativeRms = 3.0;
inline constexpr double kDirectionalMaximumCorrectionOverSourceRms = 0.4;

template <class Value>
using DirectionalCookResult = std::variant<Value, contract::ValidationReport>;

struct DirectionalTrajectoryPoint {
    double time_seconds = 0.0;
    double engine_speed_rpm = 0.0;

    friend bool operator==(const DirectionalTrajectoryPoint &,
                           const DirectionalTrajectoryPoint &) = default;
};

struct DirectionalSweepMotion {
    double start_rpm = 0.0;
    double end_rpm = 0.0;
    double sweep_duration_seconds = 0.0;
    double sweep_end_seconds = 0.0;
    double total_duration_seconds = 0.0;
    std::vector<DirectionalTrajectoryPoint> points;

    friend bool operator==(const DirectionalSweepMotion &,
                           const DirectionalSweepMotion &) = default;
};

struct DirectionalScenarioPlanRequest {
    ResponsiveBakeProfile profile;
    authoring::ScenarioDocument scenario_template;
    contract::Sha256Digest scenario_template_sha256;
};

struct DirectionalSweepScenarioSpec {
    std::string id;
    DirectionalSweepDirection direction = DirectionalSweepDirection::rising;
    ResponsiveLoadLane lane;
    DirectionalSweepMotion motion;
    std::array<double, kResponsiveRpmAnchorCount> rpm_anchors{};
    contract::Sha256Digest profile_selection_sha256;
    std::uint64_t preparation_block_count = 0U;
    std::uint64_t total_block_count = 0U;
    authoring::ScenarioDocument scenario;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalSweepScenarioSpec &,
                           const DirectionalSweepScenarioSpec &) = default;
};

// Canonical order is rising coast/mid/power followed by falling coast/mid/power.
[[nodiscard]] DirectionalCookResult<std::vector<DirectionalSweepScenarioSpec>>
plan_directional_sweep_scenarios(const DirectionalScenarioPlanRequest &request);

[[nodiscard]] contract::Sha256Digest
directional_float32_payload_identity(std::span<const float> samples);

struct DirectionalCookedCell {
    std::string id;
    double rpm = 0.0;
    DirectionalSweepDirection direction = DirectionalSweepDirection::rising;
    std::string lane_id;
    double capture_throttle_01 = 0.0;
    double crossing_revolutions = 0.0;
    double source_cycle_begin_revolutions = 0.0;
    double source_cycle_end_revolutions = 0.0;
    std::vector<float> source;
    std::vector<float> seam_closed;
    DirectionalTelemetrySummary telemetry;
    DirectionalClosureMetrics closure;
    contract::Sha256Digest source_payload_sha256;
    contract::Sha256Digest seam_closed_payload_sha256;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalCookedCell &,
                           const DirectionalCookedCell &) = default;
};

struct DirectionalCookedRouteCapture {
    std::string bus_id;
    std::vector<DirectionalCookedCell> cells;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalCookedRouteCapture &,
                           const DirectionalCookedRouteCapture &) = default;
};

struct DirectionalCookedCapture {
    std::string id;
    std::string engine_id;
    DirectionalSweepDirection direction = DirectionalSweepDirection::rising;
    ResponsiveLoadLane lane;
    std::array<double, kResponsiveRpmAnchorCount> rpm_anchors{};
    contract::Sha256Digest profile_selection_sha256;
    std::vector<std::string> selected_bus_ids;
    std::vector<DirectionalCookedRouteCapture> routes;
    contract::Sha256Digest engine_provenance_sha256;
    contract::Sha256Digest scenario_provenance_sha256;
    contract::Sha256Digest scenario_spec_sha256;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalCookedCapture &,
                           const DirectionalCookedCapture &) = default;
};

// Transforms all anchors on every selected bus from one common finite capture.
[[nodiscard]] DirectionalCookResult<DirectionalCookedCapture>
cook_directional_capture(const DirectionalSweepScenarioSpec &spec,
                         const FiniteResponsiveCapture &capture);

struct DirectionalLoadAlias {
    std::string bus_id;
    DirectionalSweepDirection direction = DirectionalSweepDirection::rising;
    double rpm = 0.0;
    double manifold_pressure_pa_abs = 0.0;
    std::string retained_lane_id;
    std::string coalesced_lane_id;
    contract::Sha256Digest source_payload_sha256;
    contract::Sha256Digest seam_closed_payload_sha256;

    friend bool operator==(const DirectionalLoadAlias &,
                           const DirectionalLoadAlias &) = default;
};

struct DirectionalRouteDirectionModel {
    std::string bus_id;
    DirectionalSweepDirection direction = DirectionalSweepDirection::rising;
    std::vector<DirectionalCookedCell> cells;
    std::vector<DirectionalLoadAlias> coalesced_load_aliases;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalRouteDirectionModel &,
                           const DirectionalRouteDirectionModel &) = default;
};

struct DirectionalCombinedSeamGate {
    double maximum_seam_over_source_adjacent_derivative_rms =
        kDirectionalMaximumSeamOverDerivativeRms;
    double maximum_correction_rms_over_source_rms =
        kDirectionalMaximumCorrectionOverSourceRms;
    double observed_maximum_seam_over_source_adjacent_derivative_rms = 0.0;
    double observed_maximum_correction_rms_over_source_rms = 0.0;
    bool passed = false;

    friend bool operator==(const DirectionalCombinedSeamGate &,
                           const DirectionalCombinedSeamGate &) = default;
};

struct DirectionalCookedModel {
    std::string engine_id;
    double outer_minimum_rpm = 0.0;
    double outer_maximum_rpm = 0.0;
    std::array<double, kResponsiveRpmAnchorCount> rpm_anchors{};
    std::vector<std::string> selected_bus_ids;
    // Bus-major, then rising/falling. Each cell list is lane-major, then RPM.
    std::vector<DirectionalRouteDirectionModel> route_directions;
    DirectionalCombinedSeamGate combined_seam_gate;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const DirectionalCookedModel &,
                           const DirectionalCookedModel &) = default;
};

// Takes ownership so the manifest-ready model can move the large Float32 payloads
// instead of duplicating them.
[[nodiscard]] DirectionalCookResult<DirectionalCookedModel>
assemble_directional_model(const ResponsiveBakeProfile &profile,
                           std::vector<DirectionalCookedCapture> captures);

} // namespace crankwave::responsive
