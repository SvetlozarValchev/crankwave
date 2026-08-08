#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/responsive/finite_capture.hpp"
#include "engine_sim_offline/responsive/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

inline constexpr std::string_view kResponsiveLifecycleSchema =
    "engine-sim-offline/responsive-audio-lifecycle";
inline constexpr std::string_view kLifecycleCaptureEvidenceSchema =
    "engine-sim-offline/lifecycle-capture-evidence";
inline constexpr std::string_view kStartupAdmissionFloorEvidenceSchema =
    "engine-sim-offline/startup-admission-floor-evidence";
inline constexpr std::string_view kContinuousStartupAdmissionSchema =
    "engine-sim-offline/continuous-startup-admission-v1";
inline constexpr std::string_view kLifecycleScenarioIdentityMethodId =
    "engine-sim-offline.responsive-lifecycle-scenario.v1";

inline constexpr std::uint32_t kLifecyclePhysicsRateHz = 10'000U;
inline constexpr std::uint32_t kLifecycleDeliveryRateHz = 192'000U;
inline constexpr std::uint32_t kLifecycleFramesPerBlock = 3'840U;
inline constexpr std::uint64_t kLifecycleFixedPresentationCoefficientCount = 30'071U;
inline constexpr std::uint64_t kLifecycleMaximumPresentationCoefficientCount = 570'654U;
inline constexpr std::uint32_t kLifecycleMaximumSeamFrames = 19'200U;
inline constexpr std::uint32_t kLifecycleEventLeadFrames = 32'640U;
inline constexpr std::uint32_t kLifecycleQuietTailFrames = 57'600U;
inline constexpr std::uint32_t kLifecyclePresentationFadeFrames = 3'840U;

enum class LifecycleScenarioRole : std::uint8_t {
    starter,
    startup_probe,
    startup,
    shutdown,
    shutdown_elevated,
};

struct LifecyclePublicationNames {
    // capture_role is written into lifecycle-capture-evidence and used as the
    // source/evidence/audio filename stem. provenance_role is the child runtime
    // manifest's scenario-role spelling. They differ only for elevated shutdown.
    std::string_view capture_role;
    std::string_view provenance_role;
};

[[nodiscard]] constexpr LifecyclePublicationNames
lifecycle_publication_names(const LifecycleScenarioRole role) noexcept {
    switch (role) {
    case LifecycleScenarioRole::starter:
        return {"starter", "starter"};
    case LifecycleScenarioRole::startup_probe:
        return {"startup-probe", "startup-probe"};
    case LifecycleScenarioRole::startup:
        return {"startup", "startup"};
    case LifecycleScenarioRole::shutdown:
        return {"shutdown", "shutdown"};
    case LifecycleScenarioRole::shutdown_elevated:
        return {"shutdown-high-rpm", "shutdown-elevated"};
    }
    return {"invalid", "invalid"};
}

enum class LifecycleErrorCode : std::uint8_t {
    invalid_request,
    cancelled,
    capture_mismatch,
    invalid_capture,
    no_admissible_event,
    no_admissible_seam,
    insufficient_quiet_tail,
    internal_error,
};

struct LifecycleError {
    LifecycleErrorCode code = LifecycleErrorCode::internal_error;
    std::string detail_code;
    std::string path;
    std::string message;

    friend bool operator==(const LifecycleError &, const LifecycleError &) = default;
};

template <class Value> using LifecycleResult = std::variant<Value, LifecycleError>;

struct LifecycleCapturePoint {
    std::uint64_t source_frame = 0U;
    std::uint64_t physics_step_end = 0U;
    double simulation_seconds = 0.0;
    double rpm = 0.0;
    bool ignition_enabled = false;
    bool fuel_enabled = false;
    bool starter_enabled = false;
    double indicated_gas_torque_nm = 0.0;
    // Exact lifecycle-capture-evidence encoding inherited from the C API/JS
    // oracle. Do not serialize contract::Availability's underlying ordinal.
    enum class PublishedAvailability : std::uint32_t {
        unavailable = 0U,
        available = 1U,
    };
    PublishedAvailability indicated_gas_availability =
        PublishedAvailability::unavailable;

    friend bool operator==(const LifecycleCapturePoint &,
                           const LifecycleCapturePoint &) = default;
};

struct LifecycleCaptureCycle {
    std::uint64_t start_frame = 0U;
    std::uint64_t end_frame = 0U;
    double mean_rpm = 0.0;

    friend bool operator==(const LifecycleCaptureCycle &,
                           const LifecycleCaptureCycle &) = default;
};

