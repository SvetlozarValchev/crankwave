#include "crankwave/authoring/parse.hpp"
#include "crankwave/compile.hpp"
#include "crankwave/responsive/finite_capture.hpp"
#include "crankwave/responsive/lifecycle.hpp"
#include "crankwave/responsive/profile.hpp"
#include "crankwave/responsive/scenario_template.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string diagnostics(const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result;
}

template <class Value>
[[nodiscard]] Value
require_authoring(std::variant<Value, authoring::DiagnosticReport> result,
                  const std::string_view label) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{label} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

template <class Value>
[[nodiscard]] Value require_lifecycle(responsive::LifecycleResult<Value> result,
                                      const std::string_view label) {
    if (const auto *failure = std::get_if<responsive::LifecycleError>(&result)) {
        throw std::runtime_error{std::string{label} + ": " + failure->detail_code +
                                 ": " + failure->message};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] responsive::ResponsiveBakeProfile
require_profile(const authoring::EnginePackageDocument &engine) {
    auto selected = responsive::derive_engine_redline_affine_profile(engine);
    if (const auto *report = std::get_if<contract::ValidationReport>(&selected)) {
        throw std::runtime_error{"profile selection failed with " +
                                 std::to_string(report->issues.size()) + " issues"};
    }
    return std::get<responsive::ResponsiveBakeProfile>(std::move(selected));
}

[[nodiscard]] responsive::ResponsiveScenarioTemplate
require_template(const authoring::EnginePackageDocument &engine,
                 const responsive::ResponsiveBakeProfile &profile) {
    auto made = responsive::make_responsive_scenario_template(engine, profile);
    if (const auto *report = std::get_if<contract::ValidationReport>(&made)) {
        throw std::runtime_error{"scenario template failed with " +
                                 std::to_string(report->issues.size()) + " issues"};
    }
    return std::get<responsive::ResponsiveScenarioTemplate>(std::move(made));
}

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> result;
    for (const auto &asset : document.presentation.assets) {
        result.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back({compile::AssetKind::accessory_configuration, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return result;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

struct PlanningFixture {
    std::filesystem::path engine_path;
    authoring::EnginePackageDocument engine;
    responsive::ResponsiveBakeProfile profile;
    responsive::ResponsiveScenarioTemplate scenario_template;
    responsive::LifecycleScenarioPlanRequest request;
};

[[nodiscard]] PlanningFixture fixture(const std::filesystem::path &repository_root) {
    const auto path =
        repository_root / "data/engines/bmw-m52tub28-cleanroom/engine.json";
    auto engine = require_authoring(authoring::parse_engine_document(read_text(path)),
                                    "engine parse");
    auto profile = require_profile(engine);
    auto scenario_template = require_template(engine, profile);
    responsive::LifecycleScenarioPlanRequest request{
        profile, engine, scenario_template.document, scenario_template.identity_sha256};
    return {path, std::move(engine), std::move(profile), std::move(scenario_template),
            std::move(request)};
}

[[nodiscard]] double
output_crank_inertia(const authoring::EnginePackageDocument &engine) {
    const auto found = std::find_if(
        engine.engine.crankshafts.begin(), engine.engine.crankshafts.end(),
        [&](const auto &candidate) {
            return candidate.id.value == engine.engine.output_crankshaft.value;
        });
    expect(found != engine.engine.crankshafts.end(), "output crank disappeared");
    return found->moment_of_inertia.value;
}

void test_planner(PlanningFixture &value) {
    using responsive::LifecycleScenarioRole;
    const auto starter =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::starter),
                          "starter plan");
    expect(starter.canonical_source_id == value.engine.engine.identity.id.value +
                                              "-lifecycle-starter-candidate" &&
               starter.id == starter.canonical_source_id + "-10khz-lifecycle-preview" &&
               starter.total_block_count == 125U &&
               starter.preparation_block_count == 0U &&
               std::holds_alternative<authoring::FixedSettlingPreparation>(
                   starter.scenario.preparation),
           "starter scenario no longer matches the accepted lifecycle oracle");

    const auto probe =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::startup_probe),
                          "startup probe plan");
    expect(probe.total_duration_seconds == 4.8 && probe.scenario.events.size() == 1U &&
               probe.scenario.events.front().id.value == "ignition-on" &&
               probe.scenario.initial_state.fuel_enabled,
           "startup probe scenario changed");

    responsive::LifecycleDynamicStarterRelease release;
    release.physics_tick = 8'750U;
    release.seconds = 0.875;
    const auto startup =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::startup, release),
                          "startup plan");
    expect(startup.total_duration_seconds == 3.88 &&
               startup.total_block_count == 194U &&
               startup.scenario.events.size() == 2U &&
               startup.starter_release_seconds == 0.875,
           "dynamic startup horizon or release event changed");

    const auto shutdown =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::shutdown),
                          "shutdown plan");
    const double expected_resistance = output_crank_inertia(value.engine) *
                                       value.profile.rpm.anchors.front() * 2.0 *
                                       std::acos(-1.0) / 60.0 / 0.5;
    expect(shutdown.total_block_count == 250U &&
               shutdown.preparation_block_count == 100U &&
               shutdown.audible_start_seconds == 2.0 &&
               shutdown.ignition_event_seconds == 2.8 &&
               shutdown.keyoff_resistance_nm.has_value() &&
               *shutdown.keyoff_resistance_nm == expected_resistance,
           "idle shutdown plan changed");
    const auto &shutdown_mode =
        std::get<authoring::FreeEngineMode>(shutdown.scenario.mode);
    expect(shutdown_mode.external_resisting_torque.has_value() &&
               shutdown_mode.external_resisting_torque->points.size() == 2U,
           "shutdown resistance trajectory is absent");

    const auto elevated =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::shutdown_elevated),
                          "elevated shutdown plan");
    expect(std::abs(elevated.audible_start_seconds - 0.24) < 1.0e-15 &&
               elevated.ignition_event_seconds.has_value() &&
               std::abs(*elevated.ignition_event_seconds - 0.32) < 1.0e-15 &&
               elevated.preparation_block_count == 12U &&
               elevated.total_block_count == 250U &&
               elevated.scenario.initial_state.fuel_enabled,
           "elevated shutdown timing changed");
    const auto *patch = std::get_if<authoring::OperatingStatePatch>(
        &elevated.scenario.events.front().payload);
    expect(patch != nullptr && patch->ignition_enabled == false &&
               !patch->fuel_enabled.has_value(),
           "elevated shutdown must key off ignition while retaining fuel");

    auto compressed_engine = value.engine;
    compressed_engine.engine.limits.redline.value = 3'600.0;
    const auto compressed_profile = require_profile(compressed_engine);
    const auto compressed_template =
        require_template(compressed_engine, compressed_profile);
    const auto compressed = require_lifecycle(
        responsive::plan_lifecycle_scenario({compressed_profile, compressed_engine,
                                             compressed_template.document,
                                             compressed_template.identity_sha256},
                                            LifecycleScenarioRole::shutdown_elevated),
        "compressed elevated shutdown plan");
    expect(
        compressed_profile.lifecycle.elevated_shutdown_rpm == 1'820.338983 &&
            compressed.audible_start_seconds == 0.4 &&
            compressed.ignition_event_seconds.has_value() &&
            *compressed.ignition_event_seconds == 0.54 &&
            (*compressed.ignition_event_seconds - compressed.audible_start_seconds) >=
                2.0 * 120.0 / compressed_profile.lifecycle.elevated_shutdown_rpm,
        "compressed elevated shutdown lacks two complete-cycle durations "
        "of audible running lead");

    const auto invalid = responsive::plan_lifecycle_scenario(
        value.request, LifecycleScenarioRole::startup);
    expect(std::holds_alternative<responsive::LifecycleError>(invalid),
           "startup without a dynamic release was admitted");

    auto disengaged_request = value.request;
    disengaged_request.engine.engine.starter =
        authoring::MechanicallyDisengagedStarter{};
    const auto disengaged = responsive::plan_lifecycle_scenario(
        disengaged_request, LifecycleScenarioRole::starter);
    const auto *disengaged_error = std::get_if<responsive::LifecycleError>(&disengaged);
    expect(disengaged_error != nullptr &&
               disengaged_error->detail_code == "cranking-starter-required",
           "mechanically disengaged starter was admitted to starter capture");

    auto slow_starter_request = value.request;
    auto &slow_starter = std::get<authoring::CrankingStarter>(
        slow_starter_request.engine.engine.starter);
    slow_starter.target_speed = {100.0, "rpm", std::nullopt};
    const auto slow_starter_plan =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              slow_starter_request, LifecycleScenarioRole::starter),
                          "slow starter plan");
    expect(slow_starter_plan.total_duration_seconds == 5.16 &&
               slow_starter_plan.total_block_count == 258U,
           "starter horizon is not derived from three post-gate target-speed cycles");

    slow_starter.target_speed = {std::numeric_limits<double>::min(), "rpm",
                                 std::nullopt};
    const auto excessive_starter = responsive::plan_lifecycle_scenario(
        slow_starter_request, LifecycleScenarioRole::starter);
    const auto *excessive_error =
        std::get_if<responsive::LifecycleError>(&excessive_starter);
    expect(excessive_error != nullptr &&
               excessive_error->detail_code == "lifecycle-horizon-out-of-range",
           "unrepresentable cycle-derived starter horizon was admitted");

    auto invalid_support_request = value.request;
    invalid_support_request.maximum_presentation_transfer_coefficient_count =
        responsive::kLifecycleFixedPresentationCoefficientCount - 1U;
    const auto short_support = responsive::plan_lifecycle_scenario(
        invalid_support_request, LifecycleScenarioRole::shutdown);
    invalid_support_request.maximum_presentation_transfer_coefficient_count =
        responsive::kLifecycleMaximumPresentationCoefficientCount + 1U;
    const auto long_support = responsive::plan_lifecycle_scenario(
        invalid_support_request, LifecycleScenarioRole::shutdown);
    const auto *short_support_error =
        std::get_if<responsive::LifecycleError>(&short_support);
    const auto *long_support_error =
        std::get_if<responsive::LifecycleError>(&long_support);
    expect(
        short_support_error != nullptr && long_support_error != nullptr &&
            short_support_error->detail_code ==
                "presentation-transfer-support-out-of-range" &&
            long_support_error->detail_code ==
                "presentation-transfer-support-out-of-range",
        "unsupported presentation transfer support was admitted to lifecycle planning");
}

