#include "crankwave/responsive/directional_cook.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crankwave::responsive {
namespace {

void require(contract::ValidationReport &report, const bool condition,
             const contract::ContractIssueCode code, std::string path,
             std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] std::string_view
direction_id(const DirectionalSweepDirection direction) noexcept {
    return direction == DirectionalSweepDirection::rising ? "rising" : "falling";
}

[[nodiscard]] double aligned_duration(const double seconds) noexcept {
    return std::ceil(seconds *
                     static_cast<double>(kDirectionalDurationAlignmentRateHz)) /
           static_cast<double>(kDirectionalDurationAlignmentRateHz);
}

[[nodiscard]] DirectionalSweepMotion
sweep_motion(const ResponsiveBakeProfile &profile,
             const DirectionalSweepDirection direction) {
    const double capture_minimum_rpm =
        std::max(50.0, profile.rpm.anchors.front() * 0.8);
    const double capture_maximum_rpm = profile.rpm.anchors.back() * 1.05;
    const bool rising = direction == DirectionalSweepDirection::rising;
    DirectionalSweepMotion result;
    result.start_rpm = rising ? capture_minimum_rpm : capture_maximum_rpm;
    result.end_rpm = rising ? capture_maximum_rpm : capture_minimum_rpm;
    const double raw_duration = std::abs(std::log(result.end_rpm / result.start_rpm)) /
                                kDirectionalLogarithmicRpmRatePerSecond;
    result.sweep_duration_seconds = aligned_duration(raw_duration);
    result.sweep_end_seconds =
        kDirectionalPreparationSeconds + result.sweep_duration_seconds;
    result.total_duration_seconds =
        aligned_duration(result.sweep_end_seconds + kDirectionalPostSweepHoldSeconds);
    result.points.push_back({0.0, result.start_rpm});
    result.points.push_back({kDirectionalPreparationSeconds, result.start_rpm});
    for (double elapsed = kDirectionalTrajectoryPointPeriodSeconds;
         elapsed < result.sweep_duration_seconds;
         elapsed += kDirectionalTrajectoryPointPeriodSeconds) {
        const double exponent = kDirectionalLogarithmicRpmRatePerSecond * elapsed;
        result.points.push_back({kDirectionalPreparationSeconds + elapsed,
                                 rising ? result.start_rpm * std::exp(exponent)
                                        : result.start_rpm * std::exp(-exponent)});
    }
    result.points.push_back({result.sweep_end_seconds, result.end_rpm});
    result.points.push_back({result.total_duration_seconds, result.end_rpm});
    return result;
}

[[nodiscard]] DirectionalSweepScenarioSpec
make_spec(const DirectionalScenarioPlanRequest &request,
          const DirectionalSweepDirection direction, const ResponsiveLoadLane &lane) {
    DirectionalSweepScenarioSpec result;
    result.direction = direction;
    result.lane = lane;
    result.motion = sweep_motion(request.profile, direction);
    result.rpm_anchors = request.profile.rpm.anchors;
    result.profile_selection_sha256 = request.profile.selection_identity_sha256;
    result.preparation_block_count = static_cast<std::uint64_t>(
        std::llround(kDirectionalPreparationSeconds *
                     static_cast<double>(kDirectionalDurationAlignmentRateHz)));
    result.total_block_count = static_cast<std::uint64_t>(
        std::llround(result.motion.total_duration_seconds *
                     static_cast<double>(kDirectionalDurationAlignmentRateHz)));
    result.id = request.scenario_template.engine.value + "-directional-" +
                std::string{direction_id(direction)} + "-" + lane.id + "-capture";
    result.scenario = request.scenario_template;
    result.scenario.schema = "crankwave/scenario";
    result.scenario.id.value = result.id;
    result.scenario.initial_state.engine_speed = {result.motion.start_rpm, "rpm",
                                                  std::nullopt};
    result.scenario.initial_state.ignition_enabled = true;
    result.scenario.initial_state.fuel_enabled = true;
    result.scenario.initial_state.starter_enabled = false;
    result.scenario.initial_state.dyno_enabled = true;
    result.scenario.initial_state.limiter_enabled = false;
    result.scenario.preparation = authoring::FixedHorizonPreparation{
        {kDirectionalPreparationSeconds, "s", std::nullopt}, 16U};

    authoring::QuantityTrajectory speed;
    speed.value_dimension = authoring::QuantityDimension::angular_speed;
    speed.interpolation = authoring::TrajectoryInterpolation::linear;
    speed.points.reserve(result.motion.points.size());
    for (const auto &point : result.motion.points) {
        speed.points.push_back({{point.time_seconds, "s", std::nullopt},
                                {point.engine_speed_rpm, "rpm", std::nullopt}});
    }
    const authoring::ScalarTrajectory throttle{
        authoring::TrajectoryInterpolation::right_continuous_hold,
        {{{0.0, "s", std::nullopt}, lane.throttle_01}}};
    result.scenario.mode = authoring::HeldDynoMode{std::move(speed),
                                                   {10'000.0, "N*m", std::nullopt},
                                                   {10'000.0, "N*m", std::nullopt},
                                                   throttle};
    result.scenario.events.clear();
    result.scenario.rates = {
        {kResponsivePhysicsRateHz, 1U, "Hz"},
        {kResponsivePhysicsRateHz, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
    };
    result.scenario.quality = {"listening", 3'840U, 7'600U, 1U};
    result.scenario.total_duration = {result.motion.total_duration_seconds, "s",
                                      std::nullopt};
    result.scenario.audible_start = {kDirectionalPreparationSeconds, "s", std::nullopt};
    result.scenario.audible_duration = {result.motion.total_duration_seconds -
                                            kDirectionalPreparationSeconds,
                                        "s", std::nullopt};
    result.scenario.public_seed = kDirectionalPublicSeed;
    result.scenario.output.telemetry_channels.clear();

    detail::CanonicalIdentityBytes identity{kDirectionalScenarioIdentityMethodId};
    identity.digest(request.scenario_template_sha256);
    identity.digest(request.profile.selection_identity_sha256);
    identity.string(result.id);
    identity.string(result.scenario.engine.value);
    identity.string(result.scenario.fuel.value);
    identity.string(direction_id(result.direction));
    identity.string(result.lane.id);
    identity.f64(result.lane.throttle_01);
    identity.f64(result.motion.start_rpm);
    identity.f64(result.motion.end_rpm);
    identity.f64(result.motion.sweep_duration_seconds);
    identity.f64(result.motion.sweep_end_seconds);
    identity.f64(result.motion.total_duration_seconds);
    identity.u64(result.motion.points.size());
    for (const auto &point : result.motion.points) {
        identity.f64(point.time_seconds);
        identity.f64(point.engine_speed_rpm);
    }
    identity.u64(result.preparation_block_count);
    identity.u64(result.total_block_count);
    identity.u64(result.scenario.output.buses.size());
    for (const auto &bus : result.scenario.output.buses) {
        identity.string(bus.value);
    }
    identity.string(kDirectionalCaptureMethodId);
    identity.string(kDirectionalSeamAlgorithmId);
    result.identity_sha256 = identity.finish();
    return result;
}

} // namespace

DirectionalCookResult<std::vector<DirectionalSweepScenarioSpec>>
plan_directional_sweep_scenarios(const DirectionalScenarioPlanRequest &request) {
    using enum contract::ContractIssueCode;
    auto report = validate_responsive_bake_profile(request.profile);
    require(report, !request.scenario_template_sha256.is_zero(), invalid_value,
            "scenario_template_sha256",
            "directional scenario template identity must be nonzero");
    require(report, !request.scenario_template.engine.value.empty(), missing_value,
            "scenario_template.engine",
            "directional scenario template must name an engine");
    require(report, !request.scenario_template.fuel.value.empty(), missing_value,
            "scenario_template.fuel", "directional scenario template must name a fuel");
    require(report,
            std::isfinite(request.scenario_template.initial_state.crank_angle.value),
            invalid_value, "scenario_template.initial_state.crank_angle",
            "directional scenario template crank angle must be finite");
    require(report, !request.scenario_template.output.buses.empty(), missing_value,
            "scenario_template.output.buses",
            "directional scenario template must select audio buses");
    if (!report.ok()) {
        return report;
    }

    std::vector<DirectionalSweepScenarioSpec> result;
    result.reserve(2U * kResponsiveLoadLaneCount);
    constexpr std::array directions{
        DirectionalSweepDirection::rising,
        DirectionalSweepDirection::falling,
    };
    for (const auto direction : directions) {
        for (const auto &lane : request.profile.capture.load_lanes) {
            result.push_back(make_spec(request, direction, lane));
        }
    }
    return result;
}

} // namespace crankwave::responsive
