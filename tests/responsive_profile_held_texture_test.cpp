#include "engine_sim_offline/responsive/held_texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <string>
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

[[nodiscard]] ResponsiveBakeProfile profile_for_redline(const double rpm) {
    authoring::EnginePackageDocument engine;
    engine.engine.limits.redline = {rpm, "rpm", std::nullopt};
    auto result = derive_engine_redline_affine_profile(engine);
    expect(std::holds_alternative<ResponsiveBakeProfile>(result),
           "valid redline selects a responsive profile");
    return std::get<ResponsiveBakeProfile>(std::move(result));
}

[[nodiscard]] authoring::ScenarioDocument scenario_template() {
    authoring::ScenarioDocument scenario;
    scenario.id.value = "template";
    scenario.engine.value = "test-engine";
    scenario.fuel.value = "test-fuel";
    scenario.initial_state.crank_angle = {0.0, "rad", std::nullopt};
    scenario.output.buses.push_back({"master.engine.raw"});
    return scenario;
}

[[nodiscard]] FiniteResponsiveCapture
synthetic_capture(const HeldStateScenarioSpec &spec) {
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
    capture.selected_bus_ids = {"route.a", "route.b"};
    capture.blocks.reserve(static_cast<std::size_t>(spec.total_block_count));
    const double revolutions_per_block = spec.rpm / 3'000.0;
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
        value.endpoint.engine_speed_rpm = spec.rpm;
        value.endpoint.mean_intake_manifold_pressure_pa_abs = 63'000.0;
        value.endpoint.requested_throttle_01 = spec.lane.throttle_01;
        value.endpoint.resolved_engine_throttle_01 = spec.lane.throttle_01;
        value.endpoint.unwrapped_crank_revolutions =
            static_cast<double>(block + 1U) * revolutions_per_block;
        capture.blocks.push_back(std::move(value));
    }
    const auto samples = static_cast<std::size_t>(capture.audible_delivery_frame_count);
    const double delivery_frames_per_revolution = 192'000.0 * 60.0 / spec.rpm;
    const auto cycle_frames =
        static_cast<std::uint64_t>(std::llround(2.0 * delivery_frames_per_revolution));
    for (std::size_t bus = 0U; bus < capture.selected_bus_ids.size(); ++bus) {
        ResponsiveCaptureBus route;
        route.descriptor.id = capture.selected_bus_ids[bus];
        route.descriptor.channel_count = 1U;
        route.descriptor.sample_rate = capture.delivery_rate;
        route.audible_interleaved_samples.resize(samples);
        const double gain = bus == 0U ? 1.0 : 0.35;
        for (std::size_t index = 0U; index < samples; ++index) {
            const std::uint64_t absolute_frame =
                capture.audible_first_delivery_frame + index;
            const double phase = static_cast<double>(absolute_frame % cycle_frames) /
                                 static_cast<double>(cycle_frames);
            route.audible_interleaved_samples[index] = static_cast<float>(
                gain * (0.7 * std::sin(2.0 * std::numbers::pi * phase) +
                        0.2 * std::sin(6.0 * std::numbers::pi * phase) +
                        0.08 * std::cos(14.0 * std::numbers::pi * phase)));
        }
        capture.buses.push_back(std::move(route));
    }
    capture.engine_provenance.bundle.sha256 = digest("engine");
    capture.scenario_provenance.bundle.sha256 = digest("scenario");
    return capture;
}

[[nodiscard]] std::vector<float> shifted_test_signal(const std::int64_t shift,
                                                     const double gain) {
    std::vector<float> result(kHeldSamplesPerCycle);
    const auto count = static_cast<std::int64_t>(kHeldSamplesPerCycle);
    for (std::int64_t index = 0; index < count; ++index) {
        std::int64_t source = (index - shift) % count;
        if (source < 0) {
            source += count;
        }
        const double phase = static_cast<double>(source) / static_cast<double>(count);
        result[static_cast<std::size_t>(index)] = static_cast<float>(
            gain * (std::sin(2.0 * std::numbers::pi * phase) +
                    0.37 * std::cos(14.0 * std::numbers::pi * phase) +
                    0.19 * std::sin(26.0 * std::numbers::pi * phase) +
                    0.07 * std::cos(62.0 * std::numbers::pi * phase)));
    }
    return result;
}

