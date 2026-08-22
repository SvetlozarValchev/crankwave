#include "crankwave/responsive/profile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace crankwave::responsive {
namespace {

constexpr std::array<double, kResponsiveRpmAnchorCount> kReferenceAnchors{
    600.0,  700.0,  900.0,  1200.0, 1600.0, 2200.0,
    3000.0, 4000.0, 5000.0, 6000.0, 6500.0};
constexpr double kReferenceMinimumRpm = 600.0;
constexpr double kReferenceRedlineRpm = 6500.0;
constexpr double kReferenceSpanRpm = kReferenceRedlineRpm - kReferenceMinimumRpm;
constexpr double kReferenceOuterMinimumRpm = 550.0;
constexpr double kReferenceOuterMaximumRpm = 6700.0;
constexpr double kReferencePreparationExtensionRpm = 700.0;
constexpr double kReferenceElevatedShutdownRpm = 3000.0;
constexpr double kMinimumAutomaticRedlineRpm = 250.0;
constexpr double kRpmRoundingFactor = 1'000'000.0;

void append_u64(std::vector<std::byte> &bytes, const std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_u32(std::vector<std::byte> &bytes, const std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_bool(std::vector<std::byte> &bytes, const bool value) {
    bytes.push_back(value ? std::byte{1U} : std::byte{0U});
}

void append_f64(std::vector<std::byte> &bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_string(std::vector<std::byte> &bytes, const std::string_view value) {
    append_u64(bytes, value.size());
    const auto payload =
        std::as_bytes(std::span<const char>{value.data(), value.size()});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void require(contract::ValidationReport &report, const bool condition,
             const contract::ContractIssueCode code, std::string path,
             std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool positive_finite(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] double canonical_rpm(const double value) noexcept {
    return std::floor(value * kRpmRoundingFactor + 0.5) / kRpmRoundingFactor;
}

} // namespace

contract::Sha256Digest
responsive_profile_identity(const ResponsiveBakeProfile &profile) {
    std::vector<std::byte> bytes;
    bytes.reserve(512U);
    append_string(bytes, kResponsiveProfileIdentityMethodId);
    append_string(bytes, profile.schema);
    append_string(bytes, profile.id);
    append_string(bytes, profile.selection_policy_id);
    append_u64(bytes, profile.rpm.anchors.size());
    for (const double rpm : profile.rpm.anchors) {
        append_f64(bytes, rpm);
    }
    append_f64(bytes, profile.rpm.outer_minimum_rpm);
    append_f64(bytes, profile.rpm.outer_maximum_rpm);
    append_f64(bytes, profile.rpm.held_preparation_floor_seconds);
    append_f64(bytes, profile.rpm.held_extend_preparation_below_rpm);
    append_u32(bytes, profile.capture.physics_rate_hz);
    append_u64(bytes, profile.capture.load_lanes.size());
    for (const auto &lane : profile.capture.load_lanes) {
        append_string(bytes, lane.id);
        append_f64(bytes, lane.throttle_01);
    }
    append_bool(bytes, profile.lifecycle.enabled);
    append_bool(bytes, profile.lifecycle.shared_recorded_starter);
    append_f64(bytes, profile.lifecycle.elevated_shutdown_rpm);
    append_f64(bytes, profile.lifecycle.elevated_shutdown_keyoff_seconds);
    return contract::sha256(bytes);
}

contract::ValidationReport
validate_responsive_bake_profile(const ResponsiveBakeProfile &profile) {
    using enum contract::ContractIssueCode;
    contract::ValidationReport report;
    require(report, profile.schema == kResponsiveBakeProfileSchema, unsupported_value,
            "schema", "responsive profile schema is unsupported");
    require(report, profile.id == kAutomaticResponsiveProfileId, unsupported_value,
            "id", "responsive profile ID is not the automatic native profile");
    require(report, profile.selection_policy_id == kEngineRedlineAffineProfilePolicyId,
            unsupported_value, "selection_policy_id",
            "responsive profile selection policy is unsupported");

    for (std::size_t index = 0U; index < profile.rpm.anchors.size(); ++index) {
        const double value = profile.rpm.anchors[index];
        require(report, positive_finite(value), invalid_value,
                "rpm.anchors[" + std::to_string(index) + "]",
                "RPM anchor must be finite and positive");
        if (index != 0U) {
            require(report, value > profile.rpm.anchors[index - 1U],
                    inconsistent_semantics,
                    "rpm.anchors[" + std::to_string(index) + "]",
                    "RPM anchors must be strictly increasing");
        }
    }
    require(report, positive_finite(profile.rpm.outer_minimum_rpm), invalid_value,
            "rpm.outer_minimum_rpm", "outer minimum RPM must be finite and positive");
    require(report, positive_finite(profile.rpm.outer_maximum_rpm), invalid_value,
            "rpm.outer_maximum_rpm", "outer maximum RPM must be finite and positive");
    require(report,
            profile.rpm.outer_minimum_rpm <= profile.rpm.anchors.front() &&
                profile.rpm.outer_maximum_rpm >= profile.rpm.anchors.back() &&
                profile.rpm.outer_maximum_rpm > profile.rpm.outer_minimum_rpm,
            inconsistent_semantics, "rpm",
            "outer RPM domain must contain every anchor");
    require(report,
            std::max(50.0, profile.rpm.anchors.front() * 0.8) <=
                    profile.rpm.outer_minimum_rpm &&
                profile.rpm.anchors.back() * 1.05 >= profile.rpm.outer_maximum_rpm,
            inconsistent_semantics, "rpm",
            "outer RPM domain exceeds the directional capture envelope");
    require(
        report,
        positive_finite(profile.rpm.held_preparation_floor_seconds) &&
            profile.rpm.held_preparation_floor_seconds >= 3.0 &&
            profile.rpm.held_preparation_floor_seconds * 50.0 ==
                std::round(profile.rpm.held_preparation_floor_seconds * 50.0),
        invalid_value, "rpm.held_preparation_floor_seconds",
        "held preparation floor must be at least three seconds on a 20 ms boundary");
    require(report, positive_finite(profile.rpm.held_extend_preparation_below_rpm),
            invalid_value, "rpm.held_extend_preparation_below_rpm",
            "held preparation extension threshold must be finite and positive");

    require(report, profile.capture.physics_rate_hz == kResponsivePhysicsRateHz,
            unsupported_value, "capture.physics_rate_hz",
            "responsive capture requires 10 kHz physics");
    constexpr std::array<std::string_view, kResponsiveLoadLaneCount> lane_ids{
        "coast", "mid", "power"};
    constexpr std::array<double, kResponsiveLoadLaneCount> lane_throttles{0.0, 0.2,
                                                                          1.0};
    for (std::size_t index = 0U; index < profile.capture.load_lanes.size(); ++index) {
        const auto &lane = profile.capture.load_lanes[index];
        require(report,
                lane.id == lane_ids[index] && lane.throttle_01 == lane_throttles[index],
                unsupported_value, "capture.load_lanes[" + std::to_string(index) + "]",
                "load lanes must equal the accepted coast/mid/power grid");
    }

    require(report,
            !profile.lifecycle.shared_recorded_starter || profile.lifecycle.enabled,
            inconsistent_semantics, "lifecycle.shared_recorded_starter",
            "shared recorded starter requires lifecycle capture");
    require(
        report,
        positive_finite(profile.lifecycle.elevated_shutdown_rpm) &&
            profile.lifecycle.elevated_shutdown_rpm >= profile.rpm.outer_minimum_rpm &&
            profile.lifecycle.elevated_shutdown_rpm <= profile.rpm.outer_maximum_rpm,
        invalid_value, "lifecycle.elevated_shutdown_rpm",
        "elevated shutdown RPM must lie inside the responsive domain");
    require(report, positive_finite(profile.lifecycle.elevated_shutdown_keyoff_seconds),
            invalid_value, "lifecycle.elevated_shutdown_keyoff_seconds",
            "elevated shutdown key-off time must be finite and positive");
    require(report,
            profile.selection_identity_sha256 == responsive_profile_identity(profile),
            invalid_value, "selection_identity_sha256",
            "responsive profile selection identity does not match its fields");
    return report;
}

ResponsiveBakeProfileSelectionResult
derive_engine_redline_affine_profile(const authoring::EnginePackageDocument &engine) {
    using enum contract::ContractIssueCode;
    contract::ValidationReport report;
    const auto &redline = engine.engine.limits.redline;
    require(report, redline.unit == "rpm", unsupported_value,
            "engine.limits.redline.unit",
            "automatic responsive profile requires an RPM redline");
    require(report, positive_finite(redline.value), invalid_value,
            "engine.limits.redline.value",
            "engine redline must be finite and positive");
    require(report,
            positive_finite(redline.value) &&
                redline.value >= kMinimumAutomaticRedlineRpm,
            unsupported_value, "engine.limits.redline.value",
            "engine-redline-affine-v1 requires a redline of at least 250 RPM");
    if (!report.ok()) {
        return report;
    }

    const double minimum_rpm = std::min(kReferenceMinimumRpm, redline.value / 5.0);
    const double span_rpm = redline.value - minimum_rpm;
    const auto map_reference_rpm = [&](const double reference_rpm) {
        return canonical_rpm(
            minimum_rpm +
            ((reference_rpm - kReferenceMinimumRpm) / kReferenceSpanRpm) * span_rpm);
    };

    ResponsiveBakeProfile profile;
    for (std::size_t index = 0U; index < kReferenceAnchors.size(); ++index) {
        profile.rpm.anchors[index] = map_reference_rpm(kReferenceAnchors[index]);
    }
    profile.rpm.anchors.front() = canonical_rpm(minimum_rpm);
    // The authored redline remains exact even when it carries more than six
    // decimal places; this matches the installed JavaScript policy.
    profile.rpm.anchors.back() = redline.value;
    profile.rpm.outer_minimum_rpm =
        std::max({50.0, profile.rpm.anchors.front() * 0.8,
                  map_reference_rpm(kReferenceOuterMinimumRpm)});
    profile.rpm.outer_maximum_rpm = map_reference_rpm(kReferenceOuterMaximumRpm);
    profile.rpm.held_preparation_floor_seconds = 3.0;
    profile.rpm.held_extend_preparation_below_rpm =
        map_reference_rpm(kReferencePreparationExtensionRpm);
    profile.capture.physics_rate_hz = kResponsivePhysicsRateHz;
    profile.capture.load_lanes = {
        ResponsiveLoadLane{"coast", 0.0},
        ResponsiveLoadLane{"mid", 0.2},
        ResponsiveLoadLane{"power", 1.0},
    };
    profile.lifecycle.enabled = true;
    profile.lifecycle.shared_recorded_starter = true;
    profile.lifecycle.elevated_shutdown_rpm =
        map_reference_rpm(kReferenceElevatedShutdownRpm);
    profile.lifecycle.elevated_shutdown_keyoff_seconds = 0.16;
    profile.selection_identity_sha256 = responsive_profile_identity(profile);

    report = validate_responsive_bake_profile(profile);
    if (!report.ok()) {
        return report;
    }
    return profile;
}

} // namespace crankwave::responsive
