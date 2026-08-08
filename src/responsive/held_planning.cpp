#include "engine_sim_offline/responsive/held_texture.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {
namespace {

void require(contract::ValidationReport &report, const bool condition,
             const contract::ContractIssueCode code, std::string path,
             std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] std::string rpm_token(const double rpm) {
    char storage[64]{};
    const auto [end, error] = std::to_chars(storage, storage + sizeof(storage), rpm);
    if (error != std::errc{}) {
        return {};
    }
    return std::string{storage, end};
}

[[nodiscard]] double preparation_duration_seconds(const ResponsiveBakeProfile &profile,
                                                  const double rpm) {
    const double floor_seconds = profile.rpm.held_preparation_floor_seconds;
    // Preserve the installed 18-cycle settling margin below the authored
    // threshold. At and above it, the three-second floor is usually sufficient,
    // but redline-affine profiles can compress that threshold below 680 RPM. The
    // compiler requires room for trailing_complete_cycle_count + 1 complete
    // cycles, so retain that exact conservative 17-cycle bound everywhere.
    const double preparation_cycle_count =
        rpm < profile.rpm.held_extend_preparation_below_rpm ? 18.0 : 17.0;
    const double cycle_bound_seconds =
        std::ceil((preparation_cycle_count * 120.0 *
                   kHeldDurationAlignmentRateHz) /
                  rpm) /
        kHeldDurationAlignmentRateHz;
    return std::max(floor_seconds, cycle_bound_seconds);
}

[[nodiscard]] HeldStateScenarioSpec make_spec(const HeldScenarioPlanRequest &request,
                                              const double rpm,
                                              const ResponsiveLoadLane &lane) {
    const double preparation_seconds =
        preparation_duration_seconds(request.profile, rpm);
    constexpr double guarded_cycle_count =
        static_cast<double>(kHeldCapturedCycleCount + kHeldGuardCycleCountBefore +
                            kHeldGuardCycleCountAfter);
    const double requested_audible_seconds =
        guarded_cycle_count * 120.0 / rpm + kHeldDurationMarginSeconds;
    std::uint64_t audible_quanta = static_cast<std::uint64_t>(
        std::ceil(requested_audible_seconds * kHeldDurationAlignmentRateHz));
    const auto preparation_quanta = static_cast<std::uint64_t>(
        std::llround(preparation_seconds * kHeldDurationAlignmentRateHz));
    while (preparation_seconds +
               static_cast<double>(audible_quanta) / kHeldDurationAlignmentRateHz !=
           static_cast<double>(preparation_quanta + audible_quanta) /
               kHeldDurationAlignmentRateHz) {
        ++audible_quanta;
    }
    const double audible_seconds =
        static_cast<double>(audible_quanta) / kHeldDurationAlignmentRateHz;
    const auto total_quanta = preparation_quanta + audible_quanta;
    const double total_seconds =
        static_cast<double>(total_quanta) / kHeldDurationAlignmentRateHz;

    HeldStateScenarioSpec result;
    result.id = rpm_token(rpm) + "rpm-" + lane.id;
    result.rpm = rpm;
    result.lane = lane;
    result.preparation_duration_seconds = preparation_seconds;
    result.audible_duration_seconds = audible_seconds;
    result.total_duration_seconds = total_seconds;
    result.preparation_block_count = preparation_quanta;
    result.total_block_count = total_quanta;
    result.scenario = request.scenario_template;
    result.scenario.schema = "engine-sim-offline/scenario";
    result.scenario.id.value = result.scenario.engine.value +
                               "-held-texture-live-preview-" + rpm_token(rpm) + "rpm-" +
                               lane.id;
    result.scenario.initial_state.engine_speed = {rpm, "rpm", std::nullopt};
    result.scenario.initial_state.ignition_enabled = true;
    result.scenario.initial_state.fuel_enabled = true;
    result.scenario.initial_state.starter_enabled = false;
    result.scenario.initial_state.dyno_enabled = true;
    result.scenario.initial_state.limiter_enabled = false;
    result.scenario.preparation = authoring::FixedHorizonPreparation{
        {preparation_seconds, "s", std::nullopt}, 16U};
    const authoring::ScalarTrajectory throttle{
        authoring::TrajectoryInterpolation::right_continuous_hold,
        {{{0.0, "s", std::nullopt}, lane.throttle_01}}};
    if (request.operating_mode == HeldCaptureOperatingMode::held_speed) {
        result.scenario.mode =
            authoring::HeldSpeedMode{{rpm, "rpm", std::nullopt}, throttle};
    } else {
        result.scenario.mode = authoring::HeldDynoMode{
            {authoring::QuantityDimension::angular_speed,
             authoring::TrajectoryInterpolation::linear,
             {{{0.0, "s", std::nullopt}, {rpm, "rpm", std::nullopt}},
              {{total_seconds, "s", std::nullopt}, {rpm, "rpm", std::nullopt}}}},
            {10'000.0, "N*m", std::nullopt},
            {10'000.0, "N*m", std::nullopt},
            throttle};
    }
    result.scenario.events.clear();
    result.scenario.rates = {
        {kResponsivePhysicsRateHz, 1U, "Hz"},
        {kResponsivePhysicsRateHz, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
        {192'000U, 1U, "Hz"},
    };
    result.scenario.quality = {"listening", 3'840U, 7'600U, 1U};
    result.scenario.total_duration = {total_seconds, "s", std::nullopt};
    result.scenario.audible_start = {preparation_seconds, "s", std::nullopt};
    result.scenario.audible_duration = {audible_seconds, "s", std::nullopt};
    result.scenario.public_seed = kHeldPublicSeed;
    result.scenario.output.telemetry_channels.clear();

    detail::CanonicalIdentityBytes identity{kHeldStateIdentityMethodId};
    identity.digest(request.scenario_template_sha256);
    identity.digest(request.profile.selection_identity_sha256);
    identity.u32(static_cast<std::uint32_t>(request.operating_mode));
    identity.string(result.id);
    identity.string(result.scenario.id.value);
    identity.string(result.scenario.engine.value);
    identity.string(result.scenario.fuel.value);
    identity.f64(result.rpm);
    identity.string(result.lane.id);
    identity.f64(result.lane.throttle_01);
    identity.f64(result.preparation_duration_seconds);
    identity.f64(result.audible_duration_seconds);
    identity.f64(result.total_duration_seconds);
    identity.u64(result.preparation_block_count);
    identity.u64(result.total_block_count);
    identity.string(kHeldCaptureMethodId);
    identity.string(kHeldDecompositionMethodId);
    result.identity_sha256 = identity.finish();
    return result;
}

} // namespace

HeldResult<std::vector<HeldStateScenarioSpec>>
plan_held_state_scenarios(const HeldScenarioPlanRequest &request) {
    using enum contract::ContractIssueCode;
    auto report = validate_responsive_bake_profile(request.profile);
    require(report, !request.scenario_template_sha256.is_zero(), invalid_value,
            "scenario_template_sha256",
            "held scenario template identity must be nonzero");
    require(report, !request.scenario_template.engine.value.empty(), missing_value,
            "scenario_template.engine", "held scenario template must name an engine");
    require(report, !request.scenario_template.fuel.value.empty(), missing_value,
            "scenario_template.fuel", "held scenario template must name a fuel");
    require(report,
            std::isfinite(request.scenario_template.initial_state.crank_angle.value),
            invalid_value, "scenario_template.initial_state.crank_angle",
            "held scenario template crank angle must be finite");
    require(report, !request.scenario_template.output.buses.empty(), missing_value,
            "scenario_template.output.buses",
            "held scenario template must select audio buses");
    if (!report.ok()) {
        return report;
    }

    std::vector<HeldStateScenarioSpec> result;
    result.reserve(kResponsiveRpmAnchorCount * kResponsiveLoadLaneCount);
    for (const double rpm : request.profile.rpm.anchors) {
        for (const auto &lane : request.profile.capture.load_lanes) {
            result.push_back(make_spec(request, rpm, lane));
        }
    }
    return result;
}

} // namespace engine_sim_offline::responsive
