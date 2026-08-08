#include "engine_sim_offline/responsive/directional_cook.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::responsive;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] contract::Sha256Digest digest(const std::string_view value) {
    return contract::sha256(
        std::as_bytes(std::span<const char>{value.data(), value.size()}));
}

[[nodiscard]] ResponsiveBakeProfile profile_6500() {
    authoring::EnginePackageDocument engine;
    engine.engine.limits.redline = {6'500.0, "rpm", std::nullopt};
    auto result = derive_engine_redline_affine_profile(engine);
    expect(std::holds_alternative<ResponsiveBakeProfile>(result),
           "directional test profile derives");
    return std::get<ResponsiveBakeProfile>(std::move(result));
}

[[nodiscard]] authoring::ScenarioDocument scenario_template() {
    authoring::ScenarioDocument scenario;
    scenario.id.value = "template";
    scenario.engine.value = "directional-engine";
    scenario.fuel.value = "directional-fuel";
    scenario.initial_state.crank_angle = {0.0, "rad", std::nullopt};
    scenario.output.buses.push_back({"master.engine.raw"});
    return scenario;
}

[[nodiscard]] double rpm_at_time(const DirectionalSweepScenarioSpec &spec,
                                 const double seconds) {
    if (seconds <= kDirectionalPreparationSeconds) {
        return spec.motion.start_rpm;
    }
    if (seconds >= spec.motion.sweep_end_seconds) {
        return spec.motion.end_rpm;
    }
    const double elapsed = seconds - kDirectionalPreparationSeconds;
    const double exponent = kDirectionalLogarithmicRpmRatePerSecond * elapsed;
    return spec.direction == DirectionalSweepDirection::rising
               ? spec.motion.start_rpm * std::exp(exponent)
               : spec.motion.start_rpm * std::exp(-exponent);
}

[[nodiscard]] double revolutions_at_time(const DirectionalSweepScenarioSpec &spec,
                                         const double seconds) {
    const double preparation_end =
        spec.motion.start_rpm * kDirectionalPreparationSeconds / 60.0;
    if (seconds <= kDirectionalPreparationSeconds) {
        return spec.motion.start_rpm * seconds / 60.0;
    }
    const double elapsed = std::min(seconds - kDirectionalPreparationSeconds,
                                    spec.motion.sweep_duration_seconds);
    const double rate = kDirectionalLogarithmicRpmRatePerSecond;
    const double sweep_revolutions =
        spec.direction == DirectionalSweepDirection::rising
            ? spec.motion.start_rpm * (std::exp(rate * elapsed) - 1.0) / (60.0 * rate)
            : spec.motion.start_rpm * (1.0 - std::exp(-rate * elapsed)) / (60.0 * rate);
    const double post_seconds = std::max(0.0, seconds - spec.motion.sweep_end_seconds);
    return preparation_end + sweep_revolutions +
           spec.motion.end_rpm * post_seconds / 60.0;
}

[[nodiscard]] FiniteResponsiveCapture
synthetic_capture(const DirectionalSweepScenarioSpec &spec) {
    FiniteResponsiveCapture capture;
    capture.engine_id = spec.scenario.engine.value;
    capture.scenario_id = spec.scenario.id.value;
    capture.physics_rate = {10'000U, 1U};
    capture.delivery_rate = {192'000U, 1U};
    capture.physics_frames_per_block = 200U;
    capture.delivery_frames_per_block = 3'840U;
    capture.preparation_block_count = spec.preparation_block_count;
    capture.total_block_count = spec.total_block_count;
    capture.total_physics_frame_count = spec.total_block_count * 200U;
    capture.total_delivery_frame_count = spec.total_block_count * 3'840U;
    capture.audible_first_delivery_frame = spec.preparation_block_count * 3'840U;
    capture.audible_delivery_frame_count =
        (spec.total_block_count - spec.preparation_block_count) * 3'840U;
    capture.motion_mode = EngineMotionMode::held_dyno;
    capture.selected_bus_ids = {"route.a", "route.b"};
    capture.blocks.reserve(static_cast<std::size_t>(spec.total_block_count));
    const auto state_flags =
        engine_cycle_state_flag_mask(EngineCycleStateFlag::ignition_enabled) |
        engine_cycle_state_flag_mask(EngineCycleStateFlag::fuel_enabled) |
        engine_cycle_state_flag_mask(EngineCycleStateFlag::dyno_enabled) |
        engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_enabled) |
        engine_cycle_state_flag_mask(EngineCycleStateFlag::limiter_cut_active);
    for (std::uint64_t block = 0U; block < spec.total_block_count; ++block) {
        ResponsiveCaptureBlock value;
        value.block_ordinal = block;
        value.phase = block < spec.preparation_block_count
                          ? EngineSessionBlockPhase::preparation
                          : EngineSessionBlockPhase::audible;
        value.first_physics_frame = block * 200U;
        value.physics_frame_count = 200U;
        value.first_delivery_frame = block * 3'840U;
        value.delivery_frame_count = 3'840U;
        value.endpoint.delivery_frame = (block + 1U) * 3'840U;
        const double time = static_cast<double>(block + 1U) / 50.0;
        value.endpoint.engine_speed_rpm = rpm_at_time(spec, time);
        value.endpoint.mean_intake_manifold_pressure_pa_abs =
            58'000.0 + spec.lane.throttle_01 * 22'000.0;
        value.endpoint.requested_throttle_01 = spec.lane.throttle_01;
        value.endpoint.resolved_engine_throttle_01 = spec.lane.throttle_01;
        value.endpoint.unwrapped_crank_revolutions = revolutions_at_time(spec, time);
        value.endpoint.state_flags = state_flags;
        capture.blocks.push_back(std::move(value));
    }

    const auto sample_count =
        static_cast<std::size_t>(capture.audible_delivery_frame_count);
    for (std::size_t bus_index = 0U; bus_index < capture.selected_bus_ids.size();
         ++bus_index) {
        ResponsiveCaptureBus bus;
        bus.descriptor.id = capture.selected_bus_ids[bus_index];
        bus.descriptor.channel_count = 1U;
        bus.descriptor.sample_rate = capture.delivery_rate;
        bus.audible_interleaved_samples.resize(sample_count);
        const double gain =
            (bus_index == 0U ? 1.0 : 0.35) * (1.0 + spec.lane.throttle_01 * 0.15);
        for (std::size_t sample = 0U; sample < sample_count; ++sample) {
            const std::size_t audible_block = sample / 3'840U;
            const std::size_t within_block = sample % 3'840U;
            const std::size_t right_block =
                static_cast<std::size_t>(spec.preparation_block_count) + audible_block;
            const double left_revolutions =
                capture.blocks[right_block - 1U].endpoint.unwrapped_crank_revolutions;
            const double right_revolutions =
                capture.blocks[right_block].endpoint.unwrapped_crank_revolutions;
            const double amount = static_cast<double>(within_block) / 3'840.0;
            const double revolutions =
                left_revolutions + (right_revolutions - left_revolutions) * amount;
            double phase = std::fmod(revolutions, 2.0) / 2.0;
            if (phase < 0.0) {
                phase += 1.0;
            }
            bus.audible_interleaved_samples[sample] = static_cast<float>(
                gain * (0.72 * std::sin(2.0 * std::numbers::pi * phase) +
                        0.19 * std::sin(6.0 * std::numbers::pi * phase) +
                        0.06 * std::cos(14.0 * std::numbers::pi * phase)));
        }
        capture.buses.push_back(std::move(bus));
    }
    capture.engine_provenance.bundle.sha256 = digest("directional-engine");
    capture.scenario_provenance.bundle.sha256 = digest(spec.id);
    return capture;
}

[[nodiscard]] std::vector<DirectionalCookedCapture>
cook_all(const std::vector<DirectionalSweepScenarioSpec> &plans) {
    std::vector<DirectionalCookedCapture> result;
    result.reserve(plans.size());
    for (const auto &plan : plans) {
        auto capture = synthetic_capture(plan);
        auto cooked = cook_directional_capture(plan, capture);
        expect(std::holds_alternative<DirectionalCookedCapture>(cooked),
               "synthetic multi-bus directional capture cooks");
        if (std::holds_alternative<DirectionalCookedCapture>(cooked)) {
            result.push_back(std::get<DirectionalCookedCapture>(std::move(cooked)));
        }
    }
    return result;
}

} // namespace

int main() {
    const auto profile = profile_6500();
    const DirectionalScenarioPlanRequest request{profile, scenario_template(),
                                                 digest("directional-template")};
    const auto plan_result = plan_directional_sweep_scenarios(request);
    expect(
        std::holds_alternative<std::vector<DirectionalSweepScenarioSpec>>(plan_result),
        "directional sweep lattice plans");
    if (!std::holds_alternative<std::vector<DirectionalSweepScenarioSpec>>(
            plan_result)) {
        return 1;
    }
    const auto &plans =
        std::get<std::vector<DirectionalSweepScenarioSpec>>(plan_result);
    expect(plans.size() == 6U &&
               plans[0U].id == "directional-engine-directional-rising-coast-capture" &&
               plans[2U].lane.id == "power" &&
               plans[3U].direction == DirectionalSweepDirection::falling &&
               plans[5U].lane.id == "power",
           "directional plan order is rising lanes then falling lanes");
    expect(plans.front().motion.start_rpm == 480.0 &&
               plans.front().motion.end_rpm == 6'825.0 &&
               plans.front().motion.sweep_duration_seconds == 7.98 &&
               plans.front().motion.sweep_end_seconds == 12.48 &&
               plans.front().motion.total_duration_seconds == 13.48 &&
               plans.front().motion.points.size() == 35U &&
               plans.front().motion.points[2U].engine_speed_rpm == 521.71394377018987 &&
               plans.front().preparation_block_count == 225U &&
               plans.front().total_block_count == 674U,
           "directional trajectory matches current exponential/20ms contract");
    expect(plans[3U].motion.start_rpm == 6'825.0 && plans[3U].motion.end_rpm == 480.0,
           "falling trajectory reverses the guarded capture domain");
    const auto repeated_plan = plan_directional_sweep_scenarios(request);
    expect(std::holds_alternative<std::vector<DirectionalSweepScenarioSpec>>(
               repeated_plan) &&
               std::get<std::vector<DirectionalSweepScenarioSpec>>(repeated_plan) ==
                   plans,
           "directional scenarios and identities repeat exactly");

    auto cooked = cook_all(plans);
    expect(cooked.size() == 6U, "all six directional multi-bus captures cook");
    if (cooked.size() != 6U) {
        return 1;
    }
    expect(cooked.front().routes.size() == 2U &&
               cooked.front().routes.front().cells.size() == 11U &&
               cooked.front().routes.front().cells.front().source.size() ==
                   3U * 4'096U &&
               cooked.front().routes.front().cells.front().telemetry.state_masks ==
                   std::vector<std::uint32_t>{27U},
           "each bus emits all anchors and maps session flags to the five-bit playback "
           "mask");
    expect(
        !cooked.front().routes.front().cells.front().source_payload_sha256.is_zero() &&
            !cooked.front().identity_sha256.is_zero(),
        "directional source/capture identities are populated");

    auto repeat_capture = synthetic_capture(plans.front());
    const auto repeated_cook = cook_directional_capture(plans.front(), repeat_capture);
    expect(std::holds_alternative<DirectionalCookedCapture>(repeated_cook) &&
               std::get<DirectionalCookedCapture>(repeated_cook) == cooked.front(),
           "directional transforms and payload identities repeat exactly");

    const auto model_first = assemble_directional_model(profile, cooked);
    const auto model_second = assemble_directional_model(profile, cooked);
    expect(std::holds_alternative<DirectionalCookedModel>(model_first) &&
               std::holds_alternative<DirectionalCookedModel>(model_second),
           "complete directional lattice assembles");
    if (const auto *model = std::get_if<DirectionalCookedModel>(&model_first)) {
        expect(model->selected_bus_ids ==
                       std::vector<std::string>{"route.a", "route.b"} &&
                   model->route_directions.size() == 4U &&
                   model->route_directions[0U].bus_id == "route.a" &&
                   model->route_directions[0U].direction ==
                       DirectionalSweepDirection::rising &&
                   model->route_directions[1U].direction ==
                       DirectionalSweepDirection::falling &&
                   model->route_directions[2U].bus_id == "route.b" &&
                   model->route_directions[0U].cells.size() == 33U,
               "model is manifest-ready in bus-major rising/falling order");
        expect(model->combined_seam_gate.passed &&
                   model->combined_seam_gate
                           .observed_maximum_seam_over_source_adjacent_derivative_rms <=
                       3.0 &&
                   model->combined_seam_gate
                           .observed_maximum_correction_rms_over_source_rms <= 0.4,
               "combined-route seam gate enforces current thresholds");
        expect(std::get<DirectionalCookedModel>(model_second).identity_sha256 ==
                   model->identity_sha256,
               "directional model identity repeats exactly");
    }

    auto coalesced_input = cooked;
    for (std::size_t direction = 0U; direction < 2U; ++direction) {
        auto &retained = coalesced_input[direction * kResponsiveLoadLaneCount];
        auto &alias = coalesced_input[direction * kResponsiveLoadLaneCount + 1U];
        for (std::size_t route = 0U; route < alias.routes.size(); ++route) {
            for (std::size_t rpm = 0U; rpm < alias.routes[route].cells.size(); ++rpm) {
                auto copied = retained.routes[route].cells[rpm];
                copied.id = alias.routes[route].cells[rpm].id;
                copied.lane_id = "mid";
                copied.capture_throttle_01 = 0.2;
                alias.routes[route].cells[rpm] = std::move(copied);
            }
        }
    }
    const auto coalesced_model =
        assemble_directional_model(profile, std::move(coalesced_input));
    expect(std::holds_alternative<DirectionalCookedModel>(coalesced_model) &&
               std::get<DirectionalCookedModel>(coalesced_model)
                       .route_directions.front()
                       .cells.size() == 22U &&
               std::get<DirectionalCookedModel>(coalesced_model)
                       .route_directions.front()
                       .coalesced_load_aliases.size() == 11U,
           "byte-identical tied load coordinates coalesce with explicit aliases");

    auto tied_distinct = cooked;
    for (std::size_t direction = 0U; direction < 2U; ++direction) {
        const auto &retained = tied_distinct[direction * kResponsiveLoadLaneCount];
        auto &conflict = tied_distinct[direction * kResponsiveLoadLaneCount + 1U];
        for (std::size_t route = 0U; route < conflict.routes.size(); ++route) {
            for (std::size_t rpm = 0U; rpm < conflict.routes[route].cells.size();
                 ++rpm) {
                conflict.routes[route]
                    .cells[rpm]
                    .telemetry.mean_manifold_pressure_pa_abs =
                    retained.routes[route]
                        .cells[rpm]
                        .telemetry.mean_manifold_pressure_pa_abs;
            }
        }
    }
    expect(std::holds_alternative<contract::ValidationReport>(
               assemble_directional_model(profile, std::move(tied_distinct))),
           "distinct material at one directional RPM/MAP coordinate fails closed");

    auto failed_gate = cooked;
    for (auto &capture : failed_gate) {
        for (auto &route : capture.routes) {
            for (auto &cell : route.cells) {
                std::ranges::fill(cell.seam_closed, 0.0F);
                cell.seam_closed_payload_sha256 =
                    directional_float32_payload_identity(cell.seam_closed);
            }
        }
    }
    expect(std::holds_alternative<contract::ValidationReport>(
               assemble_directional_model(profile, std::move(failed_gate))),
           "directional model fails closed when the combined seam gate is exceeded");

    return failures == 0 ? 0 : 1;
}