void test_low_rpm_planning_and_compile(PlanningFixture &value) {
    using responsive::LifecycleScenarioRole;
    constexpr std::array redlines{1'500.0, 250.0, 65'500.0};
    const auto assets = load_assets(value.engine, value.engine_path);
    const auto views = asset_views(assets);

    for (const double redline : redlines) {
        auto engine = value.engine;
        engine.engine.limits.redline.value = redline;
        const auto profile = require_profile(engine);
        const auto scenario_template = require_template(engine, profile);
        const responsive::LifecycleScenarioPlanRequest request{
            profile, engine, scenario_template.document,
            scenario_template.identity_sha256};
        auto compiled_engine = require_authoring(compile::compile_engine(engine, views),
                                                 "low-RPM engine compile");

        responsive::LifecycleDynamicStarterRelease release;
        release.physics_tick = 8'750U;
        release.seconds = 0.875;
        const auto starter =
            require_lifecycle(responsive::plan_lifecycle_scenario(
                                  request, LifecycleScenarioRole::starter),
                              "low-RPM starter plan");
        const auto probe =
            require_lifecycle(responsive::plan_lifecycle_scenario(
                                  request, LifecycleScenarioRole::startup_probe),
                              "low-RPM startup probe plan");
        const auto startup =
            require_lifecycle(responsive::plan_lifecycle_scenario(
                                  request, LifecycleScenarioRole::startup, release),
                              "low-RPM startup plan");
        const auto shutdown =
            require_lifecycle(responsive::plan_lifecycle_scenario(
                                  request, LifecycleScenarioRole::shutdown),
                              "low-RPM shutdown plan");
        const auto elevated =
            require_lifecycle(responsive::plan_lifecycle_scenario(
                                  request, LifecycleScenarioRole::shutdown_elevated),
                              "low-RPM elevated shutdown plan");

        const double running_cycle = 120.0 / profile.rpm.outer_minimum_rpm;
        const double idle_cycle = 120.0 / profile.rpm.anchors.front();
        expect(probe.total_duration_seconds + 1.0e-12 >= 0.7 + 2.0 * running_cycle,
               "startup probe lacks post-floor geometric cycle capacity");
        expect(startup.total_duration_seconds - release.seconds + 1.0e-12 >=
                   std::max(3.0, 1.0 + 2.0 * running_cycle),
               "startup lacks post-release stable cycle capacity");
        expect(shutdown.audible_start_seconds + 1.0e-12 >= 5.0 * idle_cycle &&
                   shutdown.ignition_event_seconds.has_value() &&
                   *shutdown.ignition_event_seconds - shutdown.audible_start_seconds +
                           1.0e-12 >=
                       2.0 * idle_cycle &&
                   shutdown.total_duration_seconds - *shutdown.ignition_event_seconds +
                           1.0e-12 >=
                       2.2,
               "shutdown lacks preparation, running lead, or post-roll capacity");
        expect(starter.total_duration_seconds >= 2.5 &&
                   elevated.total_duration_seconds >= 5.0,
               "lifecycle minimum horizons regressed");
        if (redline == 1'500.0) {
            expect(shutdown.audible_start_seconds == 2.0 &&
                       *shutdown.ignition_event_seconds == 2.8 &&
                       shutdown.total_duration_seconds == 5.0,
                   "1500-RPM boundary no longer preserves legacy shutdown timing");
            auto maximum_ir_request = request;
            maximum_ir_request.maximum_presentation_transfer_coefficient_count =
                responsive::kLifecycleMaximumPresentationCoefficientCount;
            const auto maximum_ir_shutdown = require_lifecycle(
                responsive::plan_lifecycle_scenario(maximum_ir_request,
                                                    LifecycleScenarioRole::shutdown),
                "maximum-IR shutdown plan");
            const auto maximum_ir_elevated = require_lifecycle(
                responsive::plan_lifecycle_scenario(
                    maximum_ir_request, LifecycleScenarioRole::shutdown_elevated),
                "maximum-IR elevated shutdown plan");
            expect(maximum_ir_shutdown.total_duration_seconds == 7.82 &&
                       maximum_ir_elevated.total_duration_seconds == 7.82 &&
                       maximum_ir_shutdown.ignition_event_seconds ==
                           shutdown.ignition_event_seconds &&
                       maximum_ir_shutdown.identity_sha256 != shutdown.identity_sha256,
                   "maximum native IR support does not extend shutdown by 141 blocks");
            for (const auto *spec :
                 std::array{&maximum_ir_shutdown, &maximum_ir_elevated}) {
                static_cast<void>(require_authoring(
                    compile::compile_scenario(compiled_engine, spec->scenario),
                    "maximum-IR lifecycle scenario compile"));
            }
        } else if (redline == 250.0) {
            expect(shutdown.audible_start_seconds == 12.0 &&
                       *shutdown.ignition_event_seconds == 16.8 &&
                       shutdown.total_duration_seconds == 19.0 &&
                       probe.total_duration_seconds == 5.5 &&
                       startup.total_duration_seconds == 6.68,
                   "250-RPM lifecycle horizons changed from the cycle oracle");
        } else {
            expect(profile.rpm.outer_minimum_rpm == 480.0 &&
                       profile.rpm.anchors.front() == 600.0 &&
                       shutdown.audible_start_seconds == 2.0 &&
                       *shutdown.ignition_event_seconds == 2.8 &&
                       shutdown.total_duration_seconds == 5.0 &&
                       probe.total_duration_seconds == 4.8 &&
                       startup.total_duration_seconds == 3.88,
                   "65500-RPM affine floor lifecycle horizons changed from the cycle "
                   "oracle");
        }

        for (const auto *spec :
             std::array{&starter, &probe, &startup, &shutdown, &elevated}) {
            const auto references =
                authoring::validate_scenario_references(spec->scenario, engine);
            expect(references.ok(),
                   "low-RPM lifecycle scenario references are invalid");
            static_cast<void>(require_authoring(
                compile::compile_scenario(compiled_engine, spec->scenario),
                "low-RPM lifecycle scenario compile"));
        }
    }
}