struct LifecycleDynamicStarterRelease {
    std::uint64_t physics_tick = 0U;
    double seconds = 0.0;
    LifecycleCapturePoint first_positive_combustion;
    LifecycleCapturePoint running_floor;
    LifecycleCaptureCycle release_cycle;

    friend bool operator==(const LifecycleDynamicStarterRelease &,
                           const LifecycleDynamicStarterRelease &) = default;
};

struct LifecycleScenarioPlanRequest {
    ResponsiveBakeProfile profile;
    authoring::EnginePackageDocument engine;
    authoring::ScenarioDocument trusted_template;
    contract::Sha256Digest trusted_template_sha256;
    std::uint64_t maximum_presentation_transfer_coefficient_count =
        kLifecycleFixedPresentationCoefficientCount;
};

struct LifecycleScenarioSpec {
    LifecycleScenarioRole role = LifecycleScenarioRole::starter;
    std::string canonical_source_id;
    std::string id;
    double audible_start_seconds = 0.0;
    double total_duration_seconds = 0.0;
    std::optional<double> ignition_event_seconds;
    std::optional<double> starter_release_seconds;
    std::optional<double> keyoff_resistance_nm;
    std::uint64_t preparation_block_count = 0U;
    std::uint64_t total_block_count = 0U;
    authoring::ScenarioDocument scenario;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const LifecycleScenarioSpec &,
                           const LifecycleScenarioSpec &) = default;
};

// Builds one trusted finite lifecycle scenario. startup requires a dynamic release;
// all other roles reject one. startup_probe deliberately has no starter-release
// event and is diagnostic-only.
[[nodiscard]] LifecycleResult<LifecycleScenarioSpec> plan_lifecycle_scenario(
    const LifecycleScenarioPlanRequest &request, LifecycleScenarioRole role,
    const std::optional<LifecycleDynamicStarterRelease> &release = std::nullopt);

struct LifecycleCaptureEvidence {
    LifecycleScenarioRole role = LifecycleScenarioRole::starter;
    std::string canonical_source_id;
    std::string engine_id;
    std::string scenario_id;
    std::string bus_id;
    std::uint32_t physics_rate_hz = 0U;
    std::uint32_t delivery_rate_hz = 0U;
    std::uint64_t audible_first_delivery_frame = 0U;
    std::uint64_t audible_frame_count = 0U;
    std::uint64_t final_physics_frame = 0U;
    std::uint64_t final_delivery_frame = 0U;
    std::uint64_t preparation_block_count = 0U;
    std::uint64_t total_block_count = 0U;
    std::vector<float> pcm;
    std::vector<LifecycleCapturePoint> points;
    std::vector<LifecycleCaptureCycle> completed_cycles;
    authoring::ScenarioDocument scenario;
    contract::Sha256Digest scenario_spec_sha256;
    contract::ProvenanceLedger engine_provenance;
    contract::ProvenanceLedger scenario_provenance;

    friend bool operator==(const LifecycleCaptureEvidence &,
                           const LifecycleCaptureEvidence &) = default;
};

// Selects one mono audible bus and maps every audible 20 ms endpoint plus every
// in-range completed cycle. Preparation PCM is absent, while its exact horizons
// remain represented by the aggregate counts and capture provenance.
[[nodiscard]] LifecycleResult<LifecycleCaptureEvidence>
map_lifecycle_capture(const LifecycleScenarioSpec &spec,
                      const FiniteResponsiveCapture &capture, std::size_t bus_index,
                      std::stop_token cancellation = {});

[[nodiscard]] LifecycleResult<LifecycleDynamicStarterRelease>
choose_dynamic_starter_release(const LifecycleCaptureEvidence &startup_probe,
                               double running_floor_rpm,
                               std::stop_token cancellation = {});

struct LifecycleSeam {
    std::uint64_t source_frame = 0U;
    std::uint32_t crossfade_frames = 0U;
    double source_rpm = 0.0;
    std::uint64_t target_source_frame = 0U;
    double target_rpm = 0.0;
    double correlation = 0.0;
    std::string target;
    std::string target_reference;

    friend bool operator==(const LifecycleSeam &, const LifecycleSeam &) = default;
};

struct LifecycleCheckpoint {
    std::string kind;
    std::uint64_t frame = 0U;
    double rpm = 0.0;
    std::string precision;
    std::string method;

    friend bool operator==(const LifecycleCheckpoint &,
                           const LifecycleCheckpoint &) = default;
};

