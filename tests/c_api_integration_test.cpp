#include "engine_sim_offline/c_api.h"

#include <algorithm>
#include <atomic>
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
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.75, 0U},
        {first_live_frame, 2U, ESO_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U},
        {first_live_frame, 3U, ESO_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U},
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
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.75, 0U},
        {first_live_frame, 2U, ESO_CONTROL_IGNITION_ENABLED, 1U, 0.0, 0U},
        {first_live_frame, 3U, ESO_CONTROL_FUEL_ENABLED, 1U, 0.0, 0U},
        {first_live_frame, 4U, ESO_CONTROL_LIMITER_ENABLED, 0U, 0.0, 0U},
        {first_live_frame, 5U, ESO_CONTROL_EXTERNAL_RESISTING_TORQUE, 0U, 18.0, 0U},
    };
    eso_control_rejection_t rejection{};
    expect(eso_session_enqueue_controls(context, session, controls, 5U, &rejection) ==
                   ESO_STATUS_OK &&
               rejection.code == ESO_ERROR_NONE,
           "free-engine C session rejected an advertised live control");
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

    eso_session_handle_t stale = ESO_INVALID_HANDLE;
    expect(eso_create_session(context, scenario, &stale) == ESO_STATUS_OK &&
               eso_destroy_session(context, stale) == ESO_STATUS_OK,
           "throwaway session lifecycle failed");
    eso_session_handle_t session_a = ESO_INVALID_HANDLE;
    eso_session_handle_t session_b = ESO_INVALID_HANDLE;
    eso_session_handle_t free_session = ESO_INVALID_HANDLE;
    expect(eso_create_session(context, scenario, &session_a) == ESO_STATUS_OK &&
               eso_create_session(context, scenario, &session_b) == ESO_STATUS_OK &&
               eso_create_session(context, free_scenario, &free_session) ==
                   ESO_STATUS_OK &&
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
               descriptor.physics_frames_per_block == 200U &&
               descriptor.delivery_frames_per_block == 3840U &&
               descriptor.audio_bus_count == 8U &&
               descriptor.live_control_capabilities == kInertialDynoLiveControls,
           "C session descriptor differs from the executable method");
    eso_session_descriptor_t free_descriptor{};
    constexpr auto kFreeEngineLiveControls =
        kInertialDynoLiveControls | ESO_LIVE_CONTROL_CAPABILITY_LIMITER_ENABLED |
        ESO_LIVE_CONTROL_CAPABILITY_EXTERNAL_RESISTING_TORQUE;
    expect(eso_session_get_descriptor(context, free_session, &free_descriptor) ==
                   ESO_STATUS_OK &&
               free_descriptor.live_control_capabilities == kFreeEngineLiveControls,
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

    const eso_control_command_t preparation_control{0U, 1U,  ESO_CONTROL_THROTTLE,
                                                    0U, 0.5, 0U};
    eso_control_rejection_t rejection{};
    expect(eso_session_enqueue_controls(context, session_a, nullptr, 0U,
                                        &rejection) ==
                   ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C session admitted an empty live-control batch");
    expect(eso_session_enqueue_controls(context, session_a, &preparation_control, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION,
           "preparation control was not rejected with its typed reason");

    const eso_control_command_t unsupported_controls[] = {
        {first_live_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.5, 0U},
        {first_live_frame, 2U, ESO_CONTROL_LIMITER_ENABLED, 1U, 0.0, 0U},
    };
    expect(eso_session_enqueue_controls(context, session_a, unsupported_controls, 2U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_UNSUPPORTED_FOR_OPERATING_MODE &&
               rejection.command_index == 1U,
           "C capability rejection lost the first unsupported command index");

    const eso_control_command_t noncanonical_boolean{
        free_first_live_frame, 1U, ESO_CONTROL_LIMITER_ENABLED, 1U, 0.5, 0U};
    expect(eso_session_enqueue_controls(context, free_session, &noncanonical_boolean,
                                        1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a boolean control with a scalar payload");
    const eso_control_command_t signed_zero_boolean{
        free_first_live_frame, 1U, ESO_CONTROL_LIMITER_ENABLED, 1U, -0.0, 0U};
    expect(eso_session_enqueue_controls(context, free_session, &signed_zero_boolean, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted signed negative zero as canonical scalar zero");
    const eso_control_command_t noncanonical_scalar{
        free_first_live_frame, 1U, ESO_CONTROL_EXTERNAL_RESISTING_TORQUE, 1U, 18.0, 0U};
    expect(eso_session_enqueue_controls(context, free_session, &noncanonical_scalar, 1U,
                                        &rejection) == ESO_STATUS_CONTROL_REJECTED &&
               rejection.code == ESO_ERROR_CONTROL_INVALID_PAYLOAD,
           "C boundary admitted a scalar control with a boolean payload");
    const eso_control_command_t negative_resistance{
        free_first_live_frame, 1U, ESO_CONTROL_EXTERNAL_RESISTING_TORQUE, 0U, -1.0, 0U};
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
    eso_engine_telemetry_t telemetry_a{};
    eso_process_info_t process_a{};
    expect(eso_session_process(context, session_a, &short_buffer, 1U, &telemetry_a, 1U,
                               &process_a) == ESO_STATUS_BUFFER_TOO_SMALL &&
               short_buffer.samples_written == 0U,
           "short PCM buffer advanced or partially published a session block");

    eso_audio_copy_buffer_t audio_a{bus, pcm_a.data(), pcm_a.size(), 0U};
    eso_audio_copy_buffer_t audio_b{bus, pcm_b.data(), pcm_b.size(), 0U};
    eso_engine_telemetry_t telemetry_b{};
    eso_process_info_t process_b{};
    allocation_probe::reject.store(true, std::memory_order_relaxed);
    const auto process_a_status = eso_session_process(context, session_a, &audio_a, 1U,
                                                      &telemetry_a, 1U, &process_a);
    allocation_probe::reject.store(false, std::memory_order_relaxed);
    const auto process_b_status = eso_session_process(context, session_b, &audio_b, 1U,
                                                      &telemetry_b, 1U, &process_b);
    expect(process_a_status == ESO_STATUS_OK && process_b_status == ESO_STATUS_OK,
           "C API could not process the first session block");
    expect(process_a.kind == ESO_PROCESS_BLOCK &&
               process_a.block_phase == ESO_BLOCK_PREPARATION &&
               process_a.block_ordinal == 0U && process_a.telemetry_written == 1U &&
               process_a.block_ordinal == process_b.block_ordinal &&
               audio_a.samples_written == pcm_a.size() &&
               audio_b.samples_written == pcm_b.size(),
           "C API block metadata or copy extents changed");
    expect(std::memcmp(pcm_a.data(), pcm_b.data(), pcm_a.size() * sizeof(float)) == 0,
           "independent C sessions were not byte-stable");
    expect(telemetry_a.physics_step_end == telemetry_b.physics_step_end &&
               telemetry_a.engine_step_end_index == telemetry_b.engine_step_end_index &&
               telemetry_a.engine_speed_rpm == telemetry_b.engine_speed_rpm,
           "C telemetry copy diverged between deterministic sessions");

    expect(eso_destroy_session(context, session_a) == ESO_STATUS_OK &&
               eso_destroy_session(context, session_b) == ESO_STATUS_OK &&
               eso_destroy_session(context, free_session) == ESO_STATUS_OK &&
               eso_context_destroy(context) == ESO_STATUS_OK,
           "C API teardown failed");
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