[[nodiscard]] std::vector<HeldCookedCell>
alignment_cells(const ResponsiveBakeProfile &profile) {
    std::vector<HeldCookedCell> result;
    result.reserve(kResponsiveRpmAnchorCount * kResponsiveLoadLaneCount);
    for (std::size_t rpm = 0U; rpm < kResponsiveRpmAnchorCount; ++rpm) {
        for (std::size_t lane = 0U; lane < kResponsiveLoadLaneCount; ++lane) {
            HeldCookedCell cell;
            cell.rpm = profile.rpm.anchors[rpm];
            cell.lane_id = profile.capture.load_lanes[lane].id;
            cell.throttle_01 = profile.capture.load_lanes[lane].throttle_01;
            cell.id = std::to_string(cell.rpm) + "rpm-" + cell.lane_id;
            cell.coalesced_authored_lanes = {cell.lane_id};
            cell.coalesced_capture_throttles_01 = {cell.throttle_01};
            HeldCookedRoute route;
            route.route_id = "route.a";
            route.source_interval = {44.0, 140.0};
            route.texture.mean =
                shifted_test_signal(static_cast<std::int64_t>(rpm * 37U + lane * 23U),
                                    1.0 + static_cast<double>(rpm) * 0.01 +
                                        static_cast<double>(lane) * 0.02);
            route.telemetry.mean_manifold_pressure_pa_abs =
                50'000.0 + static_cast<double>(rpm) * 10.0 +
                static_cast<double>(lane) * 1'000.0;
            route.mean_payload_sha256 =
                held_float32_payload_identity(route.texture.mean);
            route.residual_payload_sha256 = digest("empty-residual");
            route.identity_sha256 = route.mean_payload_sha256;
            cell.routes.push_back(std::move(route));
            cell.identity_sha256 = digest(cell.id);
            result.push_back(std::move(cell));
        }
    }
    return result;
}

} // namespace

