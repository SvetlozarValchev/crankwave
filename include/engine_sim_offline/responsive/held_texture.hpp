#pragma once

#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/responsive/finite_capture.hpp"
#include "engine_sim_offline/responsive/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

inline constexpr std::string_view kHeldCaptureMethodId =
    "exact-held-rpm-48-cycle-20ms-exact-sum-horizon-v8";
inline constexpr std::string_view kHeldDecompositionMethodId =
    "cyclic-mean-plus-boundary-zero-smoothstep-residual-bank-v1";
inline constexpr std::string_view kHeldPhaseAlignmentMethodId =
    "shared-route-sum-circular-correlation-unwrapped-grid-v1";
inline constexpr std::string_view kHeldStateIdentityMethodId =
    "engine-sim-offline.responsive-held-state.v1";
inline constexpr std::string_view kHeldTextureIdentityMethodId =
    "engine-sim-offline.responsive-held-texture.v1";
inline constexpr std::string_view kHeldGridIdentityMethodId =
    "engine-sim-offline.responsive-held-grid.v1";
inline constexpr std::string_view kHeldLoadCoalescingMethodId =
    "exact-map-and-route-payload-duplicate-load-coalescing-v1";

inline constexpr std::size_t kHeldSamplesPerCycle = 4'096U;
inline constexpr std::size_t kHeldCapturedCycleCount = 48U;
inline constexpr std::size_t kHeldNormalizedSampleCount =
    kHeldSamplesPerCycle * kHeldCapturedCycleCount;
inline constexpr double kHeldCycleRevolutions = 2.0;
inline constexpr std::size_t kHeldGuardCycleCountBefore = 4U;
inline constexpr std::size_t kHeldGuardCycleCountAfter = 4U;
inline constexpr double kHeldDurationMarginSeconds = 0.5;
inline constexpr std::uint32_t kHeldDurationAlignmentRateHz = 50U;
inline constexpr double kHeldResidualTaperFractionPerEdge = 1.0 / 16.0;
inline constexpr std::uint64_t kHeldPublicSeed = 12'648'430U;

template <class Value>
using HeldResult = std::variant<Value, contract::ValidationReport>;

enum class HeldCaptureOperatingMode : std::uint8_t {
    held_speed,
    held_dyno,
};

struct HeldScenarioPlanRequest {
    ResponsiveBakeProfile profile;
    authoring::ScenarioDocument scenario_template;
    contract::Sha256Digest scenario_template_sha256;
    HeldCaptureOperatingMode operating_mode = HeldCaptureOperatingMode::held_speed;
};

struct HeldStateScenarioSpec {
    std::string id;
    double rpm = 0.0;
    ResponsiveLoadLane lane;
    double preparation_duration_seconds = 0.0;
    double audible_duration_seconds = 0.0;
    double total_duration_seconds = 0.0;
    std::uint64_t preparation_block_count = 0U;
    std::uint64_t total_block_count = 0U;
    authoring::ScenarioDocument scenario;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const HeldStateScenarioSpec &,
                           const HeldStateScenarioSpec &) = default;
};

[[nodiscard]] HeldResult<std::vector<HeldStateScenarioSpec>>
plan_held_state_scenarios(const HeldScenarioPlanRequest &request);

struct HeldCaptureInterval {
    double start_revolutions = 0.0;
    double end_revolutions = 0.0;

    friend bool operator==(const HeldCaptureInterval &,
                           const HeldCaptureInterval &) = default;
};

struct HeldTextureMetrics {
    double source_rms = 0.0;
    double mean_rms = 0.0;
    double raw_residual_rms_over_source_rms = 0.0;
    double tapered_residual_rms_over_source_rms = 0.0;
    double reconstruction_error_rms_over_source_rms = 0.0;
    double mean_seam_absolute_delta = 0.0;
    double mean_seam_over_mean_derivative_rms = 0.0;
    double mean_closure_correction_absolute = 0.0;
    double mean_closure_correction_over_source_rms = 0.0;
    double maximum_residual_boundary_magnitude = 0.0;
    std::uint32_t residual_taper_frames_per_edge = 0U;

    friend bool operator==(const HeldTextureMetrics &,
                           const HeldTextureMetrics &) = default;
};

struct HeldTextureDecomposition {
    std::vector<float> mean;
    std::vector<float> residuals;
    HeldTextureMetrics metrics;

    friend bool operator==(const HeldTextureDecomposition &,
                           const HeldTextureDecomposition &) = default;
};

struct HeldTelemetrySummary {
    double mean_manifold_pressure_pa_abs = 0.0;
    double rpm_error_rms = 0.0;
    double maximum_absolute_rpm_error = 0.0;
    std::uint64_t telemetry_endpoint_count = 0U;

    friend bool operator==(const HeldTelemetrySummary &,
                           const HeldTelemetrySummary &) = default;
};

[[nodiscard]] HeldResult<HeldCaptureInterval>
choose_held_capture_interval(const FiniteResponsiveCapture &capture);

[[nodiscard]] HeldResult<std::vector<float>>
phase_normalize_held_capture(const FiniteResponsiveCapture &capture,
                             std::size_t bus_index,
                             const HeldCaptureInterval &interval);

[[nodiscard]] HeldResult<HeldTextureDecomposition>
decompose_held_texture(std::span<const float> normalized_source);

[[nodiscard]] HeldResult<HeldTelemetrySummary>
summarize_held_interval_telemetry(const FiniteResponsiveCapture &capture,
                                  const HeldCaptureInterval &interval,
                                  double target_rpm);

