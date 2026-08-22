#include "crankwave/authoring/json.hpp"
#include "crankwave/authoring/parse.hpp"
#include "crankwave/responsive/package_children.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace crankwave;
using namespace crankwave::responsive;

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

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto byte : value.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    const auto bytes = std::as_bytes(std::span{text});
    return {bytes.begin(), bytes.end()};
}

void export_child_if_requested(const ResponsiveOptionalChildPackageV1 &child) {
    const char *const requested = std::getenv("CRANKWAVE_PACKAGE_CHILD_FIXTURE_DIRECTORY");
    if (requested == nullptr || *requested == '\0') {
        return;
    }
    const std::filesystem::path root{requested};
    for (const auto &entry : child.members) {
        const auto output = root / entry.path;
        std::filesystem::create_directories(output.parent_path());
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char *>(entry.bytes.data()),
                     static_cast<std::streamsize>(entry.bytes.size()));
        expect(static_cast<bool>(stream), "optional child fixture export succeeds");
    }
}

[[nodiscard]] contract::ProvenanceLedger
provenance(const std::string_view id, const contract::Sha256Digest &sha256) {
    return {"crankwave.provenance.v1", {std::string{id}, sha256}, {}, {}, {}};
}

[[nodiscard]] ResponsiveBakeProfile profile() {
    authoring::EnginePackageDocument engine;
    engine.engine.limits.redline = {6'500.0, "rpm", std::nullopt};
    auto selected = derive_engine_redline_affine_profile(engine);
    expect(std::holds_alternative<ResponsiveBakeProfile>(selected),
           "test profile is admitted");
    return std::get<ResponsiveBakeProfile>(std::move(selected));
}

[[nodiscard]] HeldCookedGrid held_grid(const ResponsiveBakeProfile &selected,
                                       const std::span<const std::string> buses) {
    HeldCookedGrid result;
    for (std::size_t rpm_index = 0U; rpm_index < selected.rpm.anchors.size();
         ++rpm_index) {
        HeldCookedCell cell;
        cell.id = "held-r" + std::to_string(rpm_index) + "-coast";
        cell.rpm = selected.rpm.anchors[rpm_index];
        cell.lane_id = "coast";
        cell.throttle_01 = 0.0;
        cell.coalesced_authored_lanes = {"coast", "mid", "power"};
        cell.coalesced_capture_throttles_01 = {0.0, 0.2, 1.0};
        cell.identity_sha256 = digest(cell.id);
        for (std::size_t route_index = 0U; route_index < buses.size(); ++route_index) {
            HeldCookedRoute route;
            route.route_id = buses[route_index];
            route.source_interval = {20.0, 116.0};
            route.texture.mean.assign(
                kHeldSamplesPerCycle,
                static_cast<float>((rpm_index + 1U) * (route_index + 1U)) * 1.0e-6F);
            route.texture.residuals.assign(kHeldNormalizedSampleCount, 0.0F);
            route.texture.metrics.source_rms = 1.0e-5;
            route.texture.metrics.mean_rms = 1.0e-5;
            route.texture.metrics.residual_taper_frames_per_edge = 256U;
            route.telemetry.mean_manifold_pressure_pa_abs = 50'000.0;
            route.telemetry.telemetry_endpoint_count = 100U;
            route.mean_payload_sha256 =
                held_float32_payload_identity(route.texture.mean);
            route.residual_payload_sha256 =
                held_float32_payload_identity(route.texture.residuals);
            route.identity_sha256 = digest(cell.id + buses[route_index]);
            cell.routes.push_back(std::move(route));
        }
        result.phase_alignment.cells.push_back({cell.id, 0.0});
        result.cells.push_back(std::move(cell));
    }
    result.phase_alignment.reference_cell_id = result.cells.front().id;
    result.phase_alignment.identity_sha256 = digest("held-alignment");
    result.identity_sha256 = digest("held-grid");
    return result;
}

[[nodiscard]] DirectionalCookedModel
directional_model(const ResponsiveBakeProfile &selected,
                  const std::span<const std::string> buses) {
    DirectionalCookedModel result;
    result.engine_id = "test-engine";
    result.outer_minimum_rpm = selected.rpm.outer_minimum_rpm;
    result.outer_maximum_rpm = selected.rpm.outer_maximum_rpm;
    result.rpm_anchors = selected.rpm.anchors;
    result.selected_bus_ids.assign(buses.begin(), buses.end());
    for (std::size_t bus_index = 0U; bus_index < buses.size(); ++bus_index) {
        for (const auto direction :
             {DirectionalSweepDirection::rising, DirectionalSweepDirection::falling}) {
            DirectionalRouteDirectionModel route;
            route.bus_id = buses[bus_index];
            route.direction = direction;
            for (std::size_t rpm_index = 0U; rpm_index < selected.rpm.anchors.size();
                 ++rpm_index) {
                DirectionalCookedCell cell;
                cell.id =
                    "directional-b" + std::to_string(bus_index) + "-" +
                    (direction == DirectionalSweepDirection::rising ? "up-" : "down-") +
                    std::to_string(rpm_index);
                cell.rpm = selected.rpm.anchors[rpm_index];
                cell.direction = direction;
                cell.lane_id = "coast";
                cell.capture_throttle_01 = 0.0;
                cell.crossing_revolutions = 18.0;
                cell.source_cycle_begin_revolutions = 20.0;
                cell.source_cycle_end_revolutions = 26.0;
                cell.source.assign(
                    kDirectionalSamplesPerCycle * kDirectionalSourceCycleCount,
                    static_cast<float>((rpm_index + 1U) * (bus_index + 1U)) * 1.0e-6F);
                cell.seam_closed = cell.source;
                cell.telemetry.mean_manifold_pressure_pa_abs = 50'000.0;
                cell.telemetry.mean_engine_speed_rpm = cell.rpm;
                cell.telemetry.mean_requested_throttle_01 = 0.0;
                cell.telemetry.mean_resolved_engine_throttle_01 = 0.0;
                cell.telemetry.state_masks = {3U};
                cell.telemetry.endpoint_count = 3U;
                cell.closure.signal_rms = 1.0e-5;
                cell.closure.source_adjacent_derivative_rms = 1.0e-6;
                cell.source_payload_sha256 =
                    directional_float32_payload_identity(cell.source);
                cell.seam_closed_payload_sha256 =
                    directional_float32_payload_identity(cell.seam_closed);
                cell.identity_sha256 = digest(cell.id);
                route.cells.push_back(std::move(cell));
            }
            route.identity_sha256 =
                digest(route.bus_id + (direction == DirectionalSweepDirection::rising
                                           ? "rising"
                                           : "falling"));
            result.route_directions.push_back(std::move(route));
        }
    }
    result.combined_seam_gate.passed = true;
    result.identity_sha256 = digest("directional-model");
    return result;
}

[[nodiscard]] ResponsiveCompiledTransfer fixed_transfer() {
    ResponsiveCompiledTransfer result;
    result.shape = ResponsiveTransferShape::fixed_overlap_save;
    result.fft_size = 65'536U;
    result.coefficient_count = 30'071U;
    result.partition_count = 1U;
    result.spectrum_bytes.resize(65'536U * 2U * sizeof(double));
    result.spectrum_sha256 = contract::sha256(result.spectrum_bytes);
    result.identity_sha256 = digest("fixed-transfer");
    return result;
}

[[nodiscard]] ResponsiveCompiledTransfer partitioned_transfer() {
    ResponsiveCompiledTransfer result;
    result.shape = ResponsiveTransferShape::uniform_partitioned_overlap_save;
    result.fft_size = 8'192U;
    result.coefficient_count = 384'292U;
    result.partition_frame_count = 3'840U;
    result.partition_count = 101U;
    result.spectrum_bytes.resize(101U * 8'192U * 2U * sizeof(double));
    result.spectrum_sha256 = contract::sha256(result.spectrum_bytes);
    result.identity_sha256 = digest("partitioned-transfer");
    return result;
}

[[nodiscard]] ResponsiveCompiledPresentation
presentation(const std::span<const std::string> buses) {
    ResponsiveCompiledPresentation result;
    result.engine_id = "test-engine";
    result.audition_bus_id = "master-engine-audition";
    result.audition_dry_bus_order.assign(buses.begin(), buses.end());
    result.captured_to_source_scale = 67'108'864.0;
    result.master_volume_linear = 1.0;
    result.transfers.push_back(fixed_transfer());
    result.transfers.push_back(partitioned_transfer());
    for (std::size_t index = 0U; index < buses.size(); ++index) {
        result.routes.push_back({buses[index], "source.route." + std::to_string(index),
                                 index == 0U ? "smooth-39" : "smooth-45",
                                 digest("raw-ir-" + std::to_string(index)), 0.001, 1.0,
                                 index});
    }
    result.identity_sha256 = digest("presentation");
    return result;
}

[[nodiscard]] const PortableResponsivePackageMember *
member(const EncodedResponsivePackageChildrenV1 &encoded, const std::string_view path) {
    const auto found = std::ranges::find(encoded.members, path,
                                         &PortableResponsivePackageMember::path);
    return found == encoded.members.end() ? nullptr : &*found;
}

[[nodiscard]] std::string_view
text(const PortableResponsivePackageMember &member_value) {
    return {reinterpret_cast<const char *>(member_value.bytes.data()),
            member_value.bytes.size()};
}

[[nodiscard]] authoring::Quantity quantity(const double value, std::string unit) {
    return {value, std::move(unit), std::nullopt};
}

[[nodiscard]] authoring::ScenarioDocument
lifecycle_scenario(const LifecycleScenarioRole role,
                   const std::uint64_t preparation_blocks,
                   const std::uint64_t total_blocks) {
    const auto names = lifecycle_publication_names(role);
    authoring::ScenarioDocument result;
    result.id.value = "test-engine-lifecycle-" + std::string{names.capture_role} +
                      "-candidate-10khz-lifecycle-preview";
    result.engine.value = "test-engine";
    result.fuel.value = "gasoline";
    result.ambient = {quantity(101'325.0, "Pa"), quantity(298.15, "K"), 0.0};
    result.initial_thermal_state = {quantity(298.15, "K"), quantity(363.15, "K"),
                                    quantity(363.15, "K"), quantity(363.15, "K")};
    result.crankcase = {quantity(101'325.0, "Pa"), quantity(298.15, "K")};
    result.initial_state.engine_speed = quantity(0.0, "rpm");
    result.initial_state.crank_angle = quantity(0.0, "rad");
    result.initial_state.starter_enabled = role == LifecycleScenarioRole::starter ||
                                           role == LifecycleScenarioRole::startup;
    result.initial_state.fuel_enabled = role == LifecycleScenarioRole::startup;
    result.initial_state.ignition_enabled =
        role == LifecycleScenarioRole::shutdown ||
        role == LifecycleScenarioRole::shutdown_elevated;
    result.initial_state.limiter_enabled = true;
    const auto preparation_seconds = static_cast<double>(preparation_blocks) / 50.0;
    if (preparation_blocks == 0U) {
        result.preparation =
            authoring::FixedSettlingPreparation{quantity(0.0, "s"), quantity(0.0, "s")};
    } else {
        result.preparation =
            authoring::FixedHorizonPreparation{quantity(preparation_seconds, "s"), 1U};
    }
    authoring::FreeEngineMode mode;
    mode.throttle_01.interpolation =
        authoring::TrajectoryInterpolation::right_continuous_hold;
    mode.throttle_01.points.push_back({quantity(0.0, "s"), 0.0});
    if (role == LifecycleScenarioRole::shutdown ||
        role == LifecycleScenarioRole::shutdown_elevated) {
        mode.external_resisting_torque = authoring::QuantityTrajectory{
            authoring::QuantityDimension::torque,
            authoring::TrajectoryInterpolation::right_continuous_hold,
            {{quantity(0.0, "s"), quantity(0.0, "N*m")}}};
        result.initial_state.starter_enabled = false;
        result.initial_state.fuel_enabled = true;
        result.initial_state.engine_speed = quantity(
            role == LifecycleScenarioRole::shutdown ? 1'000.0 : 3'000.0, "rpm");
        result.events.push_back(
            {{"key-off"},
             quantity(preparation_seconds + 0.3, "s"),
             authoring::OperatingStatePatch{false, std::nullopt, std::nullopt,
                                            std::nullopt, std::nullopt}});
    } else if (role == LifecycleScenarioRole::startup) {
        result.events.push_back(
            {{"ignition-on"},
             quantity(0.2, "s"),
             authoring::OperatingStatePatch{true, std::nullopt, std::nullopt,
                                            std::nullopt, std::nullopt}});
        result.events.push_back(
            {{"starter-release"},
             quantity(0.4, "s"),
             authoring::OperatingStatePatch{std::nullopt, std::nullopt, false,
                                            std::nullopt, std::nullopt}});
    }
    result.mode = std::move(mode);
    result.rates = {{10'000U, 1U, "Hz"},
                    {10'000U, 1U, "Hz"},
                    {192'000U, 1U, "Hz"},
                    {192'000U, 1U, "Hz"},
                    {192'000U, 1U, "Hz"}};
    result.quality = {"listening", 3'840U, 64U, 1U};
    const auto total_seconds = static_cast<double>(total_blocks) / 50.0;
    result.total_duration = quantity(total_seconds, "s");
    result.audible_start = quantity(preparation_seconds, "s");
    result.audible_duration = quantity(total_seconds - preparation_seconds, "s");
    result.public_seed = 12'648'430U;
    result.output.buses = {authoring::AudioBusRef{"master-engine-raw"},
                           authoring::AudioBusRef{"master-engine-audition"}};
    return result;
}

[[nodiscard]] LifecycleCaptureEvidence lifecycle_capture(
    const LifecycleScenarioRole role, const std::uint64_t preparation_blocks,
    const std::uint64_t total_blocks, const contract::Sha256Digest &engine_sha256) {
    LifecycleCaptureEvidence result;
    result.role = role;
    const auto names = lifecycle_publication_names(role);
    result.canonical_source_id =
        "test-engine-lifecycle-" + std::string{names.capture_role} + "-candidate";
    result.engine_id = "test-engine";
    result.bus_id = "master.engine.audition";
    result.physics_rate_hz = kLifecyclePhysicsRateHz;
    result.delivery_rate_hz = kLifecycleDeliveryRateHz;
    result.preparation_block_count = preparation_blocks;
    result.total_block_count = total_blocks;
    result.audible_first_delivery_frame = preparation_blocks * kLifecycleFramesPerBlock;
    result.audible_frame_count =
        (total_blocks - preparation_blocks) * kLifecycleFramesPerBlock;
    result.final_physics_frame = total_blocks * 200U;
    result.final_delivery_frame = total_blocks * kLifecycleFramesPerBlock;
    result.scenario = lifecycle_scenario(role, preparation_blocks, total_blocks);
    result.scenario_id = result.scenario.id.value;
    result.scenario_spec_sha256 = digest(result.scenario_id + "-spec");
    result.engine_provenance = provenance("test-engine-provenance", engine_sha256);
    result.scenario_provenance =
        provenance("test-scenario-provenance-" + std::string{names.capture_role},
                   digest(result.scenario_id + "-provenance"));
    result.pcm.assign(result.audible_frame_count, 0.01F);
    const auto audible_blocks = total_blocks - preparation_blocks;
    result.points.reserve(audible_blocks);
    for (std::uint64_t index = 0U; index < audible_blocks; ++index) {
        const auto source_frame = (index + 1U) * kLifecycleFramesPerBlock - 1U;
        const auto physics_step = (preparation_blocks + index + 1U) * 200U;
        result.points.push_back(
            {source_frame, physics_step, static_cast<double>(physics_step) / 10'000.0,
             role == LifecycleScenarioRole::shutdown_elevated ? 3'000.0 : 900.0,
             role != LifecycleScenarioRole::starter,
             role != LifecycleScenarioRole::starter,
             role == LifecycleScenarioRole::starter, 10.0,
             LifecycleCapturePoint::PublishedAvailability::available});
    }
    for (std::uint64_t start = 0U; start + 24'000U <= result.audible_frame_count;
         start += 24'000U) {
        result.completed_cycles.push_back({start, start + 24'000U, 900.0});
    }
    return result;
}

[[nodiscard]] LifecycleCookedPackage
lifecycle_package(const contract::Sha256Digest &engine_sha256) {
    LifecycleCookedPackage result;
    result.id = "test-engine-lifecycle-preview";
    result.engine_id = "test-engine";
    result.bus_id = "master.engine.audition";
    result.captures.push_back(
        lifecycle_capture(LifecycleScenarioRole::starter, 0U, 30U, engine_sha256));
    result.captures.push_back(
        lifecycle_capture(LifecycleScenarioRole::startup, 0U, 50U, engine_sha256));
    result.captures.push_back(
        lifecycle_capture(LifecycleScenarioRole::shutdown, 10U, 60U, engine_sha256));
    result.captures.push_back(lifecycle_capture(
        LifecycleScenarioRole::shutdown_elevated, 5U, 55U, engine_sha256));

    result.starter = {500.0, 500.0, 1'000U, 100'000U, 10'000U, 3'840U, 3'840U};
    result.startup.checkpoints = {
        {"ignition-on", 20'000U, 500.0, "exact", "authored-operating-state-event"},
        {"first-combustion", 40'000U, 600.0, "advance-block",
         "first-positive-indicated-gas-torque-proxy"},
        {"starter-release", 80'000U, 800.0, "exact", "authored-operating-state-event"},
        {"running-floor", 120'000U, 900.0, "cycle-boundary",
         "post-release-cycle-correlated-to-captured-running-idle"}};
    result.startup.entry = {0U,    5'000U, 500.0,     1'000U,
                            500.0, 1.0,    "starter", "starter-loop"};
    result.startup.exit = {
        120'000U, 5'000U, 900.0,     120'000U,
        900.0,    1.0,    "running", "startup-post-release-cycle-reference"};
    result.shutdown.checkpoints = {
        {"settled-idle", 20'000U, 900.0, "cycle-window",
         "pre-key-off-window-correlated-to-captured-running-idle"},
        {"ignition-off", 60'000U, 800.0, "exact", "authored-operating-state-event"},
        {"engine-stopped", 110'000U, 0.0, "advance-block",
         "first-observed-absolute-engine-speed-at-or-below-1-rpm"}};
    result.shutdown.entry = {
        20'000U, 5'000U, 900.0,     20'000U,
        900.0,   1.0,    "running", "shutdown-pre-keyoff-cycle-reference"};
    result.shutdown.silence_frame = 120'000U;
    result.shutdown.quiet_tail_frames = 72'000U;
    result.shutdown.quiet_peak_threshold = 0.001;
    result.shutdown.quiet_rms_threshold = 0.0003;
    result.shutdown_elevated = result.shutdown;
    result.shutdown_elevated->checkpoints[0U].kind = "settled-running";
    result.shutdown_elevated->checkpoints[0U].method =
        "captured-high-rpm-pre-key-off-cycle";
    result.shutdown_elevated->entry.target_reference =
        "shutdown-high-rpm-pre-keyoff-cycle-reference";

    result.startup_admission_seed.running_floor_rpm = 900.0;
    result.startup_admission_seed.held_anchor_floor_rpm = 1'000.0;
    result.startup_admission_seed.release.physics_tick = 4'000U;
    result.startup_admission_seed.release.seconds = 0.4;
    result.startup_admission_seed.release.first_positive_combustion.source_frame =
        40'000U;
    result.startup_admission_seed.release.running_floor.source_frame = 60'000U;
    result.startup_admission_seed.release.release_cycle = {70'000U, 80'000U, 850.0};
    result.startup_admission_seed.lanes = {{"coast", 0.0, 0.5761944116355173},
                                           {"mid", 0.2, 0.7179832867135557},
                                           {"power", 1.0, 0.7940403012442127}};
    return result;
}

} // namespace

int main() {
    const auto selected = profile();
    const std::array<std::string, 2U> buses{"route.a", "route.b"};
    const auto held = held_grid(selected, buses);
    const auto directional = directional_model(selected, buses);
    const auto compiled_presentation = presentation(buses);
    ResponsivePackageChildrenViewV1 view{
        &selected,
        &held,
        &directional,
        &compiled_presentation,
        {"test-engine", digest("compiled-engine"), "crankwave-renderer-build",
         digest("renderer")},
        false,
        {},
    };
    auto encoded_result = encode_responsive_package_children_v1(view);
    expect(std::holds_alternative<EncodedResponsivePackageChildrenV1>(encoded_result),
           "valid cooked products encode into package children");
    if (!std::holds_alternative<EncodedResponsivePackageChildrenV1>(encoded_result)) {
        return 1;
    }
    const auto &encoded = std::get<EncodedResponsivePackageChildrenV1>(encoded_result);
    expect(encoded.runtime.dry_bus_ids ==
                   std::vector<std::string>(buses.begin(), buses.end()) &&
               encoded.runtime.held_package_path == "held/package.json" &&
               encoded.runtime.directional_package_path == "directional/runtime.json",
           "encoder returns exact root runtime topology");
    const auto *held_manifest = member(encoded, "held/package.json");
    expect(held_manifest != nullptr &&
               contract::sha256(held_manifest->bytes) == encoded.held_manifest_sha256,
           "held manifest SHA binds its exact bytes");
    if (held_manifest == nullptr) {
        return 1;
    }
    auto parsed = authoring::parse_json(text(*held_manifest),
                                        {16U * 1024U * 1024U, 128U, 262'144U});
    expect(std::holds_alternative<authoring::JsonDocument>(parsed),
           "held manifest is valid JSON");
    if (const auto *document = std::get_if<authoring::JsonDocument>(&parsed)) {
        const auto routes = document->root().find("presentation").find("routes");
        const auto fixed = routes.at(0U).find("transfer");
        const auto partitioned = routes.at(1U).find("transfer");
        expect(fixed.size() == 6U && !fixed.find("kind").valid() &&
                   fixed.find("fft_size").number() == 65'536.0 &&
                   fixed.find("coefficient_count").number() == 30'071.0 &&
                   fixed.find("spectrum_byte_count").number() == 1'048'576.0,
               "fixed transfer preserves the exact legacy six-field descriptor");
        expect(partitioned.size() == 9U &&
                   partitioned.find("kind").string() ==
                       kResponsivePartitionedTransferKind &&
                   partitioned.find("fft_size").number() == 8'192.0 &&
                   partitioned.find("coefficient_count").number() == 384'292.0 &&
                   partitioned.find("partition_frame_count").number() == 3'840.0 &&
                   partitioned.find("partition_count").number() == 101.0 &&
                   partitioned.find("spectrum_byte_count").number() == 13'238'272.0,
               "partitioned transfer freezes complete additive shape and byte count");
        const auto partition_path = partitioned.find("spectrum_path").string();
        expect(partition_path.has_value(),
               "partitioned transfer declares a payload path");
        if (partition_path.has_value()) {
            const auto *payload =
                member(encoded, "held/" + std::string{*partition_path});
            expect(payload != nullptr && payload->bytes.size() == 13'238'272U &&
                       contract::sha256(payload->bytes) ==
                           compiled_presentation.transfers[1U].spectrum_sha256,
                   "partitioned path publishes every compiled spectrum byte");
        }
    }

    auto truncated_presentation = compiled_presentation;
    truncated_presentation.transfers[1U].spectrum_bytes.pop_back();
    auto truncated_view = view;
    truncated_view.presentation = &truncated_presentation;
    expect(std::holds_alternative<NativeResponsivePackageError>(
               encode_responsive_package_children_v1(truncated_view)),
           "truncated partition spectra fail closed");

    auto repeated_result = encode_responsive_package_children_v1(view);
    expect(
        std::holds_alternative<EncodedResponsivePackageChildrenV1>(repeated_result) &&
            std::get<EncodedResponsivePackageChildrenV1>(repeated_result).members ==
                encoded.members &&
            std::get<EncodedResponsivePackageChildrenV1>(repeated_result)
                    .held_manifest_sha256 == encoded.held_manifest_sha256,
        "repeated child encoding produces identical paths, bytes and hashes");

    const auto cooked_lifecycle =
        lifecycle_package(view.provenance.compiled_engine_sha256);
    auto lifecycle_result = encode_responsive_lifecycle_child_v1(
        cooked_lifecycle, view.provenance, encoded.held_manifest_sha256);
    expect(std::holds_alternative<ResponsiveOptionalChildPackageV1>(lifecycle_result),
           "typed lifecycle aggregate encodes into one closed child tree");
    if (const auto *lifecycle_child =
            std::get_if<ResponsiveOptionalChildPackageV1>(&lifecycle_result)) {
        export_child_if_requested(*lifecycle_child);
        expect(lifecycle_child->members.size() == 14U &&
                   lifecycle_child->runtime_path == "lifecycle/runtime.json",
               "lifecycle child publishes four exact scenario/evidence/audio sets");
        const auto runtime =
            std::ranges::find(lifecycle_child->members, "lifecycle/runtime.json",
                              &PortableResponsivePackageMember::path);
        expect(runtime != lifecycle_child->members.end(),
               "lifecycle runtime manifest is present");
        if (runtime != lifecycle_child->members.end()) {
            auto runtime_json = authoring::parse_json(text(*runtime));
            expect(std::holds_alternative<authoring::JsonDocument>(runtime_json),
                   "lifecycle runtime is strict JSON");
            if (const auto *document =
                    std::get_if<authoring::JsonDocument>(&runtime_json)) {
                const auto root = document->root();
                const auto scenarios = root.find("provenance").find("scenarios");
                expect(
                    root.size() == 10U && root.find("shutdown_elevated").valid() &&
                        scenarios.size() == 4U &&
                        scenarios.at(3U).find("role").string() == "shutdown-elevated" &&
                        scenarios.at(3U).find("path").string() ==
                            "source/scenarios/shutdown-high-rpm-10khz.json" &&
                        root.find("startup_admission")
                                .find("coast_stability")
                                .find("lane_id")
                                .string() == "coast" &&
                        root.find("startup_admission")
                                .find("evidence")
                                .find("corrected_held_manifest_sha256")
                                .string() == digest_hex(encoded.held_manifest_sha256),
                    "lifecycle runtime preserves elevated naming and nested "
                    "admission binding");
            }
        }
        const auto admission = std::ranges::find(
            lifecycle_child->members, "lifecycle/evidence/startup-admission.json",
            &PortableResponsivePackageMember::path);
        if (admission != lifecycle_child->members.end()) {
            auto admission_json = authoring::parse_json(text(*admission));
            const auto *document =
                std::get_if<authoring::JsonDocument>(&admission_json);
            expect(document != nullptr && document->root()
                                                  .find("release")
                                                  .find("release_cycle")
                                                  .find("start_frame")
                                                  .number() == 70'000.0,
                   "admission evidence preserves nested dynamic release cycle");
        } else {
            expect(false, "lifecycle admission evidence is present");
        }
        const auto elevated_scenario =
            std::ranges::find(lifecycle_child->members,
                              "lifecycle/source/scenarios/shutdown-high-rpm-10khz.json",
                              &PortableResponsivePackageMember::path);
        expect(elevated_scenario != lifecycle_child->members.end() &&
                   std::holds_alternative<authoring::ScenarioDocument>(
                       authoring::parse_scenario_document(text(*elevated_scenario))),
               "published lifecycle source scenario passes strict authoring parse");

        auto attached =
            attach_responsive_optional_children_v1(encoded, {*lifecycle_child});
        expect(std::holds_alternative<EncodedResponsivePackageChildrenV1>(attached) &&
                   std::get<EncodedResponsivePackageChildrenV1>(attached)
                           .runtime.lifecycle_package_path == "lifecycle/runtime.json",
               "typed lifecycle child attaches through the root runtime contract");
        auto forged_lifecycle = *lifecycle_child;
        const auto forged_audio =
            std::ranges::find(forged_lifecycle.members,
                              "lifecycle/audio/starter.master-engine-audition.f32le",
                              &PortableResponsivePackageMember::path);
        if (forged_audio != forged_lifecycle.members.end()) {
            forged_audio->bytes.front() ^= std::byte{1U};
        }
        expect(std::holds_alternative<NativeResponsivePackageError>(
                   attach_responsive_optional_children_v1(
                       encoded, {std::move(forged_lifecycle)})),
               "raw lifecycle attachment cannot bypass referenced artifact hashes");
    }

    auto bad_lifecycle = cooked_lifecycle;
    bad_lifecycle.captures[0U].pcm[0U] = std::numeric_limits<float>::quiet_NaN();
    expect(std::holds_alternative<NativeResponsivePackageError>(
               encode_responsive_lifecycle_child_v1(bad_lifecycle, view.provenance,
                                                    encoded.held_manifest_sha256)),
           "non-finite lifecycle PCM fails closed");
    auto repeated_lifecycle = encode_responsive_lifecycle_child_v1(
        cooked_lifecycle, view.provenance, encoded.held_manifest_sha256);
    expect(
        std::holds_alternative<ResponsiveOptionalChildPackageV1>(lifecycle_result) &&
            std::holds_alternative<ResponsiveOptionalChildPackageV1>(
                repeated_lifecycle) &&
            std::get<ResponsiveOptionalChildPackageV1>(lifecycle_result).members ==
                std::get<ResponsiveOptionalChildPackageV1>(repeated_lifecycle).members,
        "repeated lifecycle encoding is byte deterministic");

    const auto repository = std::filesystem::path{__FILE__}.parent_path().parent_path();
    const auto starter_root =
        repository / "reference/fixtures/responsive-audio/shared-recorded-starter";
    const auto starter_runtime = read_bytes(starter_root / "runtime.json");
    const auto starter_audio =
        read_bytes(starter_root / "audio/recorded-starter.cropped.192000hz.mono.f32le");
    auto starter_result = encode_responsive_shared_recorded_starter_v1(
        {starter_runtime, starter_audio}, view.provenance);
    expect(std::holds_alternative<EncodedResponsiveSharedRecordedStarterV1>(
               starter_result),
           "installed CC0 starter passes the typed byte-only boundary");
    if (const auto *starter =
            std::get_if<EncodedResponsiveSharedRecordedStarterV1>(&starter_result)) {
        expect(starter->identity.entry_count == 2U &&
                   digest_hex(starter->identity.aggregate_sha256) ==
                       "9bc1ed5e18177e9000c8892b68aef01133b9e0110ccfc6d767d63f6cdac5129"
                       "9" &&
                   starter->child.members.size() == 2U,
               "starter identity exactly matches fingerprintRegularTree");
        auto attached =
            attach_responsive_optional_children_v1(encoded, {starter->child});
        expect(std::holds_alternative<EncodedResponsivePackageChildrenV1>(attached) &&
                   std::get<EncodedResponsivePackageChildrenV1>(attached)
                           .runtime.shared_recorded_starter_package_path ==
                       "shared-recorded-starter/runtime.json",
               "typed starter attaches with no arbitrary package-member escape");
        auto forged_child = starter->child;
        forged_child.members.front().bytes.front() ^= std::byte{1U};
        expect(std::holds_alternative<NativeResponsivePackageError>(
                   attach_responsive_optional_children_v1(encoded,
                                                          {std::move(forged_child)})),
               "raw shared-starter attachment cannot bypass exact package identity");
    }
    auto tampered_audio = starter_audio;
    if (!tampered_audio.empty()) {
        tampered_audio.front() ^= std::byte{1U};
    }
    expect(std::holds_alternative<NativeResponsivePackageError>(
               encode_responsive_shared_recorded_starter_v1(
                   {starter_runtime, tampered_audio}, view.provenance)),
           "starter PCM tampering fails its manifest binding");

    return failures == 0 ? 0 : 1;
}
