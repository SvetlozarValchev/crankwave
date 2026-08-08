#include "engine_sim_offline/responsive/lifecycle.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::responsive {
namespace {

constexpr double kIgnitionTimeSeconds = 0.7;
constexpr double kShutdownAudibleStartSeconds = 2.0;
constexpr double kKeyoffTimeSeconds = 2.8;
constexpr double kKeyoffDecelerationSeconds = 0.5;
constexpr double kLifecycleBlockRateHz = 50.0;
constexpr double kStarterStableFraction = 0.3;
constexpr double kShutdownPostrollSeconds = 2.2;

[[nodiscard]] LifecycleError error(const LifecycleErrorCode code,
                                   std::string detail_code, std::string path,
                                   std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] authoring::Quantity quantity(const double value, std::string unit) {
    return {value, std::move(unit), std::nullopt};
}

[[nodiscard]] bool finite_positive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] double align_to_lifecycle_block(const double seconds) noexcept {
    return std::ceil(seconds * kLifecycleBlockRateHz) / kLifecycleBlockRateHz;
}

[[nodiscard]] double four_stroke_cycle_seconds(const double rpm) noexcept {
    return 120.0 / rpm;
}

[[nodiscard]] std::optional<double>
starter_target_rpm(const authoring::EnginePackageDocument &engine) noexcept {
    const auto *starter =
        std::get_if<authoring::CrankingStarter>(&engine.engine.starter);
    if (starter == nullptr || !finite_positive(starter->target_speed.value)) {
        return std::nullopt;
    }
    if (starter->target_speed.unit == "rpm") {
        return starter->target_speed.value;
    }
    if (starter->target_speed.unit == "rad/s") {
        const double rpm =
            starter->target_speed.value * 60.0 / (2.0 * std::numbers::pi_v<double>);
        return finite_positive(rpm) ? std::optional<double>{rpm} : std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view role_id(const LifecycleScenarioRole role) noexcept {
    switch (role) {
    case LifecycleScenarioRole::starter:
        return "starter";
    case LifecycleScenarioRole::startup_probe:
        return "startup-probe";
    case LifecycleScenarioRole::startup:
        return "startup";
    case LifecycleScenarioRole::shutdown:
        return "shutdown";
    case LifecycleScenarioRole::shutdown_elevated:
        return "shutdown-elevated";
    }
    return "invalid";
}

[[nodiscard]] std::string_view
canonical_suffix(const LifecycleScenarioRole role) noexcept {
    switch (role) {
    case LifecycleScenarioRole::starter:
        return "lifecycle-starter-candidate";
    case LifecycleScenarioRole::startup_probe:
    case LifecycleScenarioRole::startup:
        return "lifecycle-startup-candidate";
    case LifecycleScenarioRole::shutdown:
        return "lifecycle-shutdown-candidate";
    case LifecycleScenarioRole::shutdown_elevated:
        return "lifecycle-shutdown-high-rpm-candidate";
    }
    return "invalid";
}

[[nodiscard]] authoring::ScenarioDocument
make_base(const LifecycleScenarioPlanRequest &request, const std::string &canonical_id,
          const double duration_seconds) {
    auto scenario = request.trusted_template;
    scenario.schema = "engine-sim-offline/scenario";
    scenario.id.value = canonical_id + "-10khz-lifecycle-preview";
    scenario.engine.value = request.engine.engine.identity.id.value;
    scenario.initial_state.engine_speed = quantity(0.0, "rpm");
    scenario.initial_state.ignition_enabled = false;
    scenario.initial_state.fuel_enabled = false;
    scenario.initial_state.starter_enabled = true;
    scenario.initial_state.dyno_enabled = false;
    scenario.initial_state.limiter_enabled = true;
    scenario.preparation =
        authoring::FixedSettlingPreparation{quantity(0.0, "s"), quantity(0.0, "s")};
    scenario.mode = authoring::FreeEngineMode{
        std::nullopt,
        {authoring::TrajectoryInterpolation::right_continuous_hold,
         {{{0.0, "s", std::nullopt}, 0.0}}},
        std::nullopt};
    scenario.events.clear();
    scenario.rates = {
        {kLifecyclePhysicsRateHz, 1U, "Hz"},  {kLifecyclePhysicsRateHz, 1U, "Hz"},
        {kLifecycleDeliveryRateHz, 1U, "Hz"}, {kLifecycleDeliveryRateHz, 1U, "Hz"},
        {kLifecycleDeliveryRateHz, 1U, "Hz"},
    };
    scenario.quality = {"listening", kLifecycleFramesPerBlock, 64U, 1U};
    scenario.total_duration = quantity(duration_seconds, "s");
    scenario.audible_start = quantity(0.0, "s");
    scenario.audible_duration = quantity(duration_seconds, "s");
    scenario.output = {{authoring::AudioBusRef{"master-engine-raw"},
                        authoring::AudioBusRef{"master-engine-audition"}},
                       {}};
    return scenario;
}

[[nodiscard]] std::optional<double>
output_crank_inertia(const authoring::EnginePackageDocument &engine) {
    const auto &definition = engine.engine;
    const auto crank = std::find_if(
        definition.crankshafts.begin(), definition.crankshafts.end(),
        [&](const auto &candidate) {
            return candidate.id.value == definition.output_crankshaft.value;
        });
    if (crank == definition.crankshafts.end() ||
        crank->moment_of_inertia.unit != "kg*m2" ||
        !finite_positive(crank->moment_of_inertia.value)) {
        return std::nullopt;
    }
    return crank->moment_of_inertia.value;
}

void finish_identity(LifecycleScenarioSpec &spec,
                     const LifecycleScenarioPlanRequest &request) {
    detail::CanonicalIdentityBytes identity{kLifecycleScenarioIdentityMethodId};
    identity.digest(request.trusted_template_sha256);
    identity.digest(request.profile.selection_identity_sha256);
    identity.u64(request.maximum_presentation_transfer_coefficient_count);
    identity.u32(static_cast<std::uint32_t>(spec.role));
    identity.string(spec.canonical_source_id);
    identity.string(spec.id);
    identity.string(spec.scenario.engine.value);
    identity.string(spec.scenario.fuel.value);
    identity.f64(spec.audible_start_seconds);
    identity.f64(spec.total_duration_seconds);
    identity.boolean(spec.ignition_event_seconds.has_value());
    if (spec.ignition_event_seconds) {
        identity.f64(*spec.ignition_event_seconds);
    }
    identity.boolean(spec.starter_release_seconds.has_value());
    if (spec.starter_release_seconds) {
        identity.f64(*spec.starter_release_seconds);
    }
    identity.boolean(spec.keyoff_resistance_nm.has_value());
    if (spec.keyoff_resistance_nm) {
        identity.f64(*spec.keyoff_resistance_nm);
    }
    identity.u64(spec.preparation_block_count);
    identity.u64(spec.total_block_count);
    identity.string(role_id(spec.role));
    spec.identity_sha256 = identity.finish();
}

} // namespace

LifecycleResult<LifecycleScenarioSpec>
plan_lifecycle_scenario(const LifecycleScenarioPlanRequest &request,
                        const LifecycleScenarioRole role,
                        const std::optional<LifecycleDynamicStarterRelease> &release) {
    const auto profile_report = validate_responsive_bake_profile(request.profile);
    if (!profile_report.ok()) {
        return error(LifecycleErrorCode::invalid_request, "invalid-profile", "profile",
                     "responsive lifecycle profile is invalid");
    }
    if (!request.profile.lifecycle.enabled) {
        return error(LifecycleErrorCode::invalid_request, "lifecycle-disabled",
                     "profile.lifecycle.enabled",
                     "responsive lifecycle capture is disabled");
    }
    const auto &engine_id = request.engine.engine.identity.id.value;
    if (engine_id.empty() || request.trusted_template.engine.value != engine_id ||
        request.trusted_template.fuel.value.empty() ||
        request.trusted_template_sha256.is_zero()) {
        return error(LifecycleErrorCode::invalid_request, "invalid-trusted-template",
                     "trusted_template",
                     "trusted template identity, engine, or fuel is invalid");
    }
    if (!std::isfinite(request.trusted_template.initial_state.crank_angle.value)) {
        return error(LifecycleErrorCode::invalid_request, "nonfinite-crank-angle",
                     "trusted_template.initial_state.crank_angle",
                     "trusted template crank angle must be finite");
    }
    const bool wants_release = role == LifecycleScenarioRole::startup;
    if (wants_release != release.has_value()) {
        return error(LifecycleErrorCode::invalid_request,
                     "starter-release-role-mismatch", "release",
                     "startup requires one dynamic release and other roles reject it");
    }
    if (release &&
        (!finite_positive(release->seconds) || release->physics_tick == 0U ||
         release->seconds != static_cast<double>(release->physics_tick) /
                                 static_cast<double>(kLifecyclePhysicsRateHz))) {
        return error(LifecycleErrorCode::invalid_request, "invalid-starter-release",
                     "release",
                     "dynamic starter release must be an exact positive 10 kHz tick");
    }
    const auto presentation_coefficient_count =
        request.maximum_presentation_transfer_coefficient_count;
    if (presentation_coefficient_count < kLifecycleFixedPresentationCoefficientCount ||
        presentation_coefficient_count >
            kLifecycleMaximumPresentationCoefficientCount) {
        return error(LifecycleErrorCode::invalid_request,
                     "presentation-transfer-support-out-of-range",
                     "maximum_presentation_transfer_coefficient_count",
                     "lifecycle planning requires a compiled presentation support "
                     "count in the native runtime range");
    }
    const std::uint64_t extra_presentation_blocks =
        (presentation_coefficient_count - kLifecycleFixedPresentationCoefficientCount +
         kLifecycleFramesPerBlock - 1U) /
        kLifecycleFramesPerBlock;
    const double presentation_tail_extension_seconds =
        static_cast<double>(extra_presentation_blocks) / kLifecycleBlockRateHz;

    LifecycleScenarioSpec spec;
    spec.role = role;
    spec.canonical_source_id = engine_id + "-" + std::string{canonical_suffix(role)};

    double duration_seconds = 0.0;
    switch (role) {
    case LifecycleScenarioRole::starter: {
        const auto target_rpm = starter_target_rpm(request.engine);
        if (!target_rpm) {
            return error(LifecycleErrorCode::invalid_request,
                         "cranking-starter-required", "engine.starter",
                         "starter lifecycle capture requires a positive cranking "
                         "starter target speed");
        }
        // The cooker begins its stable region at 30% of the capture. Two
        // complete retained cycles after an arbitrary phase boundary require
        // room for three nominal four-stroke cycles in the remaining 70%.
        duration_seconds = align_to_lifecycle_block(
            std::max(2.5, (3.0 * four_stroke_cycle_seconds(*target_rpm)) /
                              (1.0 - kStarterStableFraction)));
        break;
    }
    case LifecycleScenarioRole::startup_probe:
        // Reaching the running floor remains observed, not assumed. Once its
        // endpoint is observed, two nominal cycle durations provide room for
        // one complete post-floor cycle regardless of crank phase.
        duration_seconds = align_to_lifecycle_block(
            std::max(4.8, kIgnitionTimeSeconds +
                              2.0 * four_stroke_cycle_seconds(
                                        request.profile.rpm.outer_minimum_rpm)));
        break;
    case LifecycleScenarioRole::startup:
        duration_seconds = align_to_lifecycle_block(
            release->seconds +
            std::max(3.0, 1.0 + 2.0 * four_stroke_cycle_seconds(
                                          request.profile.rpm.outer_minimum_rpm)));
        break;
    case LifecycleScenarioRole::shutdown: {
        const double rpm = request.profile.rpm.anchors.front();
        const double preparation = align_to_lifecycle_block(std::max(
            kShutdownAudibleStartSeconds, 5.0 * four_stroke_cycle_seconds(rpm)));
        const double keyoff = align_to_lifecycle_block(std::max(
            kKeyoffTimeSeconds, preparation + 2.0 * four_stroke_cycle_seconds(rpm)));
        duration_seconds =
            align_to_lifecycle_block(std::max(5.0, keyoff + kShutdownPostrollSeconds)) +
            presentation_tail_extension_seconds;
        break;
    }
    case LifecycleScenarioRole::shutdown_elevated: {
        const double rpm = request.profile.lifecycle.elevated_shutdown_rpm;
        const double preparation = std::max(
            0.1, align_to_lifecycle_block(6.0 * four_stroke_cycle_seconds(rpm)));
        const double lead =
            align_to_lifecycle_block(2.0 * four_stroke_cycle_seconds(rpm));
        const double keyoff =
            std::max(request.profile.lifecycle.elevated_shutdown_keyoff_seconds,
                     preparation + lead);
        duration_seconds = std::max(5.0, align_to_lifecycle_block(keyoff + 3.0)) +
                           presentation_tail_extension_seconds;
        break;
    }
    }
    constexpr double kMaximumLifecycleDurationSeconds = static_cast<double>(
        std::numeric_limits<std::uint64_t>::max() / kLifecycleDeliveryRateHz);
    if (!finite_positive(duration_seconds) ||
        duration_seconds > kMaximumLifecycleDurationSeconds) {
        return error(LifecycleErrorCode::invalid_request,
                     "lifecycle-horizon-out-of-range", "scenario.total_duration",
                     "cycle-derived lifecycle horizon is non-finite or exceeds the "
                     "delivery frame range");
    }

    spec.scenario = make_base(request, spec.canonical_source_id, duration_seconds);
    spec.id = spec.scenario.id.value;
    spec.total_duration_seconds = duration_seconds;

    if (role == LifecycleScenarioRole::startup_probe ||
        role == LifecycleScenarioRole::startup) {
        spec.scenario.initial_state.fuel_enabled = true;
        spec.ignition_event_seconds = kIgnitionTimeSeconds;
        spec.scenario.events.push_back(
            {{"ignition-on"},
             quantity(kIgnitionTimeSeconds, "s"),
             authoring::OperatingStatePatch{true, std::nullopt, std::nullopt,
                                            std::nullopt, std::nullopt}});
        if (release) {
            spec.starter_release_seconds = release->seconds;
            spec.scenario.events.push_back(
                {{"starter-release"},
                 quantity(release->seconds, "s"),
                 authoring::OperatingStatePatch{std::nullopt, std::nullopt, false,
                                                std::nullopt, std::nullopt}});
        }
    } else if (role == LifecycleScenarioRole::shutdown) {
        const auto inertia = output_crank_inertia(request.engine);
        if (!inertia) {
            return error(LifecycleErrorCode::invalid_request,
                         "output-crank-inertia-unavailable",
                         "engine.output_crankshaft.moment_of_inertia",
                         "output crank inertia must be positive kg*m2");
        }
        const double held_floor = request.profile.rpm.anchors.front();
        const double preparation = align_to_lifecycle_block(std::max(
            kShutdownAudibleStartSeconds, 5.0 * four_stroke_cycle_seconds(held_floor)));
        const double keyoff = align_to_lifecycle_block(
            std::max(kKeyoffTimeSeconds,
                     preparation + 2.0 * four_stroke_cycle_seconds(held_floor)));
        const double resistance = *inertia * held_floor * 2.0 *
                                  std::numbers::pi_v<double> / 60.0 /
                                  kKeyoffDecelerationSeconds;
        spec.scenario.initial_state.engine_speed = quantity(held_floor, "rpm");
        spec.scenario.initial_state.ignition_enabled = true;
        spec.scenario.initial_state.fuel_enabled = true;
        spec.scenario.initial_state.starter_enabled = false;
        spec.scenario.preparation =
            authoring::FixedHorizonPreparation{quantity(preparation, "s"), 4U};
        auto &mode = std::get<authoring::FreeEngineMode>(spec.scenario.mode);
        mode.external_resisting_torque = authoring::QuantityTrajectory{
            authoring::QuantityDimension::torque,
            authoring::TrajectoryInterpolation::right_continuous_hold,
            {{{0.0, "s", std::nullopt}, {0.0, "N*m", std::nullopt}},
             {{keyoff, "s", std::nullopt}, {resistance, "N*m", std::nullopt}}}};
        spec.scenario.events.push_back(
            {{"key-off"},
             quantity(keyoff, "s"),
             authoring::OperatingStatePatch{false, false, std::nullopt, std::nullopt,
                                            std::nullopt}});
        spec.audible_start_seconds = preparation;
        spec.scenario.audible_start = quantity(preparation, "s");
        spec.scenario.audible_duration = quantity(duration_seconds - preparation, "s");
        spec.ignition_event_seconds = keyoff;
        spec.keyoff_resistance_nm = resistance;
    } else if (role == LifecycleScenarioRole::shutdown_elevated) {
        const double rpm = request.profile.lifecycle.elevated_shutdown_rpm;
        const double preparation = std::max(
            0.1, align_to_lifecycle_block(6.0 * four_stroke_cycle_seconds(rpm)));
        // The audible boundary can begin anywhere inside a 720-degree cycle.
        // Retaining two nominal cycle durations guarantees that at least one
        // complete post-boundary running cycle ends before key-off.
        const double lead =
            align_to_lifecycle_block(2.0 * four_stroke_cycle_seconds(rpm));
        const double keyoff =
            std::max(request.profile.lifecycle.elevated_shutdown_keyoff_seconds,
                     preparation + lead);
        spec.scenario.initial_state.engine_speed = quantity(rpm, "rpm");
        spec.scenario.initial_state.ignition_enabled = true;
        spec.scenario.initial_state.fuel_enabled = true;
        spec.scenario.initial_state.starter_enabled = false;
        spec.scenario.preparation =
            authoring::FixedHorizonPreparation{quantity(preparation, "s"), 1U};
        auto &mode = std::get<authoring::FreeEngineMode>(spec.scenario.mode);
        mode.throttle_01.points.front().value = 0.5;
        mode.external_resisting_torque = authoring::QuantityTrajectory{
            authoring::QuantityDimension::torque,
            authoring::TrajectoryInterpolation::right_continuous_hold,
            {{{0.0, "s", std::nullopt}, {0.0, "N*m", std::nullopt}}}};
        spec.scenario.events.push_back(
            {{"key-off"},
             quantity(keyoff, "s"),
             authoring::OperatingStatePatch{false, std::nullopt, std::nullopt,
                                            std::nullopt, std::nullopt}});
        spec.audible_start_seconds = preparation;
        spec.scenario.audible_start = quantity(preparation, "s");
        spec.scenario.audible_duration = quantity(duration_seconds - preparation, "s");
        spec.ignition_event_seconds = keyoff;
    }

    spec.preparation_block_count = static_cast<std::uint64_t>(
        std::llround(spec.audible_start_seconds * kLifecycleBlockRateHz));
    spec.total_block_count = static_cast<std::uint64_t>(
        std::llround(spec.total_duration_seconds * kLifecycleBlockRateHz));
    if (spec.total_block_count == 0U || spec.scenario.audible_duration.value <= 0.0 ||
        std::abs(spec.scenario.audible_start.value +
                 spec.scenario.audible_duration.value -
                 spec.scenario.total_duration.value) > 1.0e-12) {
        return error(LifecycleErrorCode::internal_error, "invalid-planned-horizon",
                     "scenario",
                     "planned lifecycle horizon is not an exact positive 20 ms grid");
    }
    finish_identity(spec, request);
    return spec;
}

} // namespace engine_sim_offline::responsive
