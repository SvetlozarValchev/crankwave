#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/responsive/finite_capture.hpp"
#include "../src/session/projected_engine_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

struct CompiledEngineFixture {
    authoring::EnginePackageDocument document;
    compile::CompiledEngine engine;
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
    return {std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string
diagnostics(const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

template <class Value>
[[nodiscard]] Value
require_authoring(std::variant<Value, authoring::DiagnosticReport> result,
                  const std::string_view context) {
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " +
                                 diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> assets;
    assets.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    for (const auto &asset : document.presentation.assets) {
        assets.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        assets.push_back(
            {compile::AssetKind::accessory_configuration, asset.id.value,
             read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return assets;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> views;
    views.reserve(assets.size());
    for (const auto &asset : assets) {
        views.push_back({asset.kind, asset.id, asset.bytes});
    }
    return views;
}

[[nodiscard]] CompiledEngineFixture
compile_engine_fixture(const std::filesystem::path &repository_root,
                       const bool partitioned_transfer = false) {
    const auto path =
        repository_root / "data/engines/bmw-m52tub28-cleanroom/engine.json";
    auto document = require_authoring(
        authoring::parse_engine_document(read_text(path)), "engine parse failed");
    auto assets = load_assets(document, path);
    if (partitioned_transfer) {
        const auto long_ir_path =
            repository_root /
            "assets/builtin/ir-library/payloads/"
            "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819.wav";
        expect(document.presentation.assets.size() == 1U,
               "partitioned fixture asset inventory changed");
        auto &definition = document.presentation.assets.front();
        definition.id.value = "smooth-45";
        definition.uri = "unused-content-addressed-smooth-45-locator";
        definition.sha256 =
            "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819";
        for (auto &route : document.presentation.routes) {
            expect(route.impulse_response.has_value(),
                   "partitioned fixture route lost its transfer");
            route.impulse_response->value = "smooth-45";
        }
        for (auto &asset : assets) {
            if (asset.kind == compile::AssetKind::audio) {
                asset.id = "smooth-45";
                asset.bytes = read_bytes(long_ir_path);
            }
        }
    }
    const auto views = asset_views(assets);
    auto engine = require_authoring(compile::compile_engine(document, views),
                                    "engine compile failed");
    return {std::move(document), std::move(engine)};
}

template <class Mutator>
[[nodiscard]] compile::CompiledScenario
compile_scenario_fixture(const std::filesystem::path &repository_root,
                         const CompiledEngineFixture &fixture,
                         const std::filesystem::path &relative_path,
                         Mutator mutator) {
    auto document = require_authoring(
        authoring::parse_scenario_document(
            read_text(repository_root / relative_path)),
        "scenario parse failed");
    mutator(document);
    const auto references =
        authoring::validate_scenario_references(document, fixture.document);
    if (!references.ok()) {
        throw std::runtime_error{"mutated scenario reference validation failed"};
    }
    return require_authoring(
        compile::compile_scenario(fixture.engine, document),
        "scenario compile failed");
}

[[nodiscard]] responsive::FiniteResponsiveCapture
require_capture(responsive::FiniteResponsiveCaptureResult result,
                const std::string_view context) {
    if (const auto *failure =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&result)) {
        throw std::runtime_error{std::string{context} + ": " +
                                 failure->detail_code + ": " +
                                 failure->message};
    }
    return std::get<responsive::FiniteResponsiveCapture>(std::move(result));
}

void shorten_held(authoring::ScenarioDocument &document) {
    document.total_duration.value = document.audible_start.value + 0.2;
    document.audible_duration.value = 0.2;
}

void shorten_directional(authoring::ScenarioDocument &document) {
    auto *mode = std::get_if<authoring::HeldDynoMode>(&document.mode);
    expect(mode != nullptr && mode->target_engine_speed.points.size() == 5U &&
               mode->throttle_01.points.size() == 3U,
           "directional fixture shape changed");
    mode->target_engine_speed.points[2].time.value = 3.2;
    mode->target_engine_speed.points[3].time.value = 3.3;
    mode->target_engine_speed.points[4].time.value = 3.6;
    mode->throttle_01.points[1].time.value = 3.2;
    mode->throttle_01.points[2].time.value = 3.3;
    document.total_duration.value = 3.6;
    document.audible_duration.value = 0.6;
}

void shorten_lifecycle(authoring::ScenarioDocument &document) {
    document.total_duration.value = 1.6;
    document.audible_duration.value = 1.6;
}

void shorten_stopped_lifecycle(authoring::ScenarioDocument &document) {
    document.total_duration.value = 0.1;
    document.audible_duration.value = 0.1;
}

void shorten_projection_parity(authoring::ScenarioDocument &document) {
    auto *mode = std::get_if<authoring::HeldSpeedMode>(&document.mode);
    auto *preparation =
        std::get_if<authoring::FixedHorizonPreparation>(&document.preparation);
    expect(mode != nullptr && preparation != nullptr,
           "projection parity fixture is no longer fixed-horizon held speed");
    document.initial_state.engine_speed.value = 1'500.0;
    mode->target_engine_speed.value = 1'500.0;
    preparation->preparation_duration.value = 0.18;
    preparation->trailing_complete_cycle_count = 1U;
    document.total_duration.value = 0.22;
    document.audible_start.value = 0.18;
    document.audible_duration.value = 0.04;
}

[[nodiscard]] bool exact_double(const double left, const double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool exact_quantity(const contract::QuantityValue &left,
                                  const contract::QuantityValue &right) {
    return exact_double(left.value, right.value) &&
           left.availability == right.availability &&
           left.completeness == right.completeness &&
           left.unavailable_reason == right.unavailable_reason;
}

[[nodiscard]] bool exact_torque(const contract::TorqueValueNm &left,
                                const contract::TorqueValueNm &right) {
    return exact_double(left.value_nm, right.value_nm) &&
           left.availability == right.availability &&
           left.completeness == right.completeness &&
           left.unavailable_reason == right.unavailable_reason &&
           left.included_terms == right.included_terms &&
           left.omitted_terms == right.omitted_terms;
}

[[nodiscard]] bool exact_torque_telemetry(
    const contract::TorqueTelemetry &left,
    const contract::TorqueTelemetry &right) {
    return exact_torque(left.instantaneous_indicated_gas,
                        right.instantaneous_indicated_gas) &&
           exact_torque(left.pumping_partition, right.pumping_partition) &&
           exact_torque(left.friction_pump_and_accessory,
                        right.friction_pump_and_accessory) &&
           exact_torque(left.starter, right.starter) &&
           exact_torque(left.instantaneous_net_shaft,
                        right.instantaneous_net_shaft) &&
           exact_torque(left.cycle_mean_net_shaft,
                        right.cycle_mean_net_shaft) &&
           exact_torque(left.actuator, right.actuator) &&
           exact_torque(left.dyno_reaction, right.dyno_reaction) &&
           exact_quantity(left.cycle_work_j, right.cycle_work_j) &&
           exact_quantity(left.net_bmep_pa, right.net_bmep_pa) &&
           exact_quantity(left.instantaneous_power_w,
                          right.instantaneous_power_w) &&
           exact_quantity(left.cycle_mean_power_w, right.cycle_mean_power_w);
}

[[nodiscard]] bool exact_engine_sample(
    const contract::EngineCaptureSample &left,
    const contract::EngineCaptureSample &right) {
    return left.step_end_index == right.step_end_index &&
           left.validity == right.validity &&
           exact_double(left.theta_rad, right.theta_rad) &&
           exact_double(left.theta_cycle_rad, right.theta_cycle_rad) &&
           exact_double(left.angular_speed_rad_s, right.angular_speed_rad_s) &&
           exact_double(left.angular_acceleration_rad_s2,
                        right.angular_acceleration_rad_s2) &&
           exact_double(left.engine_speed_rpm, right.engine_speed_rpm) &&
           exact_double(left.requested_throttle_01,
                        right.requested_throttle_01) &&
           exact_double(left.resolved_engine_throttle_01,
                        right.resolved_engine_throttle_01) &&
           exact_double(left.intake_plate_position_01,
                        right.intake_plate_position_01) &&
           exact_double(left.main_flow_multiplier_01,
                        right.main_flow_multiplier_01) &&
           left.ignition_enabled == right.ignition_enabled &&
           left.fuel_enabled == right.fuel_enabled &&
           left.starter_enabled == right.starter_enabled &&
           left.dyno_enabled == right.dyno_enabled &&
           left.limiter_enabled == right.limiter_enabled &&
           left.limiter_cut_active == right.limiter_cut_active &&
           exact_double(left.requested_external_resisting_torque_nm,
                        right.requested_external_resisting_torque_nm) &&
           exact_torque_telemetry(left.torque, right.torque);
}

[[nodiscard]] bool exact_held_dyno(
    const std::optional<EngineHeldDynoTelemetry> &left,
    const std::optional<EngineHeldDynoTelemetry> &right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (exact_double(left->target_engine_speed_rpm,
                         right->target_engine_speed_rpm) &&
            exact_double(left->maximum_absorbing_torque_nm,
                         right->maximum_absorbing_torque_nm) &&
            exact_double(left->maximum_driving_torque_nm,
                         right->maximum_driving_torque_nm) &&
            exact_double(left->required_actuator_torque_nm,
                         right->required_actuator_torque_nm) &&
            exact_double(left->applied_actuator_torque_nm,
                         right->applied_actuator_torque_nm) &&
            left->disposition == right->disposition);
}

[[nodiscard]] bool exact_free_vehicle(
    const std::optional<EngineFreeVehicleTelemetry> &left,
    const std::optional<EngineFreeVehicleTelemetry> &right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    const bool slip_matches =
        left->final_clutch_slip_rad_s.has_value() ==
            right->final_clutch_slip_rad_s.has_value() &&
        (!left->final_clutch_slip_rad_s.has_value() ||
         exact_double(*left->final_clutch_slip_rad_s,
                      *right->final_clutch_slip_rad_s));
    return exact_double(left->vehicle_speed_m_s, right->vehicle_speed_m_s) &&
           exact_double(left->vehicle_distance_m, right->vehicle_distance_m) &&
           left->selected_forward_gear_ordinal ==
               right->selected_forward_gear_ordinal &&
           exact_double(left->clutch_engagement_01,
                        right->clutch_engagement_01) &&
           exact_double(left->service_brake_application_01,
                        right->service_brake_application_01) &&
           left->clutch_disposition == right->clutch_disposition &&
           exact_double(left->clutch_torque_capacity_nm,
                        right->clutch_torque_capacity_nm) &&
           exact_double(left->applied_average_clutch_torque_on_engine_nm,
                        right->applied_average_clutch_torque_on_engine_nm) &&
           slip_matches &&
           left->road_load_disposition == right->road_load_disposition &&
           exact_double(left->requested_road_load_force_n,
                        right->requested_road_load_force_n) &&
           exact_double(left->applied_average_road_load_force_n,
                        right->applied_average_road_load_force_n);
}

[[nodiscard]] bool exact_telemetry(const EngineTelemetryFrame &left,
                                   const EngineTelemetryFrame &right) {
    return left.physics_step_end == right.physics_step_end &&
           exact_double(left.mean_intake_manifold_pressure_pa_abs,
                        right.mean_intake_manifold_pressure_pa_abs) &&
           exact_engine_sample(left.engine, right.engine) &&
           exact_held_dyno(left.held_dyno, right.held_dyno) &&
           exact_free_vehicle(left.free_vehicle, right.free_vehicle);
}

[[nodiscard]] bool exact_endpoint(
    const responsive::ResponsiveCaptureEndpoint &left,
    const responsive::ResponsiveCaptureEndpoint &right) {
    return left.delivery_frame == right.delivery_frame &&
           exact_double(left.engine_speed_rpm, right.engine_speed_rpm) &&
           exact_double(left.mean_intake_manifold_pressure_pa_abs,
                        right.mean_intake_manifold_pressure_pa_abs) &&
           exact_double(left.requested_throttle_01,
                        right.requested_throttle_01) &&
           exact_double(left.resolved_engine_throttle_01,
                        right.resolved_engine_throttle_01) &&
           exact_double(left.unwrapped_crank_revolutions,
                        right.unwrapped_crank_revolutions) &&
           left.state_flags == right.state_flags;
}

void expect_exact_capture_parity(
    const responsive::FiniteResponsiveCapture &complete,
    const responsive::FiniteResponsiveCapture &projected) {
    expect(complete.engine_id == projected.engine_id &&
               complete.scenario_id == projected.scenario_id &&
               complete.capacities == projected.capacities &&
               complete.physics_rate == projected.physics_rate &&
               complete.delivery_rate == projected.delivery_rate &&
               complete.physics_frames_per_block ==
                   projected.physics_frames_per_block &&
               complete.delivery_frames_per_block ==
                   projected.delivery_frames_per_block &&
               complete.total_block_count == projected.total_block_count &&
               complete.preparation_block_count ==
                   projected.preparation_block_count &&
               complete.total_physics_frame_count ==
                   projected.total_physics_frame_count &&
               complete.total_delivery_frame_count ==
                   projected.total_delivery_frame_count &&
               complete.audible_first_delivery_frame ==
                   projected.audible_first_delivery_frame &&
               complete.audible_delivery_frame_count ==
                   projected.audible_delivery_frame_count &&
               complete.live_control_capabilities ==
                   projected.live_control_capabilities &&
               complete.motion_mode == projected.motion_mode &&
               complete.forward_gears == projected.forward_gears &&
               complete.selected_bus_ids == projected.selected_bus_ids &&
               complete.engine_provenance == projected.engine_provenance &&
               complete.scenario_provenance == projected.scenario_provenance,
           "dry projection changed capture identities, clocks, or horizons");
    expect(complete.buses.size() == projected.buses.size(),
           "dry projection changed the selected bus count");
    for (std::size_t index = 0U; index < complete.buses.size(); ++index) {
        const auto &left = complete.buses[index];
        const auto &right = projected.buses[index];
        expect(left.descriptor == right.descriptor &&
                   std::ranges::equal(
                       std::as_bytes(std::span{left.audible_interleaved_samples}),
                       std::as_bytes(std::span{right.audible_interleaved_samples})),
               "dry projection changed selected Float32 PCM bytes");
    }
    expect(complete.blocks.size() == projected.blocks.size(),
           "dry projection changed the retained block count");
    for (std::size_t index = 0U; index < complete.blocks.size(); ++index) {
        const auto &left = complete.blocks[index];
        const auto &right = projected.blocks[index];
        expect(left.block_ordinal == right.block_ordinal &&
                   left.phase == right.phase &&
                   left.first_physics_frame == right.first_physics_frame &&
                   left.physics_frame_count == right.physics_frame_count &&
                   left.first_delivery_frame == right.first_delivery_frame &&
                   left.delivery_frame_count == right.delivery_frame_count &&
                   exact_endpoint(left.endpoint, right.endpoint) &&
                   exact_telemetry(left.telemetry, right.telemetry) &&
                   left.completed_cycles == right.completed_cycles &&
                   left.event_counters == right.event_counters,
               "dry projection changed endpoint, telemetry, cycle, event, or clock "
               "evidence");
    }
    expect(complete.completion.physics_frame_count ==
                   projected.completion.physics_frame_count &&
               complete.completion.delivery_frame_count ==
                   projected.completion.delivery_frame_count &&
               complete.completion.block_count ==
                   projected.completion.block_count &&
               complete.completion.live_controls_accepted ==
                   projected.completion.live_controls_accepted &&
               complete.completion.held_speed_operating_point ==
                   projected.completion.held_speed_operating_point &&
               complete.completion.inertial_dyno ==
                   projected.completion.inertial_dyno,
           "dry projection changed terminal completion evidence");
}

void verify_exact_block_and_cycle_accounting(
    const responsive::FiniteResponsiveCapture &capture) {
    expect(capture.blocks.size() == capture.total_block_count,
           "capture lost native blocks");
    std::uint64_t cycle_ordinal = 0U;
    for (std::size_t index = 0U; index < capture.blocks.size(); ++index) {
        const auto &block = capture.blocks[index];
        expect(block.block_ordinal == index,
               "capture reordered native blocks");
        expect(block.endpoint.delivery_frame ==
                   block.first_delivery_frame + block.delivery_frame_count,
               "reduced endpoint lost the exact delivery horizon");
        expect(block.telemetry.physics_step_end ==
                   block.first_physics_frame + block.physics_frame_count,
               "full endpoint lost the exact physics horizon");
        for (const auto &cycle : block.completed_cycles) {
            expect(cycle.completed_cycle_ordinal == cycle_ordinal,
                   "cycle evidence was duplicated, dropped, or reordered");
            ++cycle_ordinal;
        }
    }
    expect(!responsive::validate_finite_responsive_capture(capture).has_value(),
           "published capture failed its standalone validator");
}

void test_dry_projection_parity(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture,
    const std::string_view transfer_shape,
    const bool test_rejection) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "held-idle-region-700rpm.json",
        shorten_projection_parity);
    constexpr std::array<std::string_view, 2U> buses{
        "exhaust.rear.dry", "exhaust.front.dry"};

    const auto complete = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "complete presentation parity capture failed");
    const auto projected = require_capture(
        responsive::capture_finite_responsive_dry_routes(scenario, buses),
        "dry-projected parity capture failed");
    expect_exact_capture_parity(complete, projected);
    expect(std::ranges::all_of(projected.buses, [](const auto &bus) {
               return bus.descriptor.kind ==
                      EngineAudioBusKind::source_route_dry;
           }),
           "projected capture exposed a non-dry bus");

    auto projected_session =
        session_detail::create_dry_projected_engine_session(scenario, buses);
    auto *session = std::get_if<EngineSession>(&projected_session);
    expect(session != nullptr,
           "internal dry-projected session creation failed");
    const auto descriptor = session->descriptor();
    expect(descriptor.audio_buses.size() == buses.size() &&
               descriptor.audio_buses[0].id == buses[0] &&
               descriptor.audio_buses[1].id == buses[1] &&
               std::ranges::all_of(descriptor.audio_buses, [](const auto &bus) {
                   return bus.kind == EngineAudioBusKind::source_route_dry;
               }),
           "projected session exposed omitted transfer or master buses");

    expect(!complete.buses.front().audible_interleaved_samples.empty(),
           "projection parity fixture produced no audible PCM");
    if (test_rejection) {
        constexpr std::array<std::string_view, 2U> invalid{
            "exhaust.front.dry", "master.engine.raw"};
        const auto rejected =
            responsive::capture_finite_responsive_dry_routes(scenario, invalid);
        const auto *failure =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&rejected);
        expect(failure != nullptr &&
                   failure->code ==
                       responsive::FiniteResponsiveCaptureErrorCode::invalid_request &&
                   failure->detail_code ==
                       "session-dry-projection-requires-source-route-dry-buses",
               "dry projection admitted a non-dry bus selection");
    }

    static_cast<void>(transfer_shape);
}

void test_held_multi_bus_capture(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "held-idle-region-700rpm.json",
        shorten_held);
    constexpr std::array<std::string_view, 2> buses{
        "master.engine.audition", "master.engine.raw"};
    const auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "held multi-bus capture failed");