[[nodiscard]] std::uint64_t
frame_for_event(const responsive::LifecycleScenarioSpec &spec,
                const std::string_view id) {
    const auto found =
        std::find_if(spec.scenario.events.begin(), spec.scenario.events.end(),
                     [&](const auto &event) { return event.id.value == id; });
    expect(found != spec.scenario.events.end(), "synthetic event missing");
    return static_cast<std::uint64_t>(
        std::floor((found->time.value - spec.audible_start_seconds) *
                       responsive::kLifecycleDeliveryRateHz +
                   0.5));
}

[[nodiscard]] responsive::LifecycleCaptureEvidence
synthetic_capture(const responsive::LifecycleScenarioSpec &spec,
                  const std::optional<std::uint64_t> quiet_from = std::nullopt) {
    responsive::LifecycleCaptureEvidence result;
    result.role = spec.role;
    result.canonical_source_id = spec.canonical_source_id;
    result.engine_id = spec.scenario.engine.value;
    result.scenario_id = spec.id;
    result.bus_id = "master.engine.audition";
    result.physics_rate_hz = responsive::kLifecyclePhysicsRateHz;
    result.delivery_rate_hz = responsive::kLifecycleDeliveryRateHz;
    result.preparation_block_count = spec.preparation_block_count;
    result.total_block_count = spec.total_block_count;
    result.audible_first_delivery_frame =
        spec.preparation_block_count * responsive::kLifecycleFramesPerBlock;
    result.audible_frame_count =
        (spec.total_block_count - spec.preparation_block_count) *
        responsive::kLifecycleFramesPerBlock;
    result.final_physics_frame = spec.total_block_count * 200U;
    result.final_delivery_frame =
        spec.total_block_count * responsive::kLifecycleFramesPerBlock;
    result.scenario = spec.scenario;
    result.scenario_spec_sha256 = spec.identity_sha256;
    result.pcm.resize(result.audible_frame_count);
    for (std::uint64_t frame = 0U; frame < result.pcm.size(); ++frame) {
        result.pcm[frame] =
            quiet_from && frame >= *quiet_from
                ? 0.0F
                : static_cast<float>(0.2 *
                                     std::sin(2.0 * std::acos(-1.0) *
                                              static_cast<double>(frame) / 480.0));
    }

    std::optional<std::uint64_t> ignition;
    std::optional<std::uint64_t> release;
    if (spec.role == responsive::LifecycleScenarioRole::startup_probe ||
        spec.role == responsive::LifecycleScenarioRole::startup) {
        ignition = frame_for_event(spec, "ignition-on");
    }
    if (spec.role == responsive::LifecycleScenarioRole::startup) {
        release = frame_for_event(spec, "starter-release");
    }
    std::optional<std::uint64_t> keyoff;
    if (spec.role == responsive::LifecycleScenarioRole::shutdown ||
        spec.role == responsive::LifecycleScenarioRole::shutdown_elevated) {
        keyoff = frame_for_event(spec, "key-off");
    }

    const auto audible_blocks = spec.total_block_count - spec.preparation_block_count;
    result.points.reserve(audible_blocks);
    for (std::uint64_t index = 0U; index < audible_blocks; ++index) {
        const std::uint64_t frame =
            (index + 1U) * responsive::kLifecycleFramesPerBlock - 1U;
        const std::uint64_t absolute_block = spec.preparation_block_count + index + 1U;
        bool ignition_enabled = false;
        bool fuel_enabled = false;
        bool starter_enabled = true;
        double rpm = 200.0;
        double indicated_torque = 0.0;
        auto availability =
            responsive::LifecycleCapturePoint::PublishedAvailability::unavailable;
        switch (spec.role) {
        case responsive::LifecycleScenarioRole::starter:
            break;
        case responsive::LifecycleScenarioRole::startup_probe:
        case responsive::LifecycleScenarioRole::startup:
            fuel_enabled = true;
            ignition_enabled = frame >= *ignition;
            starter_enabled = !release || frame < *release;
            if (ignition_enabled) {
                rpm = 700.0;
                indicated_torque = 12.0;
                availability =
                    responsive::LifecycleCapturePoint::PublishedAvailability::available;
            }
            break;
        case responsive::LifecycleScenarioRole::shutdown:
            ignition_enabled = frame < *keyoff;
            fuel_enabled = frame < *keyoff;
            starter_enabled = false;
            rpm = quiet_from && frame >= *quiet_from ? 0.0 : 600.0;
            break;
        case responsive::LifecycleScenarioRole::shutdown_elevated:
            ignition_enabled = frame < *keyoff;
            fuel_enabled = true;
            starter_enabled = false;
            rpm = quiet_from && frame >= *quiet_from ? 0.0 : 3000.0;
            break;
        }
        result.points.push_back({frame, absolute_block * 200U,
                                 static_cast<double>(absolute_block * 200U) /
                                     responsive::kLifecyclePhysicsRateHz,
                                 rpm, ignition_enabled, fuel_enabled, starter_enabled,
                                 indicated_torque, availability});
    }

    if (spec.role == responsive::LifecycleScenarioRole::shutdown_elevated) {
        result.completed_cycles.push_back({0U, 6'000U, 3000.0});
    } else {
        std::uint64_t cycle_limit = result.pcm.size();
        if (keyoff) {
            cycle_limit = *keyoff;
        }
        for (std::uint64_t start = 0U; start + 24'000U <= cycle_limit;
             start += 24'000U) {
            result.completed_cycles.push_back(
                {start, start + 24'000U,
                 spec.role == responsive::LifecycleScenarioRole::shutdown ? 600.0
                                                                          : 960.0});
        }
    }
    return result;
}

void test_cooking(PlanningFixture &value) {
    using responsive::LifecycleScenarioRole;
    const auto starter_spec =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::starter),
                          "starter plan");
    const auto probe_spec =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::startup_probe),
                          "probe plan");
    auto starter_capture = synthetic_capture(starter_spec);
    auto probe_capture = synthetic_capture(probe_spec);
    const auto release =
        require_lifecycle(responsive::choose_dynamic_starter_release(
                              probe_capture, value.profile.rpm.outer_minimum_rpm),
                          "dynamic release");
    expect(release.physics_tick == 8'750U && release.seconds == 0.875 &&
               release.first_positive_combustion.source_frame == 138'239U &&
               release.running_floor.source_frame == 138'239U &&
               release.release_cycle.start_frame == 144'000U &&
               release.release_cycle.end_frame == 168'000U,
           "dynamic starter release no longer matches the 20 ms/cycle oracle");

    const auto startup_spec =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::startup, release),
                          "startup plan");
    const auto shutdown_spec =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::shutdown),
                          "shutdown plan");
    const auto elevated_spec =
        require_lifecycle(responsive::plan_lifecycle_scenario(
                              value.request, LifecycleScenarioRole::shutdown_elevated),
                          "elevated plan");
    auto startup_capture = synthetic_capture(startup_spec);
    auto shutdown_capture = synthetic_capture(shutdown_spec, 249'000U);
    auto elevated_capture = synthetic_capture(elevated_spec, 100'000U);

    const auto starter = require_lifecycle(
        responsive::cook_lifecycle_starter(starter_capture), "starter cook");
    expect(starter.presentation.loop_start_frame == 144'000U &&
               starter.presentation.loop_end_frame == 480'000U &&
               starter.presentation.crossfade_frames == 24'000U &&
               starter.presentation.mean_crank_rpm == 960.0 &&
               starter.stable_cycles.size() == 14U,
           "starter loop geometry changed");

    auto high_rpm_starter_capture = synthetic_capture(starter_spec);
    high_rpm_starter_capture.completed_cycles.clear();
    constexpr std::uint64_t high_rpm_cycle_frames = 15'360U;
    for (std::uint64_t start = 0U;
         start + high_rpm_cycle_frames <= high_rpm_starter_capture.pcm.size();
         start += high_rpm_cycle_frames) {
        high_rpm_starter_capture.completed_cycles.push_back(
            {start, start + high_rpm_cycle_frames, 1'500.0});
    }
    const auto high_rpm_starter =
        require_lifecycle(responsive::cook_lifecycle_starter(high_rpm_starter_capture),
                          "high-RPM starter cook");
    auto high_rpm_startup_capture = synthetic_capture(startup_spec);
    const auto high_rpm_startup = require_lifecycle(
        responsive::cook_lifecycle_startup(startup_spec, high_rpm_startup_capture,
                                           high_rpm_starter_capture, high_rpm_starter),
        "high-RPM starter startup cook");
    expect(high_rpm_starter.presentation.mean_crank_rpm == 1'500.0 &&
               high_rpm_startup.entry.crossfade_frames == high_rpm_cycle_frames,
           "starter cycles above 1200 RPM were not admitted to the startup seam");

    auto startup =
        require_lifecycle(responsive::cook_lifecycle_startup(
                              startup_spec, startup_capture, starter_capture, starter),
                          "startup cook");
    expect(startup.entry.source_frame == 105'599U &&
               startup.entry.target_source_frame == 144'000U &&
               startup.entry.crossfade_frames == 19'200U &&
               startup.exit.source_frame == 360'000U &&
               startup.exit.target_source_frame == 360'000U &&
               startup.checkpoints.size() == 4U &&
               startup.checkpoints[0].frame == 134'400U &&
               startup.checkpoints[1].frame == 138'239U &&
               startup.checkpoints[2].frame == 168'000U &&
               startup.checkpoints[3].frame == 360'000U,
           "startup seams or checkpoints changed");

    auto shutdown = require_lifecycle(
        responsive::cook_lifecycle_shutdown(shutdown_spec, shutdown_capture),
        "shutdown cook");
    expect(shutdown.entry.source_frame == 120'960U &&
               shutdown.entry.target_source_frame == 0U &&
               shutdown.entry.crossfade_frames == 19'200U &&
               shutdown.checkpoints[1].frame == 153'600U &&
               shutdown.checkpoints[2].frame == 249'599U &&
               shutdown.silence_frame == 249'599U &&
               shutdown.quiet_tail_frames == shutdown_capture.pcm.size() - 249'599U,
           "idle shutdown seam/checkpoints/quiet tail changed");

    auto elevated = require_lifecycle(
        responsive::cook_lifecycle_elevated_shutdown(elevated_spec, elevated_capture),
        "elevated shutdown cook");
    expect(elevated.entry.source_frame == 0U &&
               elevated.entry.crossfade_frames == 3'840U &&
               elevated.checkpoints[1].frame == 15'360U &&
               elevated.checkpoints[2].frame == 103'679U &&
               elevated.silence_frame == 103'679U &&
               elevated.entry.target_reference ==
                   "shutdown-high-rpm-pre-keyoff-cycle-reference",
           "elevated shutdown presentation changed");

    auto admission_seed = require_lifecycle(
        responsive::make_lifecycle_startup_admission_seed(value.profile, release),
        "startup admission seed");
    expect(
        admission_seed.lanes.size() == 3U && admission_seed.lanes[0].id == "coast" &&
            admission_seed.lanes[1].id == "mid" &&
            admission_seed.lanes[2].id == "power" &&
            admission_seed.running_floor_rpm == value.profile.rpm.outer_minimum_rpm &&
            admission_seed.held_anchor_floor_rpm == value.profile.rpm.anchors.front(),
        "startup admission semantic seed changed");
    contract::Sha256Digest held_digest;
    held_digest.bytes.front() = 1U;
    auto floor_evidence = require_lifecycle(
        responsive::bind_lifecycle_startup_admission_floor_evidence(
            admission_seed, {"../../held/package.json", held_digest,
                             "measured-intake-manifold-pressure-pa-abs"}),
        "floor evidence binding");
    expect(floor_evidence.release.seconds == release.seconds &&
               floor_evidence.release.first_positive_combustion_frame ==
                   release.first_positive_combustion.source_frame &&
               floor_evidence.release.running_floor_frame ==
                   release.running_floor.source_frame &&
               floor_evidence.release.release_cycle == release.release_cycle,
           "startup admission release evidence is not the exact JS shape");
    contract::Sha256Digest evidence_digest;
    evidence_digest.bytes.front() = 2U;
    const auto admission = require_lifecycle(
        responsive::bind_lifecycle_startup_admission_presentation(
            floor_evidence, {"evidence/startup-admission.json", evidence_digest}),
        "admission presentation binding");
    expect(admission.evidence.corrected_held_manifest_sha256 == held_digest &&
               admission.evidence.sha256 == evidence_digest &&
               admission.running_bed_load_coordinate ==
                   "measured-intake-manifold-pressure-pa-abs",
           "startup admission does not bind exact held/evidence digests");

    auto package =
        require_lifecycle(responsive::assemble_lifecycle_package(
                              std::move(starter_capture), std::move(startup_capture),
                              std::move(shutdown_capture), std::move(elevated_capture),
                              starter, std::move(startup), std::move(shutdown),
                              std::move(elevated), std::move(admission_seed)),
                          "lifecycle aggregate");
    expect(package.schema == responsive::kResponsiveLifecycleSchema &&
               package.id ==
                   value.engine.engine.identity.id.value + "-lifecycle-preview" &&
               package.captures.size() == 4U &&
               package.captures[0].role == LifecycleScenarioRole::starter &&
               package.captures[3].role == LifecycleScenarioRole::shutdown_elevated &&
               package.shutdown_elevated.has_value(),
           "manifest-ready lifecycle aggregate changed");
    expect(responsive::lifecycle_publication_names(
               LifecycleScenarioRole::shutdown_elevated)
                       .capture_role == "shutdown-high-rpm" &&
               responsive::lifecycle_publication_names(
                   LifecycleScenarioRole::shutdown_elevated)
                       .provenance_role == "shutdown-elevated",
           "elevated shutdown publication names changed");

    auto invalid_probe = probe_capture;
    invalid_probe.pcm.front() = std::numeric_limits<float>::quiet_NaN();
    expect(std::holds_alternative<responsive::LifecycleError>(
               responsive::choose_dynamic_starter_release(
                   invalid_probe, value.profile.rpm.outer_minimum_rpm)),
           "non-finite lifecycle PCM was admitted");
    std::stop_source stop;
    stop.request_stop();
    const auto stopped = responsive::choose_dynamic_starter_release(
        probe_capture, value.profile.rpm.outer_minimum_rpm, stop.get_token());
    const auto *cancelled = std::get_if<responsive::LifecycleError>(&stopped);
    expect(cancelled != nullptr &&
               cancelled->code == responsive::LifecycleErrorCode::cancelled,
           "lifecycle cancellation did not fail closed");
}

void test_real_capture_mapping(const std::filesystem::path &repository_root,
                               PlanningFixture &value) {
    static_cast<void>(repository_root);
    const auto assets = load_assets(value.engine, value.engine_path);
    const auto views = asset_views(assets);
    auto compiled_engine = require_authoring(
        compile::compile_engine(value.engine, views), "engine compile");
    const auto spec = require_lifecycle(
        responsive::plan_lifecycle_scenario(value.request,
                                            responsive::LifecycleScenarioRole::starter),
        "real starter plan");
    const auto references =
        authoring::validate_scenario_references(spec.scenario, value.engine);
    expect(references.ok(), "planned starter references are invalid");
    auto compiled_scenario =
        require_authoring(compile::compile_scenario(compiled_engine, spec.scenario),
                          "starter scenario compile");
    constexpr std::array<std::string_view, 1U> buses{"master.engine.audition"};
    auto captured =
        responsive::capture_finite_responsive_session(compiled_scenario, buses);
    if (const auto *failure =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&captured)) {
        throw std::runtime_error{"real starter capture: " + failure->detail_code +
                                 ": " + failure->message};
    }
    const auto &finite_capture =
        std::get<responsive::FiniteResponsiveCapture>(captured);
    const auto evidence =
        require_lifecycle(responsive::map_lifecycle_capture(spec, finite_capture, 0U),
                          "real starter map");
    expect(evidence.points.size() == spec.total_block_count &&
               evidence.points.front().source_frame == 3'839U &&
               evidence.points.back().source_frame == 479'999U &&
               evidence.pcm ==
                   finite_capture.buses.front().audible_interleaved_samples &&
               evidence.final_physics_frame ==
                   finite_capture.completion.physics_frame_count &&
               evidence.final_delivery_frame ==
                   finite_capture.completion.delivery_frame_count,
           "real 20 ms endpoint/PCM/horizon mapping is not exact");
    const auto cooked = require_lifecycle(responsive::cook_lifecycle_starter(evidence),
                                          "real starter cooking");
    expect(cooked.stable_cycles.size() >= 2U,
           "real starter capture has no stable crank loop");

    std::stop_source stop;
    stop.request_stop();
    const auto cancelled =
        responsive::map_lifecycle_capture(spec, finite_capture, 0U, stop.get_token());
    const auto *failure = std::get_if<responsive::LifecycleError>(&cancelled);
    expect(failure != nullptr &&
               failure->code == responsive::LifecycleErrorCode::cancelled,
           "mapped capture cancellation did not fail closed");
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        const std::filesystem::path repository_root = argv[1];
        auto value = fixture(repository_root);
        test_planner(value);
        test_low_rpm_planning_and_compile(value);
        test_cooking(value);
        test_real_capture_mapping(repository_root, value);
        std::cout << "responsive lifecycle tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