[[nodiscard]] contract::Sha256Digest
held_float32_payload_identity(std::span<const float> samples);

struct HeldCookedRoute {
    std::string route_id;
    HeldCaptureInterval source_interval;
    HeldTextureDecomposition texture;
    HeldTelemetrySummary telemetry;
    contract::Sha256Digest mean_payload_sha256;
    contract::Sha256Digest residual_payload_sha256;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const HeldCookedRoute &, const HeldCookedRoute &) = default;
};

struct HeldCookedCell {
    std::string id;
    double rpm = 0.0;
    std::string lane_id;
    double throttle_01 = 0.0;
    std::vector<HeldCookedRoute> routes;
    std::vector<std::string> coalesced_authored_lanes;
    std::vector<double> coalesced_capture_throttles_01;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const HeldCookedCell &, const HeldCookedCell &) = default;
};

// Transforms every selected bus from the one common finite capture. No route is
// re-simulated and all routes consume the same endpoint timeline.
[[nodiscard]] HeldResult<HeldCookedCell>
cook_held_state(const HeldStateScenarioSpec &spec,
                const FiniteResponsiveCapture &capture);

[[nodiscard]] HeldResult<std::vector<HeldCookedCell>>
coalesce_duplicate_held_loads(std::span<const HeldCookedCell> cells);

struct HeldDistributionSummary {
    double minimum = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double maximum = 0.0;

    friend bool operator==(const HeldDistributionSummary &,
                           const HeldDistributionSummary &) = default;
};

enum class HeldAlignmentAxis : std::uint8_t {
    rpm,
    load,
};

struct HeldPhaseShift {
    std::string cell_id;
    double shift_to_canonical_samples = 0.0;

    friend bool operator==(const HeldPhaseShift &, const HeldPhaseShift &) = default;
};

struct HeldPriorPhaseAlignment {
    std::string reference_cell_id;
    std::vector<HeldPhaseShift> cells;

    friend bool operator==(const HeldPriorPhaseAlignment &,
                           const HeldPriorPhaseAlignment &) = default;
};

struct HeldAdjacentAlignmentEdge {
    std::string left_cell_id;
    std::string right_cell_id;
    HeldAlignmentAxis axis = HeldAlignmentAxis::rpm;
    double constrained_shift_right_to_left_samples = 0.0;
    double constrained_peak_correlation = 0.0;
    double ambiguity_margin = 0.0;
    double unconstrained_shift_right_to_left_samples = 0.0;
    double unconstrained_peak_correlation = 0.0;
    double lifted_shift_right_to_left_samples = 0.0;
    double solved_shift_delta_samples = 0.0;
    double weight = 0.0;

    friend bool operator==(const HeldAdjacentAlignmentEdge &,
                           const HeldAdjacentAlignmentEdge &) = default;
};

struct HeldMidpointComparison {
    std::string left_cell_id;
    std::string right_cell_id;
    HeldAlignmentAxis axis = HeldAlignmentAxis::rpm;
    double direct_midpoint_rms = 0.0;
    double aligned_midpoint_rms = 0.0;
    double linear_anchor_rms_target = 0.0;
    double direct_retained_target_01 = 0.0;
    double aligned_retained_target_01 = 0.0;

    friend bool operator==(const HeldMidpointComparison &,
                           const HeldMidpointComparison &) = default;
};

struct HeldPhaseAlignmentReport {
    std::uint64_t edge_count = 0U;
    HeldDistributionSummary edge_residual_samples;
    double minimum_peak_correlation = 0.0;
    double minimum_ambiguity_margin = 0.0;
    std::uint64_t unconstrained_peak_outside_adjacent_window_count = 0U;
    HeldDistributionSummary direct_midpoint_retained_target_01;
    HeldDistributionSummary aligned_midpoint_retained_target_01;
    std::vector<HeldMidpointComparison> midpoint_comparisons;
    std::uint64_t preserved_fixed_cell_count = 0U;
    double maximum_preserved_fixed_shift_error_samples = 0.0;
    std::vector<HeldAdjacentAlignmentEdge> adjacent_edges;

    friend bool operator==(const HeldPhaseAlignmentReport &,
                           const HeldPhaseAlignmentReport &) = default;
};

struct HeldPhaseAlignment {
    std::string method = std::string{kHeldPhaseAlignmentMethodId};
    std::string reference_cell_id;
    std::vector<HeldPhaseShift> cells;
    HeldPhaseAlignmentReport report;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const HeldPhaseAlignment &,
                           const HeldPhaseAlignment &) = default;
};

[[nodiscard]] HeldResult<HeldPhaseAlignment> align_held_texture_grid(
    const ResponsiveBakeProfile &profile,
    std::span<const HeldCookedCell> uncoalesced_cells,
    const std::optional<HeldPriorPhaseAlignment> &prior = std::nullopt);

struct HeldCookedGrid {
    std::vector<HeldCookedCell> cells;
    HeldPhaseAlignment phase_alignment;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const HeldCookedGrid &, const HeldCookedGrid &) = default;
};

// Alignment is solved on the complete authored rectangle. Duplicate-load cells
// are then coalesced and the published shift list is filtered to retained cells,
// matching the current package semantics.
[[nodiscard]] HeldResult<HeldCookedGrid> cook_held_texture_grid(
    const ResponsiveBakeProfile &profile,
    std::span<const HeldCookedCell> uncoalesced_cells,
    const std::optional<HeldPriorPhaseAlignment> &prior = std::nullopt);

} // namespace engine_sim_offline::responsive