    expect(capture.motion_mode == EngineMotionMode::held_speed,
           "held capture lost its motion mode");
    expect(capture.selected_bus_ids.size() == 2U &&
               capture.selected_bus_ids[0] == buses[0] &&
               capture.selected_bus_ids[1] == buses[1] &&
               capture.buses[0].descriptor.id == buses[0] &&
               capture.buses[1].descriptor.id == buses[1],
           "capture did not preserve requested bus order");
    expect(capture.engine_provenance == fixture.engine.provenance() &&
               capture.scenario_provenance == scenario.provenance(),
           "capture did not preserve compiler provenance");
    expect(capture.preparation_block_count > 0U &&
               capture.blocks.front().phase ==
                   EngineSessionBlockPhase::preparation &&
               capture.blocks.back().phase == EngineSessionBlockPhase::audible,
           "capture did not retain preparation and audible evidence");
    verify_exact_block_and_cycle_accounting(capture);
}

void test_directional_capture(const std::filesystem::path &repository_root,
                              const CompiledEngineFixture &fixture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "canonical-loaded-rise-part-load-coast-1500-4500rpm.json",
        shorten_directional);
    constexpr std::array<std::string_view, 1> buses{"master.engine.raw"};
    const auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "directional capture failed");

    expect(capture.motion_mode == EngineMotionMode::held_dyno,
           "directional capture lost held-dyno mode");
    bool saw_rise = false;
    bool saw_fall = false;
    bool every_sidecar_present = true;
    std::optional<double> previous;
    for (const auto &block : capture.blocks) {
        if (block.phase != EngineSessionBlockPhase::audible) {
            continue;
        }
        every_sidecar_present &= block.telemetry.held_dyno.has_value();
        if (previous.has_value()) {
            saw_rise |= block.endpoint.engine_speed_rpm > *previous + 1.0;
            saw_fall |= block.endpoint.engine_speed_rpm < *previous - 1.0;
        }
        previous = block.endpoint.engine_speed_rpm;
    }
    expect(every_sidecar_present,
           "directional endpoints lost held-dyno sidecar evidence");
    expect(saw_rise, "directional endpoints did not preserve an RPM rise");
    expect(saw_fall, "directional endpoints did not preserve an RPM fall");
    verify_exact_block_and_cycle_accounting(capture);
}

