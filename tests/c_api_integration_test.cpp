#include "crankwave/c_api.h"

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
#include <iostream>
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

[[nodiscard]] std::string digest_hex(const crankwave_sha256_digest_t &digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(CRANKWAVE_SHA256_DIGEST_SIZE * 2U);
    for (const auto byte : digest.bytes) {
        result.push_back(kHex[byte >> 4U]);
        result.push_back(kHex[byte & 0x0fU]);
    }
    return result;
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

[[nodiscard]] crankwave_utf8_view_t view(const std::string &value) noexcept {
    return {value.data(), value.size()};
}

[[nodiscard]] crankwave_byte_view_t view(const std::vector<std::uint8_t> &value) noexcept {
    return {value.data(), value.size()};
}

template <class Value> [[nodiscard]] bool bytes_are_zero(const Value &value) noexcept {
    const auto *const first = reinterpret_cast<const unsigned char *>(&value);
    return std::all_of(first, first + sizeof(value),
                       [](const unsigned char byte) { return byte == 0U; });
}

[[nodiscard]] crankwave_session_telemetry_t process_to_first_audible_block(
    crankwave_context_t *context, const crankwave_session_handle_t session,
    const crankwave_session_descriptor_t &descriptor, const std::string_view label) {
    std::vector<crankwave_completed_cycle_evidence_t> cycles(
        descriptor.maximum_cycle_evidence_per_process_call);
    bool saw_completed_cycle = false;
    for (std::uint64_t block = 0U; block <= descriptor.preparation_block_count;
         ++block) {
        crankwave_session_telemetry_t telemetry{};
        crankwave_process_info_t process{};
        expect(crankwave_session_process(context, session, nullptr, 0U, &telemetry, 1U,
                                   cycles.data(), cycles.size(),
                                   &process) == CRANKWAVE_STATUS_OK,
               std::string{label} + " failed before its first audible block");
        expect(process.kind == CRANKWAVE_PROCESS_BLOCK && process.block_ordinal == block &&
                   process.telemetry_written == 1U &&
                   process.cycle_evidence_written <= cycles.size(),
               std::string{label} + " returned a discontinuous block");
        for (std::size_t index = 0; index < process.cycle_evidence_written; ++index) {
            const auto &cycle = cycles[index];
            const auto known_state_flags = CRANKWAVE_ENGINE_CYCLE_STATE_IGNITION_ENABLED |
                                           CRANKWAVE_ENGINE_CYCLE_STATE_FUEL_ENABLED |
                                           CRANKWAVE_ENGINE_CYCLE_STATE_STARTER_ENABLED |
                                           CRANKWAVE_ENGINE_CYCLE_STATE_DYNO_ENABLED |
                                           CRANKWAVE_ENGINE_CYCLE_STATE_LIMITER_ENABLED |
                                           CRANKWAVE_ENGINE_CYCLE_STATE_LIMITER_CUT_ACTIVE;
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
        if (process.block_phase == CRANKWAVE_BLOCK_PREPARATION) {
            expect(telemetry.has_held_dyno == 0U && telemetry.has_free_vehicle == 0U &&
                       bytes_are_zero(telemetry.held_dyno) &&
                       bytes_are_zero(telemetry.free_vehicle),
                   std::string{label} +
                       " published or dirtied an absent preparation sidecar");
            continue;
        }
        expect(process.block_phase == CRANKWAVE_BLOCK_AUDIBLE &&
                   block == descriptor.preparation_block_count && saw_completed_cycle,
               std::string{label} + " released at the wrong block");
        return telemetry;
    }
    throw std::runtime_error{std::string{label} + " has no audible block"};
}

[[nodiscard]] std::string copy_engine_id(crankwave_context_t *context,
                                         const crankwave_engine_handle_t engine) {
    std::size_t size = 0;
    expect(crankwave_engine_copy_id(context, engine, {nullptr, 0U}, &size) == CRANKWAVE_STATUS_OK,
           "engine ID size query failed");
    std::string result(size, '\0');
    std::vector<char> buffer(size + 1U);
    expect(crankwave_engine_copy_id(context, engine, {buffer.data(), buffer.size()}, &size) ==
               CRANKWAVE_STATUS_OK,
           "engine ID copy failed");
    result.assign(buffer.data(), size);
    return result;
}

void test_diagnostic_surface(crankwave_context_t *context) {
    const std::string malformed = "{";
    crankwave_engine_handle_t engine = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_engine_json(context, view(malformed), nullptr, 0U, &engine) ==
               CRANKWAVE_STATUS_ENGINE_PARSE_FAILED,
           "malformed JSON did not fail at parse time");

    crankwave_error_info_t error{};
    expect(crankwave_context_get_last_error(context, &error) == CRANKWAVE_STATUS_OK &&
               error.stage == CRANKWAVE_ERROR_STAGE_ENGINE_PARSE &&
               error.code == CRANKWAVE_ERROR_AUTHORING_DIAGNOSTICS &&
               error.diagnostic_count != 0U,
           "parse failure lost its structured error record");

    crankwave_diagnostic_info_t info{};
    expect(crankwave_context_get_diagnostic(context, 0U, &info) == CRANKWAVE_STATUS_OK &&
               info.severity == CRANKWAVE_DIAGNOSTIC_ERROR,
           "parse failure lost its first diagnostic");
    std::vector<char> path(info.json_pointer_utf8_bytes + 1U);
    std::vector<char> message(info.message_utf8_bytes + 1U);
    crankwave_diagnostic_text_buffers_t buffers{
        {path.data(), path.size()},
        {nullptr, 0U},
        {nullptr, 0U},
        {message.data(), message.size()},
    };
    expect(crankwave_context_copy_diagnostic_text(context, 0U, &buffers) == CRANKWAVE_STATUS_OK &&
               !std::string_view{message.data()}.empty(),
           "diagnostic text could not be copied into caller storage");
}

[[nodiscard]] std::uint32_t audition_bus(crankwave_context_t *context,
                                         const crankwave_session_handle_t session,
                                         const std::uint32_t bus_count) {
    for (std::uint32_t index = 0; index < bus_count; ++index) {
        crankwave_audio_bus_descriptor_t bus{};
        expect(crankwave_session_get_audio_bus_descriptor(context, session, index, &bus) ==
                   CRANKWAVE_STATUS_OK,
               "audio bus descriptor query failed");
        const bool source_route_bus =
            bus.kind == CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_DRY ||
            bus.kind == CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_CONFIGURED_TRANSFER ||
            bus.kind == CRANKWAVE_AUDIO_BUS_SOURCE_ROUTE_SELECTED;
        const auto expected_signal_disposition =
            bus.source_route_kind == CRANKWAVE_SOURCE_ROUTE_EXHAUST_OUTLET
                ? CRANKWAVE_AUDIO_SIGNAL_ACTIVE
            : source_route_bus ? CRANKWAVE_AUDIO_SIGNAL_DECLARED_SILENT
                               : CRANKWAVE_AUDIO_SIGNAL_ACTIVE;
        expect(source_route_bus
                   ? bus.has_route_id == 1U && bus.route_id != 0U &&
                         bus.source_route_kind != CRANKWAVE_SOURCE_ROUTE_UNSPECIFIED &&
                         bus.signal_disposition == expected_signal_disposition
                   : bus.has_route_id == 0U && bus.route_id == 0U &&
                         bus.source_route_kind == CRANKWAVE_SOURCE_ROUTE_UNSPECIFIED &&
                         bus.signal_disposition == expected_signal_disposition,
               "audio bus route identity, source kind, and signal disposition "
               "disagree");
        if (bus.kind == CRANKWAVE_AUDIO_BUS_ENGINE_AUDITION_MASTER) {
            std::vector<char> id(bus.id_utf8_bytes + 1U);
            expect(crankwave_session_copy_audio_bus_id(context, session, index,
                                                 {id.data(), id.size()}) ==
                           CRANKWAVE_STATUS_OK &&
                       !std::string_view{id.data()}.empty(),
                   "audition bus ID copy failed");
            return index;
        }
    }
    throw std::runtime_error{"session descriptor has no audition master"};
}

void enqueue_live_batch(crankwave_context_t *context, const crankwave_session_handle_t session,
                        const std::uint64_t first_live_frame,
                        const bool require_no_allocation = false) {
    const crankwave_control_command_t controls[] = {
        {first_live_frame, 1U, CRANKWAVE_CONTROL_THROTTLE, 0U, 0.75, 0U, 0U},
        {first_live_frame, 2U, CRANKWAVE_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 3U, CRANKWAVE_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U, 0U},
    };
    crankwave_control_rejection_t rejection{};
    allocation_probe::reject.store(require_no_allocation, std::memory_order_relaxed);
    const auto status =
        crankwave_session_enqueue_controls(context, session, controls, 3U, &rejection);
    allocation_probe::reject.store(false, std::memory_order_relaxed);
    expect(status == CRANKWAVE_STATUS_OK && rejection.code == CRANKWAVE_ERROR_NONE,
           "typed live-control batch was rejected");
}

void enqueue_free_live_batch(crankwave_context_t *context, const crankwave_session_handle_t session,
                             const std::uint64_t first_live_frame) {
    const crankwave_control_command_t controls[] = {
        {first_live_frame, 1U, CRANKWAVE_CONTROL_THROTTLE, 0U, 1.0, 0U, 0U},
        {first_live_frame, 2U, CRANKWAVE_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 3U, CRANKWAVE_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U, 0U},
        {first_live_frame, 4U, CRANKWAVE_CONTROL_LIMITER_ENABLED, 0U, 0.0, 0U, 0U},
        {first_live_frame, 5U, CRANKWAVE_CONTROL_EXTERNAL_RESISTING_TORQUE, 0U, 0.0, 0U, 0U},
    };
    crankwave_control_rejection_t rejection{};
    expect(crankwave_session_enqueue_controls(context, session, controls, 5U, &rejection) ==
                   CRANKWAVE_STATUS_OK &&
               rejection.code == CRANKWAVE_ERROR_NONE,
           "free-engine C session rejected an advertised live control");
}

void test_motion_contract_surface(crankwave_context_t *context,
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
        "reference/fixtures/crankwave-ir-library/presentation/smooth_39.wav");
    const auto accessory =
        read_bytes(repository_root /
                   "data/profiles/bmw-m52tub28-cleanroom/accessory-configurations/"
                   "bmw-m52tub28-cleanroom-warm-generic-accessories-v1.json");
    const std::string ir_id = "smooth-39";
    const std::string accessory_id = "warm-generic-accessories";
    const crankwave_asset_payload_t assets[] = {
        {CRANKWAVE_ASSET_AUDIO, view(ir_id), view(ir)},
        {CRANKWAVE_ASSET_ACCESSORY_CONFIGURATION, view(accessory_id), view(accessory)},
    };

    crankwave_engine_handle_t engine = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_engine_json(context, view(engine_json), assets, 2U, &engine) ==
               CRANKWAVE_STATUS_OK,
           "M52TU engine compilation through C ABI v9 failed");

    crankwave_sha256_digest_t engine_provenance{};
    expect(crankwave_engine_copy_provenance_sha256(context, engine, nullptr) ==
               CRANKWAVE_STATUS_INVALID_ARGUMENT,
           "engine provenance admitted a null output digest");
    engine_provenance.bytes[0] = 0xa5U;
    expect(crankwave_engine_copy_provenance_sha256(context, CRANKWAVE_INVALID_HANDLE,
                                             &engine_provenance) ==
                   CRANKWAVE_STATUS_INVALID_HANDLE &&
               engine_provenance.bytes[0] == 0xa5U,
           "engine provenance lost invalid-handle or transactional output behavior");
    expect(crankwave_engine_copy_provenance_sha256(context, engine, &engine_provenance) ==
               CRANKWAVE_STATUS_OK,
           "C ABI could not copy compiled-engine provenance");
    const auto engine_provenance_hex = digest_hex(engine_provenance);
    constexpr std::string_view kExpectedEngineProvenance =
        "d47d239c0a8767af26b6f805306ccd140e5c821de1b02389246ae0a286fc2491";
    if (engine_provenance_hex != kExpectedEngineProvenance) {
        throw std::runtime_error{
            "C ABI engine provenance differs from the compiled bundle SHA-256: " +
            engine_provenance_hex};
    }

    crankwave_sha256_digest_t renderer_source{};
    const auto renderer_status =
        crankwave_renderer_copy_source_closure_sha256(context, &renderer_source);
    if (renderer_status == CRANKWAVE_STATUS_OK) {
        const auto renderer_hex = digest_hex(renderer_source);
        expect(renderer_hex.size() == 64U &&
                   renderer_hex.find_first_not_of("0123456789abcdef") ==
                       std::string::npos &&
                   renderer_hex.find_first_not_of('0') != std::string::npos,
               "C ABI renderer source closure is not a canonical SHA-256");
    } else {
        crankwave_error_info_t error{};
        expect(renderer_status == CRANKWAVE_STATUS_NOT_AVAILABLE &&
                   crankwave_context_get_last_error(context, &error) == CRANKWAVE_STATUS_OK &&
                   error.code == CRANKWAVE_ERROR_RENDERER_SOURCE_STAMP_UNAVAILABLE,
               "an inadmissible renderer stamp did not fail closed");
    }
    crankwave_scenario_handle_t held_dyno_scenario = CRANKWAVE_INVALID_HANDLE;
    crankwave_scenario_handle_t free_vehicle_scenario = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_scenario_json(context, engine, view(held_dyno_json),
                                     &held_dyno_scenario) == CRANKWAVE_STATUS_OK &&
               crankwave_compile_scenario_json(context, engine, view(free_vehicle_json),
                                         &free_vehicle_scenario) == CRANKWAVE_STATUS_OK,
           "C ABI v9 motion-scenario compilation failed");

    crankwave_session_handle_t held_dyno_session = CRANKWAVE_INVALID_HANDLE;
    crankwave_session_handle_t free_vehicle_session = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_create_session(context, held_dyno_scenario,
                              CRANKWAVE_SESSION_EXECUTION_OPEN_ENDED,
                              &held_dyno_session) == CRANKWAVE_STATUS_OK &&
               crankwave_create_session(context, free_vehicle_scenario,
                                  CRANKWAVE_SESSION_EXECUTION_OPEN_ENDED,
                                  &free_vehicle_session) == CRANKWAVE_STATUS_OK,
           "C ABI v9 open operating-bench session creation failed");

    constexpr auto kCoreLiveControls = CRANKWAVE_LIVE_CONTROL_CAPABILITY_THROTTLE |
                                       CRANKWAVE_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED |
                                       CRANKWAVE_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED;
    constexpr auto kHeldDynoLiveControls =
        kCoreLiveControls | CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_TARGET_ENGINE_SPEED |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_HELD_DYNO_MAXIMUM_DRIVING_TORQUE;
    constexpr auto kFreeVehicleLiveControls =
        kCoreLiveControls | CRANKWAVE_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_SELECTED_FORWARD_GEAR |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_CLUTCH_ENGAGEMENT |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_VEHICLE_SERVICE_BRAKE_APPLICATION;

    crankwave_session_descriptor_t held_descriptor{};
    crankwave_session_descriptor_t vehicle_descriptor{};
    expect(crankwave_session_get_descriptor(context, held_dyno_session, &held_descriptor) ==
                   CRANKWAVE_STATUS_OK &&
               held_descriptor.motion_mode == CRANKWAVE_MOTION_HELD_DYNO &&
               held_descriptor.execution_kind == CRANKWAVE_SESSION_EXECUTION_OPEN_ENDED &&
               held_descriptor.total_block_count == 0U &&
               held_descriptor.forward_gear_count == 0U &&
               held_descriptor.live_control_capabilities == kHeldDynoLiveControls,
           "held-dyno C descriptor lost its exact motion contract");
    expect(crankwave_session_get_descriptor(context, free_vehicle_session,
                                      &vehicle_descriptor) == CRANKWAVE_STATUS_OK &&
               vehicle_descriptor.motion_mode == CRANKWAVE_MOTION_FREE_VEHICLE &&
               vehicle_descriptor.execution_kind == CRANKWAVE_SESSION_EXECUTION_OPEN_ENDED &&
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
        crankwave_forward_gear_descriptor_t gear{};
        expect(crankwave_session_get_forward_gear_descriptor(context, free_vehicle_session,
                                                       index, &gear) == CRANKWAVE_STATUS_OK &&
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
        expect(crankwave_session_copy_forward_gear_semantic_id(
                   context, free_vehicle_session, index,
                   {semantic_id.data(), semantic_id.size()}) == CRANKWAVE_STATUS_OK &&
                   std::string_view{semantic_id.data()} ==
                       kExpectedGearSemanticIds[index],
               "C forward-gear semantic ID copy changed");
    }
    crankwave_forward_gear_descriptor_t invalid_gear_descriptor{};
    expect(crankwave_session_get_forward_gear_descriptor(
               context, free_vehicle_session, vehicle_descriptor.forward_gear_count,
               &invalid_gear_descriptor) == CRANKWAVE_STATUS_INVALID_ARGUMENT,
           "C forward-gear query admitted an out-of-inventory index");

    const auto held_first_live_frame = held_descriptor.preparation_block_count *
                                       held_descriptor.delivery_frames_per_block;
    crankwave_control_rejection_t rejection{};
    const crankwave_control_command_t target_with_discrete_payload{
        held_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
        0U,
        1750.0,
        1U,
        0U};
    expect(crankwave_session_enqueue_controls(context, held_dyno_session,
                                        &target_with_discrete_payload, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C dyno scalar admitted a nonzero discrete payload");
    const crankwave_control_command_t target_with_reserved_payload{
        held_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
        0U,
        1750.0,
        0U,
        1U};
    expect(crankwave_session_enqueue_controls(context, held_dyno_session,
                                        &target_with_reserved_payload, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C dyno scalar admitted a nonzero reserved field");
    const crankwave_control_command_t zero_target{held_first_live_frame,
                                            1U,
                                            CRANKWAVE_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED,
                                            0U,
                                            0.0,
                                            0U,
                                            0U};
    expect(crankwave_session_enqueue_controls(context, held_dyno_session, &zero_target, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C held dyno admitted a zero target engine speed");

    constexpr double kCommandedDynoTargetRpm = 1750.0;
    constexpr double kCommandedMaximumAbsorbingTorqueNm = 333.0;
    constexpr double kCommandedMaximumDrivingTorqueNm = 17.0;
    const crankwave_control_command_t held_controls[] = {
        {held_first_live_frame, 1U, CRANKWAVE_CONTROL_HELD_DYNO_TARGET_ENGINE_SPEED, 0U,
         kCommandedDynoTargetRpm, 0U, 0U},
        {held_first_live_frame, 2U, CRANKWAVE_CONTROL_HELD_DYNO_MAXIMUM_ABSORBING_TORQUE, 0U,
         kCommandedMaximumAbsorbingTorqueNm, 0U, 0U},
        {held_first_live_frame, 3U, CRANKWAVE_CONTROL_HELD_DYNO_MAXIMUM_DRIVING_TORQUE, 0U,
         kCommandedMaximumDrivingTorqueNm, 0U, 0U},
    };
    expect(crankwave_session_enqueue_controls(context, held_dyno_session, held_controls, 3U,
                                        &rejection) == CRANKWAVE_STATUS_OK,
           "C held dyno rejected its advertised controls");

    const auto vehicle_first_live_frame = vehicle_descriptor.preparation_block_count *
                                          vehicle_descriptor.delivery_frames_per_block;
    const crankwave_control_command_t gear_with_boolean_payload{
        vehicle_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        1U,
        0.0,
        2U,
        0U};
    expect(crankwave_session_enqueue_controls(context, free_vehicle_session,
                                        &gear_with_boolean_payload, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted a boolean payload");
    const crankwave_control_command_t gear_with_signed_zero{
        vehicle_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        0U,
        -0.0,
        2U,
        0U};
    expect(crankwave_session_enqueue_controls(context, free_vehicle_session,
                                        &gear_with_signed_zero, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted signed negative zero");
    const crankwave_control_command_t out_of_inventory_gear{
        vehicle_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR,
        0U,
        0.0,
        vehicle_descriptor.forward_gear_count + 1U,
        0U};
    expect(crankwave_session_enqueue_controls(context, free_vehicle_session,
                                        &out_of_inventory_gear, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C gear control admitted an ordinal outside its descriptor");

    constexpr std::uint32_t kCommandedGearOrdinal = 2U;
    constexpr double kCommandedClutchEngagement = 0.5;
    constexpr double kCommandedServiceBrakeApplication = 0.5;
    const crankwave_control_command_t vehicle_controls[] = {
        {vehicle_first_live_frame, 1U, CRANKWAVE_CONTROL_VEHICLE_SELECTED_FORWARD_GEAR, 0U,
         0.0, kCommandedGearOrdinal, 0U},
        {vehicle_first_live_frame, 2U, CRANKWAVE_CONTROL_VEHICLE_CLUTCH_ENGAGEMENT, 0U,
         kCommandedClutchEngagement, 0U, 0U},
        {vehicle_first_live_frame, 3U, CRANKWAVE_CONTROL_VEHICLE_SERVICE_BRAKE_APPLICATION,
         0U, kCommandedServiceBrakeApplication, 0U, 0U},
    };
    expect(crankwave_session_enqueue_controls(context, free_vehicle_session, vehicle_controls,
                                        3U, &rejection) == CRANKWAVE_STATUS_OK,
           "C FreeVehicle rejected its advertised controls");

    expect(crankwave_destroy_scenario(context, held_dyno_scenario) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_scenario(context, free_vehicle_scenario) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_engine(context, engine) == CRANKWAVE_STATUS_OK,
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
               held_telemetry.held_dyno.disposition >= CRANKWAVE_HELD_DYNO_TRACKING &&
               held_telemetry.held_dyno.disposition <=
                   CRANKWAVE_HELD_DYNO_DRIVING_TORQUE_LIMITED,
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
            vehicle_telemetry.free_vehicle.clutch_disposition >= CRANKWAVE_CLUTCH_NEUTRAL &&
            vehicle_telemetry.free_vehicle.clutch_disposition <= CRANKWAVE_CLUTCH_TRACKING &&
            vehicle_telemetry.free_vehicle.road_load_disposition >=
                CRANKWAVE_ROAD_LOAD_MOVING &&
            vehicle_telemetry.free_vehicle.road_load_disposition <=
                CRANKWAVE_ROAD_LOAD_HELD_AT_REST,
        "C FreeVehicle telemetry did not preserve its complete sidecar");

    expect(crankwave_destroy_session(context, held_dyno_session) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_session(context, free_vehicle_session) == CRANKWAVE_STATUS_OK,
           "C ABI v9 motion-session teardown failed");
}

void run(const std::filesystem::path &repository_root) {
    crankwave_context_t *context = nullptr;
    expect(crankwave_context_create(CRANKWAVE_C_API_VERSION, &context) == CRANKWAVE_STATUS_OK &&
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
    const crankwave_asset_payload_t assets[] = {
        {CRANKWAVE_ASSET_AUDIO, view(ir_id), view(ir)},
        {CRANKWAVE_ASSET_ACCESSORY_CONFIGURATION, view(accessory_id), view(accessory)},
    };

    crankwave_engine_handle_t engine = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_engine_json(context, view(engine_json), assets, 2U, &engine) ==
                   CRANKWAVE_STATUS_OK &&
               engine != CRANKWAVE_INVALID_HANDLE,
           "BMW engine compilation through the C ABI failed");
    expect(copy_engine_id(context, engine) == "bmw-m52b28",
           "compiled-engine identity changed at the C boundary");
    crankwave_error_info_t no_error{};
    expect(crankwave_context_get_last_error(context, &no_error) == CRANKWAVE_STATUS_NOT_AVAILABLE,
           "successful engine compilation did not clear an older diagnostic");

    // The compiler must retain asset content, not caller byte views.
    ir.clear();
    ir.shrink_to_fit();
    accessory.clear();
    accessory.shrink_to_fit();

    crankwave_scenario_handle_t scenario = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_scenario_json(context, engine, view(scenario_json), &scenario) ==
                   CRANKWAVE_STATUS_OK &&
               scenario != CRANKWAVE_INVALID_HANDLE,
           "BMW scenario compilation through the C ABI failed");
    crankwave_scenario_handle_t free_scenario = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_compile_scenario_json(context, engine, view(free_scenario_json),
                                     &free_scenario) == CRANKWAVE_STATUS_OK &&
               free_scenario != CRANKWAVE_INVALID_HANDLE,
           "BMW free-engine scenario compilation through the C ABI failed");

    // Kind bits prevent accidental cross-resource use even though C handle aliases
    // have one fixed integer representation.
    expect(crankwave_destroy_scenario(context, engine) == CRANKWAVE_STATUS_INVALID_HANDLE,
           "engine handle was accepted as a scenario handle");

    crankwave_session_handle_t invalid_execution_session = UINT64_C(123);
    expect(crankwave_create_session(context, scenario, 0U, &invalid_execution_session) ==
                   CRANKWAVE_STATUS_INVALID_ARGUMENT &&
               invalid_execution_session == CRANKWAVE_INVALID_HANDLE,
           "unknown session execution kind was not rejected atomically");

    crankwave_session_handle_t stale = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_create_session(context, scenario, CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO,
                              &stale) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_session(context, stale) == CRANKWAVE_STATUS_OK,
           "throwaway session lifecycle failed");
    crankwave_session_handle_t session_a = CRANKWAVE_INVALID_HANDLE;
    crankwave_session_handle_t session_b = CRANKWAVE_INVALID_HANDLE;
    crankwave_session_handle_t free_session = CRANKWAVE_INVALID_HANDLE;
    expect(crankwave_create_session(context, scenario, CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO,
                              &session_a) == CRANKWAVE_STATUS_OK &&
               crankwave_create_session(context, scenario,
                                  CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO,
                                  &session_b) == CRANKWAVE_STATUS_OK &&
               crankwave_create_session(context, free_scenario,
                                  CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO,
                                  &free_session) == CRANKWAVE_STATUS_OK &&
               session_a != stale,
           "generation-checked session slot was not recycled safely");
    crankwave_session_descriptor_t stale_descriptor{};
    expect(crankwave_session_get_descriptor(context, stale, &stale_descriptor) ==
               CRANKWAVE_STATUS_INVALID_HANDLE,
           "destroyed session handle remained usable");

    crankwave_session_descriptor_t descriptor{};
    constexpr auto kInertialDynoLiveControls =
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_THROTTLE |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_IGNITION_ENABLED |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_FUEL_ENABLED;
    expect(crankwave_session_get_descriptor(context, session_a, &descriptor) ==
                   CRANKWAVE_STATUS_OK &&
               descriptor.physics_frames_per_block == 400U &&
               descriptor.maximum_cycle_evidence_per_process_call == 400U &&
               descriptor.delivery_frames_per_block == 3840U &&
               descriptor.audio_bus_count == 8U &&
               descriptor.live_control_capabilities == kInertialDynoLiveControls &&
               descriptor.execution_kind == CRANKWAVE_SESSION_EXECUTION_FINITE_SCENARIO &&
               descriptor.motion_mode == CRANKWAVE_MOTION_INERTIAL_DYNO &&
               descriptor.forward_gear_count == 0U,
           "C session descriptor differs from the executable method");
    crankwave_session_descriptor_t free_descriptor{};
    constexpr auto kFreeEngineLiveControls =
        kInertialDynoLiveControls | CRANKWAVE_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE |
        CRANKWAVE_LIVE_CONTROL_CAPABILITY_STARTER_ENABLED;
    expect(crankwave_session_get_descriptor(context, free_session, &free_descriptor) ==
                   CRANKWAVE_STATUS_OK &&
               free_descriptor.live_control_capabilities == kFreeEngineLiveControls &&
               free_descriptor.motion_mode == CRANKWAVE_MOTION_FREE_ENGINE &&
               free_descriptor.forward_gear_count == 0U,
           "C free-engine descriptor lost its exact live-control capabilities");
    std::vector<char> engine_id(descriptor.engine_id_utf8_bytes + 1U);
    std::vector<char> scenario_id(descriptor.scenario_id_utf8_bytes + 1U);
    crankwave_session_identity_buffers_t identities{
        {engine_id.data(), engine_id.size()},
        {scenario_id.data(), scenario_id.size()},
    };
    expect(crankwave_session_copy_identity(context, session_a, &identities) ==
                   CRANKWAVE_STATUS_OK &&
               std::string_view{engine_id.data()} == "bmw-m52b28" &&
               std::string_view{scenario_id.data()} ==
                   "bmw-m52b28-inertial-dyno-1500-6500rpm",
           "session identity copy changed");

    const auto bus = audition_bus(context, session_a, descriptor.audio_bus_count);
    const auto first_live_frame =
        descriptor.preparation_block_count * descriptor.delivery_frames_per_block;
    const auto free_first_live_frame = free_descriptor.preparation_block_count *
                                       free_descriptor.delivery_frames_per_block;

    const crankwave_control_command_t preparation_control{
        0U, 1U, CRANKWAVE_CONTROL_THROTTLE, 0U, 0.5, 0U, 0U};
    crankwave_control_rejection_t rejection{};
    expect(crankwave_session_enqueue_controls(context, session_a, nullptr, 0U, &rejection) ==
                   CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C session admitted an empty live-control batch");
    expect(crankwave_session_enqueue_controls(context, session_a, &preparation_control, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION,
           "preparation control was not rejected with its typed reason");

    const crankwave_control_command_t unsupported_controls[] = {
        {first_live_frame, 1U, CRANKWAVE_CONTROL_THROTTLE, 0U, 0.5, 0U, 0U},
        {first_live_frame, 2U, CRANKWAVE_CONTROL_LIMITER_ENABLED, 1U, 0.0, 0U, 0U},
    };
    expect(crankwave_session_enqueue_controls(context, session_a, unsupported_controls, 2U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE &&
               rejection.command_index == 1U,
           "C capability rejection lost the first unsupported command index");

    const crankwave_control_command_t noncanonical_boolean{
        free_first_live_frame, 1U, CRANKWAVE_CONTROL_LIMITER_ENABLED, 1U, 0.5, 0U, 0U};
    expect(crankwave_session_enqueue_controls(context, free_session, &noncanonical_boolean,
                                        1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a boolean control with a scalar payload");
    const crankwave_control_command_t signed_zero_boolean{
        free_first_live_frame, 1U, CRANKWAVE_CONTROL_LIMITER_ENABLED, 1U, -0.0, 0U, 0U};
    expect(crankwave_session_enqueue_controls(context, free_session, &signed_zero_boolean, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted signed negative zero as canonical scalar zero");
    const crankwave_control_command_t noncanonical_scalar{
        free_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_EXTERNAL_RESISTING_TORQUE,
        1U,
        18.0,
        0U,
        0U};
    expect(crankwave_session_enqueue_controls(context, free_session, &noncanonical_scalar, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a scalar control with a boolean payload");
    const crankwave_control_command_t negative_resistance{
        free_first_live_frame,
        1U,
        CRANKWAVE_CONTROL_EXTERNAL_RESISTING_TORQUE,
        0U,
        -1.0,
        0U,
        0U};
    expect(crankwave_session_enqueue_controls(context, free_session, &negative_resistance, 1U,
                                        &rejection) == CRANKWAVE_STATUS_CONTROL_REJECTED &&
               rejection.code == CRANKWAVE_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted negative external resisting torque");

    enqueue_live_batch(context, session_a, first_live_frame, true);
    enqueue_live_batch(context, session_b, first_live_frame);
    enqueue_free_live_batch(context, free_session, free_first_live_frame);

    // A live session owns the shared immutable scenario/engine it needs.
    expect(crankwave_destroy_scenario(context, scenario) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_scenario(context, free_scenario) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_engine(context, engine) == CRANKWAVE_STATUS_OK,
           "compiled parent handles could not be released after session creation");

    std::vector<float> pcm_a(descriptor.delivery_frames_per_block);
    std::vector<float> pcm_b(descriptor.delivery_frames_per_block);
    crankwave_audio_copy_buffer_t short_buffer{bus, pcm_a.data(), pcm_a.size() - 1U, 99U};
    crankwave_session_telemetry_t telemetry_a{};
    crankwave_process_info_t process_a{};
    std::vector<crankwave_completed_cycle_evidence_t> cycle_evidence_a(
        descriptor.maximum_cycle_evidence_per_process_call);
    std::vector<crankwave_completed_cycle_evidence_t> cycle_evidence_b(
        descriptor.maximum_cycle_evidence_per_process_call);
    expect(crankwave_session_process(context, session_a, nullptr, 0U, &telemetry_a, 1U,
                               nullptr, 1U,
                               &process_a) == CRANKWAVE_STATUS_INVALID_ARGUMENT &&
               process_a.kind == 0U,
           "null cycle-evidence pointer with nonzero capacity was admitted");
    expect(crankwave_session_process(context, session_a, nullptr, 0U, &telemetry_a, 1U,
                               cycle_evidence_a.data(), cycle_evidence_a.size() - 1U,
                               &process_a) == CRANKWAVE_STATUS_BUFFER_TOO_SMALL &&
               process_a.kind == 0U,
           "short cycle-evidence buffer advanced or published a session block");
    expect(crankwave_session_process(context, session_a, &short_buffer, 1U, &telemetry_a, 1U,
                               nullptr, 0U,
                               &process_a) == CRANKWAVE_STATUS_BUFFER_TOO_SMALL &&
               short_buffer.samples_written == 0U,
           "short PCM buffer advanced or partially published a session block");

    crankwave_audio_copy_buffer_t audio_a{bus, pcm_a.data(), pcm_a.size(), 0U};
    crankwave_audio_copy_buffer_t audio_b{bus, pcm_b.data(), pcm_b.size(), 0U};
    crankwave_session_telemetry_t telemetry_b{};
    crankwave_process_info_t process_b{};
    allocation_probe::reject.store(true, std::memory_order_relaxed);
    const auto process_a_status = crankwave_session_process(
        context, session_a, &audio_a, 1U, &telemetry_a, 1U, cycle_evidence_a.data(),
        cycle_evidence_a.size(), &process_a);
    allocation_probe::reject.store(false, std::memory_order_relaxed);
    const auto process_b_status = crankwave_session_process(
        context, session_b, &audio_b, 1U, &telemetry_b, 1U, cycle_evidence_b.data(),
        cycle_evidence_b.size(), &process_b);
    expect(process_a_status == CRANKWAVE_STATUS_OK && process_b_status == CRANKWAVE_STATUS_OK,
           "C API could not process the first session block");
    expect(process_a.kind == CRANKWAVE_PROCESS_BLOCK &&
               process_a.block_phase == CRANKWAVE_BLOCK_PREPARATION &&
               process_a.block_ordinal == 0U && process_a.telemetry_written == 1U &&
               process_a.cycle_evidence_written == process_b.cycle_evidence_written &&
               process_a.block_ordinal == process_b.block_ordinal &&
               audio_a.samples_written == pcm_a.size() &&
               audio_b.samples_written == pcm_b.size(),
           "C API block metadata or copy extents changed");
    expect(std::memcmp(pcm_a.data(), pcm_b.data(), pcm_a.size() * sizeof(float)) == 0,
           "independent C sessions were not byte-stable");
    expect(telemetry_a.physics_step_end == telemetry_b.physics_step_end &&
               telemetry_a.mean_intake_manifold_pressure_pa_abs ==
                   telemetry_b.mean_intake_manifold_pressure_pa_abs &&
               std::isfinite(telemetry_a.mean_intake_manifold_pressure_pa_abs) &&
               telemetry_a.mean_intake_manifold_pressure_pa_abs > 0.0 &&
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
        crankwave_session_telemetry_t free_telemetry{};
        crankwave_process_info_t free_process{};
        expect(crankwave_session_process(context, free_session, nullptr, 0U, &free_telemetry,
                                   1U, nullptr, 0U, &free_process) == CRANKWAVE_STATUS_OK,
               "free-engine RPM trajectory session failed");
        expect(free_process.kind == CRANKWAVE_PROCESS_BLOCK,
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

    expect(crankwave_destroy_session(context, session_a) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_session(context, session_b) == CRANKWAVE_STATUS_OK &&
               crankwave_destroy_session(context, free_session) == CRANKWAVE_STATUS_OK,
           "C API baseline-session teardown failed");

    test_motion_contract_surface(context, repository_root);
    expect(crankwave_context_destroy(context) == CRANKWAVE_STATUS_OK,
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
        std::cerr << "C API integration test failed: " << error.what() << '\n';
        return 1;
    }
}
