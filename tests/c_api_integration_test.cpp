#include "engine_sim_offline/c_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace allocation_probe {
std::atomic<bool> reject{false};
}

void *operator new(const std::size_t size) {
    if (allocation_probe::reject.load(std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    if (void *const memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void *operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *const memory) noexcept {
    std::free(memory);
}

void operator delete[](void *const memory) noexcept {
    std::free(memory);
}

void operator delete(void *const memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void *const memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {

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

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    return {text.begin(), text.end()};
}

[[nodiscard]] eso_utf8_view_t view(const std::string &value) noexcept {
    return {value.data(), value.size()};
}

[[nodiscard]] eso_byte_view_t view(const std::vector<std::uint8_t> &value) noexcept {
    return {value.data(), value.size()};
}

template <class Value> [[nodiscard]] bool bytes_are_zero(const Value &value) noexcept {
    const auto *const first = reinterpret_cast<const unsigned char *>(&value);
    return std::all_of(first, first + sizeof(value),
                       [](const unsigned char byte) { return byte == 0U; });
}

[[nodiscard]] eso_session_telemetry_t process_to_first_audible_block(
    eso_context_t *context, const eso_session_handle_t session,
    const eso_session_descriptor_t &descriptor, const std::string_view label) {
    std::vector<eso_completed_cycle_evidence_t> cycles(
        descriptor.maximum_cycle_evidence_per_process_call);
    bool saw_completed_cycle = false;
    for (std::uint64_t block = 0U; block <= descriptor.preparation_block_count;
         ++block) {
        eso_session_telemetry_t telemetry{};
        eso_process_info_t process{};
        expect(eso_session_process(context, session, nullptr, 0U, &telemetry, 1U,
                                   cycles.data(), cycles.size(),
                                   &process) == ESO_STATUS_OK,
               std::string{label} + " failed before its first audible block");
        expect(process.kind == ESO_PROCESS_BLOCK && process.block_ordinal == block &&
                   process.telemetry_written == 1U &&
                   process.cycle_evidence_written <= cycles.size(),
               std::string{label} + " returned a discontinuous block");
        for (std::size_t index = 0; index < process.cycle_evidence_written; ++index) {
            const auto &cycle = cycles[index];
            const auto known_state_flags = ESO_ENGINE_CYCLE_STATE_IGNITION_ENABLED |
                                           ESO_ENGINE_CYCLE_STATE_FUEL_ENABLED |
                                           ESO_ENGINE_CYCLE_STATE_STARTER_ENABLED |
                                           ESO_ENGINE_CYCLE_STATE_DYNO_ENABLED |
                                           ESO_ENGINE_CYCLE_STATE_LIMITER_ENABLED |
                                           ESO_ENGINE_CYCLE_STATE_LIMITER_CUT_ACTIVE;
            expect(cycle.end_boundary.cycle_ordinal ==
                           cycle.start_boundary.cycle_ordinal + 1 &&
                       cycle.start_boundary.left_physics_frame <=
                           cycle.start_boundary.right_physics_frame &&
                       cycle.end_boundary.left_physics_frame <=
                           cycle.end_boundary.right_physics_frame &&
                       cycle.start_boundary.delivery_frame <
                           cycle.end_boundary.delivery_frame &&
                       cycle.duration_s > 0.0 &&
                       std::isfinite(cycle.mean_engine_speed_rpm) &&
                       cycle.requested_throttle.minimum_01 <=
                           cycle.requested_throttle.time_weighted_mean_01 + 1.0e-12 &&
                       cycle.requested_throttle.time_weighted_mean_01 <=
                           cycle.requested_throttle.maximum_01 + 1.0e-12 &&
                       (cycle.start_state_flags & ~known_state_flags) == 0U &&
                       (cycle.end_state_flags & ~known_state_flags) == 0U &&
                       (cycle.state_transition_flags & ~known_state_flags) == 0U,
                   std::string{label} +
                       " returned malformed exact completed-cycle evidence");
            saw_completed_cycle = true;
        }
        if (process.block_phase == ESO_BLOCK_PREPARATION) {
            expect(telemetry.has_held_dyno == 0U && telemetry.has_free_vehicle == 0U &&
                       bytes_are_zero(telemetry.held_dyno) &&
                       bytes_are_zero(telemetry.free_vehicle),
                   std::string{label} +
                       " published or dirtied an absent preparation sidecar");
            continue;
        }
        expect(process.block_phase == ESO_BLOCK_AUDIBLE &&
                   block == descriptor.preparation_block_count && saw_completed_cycle,
               std::string{label} + " released at the wrong block");
        return telemetry;
    }
    throw std::runtime_error{std::string{label} + " has no audible block"};
}

[[nodiscard]] std::string copy_engine_id(eso_context_t *context,
                                         const eso_engine_handle_t engine) {
    std::size_t size = 0;
    expect(eso_engine_copy_id(context, engine, {nullptr, 0U}, &size) == ESO_STATUS_OK,
           "engine ID size query failed");
    std::string result(size, '\0');
    std::vector<char> buffer(size + 1U);
    expect(eso_engine_copy_id(context, engine, {buffer.data(), buffer.size()}, &size) ==
               ESO_STATUS_OK,
           "engine ID copy failed");
    result.assign(buffer.data(), size);
    return result;
}

void test_diagnostic_surface(eso_context_t *context) {
    const std::string malformed = "{";
    eso_engine_handle_t engine = ESO_INVALID_HANDLE;
    expect(eso_compile_engine_json(context, view(malformed), nullptr, 0U, &engine) ==
               ESO_STATUS_ENGINE_PARSE_FAILED,
           "malformed JSON did not fail at parse time");

    eso_error_info_t error{};
    expect(eso_context_get_last_error(context, &error) == ESO_STATUS_OK &&
               error.stage == ESO_ERROR_STAGE_ENGINE_PARSE &&
               error.code == ESO_ERROR_AUTHORING_DIAGNOSTICS &&
               error.diagnostic_count != 0U,
           "parse failure lost its structured error record");

    eso_diagnostic_info_t info{};
    expect(eso_context_get_diagnostic(context, 0U, &info) == ESO_STATUS_OK &&
               info.severity == ESO_DIAGNOSTIC_ERROR,
           "parse failure lost its first diagnostic");
    std::vector<char> path(info.json_pointer_utf8_bytes + 1U);
    std::vector<char> message(info.message_utf8_bytes + 1U);
    eso_diagnostic_text_buffers_t buffers{
        {path.data(), path.size()},
        {nullptr, 0U},
        {nullptr, 0U},
        {message.data(), message.size()},
    };
    expect(eso_context_copy_diagnostic_text(context, 0U, &buffers) == ESO_STATUS_OK &&
               !std::string_view{message.data()}.empty(),
           "diagnostic text could not be copied into caller storage");
}

[[nodiscard]] std::uint32_t audition_bus(eso_context_t *context,
                                         const eso_session_handle_t session,
                                         const std::uint32_t bus_count) {
    for (std::uint32_t index = 0; index < bus_count; ++index) {
        eso_audio_bus_descriptor_t bus{};
        expect(eso_session_get_audio_bus_descriptor(context, session, index, &bus) ==
                   ESO_STATUS_OK,
               "audio bus descriptor query failed");
        const bool source_route_bus =
            bus.kind == ESO_AUDIO_BUS_SOURCE_ROUTE_DRY ||
            bus.kind == ESO_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER ||
            bus.kind == ESO_AUDIO_BUS_SOURCE_ROUTE_SELECTED;
        const auto expected_signal_disposition =
            bus.source_route_kind == ESO_SOURCE_ROUTE_EXHAUST_OUTLET
                ? ESO_AUDIO_SIGNAL_ACTIVE
            : source_route_bus ? ESO_AUDIO_SIGNAL_DECLARED_SILENT
                               : ESO_AUDIO_SIGNAL_ACTIVE;
        expect(source_route_bus
                   ? bus.has_route_id == 1U && bus.route_id != 0U &&
                         bus.source_route_kind != ESO_SOURCE_ROUTE_UNSPECIFIED &&
                         bus.signal_disposition == expected_signal_disposition
                   : bus.has_route_id == 0U && bus.route_id == 0U &&
                         bus.source_route_kind == ESO_SOURCE_ROUTE_UNSPECIFIED &&
                         bus.signal_disposition == expected_signal_disposition,
               "audio bus route identity, source kind, and signal disposition "
               "disagree");
        if (bus.kind == ESO_AUDIO_BUS_ENGINE_AUDITION_MASTER) {
            std::vector<char> id(bus.id_utf8_bytes + 1U);
            expect(eso_session_copy_audio_bus_id(context, session, index,
                                                 {id.data(), id.size()}) ==
                           ESO_STATUS_OK &&
                       !std::string_view{id.data()}.empty(),
                   "audition bus ID copy failed");
            return index;
        }
    }
    throw std::runtime_error{"session descriptor has no audition master"};
}

void enqueue_live_batch(eso_context_t *context, const eso_session_handle_t session,
                        const std::uint64_t first_live_frame,
                        const bool require_no_allocation = false) {
    const eso_control_command_t controls[] = {
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.75, 0U, 0U},
        {first_live_frame, 2U, ESO_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 3U, ESO_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U, 0U},
    };
    eso_control_rejection_t rejection{};
    allocation_probe::reject.store(require_no_allocation, std::memory_order_relaxed);
    const auto status =
        eso_session_enqueue_controls(context, session, controls, 3U, &rejection);
    allocation_probe::reject.store(false, std::memory_order_relaxed);
    expect(status == ESO_STATUS_OK && rejection.code == ESO_ERROR_NONE,
           "typed live-control batch was rejected");
}

void enqueue_free_live_batch(eso_context_t *context, const eso_session_handle_t session,
                             const std::uint64_t first_live_frame) {
    const eso_control_command_t controls[] = {
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 1.0, 0U, 0U},
        {first_live_frame, 2U, ESO_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 3U, ESO_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 4U, ESO_CONTROL_LIMITER_ENABLED, 0U, 0.0, 0U, 0U},
        {first_live_frame, 5U, ESO_CONTROL_EXTERNAL_RESISTING_TORQUE, 0U, 0.0, 0U, 0U},
    };
    eso_control_rejection_t rejection{};
    expect(eso_session_enqueue_controls(context, session, controls, 5U, &rejection) ==
                   ESO_STATUS_OK &&
               rejection.code == ESO_ERROR_NONE,
           "free-engine C session rejected an advertised live control");
}

void test_motion_contract_surface(eso_context_t *context,
                                  const std::filesystem::path &repository_root) {
    const auto engine_json =
        read_text(repository_root / "data/engines/bmw-m52tub28-cleanroom/engine.json");
    const auto held_dyno_json =
        read_text(repository_root / "data/engines/bmw-m52tub28-cleanroom/scenarios/"
                                    "held-dyno-pull-lift-1500-6500rpm.json");
    const auto free_vehicle_json =
        read_text(repository_root / "data/engines/bmw-m52tub28-cleanroom/scenarios/"
                                    "free-vehicle-launch-first-second.json");
    const auto ir = read_bytes(
        repository_root /
        "reference/fixtures/engine-sim-ir-library/presentation/smooth_39.wav");
    const auto accessory =
        read_bytes(repository_root /
                   "data/profiles/bmw-m52tub28-cleanroom/accessory-configurations/"
                   "bmw-m52tub28-cleanroom-warm-generic-accessories-v1.json");
    const std::string ir_id = "smooth-39";
    const std::string accessory_id = "warm-generic-accessories";
    const eso_asset_payload_t assets[] = {
        {ESO_ASSET_AUDIO, view(ir_id), view(ir)},
        {ESO_ASSET_ACCESSORY_CONFIGURATION, view(accessory_id), view(accessory)},
    };

    eso_engine_handle_t engine = ESO_INVALID_HANDLE;
    expect(eso_compile_engine_json(context, view(engine_json), assets, 2U, &engine) ==
               ESO_STATUS_OK,
           "M52TU engine compilation through C ABI v7 failed");
    eso_scenario_handle_t held_dyno_scenario = ESO_INVALID_HANDLE;
    eso_scenario_handle_t free_vehicle_scenario = ESO_INVALID_HANDLE;
    expect(eso_compile_scenario_json(context, engine, view(held_dyno_json),
                                     &held_dyno_scenario) == ESO_STATUS_OK &&
               eso_compile_scenario_json(context, engine, view(free_vehicle_json),
                                         &free_vehicle_scenario) == ESO_STATUS_OK,
           "C ABI v7 motion-scenario compilation failed");

    eso_session_handle_t held_dyno_session = ESO_INVALID_HANDLE;
    eso_session_handle_t free_vehicle_session = ESO_INVALID_HANDLE;
    expect(eso_create_session(context, held_dyno_scenario,
                              ESO_SESSION_EXECUTION_OPEN_ENDED,
                              &held_dyno_session) == ESO_STATUS_OK &&
               eso_create_session(context, free_vehicle_scenario,
                                  ESO_SESSION_EXECUTION_OPEN_ENDED,
                                  &free_vehicle_session) == ESO_STATUS_OK,
           "C ABI v7 open operating-bench session creation failed");

    constexpr auto kCoreLiveControls = ESO_LIVE_CONTROL_CAPABILITY_THROTTLE |
                                       ESO_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED |
                                       ESO_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED;
    constexpr auto kHeldDynoLiveControls =
        kCoreLiveControls | ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_TARGET_ENGINE_SPEED |
        ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE |
        ESO_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_DRIVING_TORQUE;
    constexpr auto kFreeVehicleLiveControls =
        kCoreLiveControls | ESO_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED |
        ESO_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED |
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SELECTED_FORWARD_GEAR |
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_CLUTCH_ENGAGEMENT |
        ESO_LIVE_CONTROL_CAPABILITY_VEHICLE_SERVICE_BRAKE_APPLICATION;

    eso_session_descriptor_t held_descriptor{};
    eso_session_descriptor_t vehicle_descriptor{};
    expect(eso_session_get_descriptor(context, held_dyno_session, &held_descriptor) ==
                   ESO_STATUS_OK &&
               held_descriptor.motion_mode == ESO_MOTION_HELD_DYNO &&
               held_descriptor.execution_kind == ESO_SESSION_EXECUTION_OPEN_ENDED &&
               held_descriptor.total_block_count == 0U &&
               held_descriptor.forward_gear_count == 0U &&
               held_descriptor.live_control_capabilities == kHeldDynoLiveControls,
           "held-dyno C descriptor lost its exact motion contract");
    expect(eso_session_get_descriptor(context, free_vehicle_session,
                                      &vehicle_descriptor) == ESO_STATUS_OK &&
               vehicle_descriptor.motion_mode == ESO_MOTION_FREE_VEHICLE &&
               vehicle_descriptor.execution_kind == ESO_SESSION_EXECUTION_OPEN_ENDED &&
               vehicle_descriptor.total_block_count == 0U &&
               vehicle_descriptor.forward_gear_count == 5U &&
               vehicle_descriptor.live_control_capabilities == kFreeVehicleLiveControls,
           "FreeVehicle C descriptor lost its exact motion contract");

    constexpr std::array<std::string_view, 5U> kExpectedGearSemanticIds{
        "gear-1", "gear-2", "gear-3", "gear-4", "gear-5"};
    constexpr std::array<double, 5U> kExpectedGearRatios{4.21, 2.49, 1.66, 1.24, 1.0};
    std::array<std::uint32_t, 5U> stable_gear_ids{};
    for (std::uint32_t index = 0U; index < vehicle_descriptor.forward_gear_count;
         ++index) {
        eso_forward_gear_descriptor_t gear{};
        expect(eso_session_get_forward_gear_descriptor(context, free_vehicle_session,
                                                       index, &gear) == ESO_STATUS_OK &&
                   gear.gear_id != 0U && gear.authored_ordinal == index + 1U &&
                   gear.ratio == kExpectedGearRatios[index] &&
                   gear.semantic_id_utf8_bytes ==
                       kExpectedGearSemanticIds[index].size(),
               "C forward-gear descriptor order or identity changed");
        stable_gear_ids[index] = gear.gear_id;
        expect(std::find(stable_gear_ids.begin(), stable_gear_ids.begin() + index,
                         gear.gear_id) == stable_gear_ids.begin() + index,
               "C forward-gear stable IDs are not unique");
        std::vector<char> semantic_id(gear.semantic_id_utf8_bytes + 1U);
        expect(eso_session_copy_forward_gear_semantic_id(
                   context, free_vehicle_session, index,
                   {semantic_id.data(), semantic_id.size()}) == ESO_STATUS_OK &&
                   std::string_view{semantic_id.data()} ==
                       kExpectedGearSemanticIds[index],
               "C forward-gear semantic ID copy changed");
    }
    eso_forward_gear_descriptor_t invalid_gear_descriptor{};
    expect(eso_session_get_forward_gear_descriptor(
               context, free_vehicle_session, vehicle_descriptor.forward_gear_count,
               &invalid_gear_descriptor) == ESO_STATUS_INVALID_ARGUMENT,
           "C forward-gear query admitted an out-of-inventory index");

    const auto held_first_live_frame = held_descriptor.preparation_block_count *
                                       held_descriptor.delivery_frames_per_block;
    eso_control_rejection_t rejection{};
    const eso_control_command_t target_with_discrete_payload{
        held_first_live_frame,
        1U,
        ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
        0U,
        1750.0,
        1U,
        0U};
    expect(eso_session_enqueue_controls(context, held_dyno_session,
                                        &target_with_discrete_payload, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C dyno scalar admitted a nonzero discrete payload");
    const eso_control_command_t target_with_reserved_payload{
        held_first_live_frame,
        1U,
        ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
        0U,
        1750.0,
        0U,
        1U};
    expect(eso_session_enqueue_controls(context, held_dyno_session,
                                        &target_with_reserved_payload, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C dyno scalar admitted a nonzero reserved field");
    const eso_control_command_t zero_target{held_first_live_frame,
                                            1U,
                                            ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
                                            0U,
                                            0.0,
                                            0U,
                                            0U};
    expect(eso_session_enqueue_controls(context, held_dyno_session, &zero_target, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C held dyno admitted a zero target engine speed");

    constexpr double kCommandedDynoTargetRpm = 1750.0;
    constexpr double kCommandedMaximumAbsorbingTorqueNm = 333.0;
    constexpr double kCommandedMaximumDrivingTorqueNm = 17.0;
    const eso_control_command_t held_controls[] = {
        {held_first_live_frame, 1U, ESO_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED, 0U,
         kCommandedDynoTargetRpm, 0U, 0U},
        {held_first_live_frame, 2U, ESO_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE, 0U,
         kCommandedMaximumAbsorbingTorqueNm, 0U, 0U},
        {held_first_live_frame, 3U, ESO_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE, 0U,
         kCommandedMaximumDrivingTorqueNm, 0U, 0U},
    };
    expect(eso_session_enqueue_controls(context, held_dyno_session, held_controls, 3U,
                                        &rejection) == ESO_STATUS_OK,
           "C held dyno rejected its advertised controls");

    const auto vehicle_first_live_frame = vehicle_descriptor.preparation_block_count *
                                          vehicle_descriptor.delivery_frames_per_block;
    const eso_control_command_t gear_with_boolean_payload{
        vehicle_first_live_frame,
        1U,
        ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        1U,
        0.0,
        2U,
        0U};
    expect(eso_session_enqueue_controls(context, free_vehicle_session,
                                        &gear_with_boolean_payload, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted a boolean payload");
    const eso_control_command_t gear_with_signed_zero{
        vehicle_first_live_frame,
        1U,
        ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        0U,
        -0.0,
        2U,
        0U};
    expect(eso_session_enqueue_controls(context, free_vehicle_session,
                                        &gear_with_signed_zero, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted signed negative zero");
    const eso_control_command_t out_of_inventory_gear{
        vehicle_first_live_frame,
        1U,
        ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        0U,
        0.0,
        vehicle_descriptor.forward_gear_count + 1U,
        0U};
    expect(eso_session_enqueue_controls(context, free_vehicle_session,
                                        &out_of_inventory_gear, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted an ordinal outside its descriptor");

    constexpr std::uint32_t kCommandedGearOrdinal = 2U;
    constexpr double kCommandedClutchEngagement = 0.5;
    constexpr double kCommandedServiceBrakeApplication = 0.5;
    const eso_control_command_t vehicle_controls[] = {
        {vehicle_first_live_frame, 1U, ESO_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR, 0U,
         0.0, kCommandedGearOrdinal, 0U},
        {vehicle_first_live_frame, 2U, ESO_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT, 0U,
         kCommandedClutchEngagement, 0U, 0U},
        {vehicle_first_live_frame, 3U, ESO_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION,
         0U, kCommandedServiceBrakeApplication, 0U, 0U},
    };
    expect(eso_session_enqueue_controls(context, free_vehicle_session, vehicle_controls,
                                        3U, &rejection) == ESO_STATUS_OK,
           "C FreeVehicle rejected its advertised controls");

    expect(eso_destroy_scenario(context, held_dyno_scenario) == ESO_STATUS_OK &&
               eso_destroy_scenario(context, free_vehicle_scenario) == ESO_STATUS_OK &&
               eso_destroy_engine(context, engine) == ESO_STATUS_OK,
           "C motion sessions failed to retain their compiled parents");

    const auto held_telemetry = process_to_first_audible_block(
        context, held_dyno_session, held_descriptor, "held-dyno C session");
    expect(held_telemetry.has_held_dyno == 1U &&
               held_telemetry.has_free_vehicle == 0U &&
               bytes_are_zero(held_telemetry.free_vehicle) &&
               held_telemetry.held_dyno.target_engine_speed_rpm ==
                   kCommandedDynoTargetRpm &&
               held_telemetry.held_dyno.maximum_absorbing_torque_nm ==
                   kCommandedMaximumAbsorbingTorqueNm &&
               held_telemetry.held_dyno.maximum_driving_torque_nm ==
                   kCommandedMaximumDrivingTorqueNm &&
               std::isfinite(held_telemetry.held_dyno.required_actuator_torque_nm) &&
               std::isfinite(held_telemetry.held_dyno.applied_actuator_torque_nm) &&
               held_telemetry.held_dyno.disposition >= ESO_HELD_DYNO_TRACKING &&
               held_telemetry.held_dyno.disposition <=
                   ESO_HELD_DYNO_DRIVING_TORQUE_LIMITED,
           "C held-dyno telemetry did not preserve its complete sidecar");

    const auto vehicle_telemetry = process_to_first_audible_block(
        context, free_vehicle_session, vehicle_descriptor, "FreeVehicle C session");
    expect(
        vehicle_telemetry.has_held_dyno == 0U &&
            bytes_are_zero(vehicle_telemetry.held_dyno) &&
            vehicle_telemetry.has_free_vehicle == 1U &&
            vehicle_telemetry.free_vehicle.has_selected_forward_gear == 1U &&
            vehicle_telemetry.free_vehicle.selected_forward_gear_ordinal ==
                kCommandedGearOrdinal &&
            vehicle_telemetry.free_vehicle.clutch_engagement_01 ==
                kCommandedClutchEngagement &&
            vehicle_telemetry.free_vehicle.service_brake_application_01 ==
                kCommandedServiceBrakeApplication &&
            vehicle_telemetry.free_vehicle.has_final_clutch_slip == 1U &&
            std::isfinite(vehicle_telemetry.free_vehicle.final_clutch_slip_rad_s) &&
            vehicle_telemetry.free_vehicle.clutch_disposition >= ESO_CLUTCH_NEUTRAL &&
            vehicle_telemetry.free_vehicle.clutch_disposition <= ESO_CLUTCH_TRACKING &&
            vehicle_telemetry.free_vehicle.road_load_disposition >=
                ESO_ROAD_LOAD_MOVING &&
            vehicle_telemetry.free_vehicle.road_load_disposition <=
                ESO_ROAD_LOAD_HELD_AT_REST,
        "C FreeVehicle telemetry did not preserve its complete sidecar");

    expect(eso_destroy_session(context, held_dyno_session) == ESO_STATUS_OK &&
               eso_destroy_session(context, free_vehicle_session) == ESO_STATUS_OK,
           "C ABI v7 motion-session teardown failed");
}

void run(const std::filesystem::path &repository_root) {
    eso_context_t *context = nullptr;
    expect(eso_context_create(ESO_C_API_VERSION, &context) == ESO_STATUS_OK &&
               context != nullptr,
           "C API context creation failed");

    test_diagnostic_surface(context);

    const auto engine_path = repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_path = repository_root / "data/engines/bmw-m52b28/scenarios/"
                                                 "inertial-dyno-1500-6500rpm.json";
    const auto free_scenario_path = repository_root /
                                    "data/engines/bmw-m52b28/scenarios/"
                                    "warm-running-free-rev-1500rpm.json";
    std::string engine_json = read_text(engine_path);
    std::string scenario_json = read_text(scenario_path);
    std::string free_scenario_json = read_text(free_scenario_path);
    std::vector<std::uint8_t> ir =
        read_bytes(repository_root /
                   "reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav");
    std::vector<std::uint8_t> accessory = read_bytes(
        repository_root / "data/profiles/bmw-m52b28/accessory-configurations/"
                          "bmw-m52b28-warm-stock-accessories-v1.json");
    const std::string ir_id = "smooth-39";
    const std::string accessory_id = "warm-stock-accessories";
    const eso_asset_payload_t assets[] = {
        {ESO_ASSET_AUDIO, view(ir_id), view(ir)},
        {ESO_ASSET_ACCESSORY_CONFIGURATION, view(accessory_id), view(accessory)},
    };

    eso_engine_handle_t engine = ESO_INVALID_HANDLE;
    expect(eso_compile_engine_json(context, view(engine_json), assets, 2U, &engine) ==
                   ESO_STATUS_OK &&
               engine != ESO_INVALID_HANDLE,
           "BMW engine compilation through the C ABI failed");
    expect(copy_engine_id(context, engine) == "bmw-m52b28",
           "compiled-engine identity changed at the C boundary");
    eso_error_info_t no_error{};
    expect(eso_context_get_last_error(context, &no_error) == ESO_STATUS_NOT_AVAILABLE,
           "successful engine compilation did not clear an older diagnostic");

    // The compiler must retain asset content, not caller byte views.
    ir.clear();
    ir.shrink_to_fit();
    accessory.clear();
    accessory.shrink_to_fit();

    eso_scenario_handle_t scenario = ESO_INVALID_HANDLE;
    expect(eso_compile_scenario_json(context, engine, view(scenario_json), &scenario) ==
                   ESO_STATUS_OK &&
               scenario != ESO_INVALID_HANDLE,
           "BMW scenario compilation through the C ABI failed");
    eso_scenario_handle_t free_scenario = ESO_INVALID_HANDLE;
    expect(eso_compile_scenario_json(context, engine, view(free_scenario_json),
                                     &free_scenario) == ESO_STATUS_OK &&
               free_scenario != ESO_INVALID_HANDLE,
           "BMW free-engine scenario compilation through the C ABI failed");

    // Kind bits prevent accidental cross-resource use even though C handle aliases
    // have one fixed integer representation.
    expect(eso_destroy_scenario(context, engine) == ESO_STATUS_INVALID_HANDLE,
           "engine handle was accepted as a scenario handle");

    eso_session_handle_t invalid_execution_session = UINT64_C(123);
    expect(eso_create_session(context, scenario, 0U, &invalid_execution_session) ==
                   ESO_STATUS_INVALID_ARGUMENT &&
               invalid_execution_session == ESO_INVALID_HANDLE,
           "unknown session execution kind was not rejected atomically");

    eso_session_handle_t stale = ESO_INVALID_HANDLE;
    expect(eso_create_session(context, scenario, ESO_SESSION_EXECUTION_FINITE_SCENARIO,
                              &stale) == ESO_STATUS_OK &&
               eso_destroy_session(context, stale) == ESO_STATUS_OK,
           "throwaway session lifecycle failed");
    eso_session_handle_t session_a = ESO_INVALID_HANDLE;
    eso_session_handle_t session_b = ESO_INVALID_HANDLE;
    eso_session_handle_t free_session = ESO_INVALID_HANDLE;
    expect(eso_create_session(context, scenario, ESO_SESSION_EXECUTION_FINITE_SCENARIO,
                              &session_a) == ESO_STATUS_OK &&
               eso_create_session(context, scenario,
                                  ESO_SESSION_EXECUTION_FINITE_SCENARIO,
                                  &session_b) == ESO_STATUS_OK &&
               eso_create_session(context, free_scenario,
                                  ESO_SESSION_EXECUTION_FINITE_SCENARIO,
                                  &free_session) == ESO_STATUS_OK &&
               session_a != stale,
           "generation-checked session slot was not recycled safely");
    eso_session_descriptor_t stale_descriptor{};
    expect(eso_session_get_descriptor(context, stale, &stale_descriptor) ==
               ESO_STATUS_INVALID_HANDLE,
           "destroyed session handle remained usable");

    eso_session_descriptor_t descriptor{};
    constexpr auto kInertialDynoLiveControls =
        ESO_LIVE_CONTROL_CAPABILITY_THROTTLE |
        ESO_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED |
        ESO_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED;
    expect(eso_session_get_descriptor(context, session_a, &descriptor) ==
                   ESO_STATUS_OK &&
               descriptor.physics_frames_per_block == 400U &&
               descriptor.maximum_cycle_evidence_per_process_call == 400U &&
               descriptor.delivery_frames_per_block == 3840U &&
               descriptor.audio_bus_count == 8U &&
               descriptor.live_control_capabilities == kInertialDynoLiveControls &&
               descriptor.execution_kind == ESO_SESSION_EXECUTION_FINITE_SCENARIO &&
               descriptor.motion_mode == ESO_MOTION_INERTIAL_DYNO &&
               descriptor.forward_gear_count == 0U,
           "C session descriptor differs from the executable method");
    eso_session_descriptor_t free_descriptor{};
    constexpr auto kFreeEngineLiveControls =
        kInertialDynoLiveControls | ESO_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED |
        ESO_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE |
        ESO_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED;
    expect(eso_session_get_descriptor(context, free_session, &free_descriptor) ==
                   ESO_STATUS_OK &&
               free_descriptor.live_control_capabilities == kFreeEngineLiveControls &&
               free_descriptor.motion_mode == ESO_MOTION_FREE_ENGINE &&
               free_descriptor.forward_gear_count == 0U,
           "C free-engine descriptor lost its exact live-control capabilities");
    std::vector<char> engine_id(descriptor.engine_id_utf8_bytes + 1U);
    std::vector<char> scenario_id(descriptor.scenario_id_utf8_bytes + 1U);
    eso_session_identity_buffers_t identities{
        {engine_id.data(), engine_id.size()},
        {scenario_id.data(), scenario_id.size()},
    };
    expect(eso_session_copy_identity(context, session_a, &identities) ==
                   ESO_STATUS_OK &&
               std::string_view{engine_id.data()} == "bmw-m52b28" &&
               std::string_view{scenario_id.data()} ==
                   "bmw-m52b28-inertial-dyno-1500-6500rpm",
           "session identity copy changed");

    const auto bus = audition_bus(context, session_a, descriptor.audio_bus_count);
    const auto first_live_frame =
        descriptor.preparation_block_count * descriptor.delivery_frames_per_block;
    const auto free_first_live_frame = free_descriptor.preparation_block_count *
                                       free_descriptor.delivery_frames_per_block;

    const eso_control_command_t preparation_control{
        0U, 1U, ESO_CONTROL_THROTTLE, 0U, 0.5, 0U, 0U};
    eso_control_rejection_t rejection{};
    expect(eso_session_enqueue_controls(context, session_a, nullptr, 0U, &rejection) ==
                   ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C session admitted an empty live-control batch");
    expect(eso_session_enqueue_controls(context, session_a, &preparation_control, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION,
           "preparation control was not rejected with its typed reason");

    const eso_control_command_t unsupported_controls[] = {
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.5, 0U, 0U},
        {first_live_frame, 2U, ESO_CONTROL_LIMITER_ENABLED, 1U, 0.0, 0U, 0U},
    };
    expect(eso_session_enqueue_controls(context, session_a, unsupported_controls, 2U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE &&
               rejection.command_index == 1U,
           "C capability rejection lost the first unsupported command index");

    const eso_control_command_t noncanonical_boolean{
        free_first_live_frame, 1U, ESO_CONTROL_LIMITER_ENABLED, 1U, 0.5, 0U, 0U};
    expect(eso_session_enqueue_controls(context, free_session, &noncanonical_boolean,
                                        1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a boolean control with a scalar payload");
    const eso_control_command_t signed_zero_boolean{
        free_first_live_frame, 1U, ESO_CONTROL_LIMITER_ENABLED, 1U, -0.0, 0U, 0U};
    expect(eso_session_enqueue_controls(context, free_session, &signed_zero_boolean, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted signed negative zero as canonical scalar zero");
    const eso_control_command_t noncanonical_scalar{
        free_first_live_frame,
        1U,
        ESO_CONTROL_EXTERNAL_RESISTING_TORQUE,
        1U,
        18.0,
        0U,
        0U};
    expect(eso_session_enqueue_controls(context, free_session, &noncanonical_scalar, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a scalar control with a boolean payload");
    const eso_control_command_t negative_resistance{
        free_first_live_frame,
        1U,
        ESO_CONTROL_EXTERNAL_RESISTING_TORQUE,
        0U,
        -1.0,
        0U,
        0U};
    expect(eso_session_enqueue_controls(context, free_session, &negative_resistance, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted negative external resisting torque");

    enqueue_live_batch(context, session_a, first_live_frame, true);
    enqueue_live_batch(context, session_b, first_live_frame);
    enqueue_free_live_batch(context, free_session, free_first_live_frame);

    // A live session owns the shared immutable scenario/engine it needs.
    expect(eso_destroy_scenario(context, scenario) == ESO_STATUS_OK &&
               eso_destroy_scenario(context, free_scenario) == ESO_STATUS_OK &&
               eso_destroy_engine(context, engine) == ESO_STATUS_OK,
           "compiled parent handles could not be released after session creation");

    std::vector<float> pcm_a(descriptor.delivery_frames_per_block);
    std::vector<float> pcm_b(descriptor.delivery_frames_per_block);
    eso_audio_copy_buffer_t short_buffer{bus, pcm_a.data(), pcm_a.size() - 1U, 99U};
    eso_session_telemetry_t telemetry_a{};
    eso_process_info_t process_a{};
    std::vector<eso_completed_cycle_evidence_t> cycle_evidence_a(
        descriptor.maximum_cycle_evidence_per_process_call);
    std::vector<eso_completed_cycle_evidence_t> cycle_evidence_b(
        descriptor.maximum_cycle_evidence_per_process_call);
    expect(eso_session_process(context, session_a, nullptr, 0U, &telemetry_a, 1U,
                               nullptr, 1U,
                               &process_a) == ESO_STATUS_INVALID_ARGUMENT &&
               process_a.kind == 0U,
           "null cycle-evidence pointer with nonzero capacity was admitted");
    expect(eso_session_process(context, session_a, nullptr, 0U, &telemetry_a, 1U,
                               cycle_evidence_a.data(), cycle_evidence_a.size() - 1U,
                               &process_a) == ESO_STATUS_BUFFER_TOO_SMALL &&
               process_a.kind == 0U,
           "short cycle-evidence buffer advanced or published a session block");
    expect(eso_session_process(context, session_a, &short_buffer, 1U, &telemetry_a, 1U,
                               nullptr, 0U,
                               &process_a) == ESO_STATUS_BUFFER_TOO_SMALL &&
               short_buffer.samples_written == 0U,
           "short PCM buffer advanced or partially published a session block");

    eso_audio_copy_buffer_t audio_a{bus, pcm_a.data(), pcm_a.size(), 0U};
    eso_audio_copy_buffer_t audio_b{bus, pcm_b.data(), pcm_b.size(), 0U};
    eso_session_telemetry_t telemetry_b{};
    eso_process_info_t process_b{};
    allocation_probe::reject.store(true, std::memory_order_relaxed);
    const auto process_a_status = eso_session_process(
        context, session_a, &audio_a, 1U, &telemetry_a, 1U, cycle_evidence_a.data(),
        cycle_evidence_a.size(), &process_a);
    allocation_probe::reject.store(false, std::memory_order_relaxed);
    const auto process_b_status = eso_session_process(
        context, session_b, &audio_b, 1U, &telemetry_b, 1U, cycle_evidence_b.data(),
        cycle_evidence_b.size(), &process_b);
    expect(process_a_status == ESO_STATUS_OK && process_b_status == ESO_STATUS_OK,
           "C API could not process the first session block");
    expect(process_a.kind == ESO_PROCESS_BLOCK &&
               process_a.block_phase == ESO_BLOCK_PREPARATION &&
               process_a.block_ordinal == 0U && process_a.telemetry_written == 1U &&
               process_a.cycle_evidence_written == process_b.cycle_evidence_written &&
               process_a.block_ordinal == process_b.block_ordinal &&
               audio_a.samples_written == pcm_a.size() &&
               audio_b.samples_written == pcm_b.size(),
           "C API block metadata or copy extents changed");
    expect(std::memcmp(pcm_a.data(), pcm_b.data(), pcm_a.size() * sizeof(float)) == 0,
           "independent C sessions were not byte-stable");
    expect(telemetry_a.physics_step_end == telemetry_b.physics_step_end &&
               telemetry_a.engine.engine_step_end_index ==
                   telemetry_b.engine.engine_step_end_index &&
               telemetry_a.engine.engine_speed_rpm ==
                   telemetry_b.engine.engine_speed_rpm &&
               telemetry_a.has_held_dyno == 0U && telemetry_a.has_free_vehicle == 0U,
           "C telemetry copy diverged between deterministic sessions");

    const auto free_release_physics_frame = free_descriptor.preparation_block_count *
                                            free_descriptor.physics_frames_per_block;
    double free_wot_crossing_time_s = -1.0;
    while (free_wot_crossing_time_s < 0.0) {
        eso_session_telemetry_t free_telemetry{};
        eso_process_info_t free_process{};
        expect(eso_session_process(context, free_session, nullptr, 0U, &free_telemetry,
                                   1U, nullptr, 0U, &free_process) == ESO_STATUS_OK,
               "free-engine RPM trajectory session failed");
        expect(free_process.kind == ESO_PROCESS_BLOCK,
               "free-engine completed before its 7,000-rpm WOT crossing");
        if (free_telemetry.engine.engine_speed_rpm >= 7000.0) {
            expect(free_telemetry.physics_step_end >= free_release_physics_frame,
                   "free-engine crossed 7,000 rpm before release");
            const auto elapsed_physics_frames =
                free_telemetry.physics_step_end - free_release_physics_frame;
            free_wot_crossing_time_s =
                static_cast<double>(elapsed_physics_frames) *
                static_cast<double>(free_descriptor.physics_rate_denominator) /
                static_cast<double>(free_descriptor.physics_rate_numerator_hz);
        }
    }
    expect(free_wot_crossing_time_s >= 0.44 && free_wot_crossing_time_s <= 0.50,
           "BMW interactive FreeEngine WOT smoke trajectory regressed outside "
           "its scenario-specific envelope");

    expect(eso_destroy_session(context, session_a) == ESO_STATUS_OK &&
               eso_destroy_session(context, session_b) == ESO_STATUS_OK &&
               eso_destroy_session(context, free_session) == ESO_STATUS_OK,
           "C API baseline-session teardown failed");

    test_motion_contract_surface(context, repository_root);
    expect(eso_context_destroy(context) == ESO_STATUS_OK,
           "C API context teardown failed");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"usage: c_api_integration_test <repository-root>"};
        }
        run(std::filesystem::path{argv[1]});
        return 0;
    } catch (const std::exception &error) {
        return error.what() == nullptr ? 2 : 1;
    }
}