responsive::FiniteResponsiveCapture test_lifecycle_capture(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture) {
    const auto stopped_scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "interactive-lifecycle-0rpm.json",
        shorten_stopped_lifecycle);
    constexpr std::array<std::string_view, 1> buses{
        "master.engine.audition"};
    const auto stopped = require_capture(
        responsive::capture_finite_responsive_session(stopped_scenario, buses),
        "stopped lifecycle capture failed");
    expect(std::ranges::any_of(stopped.blocks, [](const auto &block) {
               return std::abs(block.endpoint.engine_speed_rpm) < 1.0e-9;
           }),
           "lifecycle endpoints did not retain a zero-RPM state");
    verify_exact_block_and_cycle_accounting(stopped);

    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "cold-start-crank-catch-0rpm.json",
        shorten_lifecycle);
    auto capture = require_capture(
        responsive::capture_finite_responsive_session(scenario, buses),
        "lifecycle capture failed");

    bool saw_ignition_off = false;
    bool saw_ignition_on = false;
    bool saw_starter_on = false;
    bool saw_starter_off_after_on = false;
    for (const auto &block : capture.blocks) {
        const auto flags = block.endpoint.state_flags;
        const bool ignition =
            (flags & engine_cycle_state_flag_mask(
                         EngineCycleStateFlag::ignition_enabled)) != 0U;
        const bool starter =
            (flags & engine_cycle_state_flag_mask(
                         EngineCycleStateFlag::starter_enabled)) != 0U;
        saw_ignition_off |= !ignition;
        saw_ignition_on |= ignition;
        saw_starter_on |= starter;
        saw_starter_off_after_on |= saw_starter_on && !starter;
    }
    expect(capture.preparation_block_count == 0U,
           "lifecycle capture invented a preparation horizon");
    expect(saw_ignition_off && saw_ignition_on,
           "lifecycle endpoints lost the ignition transition");
    expect(saw_starter_on && saw_starter_off_after_on,
           "lifecycle endpoints lost the starter transition");
    verify_exact_block_and_cycle_accounting(capture);
    return capture;
}