struct LifecycleStarterPresentation {
    double mean_crank_rpm = 0.0;
    double reference_rpm = 0.0;
    std::uint64_t loop_start_frame = 0U;
    std::uint64_t loop_end_frame = 0U;
    std::uint32_t crossfade_frames = 0U;
    std::uint32_t attack_fade_frames = kLifecyclePresentationFadeFrames;
    std::uint32_t release_fade_frames = kLifecyclePresentationFadeFrames;

    friend bool operator==(const LifecycleStarterPresentation &,
                           const LifecycleStarterPresentation &) = default;
};

struct LifecycleCookedStarter {
    LifecycleStarterPresentation presentation;
    // Required only while choosing the startup entry seam; package encoders omit it.
    std::vector<LifecycleCaptureCycle> stable_cycles;

    friend bool operator==(const LifecycleCookedStarter &,
                           const LifecycleCookedStarter &) = default;
};

struct LifecycleStartupPresentation {
    std::vector<LifecycleCheckpoint> checkpoints;
    LifecycleSeam entry;
    LifecycleSeam exit;

    friend bool operator==(const LifecycleStartupPresentation &,
                           const LifecycleStartupPresentation &) = default;
};

struct LifecycleShutdownPresentation {
    std::vector<LifecycleCheckpoint> checkpoints;
    LifecycleSeam entry;
    std::uint64_t silence_frame = 0U;
    std::uint32_t exit_fade_frames = kLifecyclePresentationFadeFrames;
    std::uint64_t quiet_tail_frames = 0U;
    double quiet_peak_threshold = 0.0;
    double quiet_rms_threshold = 0.0;

    friend bool operator==(const LifecycleShutdownPresentation &,
                           const LifecycleShutdownPresentation &) = default;
};

[[nodiscard]] LifecycleResult<LifecycleCookedStarter>
cook_lifecycle_starter(const LifecycleCaptureEvidence &capture,
                       std::stop_token cancellation = {});

[[nodiscard]] LifecycleResult<LifecycleStartupPresentation> cook_lifecycle_startup(
    const LifecycleScenarioSpec &spec, const LifecycleCaptureEvidence &capture,
    const LifecycleCaptureEvidence &starter_capture,
    const LifecycleCookedStarter &starter, std::stop_token cancellation = {});

[[nodiscard]] LifecycleResult<LifecycleShutdownPresentation>
cook_lifecycle_shutdown(const LifecycleScenarioSpec &spec,
                        const LifecycleCaptureEvidence &capture,
                        std::stop_token cancellation = {});

[[nodiscard]] LifecycleResult<LifecycleShutdownPresentation>
cook_lifecycle_elevated_shutdown(const LifecycleScenarioSpec &spec,
                                 const LifecycleCaptureEvidence &capture,
                                 std::stop_token cancellation = {});

struct LifecycleStartupAdmissionLane {
    std::string id;
    double throttle_01 = 0.0;
    double floor_running_gain_linear = 0.0;

    friend bool operator==(const LifecycleStartupAdmissionLane &,
                           const LifecycleStartupAdmissionLane &) = default;
};

// Semantic data that is known before package serialization. It intentionally does
// not claim a held-manifest or evidence digest; those are attached by the encoder
// after their exact bytes exist.
struct LifecycleStartupAdmissionSeed {
    std::string candidate_status = "generic-variant-3-audition-not-per-engine-fitted";
    double running_floor_rpm = 0.0;
    double held_anchor_floor_rpm = 0.0;
    LifecycleDynamicStarterRelease release;
    std::vector<LifecycleStartupAdmissionLane> lanes;

    friend bool operator==(const LifecycleStartupAdmissionSeed &,
                           const LifecycleStartupAdmissionSeed &) = default;
};

[[nodiscard]] LifecycleResult<LifecycleStartupAdmissionSeed>
make_lifecycle_startup_admission_seed(const ResponsiveBakeProfile &profile,
                                      const LifecycleDynamicStarterRelease &release);

struct LifecycleHeldAtlasBinding {
    std::string manifest_path = "../../held/package.json";
    contract::Sha256Digest manifest_sha256;
    std::string load_coordinate;
};

