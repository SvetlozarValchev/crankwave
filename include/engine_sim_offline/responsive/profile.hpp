#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/contract/common.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::responsive {

inline constexpr std::string_view kResponsiveBakeProfileSchema =
    "engine-sim-offline/responsive-audio-bake-profile-v1";
inline constexpr std::string_view kEngineRedlineAffineProfilePolicyId =
    "engine-redline-affine-v1";
inline constexpr std::string_view kAutomaticResponsiveProfileId =
    "interactive-preview-redline-v1";
inline constexpr std::string_view kResponsiveProfileIdentityMethodId =
    "engine-sim-offline.responsive-profile-selection.v1";

inline constexpr std::size_t kResponsiveRpmAnchorCount = 11U;
inline constexpr std::size_t kResponsiveLoadLaneCount = 3U;
inline constexpr std::uint32_t kResponsivePhysicsRateHz = 10'000U;

struct ResponsiveLoadLane {
    std::string id;
    double throttle_01 = 0.0;

    friend bool operator==(const ResponsiveLoadLane &,
                           const ResponsiveLoadLane &) = default;
};

struct ResponsiveRpmProfile {
    std::array<double, kResponsiveRpmAnchorCount> anchors{};
    double outer_minimum_rpm = 0.0;
    double outer_maximum_rpm = 0.0;
    double held_preparation_floor_seconds = 0.0;
    double held_extend_preparation_below_rpm = 0.0;

    friend bool operator==(const ResponsiveRpmProfile &,
                           const ResponsiveRpmProfile &) = default;
};

struct ResponsiveCaptureProfile {
    std::uint32_t physics_rate_hz = kResponsivePhysicsRateHz;
    std::array<ResponsiveLoadLane, kResponsiveLoadLaneCount> load_lanes{};

    friend bool operator==(const ResponsiveCaptureProfile &,
                           const ResponsiveCaptureProfile &) = default;
};

struct ResponsiveLifecycleProfile {
    bool enabled = true;
    bool shared_recorded_starter = true;
    double elevated_shutdown_rpm = 0.0;
    double elevated_shutdown_keyoff_seconds = 0.0;

    friend bool operator==(const ResponsiveLifecycleProfile &,
                           const ResponsiveLifecycleProfile &) = default;
};

// This is the fixed-complexity native selection result. The digest is over a
// versioned canonical binary preimage of every preceding field; it is not the
// SHA-256 of a JSON serialization and must not be described as one.
struct ResponsiveBakeProfile {
    std::string schema = std::string{kResponsiveBakeProfileSchema};
    std::string id = std::string{kAutomaticResponsiveProfileId};
    std::string selection_policy_id = std::string{kEngineRedlineAffineProfilePolicyId};
    ResponsiveRpmProfile rpm;
    ResponsiveCaptureProfile capture;
    ResponsiveLifecycleProfile lifecycle;
    contract::Sha256Digest selection_identity_sha256;

    friend bool operator==(const ResponsiveBakeProfile &,
                           const ResponsiveBakeProfile &) = default;
};

using ResponsiveBakeProfileSelectionResult =
    std::variant<ResponsiveBakeProfile, contract::ValidationReport>;

[[nodiscard]] contract::Sha256Digest
responsive_profile_identity(const ResponsiveBakeProfile &profile);

[[nodiscard]] contract::ValidationReport
validate_responsive_bake_profile(const ResponsiveBakeProfile &profile);

// Reproduces the installed engine-redline-affine-v1 policy. It reads only the
// authored engine redline; asset, route, runtime, and publication selection are
// deliberately outside this boundary.
[[nodiscard]] ResponsiveBakeProfileSelectionResult
derive_engine_redline_affine_profile(const authoring::EnginePackageDocument &engine);

} // namespace engine_sim_offline::responsive