void test_cancellation_and_fail_closed_validation(
    const std::filesystem::path &repository_root,
    const CompiledEngineFixture &fixture,
    const responsive::FiniteResponsiveCapture &valid_capture) {
    const auto scenario = compile_scenario_fixture(
        repository_root, fixture,
        "data/engines/bmw-m52tub28-cleanroom/scenarios/"
        "cold-start-crank-catch-0rpm.json",
        shorten_lifecycle);
    constexpr std::array<std::string_view, 1> buses{
        "master.engine.audition"};
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = responsive::capture_finite_responsive_session(
        scenario, buses, stop.get_token());
    const auto *cancel_error =
        std::get_if<responsive::FiniteResponsiveCaptureError>(&cancelled);
    expect(cancel_error != nullptr &&
               cancel_error->code ==
                   responsive::FiniteResponsiveCaptureErrorCode::cancelled,
           "pre-cancelled capture did not terminate as cancelled");

    auto malformed_pcm = valid_capture;
    malformed_pcm.buses.front().audible_interleaved_samples.front() =
        std::numeric_limits<float>::quiet_NaN();
    expect(responsive::validate_finite_responsive_capture(malformed_pcm)
               .has_value(),
           "standalone validator accepted non-finite PCM");

    auto malformed_endpoint = valid_capture;
    malformed_endpoint.blocks.front().endpoint.engine_speed_rpm =
        std::numeric_limits<double>::infinity();
    expect(responsive::validate_finite_responsive_capture(malformed_endpoint)
               .has_value(),
           "standalone validator accepted a non-finite reduced endpoint");

    auto malformed_range = valid_capture;
    ++malformed_range.blocks.front().first_delivery_frame;
    expect(responsive::validate_finite_responsive_capture(malformed_range)
               .has_value(),
           "standalone validator accepted a discontinuous frame range");

    auto malformed_events = valid_capture;
    ++malformed_events.blocks.front()
          .event_counters.total_event_record_count;
    expect(responsive::validate_finite_responsive_capture(malformed_events)
               .has_value(),
           "standalone validator accepted inconsistent event counters");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "usage: responsive_finite_capture_test <repo-root>");
        const std::filesystem::path repository_root = argv[1];
        const auto fixture = compile_engine_fixture(repository_root);
        const auto partitioned_fixture =
            compile_engine_fixture(repository_root, true);
        test_dry_projection_parity(repository_root, fixture, "fixed", true);
        test_dry_projection_parity(repository_root, partitioned_fixture,
                                   "partitioned", false);
        test_held_multi_bus_capture(repository_root, fixture);
        test_directional_capture(repository_root, fixture);
        const auto lifecycle =
            test_lifecycle_capture(repository_root, fixture);
        test_cancellation_and_fail_closed_validation(repository_root, fixture,
                                                     lifecycle);
        std::cout << "responsive finite capture tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "responsive finite capture tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