struct LifecycleStartupAdmissionFloorEvidence {
    std::string schema = std::string{kStartupAdmissionFloorEvidenceSchema};
    std::string candidate_status;
    std::string atlas_manifest;
    contract::Sha256Digest atlas_manifest_sha256;
    std::string atlas_load_coordinate;
    double running_floor_rpm = 0.0;
    double held_anchor_floor_rpm = 0.0;
    struct ReleaseEvidence {
        std::string method = "first-complete-four-stroke-cycle-after-first-positive-"
                             "combustion-and-running-floor";
        double seconds = 0.0;
        std::uint64_t first_positive_combustion_frame = 0U;
        std::uint64_t running_floor_frame = 0U;
        LifecycleCaptureCycle release_cycle;
    } release;
    std::vector<LifecycleStartupAdmissionLane> lanes;
};

[[nodiscard]] LifecycleResult<LifecycleStartupAdmissionFloorEvidence>
bind_lifecycle_startup_admission_floor_evidence(
    const LifecycleStartupAdmissionSeed &seed, const LifecycleHeldAtlasBinding &held);

struct LifecycleEvidenceArtifactBinding {
    std::string path = "evidence/startup-admission.json";
    contract::Sha256Digest sha256;
};

struct LifecycleStartupAdmissionPresentation {
    std::string schema = std::string{kContinuousStartupAdmissionSchema};
    std::string running_bed_load_coordinate;
    std::string admission_lane_coordinate = "authored-throttle-01";
    std::string blend = "constant-power";
    std::string pre_floor_progress = "smoothstep-first-fire-rpm-to-running-floor";
    std::string completion_progress = "smoothstep-committed-crank-travel";
    double completion_crank_travel_revolutions = 2.0;
    bool monotone_ownership = true;
    std::vector<LifecycleStartupAdmissionLane> lanes;
    struct CoastStability {
        std::string lane_id = "coast";
        bool requires_starter_released = true;
        double post_peak_crank_travel_revolutions = 4.0;
        std::string clock_law = "lane-weighted-post-peak-admission";
    } coast_stability;
    struct Evidence {
        std::string method = "generic accepted Variant 3 semantic-lane gains; audition "
                             "candidate, not per-engine fitted";
        std::string path;
        contract::Sha256Digest sha256;
        contract::Sha256Digest corrected_held_manifest_sha256;
    } evidence;
};

[[nodiscard]] LifecycleResult<LifecycleStartupAdmissionPresentation>
bind_lifecycle_startup_admission_presentation(
    const LifecycleStartupAdmissionFloorEvidence &evidence,
    const LifecycleEvidenceArtifactBinding &artifact);

struct LifecycleCookedPackage {
    std::string schema = std::string{kResponsiveLifecycleSchema};
    std::string id;
    std::string engine_id;
    std::uint32_t sample_rate_hz = kLifecycleDeliveryRateHz;
    std::string encoding = "float32le";
    std::string channel_layout = "mono";
    std::string bus_id;
    LifecycleStarterPresentation starter;
    LifecycleStartupPresentation startup;
    LifecycleShutdownPresentation shutdown;
    std::optional<LifecycleShutdownPresentation> shutdown_elevated;
    LifecycleStartupAdmissionSeed startup_admission_seed;
    std::vector<LifecycleCaptureEvidence> captures;
    std::string representation =
        "fresh-10khz-physics-192khz-presented-master-lifecycle-performances-with-cycle-"
        "correlated-event-seams";
    std::string fidelity_alignment =
        "matches-current-responsive-preview;canonical-20khz-events-are-not-mixed-into-"
        "10khz-running-package";
    std::string checkpoint_alignment =
        "authored-control-times-are-exact-on-final-pcm;combustion-and-stop-use-one-"
        "block-c-api-telemetry-bounds;renderer-exposes-no-separate-audio-latency-field";
    std::string seam_reference_scope =
        "same-performance-final-master-cycle-correlation;the-responsive-held-"
        "representation-exposes-no-contiguous-final-master-reference-tape";
};

// Captures are moved into the aggregate so their large PCM buffers are never
// duplicated. The startup probe is deliberately excluded from publication.
[[nodiscard]] LifecycleResult<LifecycleCookedPackage> assemble_lifecycle_package(
    LifecycleCaptureEvidence starter_capture, LifecycleCaptureEvidence startup_capture,
    LifecycleCaptureEvidence shutdown_capture,
    std::optional<LifecycleCaptureEvidence> elevated_shutdown_capture,
    const LifecycleCookedStarter &starter, LifecycleStartupPresentation startup,
    LifecycleShutdownPresentation shutdown,
    std::optional<LifecycleShutdownPresentation> elevated_shutdown,
    LifecycleStartupAdmissionSeed startup_admission_seed);

} // namespace engine_sim_offline::responsive