int main() {
    const auto profile = profile_for_redline(6'500.0);
    constexpr std::array<double, kResponsiveRpmAnchorCount> expected_anchors{
        600.0,   700.0,   900.0,   1'200.0, 1'600.0, 2'200.0,
        3'000.0, 4'000.0, 5'000.0, 6'000.0, 6'500.0};
    expect(profile.rpm.anchors == expected_anchors,
           "6500-RPM profile preserves the installed anchor grid");
    expect(profile.rpm.outer_minimum_rpm == 550.0 &&
               profile.rpm.outer_maximum_rpm == 6'700.0 &&
               profile.lifecycle.elevated_shutdown_rpm == 3'000.0,
           "6500-RPM profile preserves installed envelope/lifecycle anchors");
    expect(validate_responsive_bake_profile(profile).ok(),
           "derived profile validates with its stable identity");
    expect(profile_for_redline(250.0).rpm.anchors.front() == 50.0,
           "low-redline affine profile keeps the documented one-fifth floor");
    const auto high_redline_profile = profile_for_redline(65'500.0);
    expect(high_redline_profile.rpm.anchors.front() == 600.0 &&
               high_redline_profile.rpm.outer_minimum_rpm == 480.0 &&
               validate_responsive_bake_profile(high_redline_profile).ok(),
           "high-redline affine profile does not clamp to its directional envelope");

    authoring::EnginePackageDocument rejected_engine;
    rejected_engine.engine.limits.redline = {249.0, "rpm", std::nullopt};
    const auto rejected_profile = derive_engine_redline_affine_profile(rejected_engine);
    expect(std::holds_alternative<contract::ValidationReport>(rejected_profile),
           "redlines below the automatic-profile floor fail closed");

    HeldScenarioPlanRequest plan_request{profile, scenario_template(),
                                         digest("scenario-template"),
                                         HeldCaptureOperatingMode::held_speed};
    auto plan_result = plan_held_state_scenarios(plan_request);
    expect(std::holds_alternative<std::vector<HeldStateScenarioSpec>>(plan_result),
           "held state scenarios plan from a zero-radian crank template");
    if (!std::holds_alternative<std::vector<HeldStateScenarioSpec>>(plan_result)) {
        return 1;
    }
    const auto &plans = std::get<std::vector<HeldStateScenarioSpec>>(plan_result);
    expect(plans.size() == 33U, "held planner emits the fixed 11 by 3 grid");
    expect(plans.front().id == "600rpm-coast" &&
               plans.front().preparation_duration_seconds == 3.6 &&
               plans.front().audible_duration_seconds == 11.72 &&
               plans.front().total_duration_seconds == 15.32 &&
               plans.front().preparation_block_count == 180U &&
               plans.front().total_block_count == 766U,
           "slow-anchor plan matches exact 20 ms installed quantization");
    expect(plans.back().id == "6500rpm-power" &&
               plans.back().preparation_duration_seconds == 3.0 &&
               plans.back().audible_duration_seconds == 1.54 &&
               plans.back().total_duration_seconds == 4.54,
           "redline plan matches the installed horizon");

    const auto compressed_profile = profile_for_redline(3'600.0);
    auto compressed_plan_result = plan_held_state_scenarios(
        {compressed_profile, scenario_template(), digest("compressed-template"),
         HeldCaptureOperatingMode::held_speed});
    expect(std::holds_alternative<std::vector<HeldStateScenarioSpec>>(
               compressed_plan_result),
           "redline-compressed held grid plans successfully");
    if (const auto *compressed =
            std::get_if<std::vector<HeldStateScenarioSpec>>(&compressed_plan_result)) {
        expect(compressed->at(3U).rpm == 650.847458 &&
                   compressed->at(3U).preparation_duration_seconds == 3.14,
               "compressed extension threshold retains the compiler's "
               "17-cycle preparation bound");
        for (const auto &spec : *compressed) {
            expect(spec.preparation_duration_seconds >= 17.0 * 120.0 / spec.rpm,
                   "compressed held preparation violates complete-cycle "
                   "admission");
        }
    }

    const auto minimum_profile = profile_for_redline(250.0);
    auto minimum_plan_result = plan_held_state_scenarios(
        {minimum_profile, scenario_template(), digest("minimum-template"),
         HeldCaptureOperatingMode::held_speed});
    expect(
        std::holds_alternative<std::vector<HeldStateScenarioSpec>>(minimum_plan_result),
        "minimum admitted redline held grid plans successfully");
    if (const auto *minimum =
            std::get_if<std::vector<HeldStateScenarioSpec>>(&minimum_plan_result)) {
        for (const auto &spec : *minimum) {
            expect(spec.preparation_duration_seconds >= 17.0 * 120.0 / spec.rpm,
                   "minimum-redline held preparation violates complete-cycle "
                   "admission");
        }
    }
    const auto repeated_plan = plan_held_state_scenarios(plan_request);
    expect(std::holds_alternative<std::vector<HeldStateScenarioSpec>>(repeated_plan) &&
               std::get<std::vector<HeldStateScenarioSpec>>(repeated_plan) == plans,
           "held scenario plans and identities repeat exactly");

    auto capture = synthetic_capture(plans.front());
    const auto interval_result = choose_held_capture_interval(capture);
    expect(std::holds_alternative<HeldCaptureInterval>(interval_result) &&
               std::get<HeldCaptureInterval>(interval_result) ==
                   HeldCaptureInterval{44.0, 140.0},
           "held interval chooses four guarded cycles then exactly 48 cycles");
    if (const auto *interval = std::get_if<HeldCaptureInterval>(&interval_result)) {
        const auto normalized_result =
            phase_normalize_held_capture(capture, 0U, *interval);
        expect(std::holds_alternative<std::vector<float>>(normalized_result),
               "held PCM phase-normalizes against capture endpoints");
        if (const auto *normalized =
                std::get_if<std::vector<float>>(&normalized_result)) {
            expect(normalized->size() == kHeldNormalizedSampleCount,
                   "phase normalization emits 48 times 4096 samples");
            expect(std::equal(normalized->begin(),
                              normalized->begin() +
                                  static_cast<std::ptrdiff_t>(kHeldSamplesPerCycle),
                              normalized->begin() +
                                  static_cast<std::ptrdiff_t>(kHeldSamplesPerCycle)),
                   "identical synthetic cycles remain sample-identical");
            const auto decomposition = decompose_held_texture(*normalized);
            expect(std::holds_alternative<HeldTextureDecomposition>(decomposition),
                   "phase-normalized source decomposes");
            if (const auto *texture =
                    std::get_if<HeldTextureDecomposition>(&decomposition)) {
                expect(texture->mean.size() == kHeldSamplesPerCycle &&
                           texture->residuals.size() == kHeldNormalizedSampleCount,
                       "decomposition has the fixed mean/residual shapes");
                expect(texture->metrics.residual_taper_frames_per_edge == 256U &&
                           texture->metrics.maximum_residual_boundary_magnitude == 0.0,
                       "residual taper is the fixed boundary-zero smoothstep");
                expect(held_float32_payload_identity(texture->mean) ==
                           held_float32_payload_identity(texture->mean),
                       "Float32 payload identity is deterministic");
            }
        }
    }
    const auto cooked_state = cook_held_state(plans.front(), capture);
    expect(std::holds_alternative<HeldCookedCell>(cooked_state) &&
               std::get<HeldCookedCell>(cooked_state).routes.size() == 2U,
           "one common multi-bus capture cooks every selected route");
    const auto repeated_cooked_state = cook_held_state(plans.front(), capture);
    expect(std::holds_alternative<HeldCookedCell>(repeated_cooked_state) &&
               std::holds_alternative<HeldCookedCell>(cooked_state) &&
               std::get<HeldCookedCell>(repeated_cooked_state) ==
                   std::get<HeldCookedCell>(cooked_state),
           "held state payloads and identities repeat exactly");

    auto cells = alignment_cells(profile);
    const auto alignment_result = align_held_texture_grid(profile, cells);
    expect(std::holds_alternative<HeldPhaseAlignment>(alignment_result),
           "complete held grid phase-aligns");
    if (const auto *alignment = std::get_if<HeldPhaseAlignment>(&alignment_result)) {
        expect(alignment->reference_cell_id == "3000.000000rpm-power" &&
                   alignment->cells.size() == 33U &&
                   alignment->report.edge_count == 52U &&
                   alignment->report.adjacent_edges.size() == 52U,
               "phase alignment uses the documented reference and 52 grid edges");
        bool shifts_match = true;
        constexpr double reference_source_shift = 6.0 * 37.0 + 2.0 * 23.0;
        for (std::size_t index = 0U; index < alignment->cells.size(); ++index) {
            const std::size_t rpm = index / kResponsiveLoadLaneCount;
            const std::size_t lane = index % kResponsiveLoadLaneCount;
            const double expected =
                reference_source_shift - static_cast<double>(rpm * 37U + lane * 23U);
            shifts_match = shifts_match &&
                           std::abs(alignment->cells[index].shift_to_canonical_samples -
                                    expected) < 1e-6;
        }
        expect(shifts_match,
               "correlation and solve recover the synthetic canonical phase");
        const auto repeated_alignment = align_held_texture_grid(profile, cells);
        expect(std::holds_alternative<HeldPhaseAlignment>(repeated_alignment) &&
                   std::get<HeldPhaseAlignment>(repeated_alignment) == *alignment,
               "FFT correlation, unwrap, and graph solve repeat exactly");
        HeldPriorPhaseAlignment prior;
        prior.reference_cell_id = alignment->reference_cell_id;
        prior.cells = alignment->cells;
        const auto reused = align_held_texture_grid(profile, cells, prior);
        expect(std::holds_alternative<HeldPhaseAlignment>(reused) &&
                   std::get<HeldPhaseAlignment>(reused)
                           .report.preserved_fixed_cell_count == 33U &&
                   std::get<HeldPhaseAlignment>(reused)
                           .report.maximum_preserved_fixed_shift_error_samples == 0.0,
               "prior alignment shifts remain exact fixed constraints");
    }
    const auto grid_result = cook_held_texture_grid(profile, cells);
    expect(std::holds_alternative<HeldCookedGrid>(grid_result) &&
               std::get<HeldCookedGrid>(grid_result).cells.size() == 33U,
           "native grid cook returns aligned/coalesced package inputs");

    std::vector<HeldCookedCell> duplicate_loads{cells[0U], cells[1U], cells[2U]};
    duplicate_loads[1U].routes = duplicate_loads[0U].routes;
    const auto coalesced = coalesce_duplicate_held_loads(duplicate_loads);
    expect(std::holds_alternative<std::vector<HeldCookedCell>>(coalesced) &&
               std::get<std::vector<HeldCookedCell>>(coalesced).size() == 2U &&
               std::get<std::vector<HeldCookedCell>>(coalesced)
                       .front()
                       .coalesced_authored_lanes ==
                   std::vector<std::string>{"coast", "mid"},
           "exact duplicate MAP/payload loads coalesce in authored order");
    duplicate_loads[1U].routes[0U].mean_payload_sha256 = digest("different");
    expect(std::holds_alternative<contract::ValidationReport>(
               coalesce_duplicate_held_loads(duplicate_loads)),
           "distinct audio at one MAP coordinate fails closed");

    auto zero_energy = cells;
    std::ranges::fill(zero_energy.front().routes.front().texture.mean, 0.0F);
    zero_energy.front().routes.front().mean_payload_sha256 =
        held_float32_payload_identity(zero_energy.front().routes.front().texture.mean);
    expect(std::holds_alternative<contract::ValidationReport>(
               align_held_texture_grid(profile, zero_energy)),
           "zero-energy phase correlation fails closed");

    return failures == 0 ? 0 : 1;
}
