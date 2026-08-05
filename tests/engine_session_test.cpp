#include "bmw_m52b28_render_gate_support.hpp"

#include "engine_sim_offline/session.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

namespace allocation_probe {

std::size_t allocation_count = 0;
bool count_allocations = false;

} // namespace allocation_probe

void *operator new(std::size_t size) {
    if (allocation_probe::count_allocations) {
        ++allocation_probe::allocation_count;
    }
    if (void *allocation = std::malloc(size == 0U ? 1U : size)) {
        return allocation;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void *allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void *allocation) noexcept {
    ::operator delete(allocation);
}

void operator delete(void *allocation, std::size_t) noexcept {
    ::operator delete(allocation);
}

void operator delete[](void *allocation, std::size_t) noexcept {
    ::operator delete(allocation);
}

namespace {

namespace gate = engine_sim_offline::test::bmw_m52b28_render_gate;
using namespace engine_sim_offline;

[[nodiscard]] std::uint32_t read_u32le(std::span<const std::byte> bytes,
                                       std::size_t offset) {
    gate::expect(offset <= bytes.size() && bytes.size() - offset >= 4U,
                 "WAVE u32 read is out of range");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::span<const std::byte> wave_data(std::span<const std::byte> wave) {
    gate::expect(wave.size() >= 12U &&
                     std::string_view{reinterpret_cast<const char *>(wave.data()),
                                      4U} == "RIFF" &&
                     std::string_view{reinterpret_cast<const char *>(wave.data() + 8U),
                                      4U} == "WAVE",
                 "oracle is not a RIFF/WAVE container");
    std::size_t offset = 12U;
    while (offset <= wave.size() && wave.size() - offset >= 8U) {
        const std::string_view id{reinterpret_cast<const char *>(wave.data() + offset),
                                  4U};
        const auto size = static_cast<std::size_t>(read_u32le(wave, offset + 4U));
        const auto payload = offset + 8U;
        gate::expect(payload <= wave.size() && size <= wave.size() - payload,
                     "oracle WAVE chunk exceeds its container");
        if (id == "data") {
            return wave.subspan(payload, size);
        }
        offset = payload + size + (size & 1U);
    }
    throw std::runtime_error{"oracle WAVE has no data chunk"};
}

[[nodiscard]] std::string session_error_text(const EngineSessionError &error) {
    auto result = error.detail_code + ": " + error.message;
    if (error.simulation_failure.has_value()) {
        result += "; " + error.simulation_failure->detail_code + ": " +
                  error.simulation_failure->state_summary;
    }
    return result;
}

[[nodiscard]] EngineSession
require_session(const compile::CompiledScenario &scenario,
                const EngineSessionExecutionKind execution_kind =
                    EngineSessionExecutionKind::finite_scenario) {
    auto result = create_engine_session(scenario, execution_kind);
    if (const auto *error = std::get_if<EngineSessionError>(&result)) {
        throw std::runtime_error{"session creation failed: " +
                                 session_error_text(*error)};
    }
    return std::get<EngineSession>(std::move(result));
}

[[nodiscard]] const EngineAudioBusBlockView &
raw_master_bus(const EngineSessionBlockView &block) {
    const auto found = std::ranges::find(
        block.audio_buses(), EngineAudioBusKind::engine_raw_master,
        [](const auto &bus) { return bus.descriptor.kind; });
    if (found == block.audio_buses().end()) {
        throw std::runtime_error{"session block has no raw master"};
    }
    return *found;
}

void require_pcm_block(const EngineSessionBlockView &block,
                       std::span<const std::byte> oracle_pcm,
                       std::uint64_t audible_first_frame,
                       const presentation::MasteringSettings &mastering) {
    const auto samples = raw_master_bus(block).samples;
    gate::expect(samples.size() == kEngineSessionDeliveryFramesPerBlock,
                 "session raw-master bus has the wrong quantum");
    const auto byte_offset = static_cast<std::size_t>(audible_first_frame) * 3U;
    gate::expect(byte_offset <= oracle_pcm.size() &&
                     samples.size() * 3U <= oracle_pcm.size() - byte_offset,
                 "session block exceeds the oracle PCM interval");

    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const auto absolute = audible_first_frame + frame;
        const auto fade = presentation::audition_fade_gain(absolute, mastering);
        // This historical oracle predates the stateful audition master. Rebuild
        // its exact fixed-gain signal from the still-public raw master so the gate
        // continues to freeze the upstream source/conditioning/IR result without
        // falsely constraining the independently versioned listening dynamics.
        const float monitored = samples[frame] * 128.0F;
        const float faded = static_cast<float>(static_cast<double>(monitored) * fade);
        const auto quantized = presentation::quantize_pcm24(faded);
        const auto encoded = presentation::serialize_pcm24le(quantized.pcm24);
        const auto expected = oracle_pcm.subspan(byte_offset + frame * 3U, 3U);
        if (!std::equal(encoded.begin(), encoded.end(), expected.begin())) {
            throw std::runtime_error{"session raw master differs from the historical "
                                     "canonical 20 kHz oracle at audible frame " +
                                     std::to_string(absolute)};
        }
    }
}

[[nodiscard]] EngineSessionBlockView
require_next_block(EngineSession &session, const std::string_view context) {
    auto result = session.process_block();
    if (const auto *error = std::get_if<EngineSessionError>(&result)) {
        throw std::runtime_error{std::string{context} +
                                 " failed: " + session_error_text(*error)};
    }
    const auto *block = std::get_if<EngineSessionBlockView>(&result);
    if (block == nullptr) {
        throw std::runtime_error{std::string{context} +
                                 " completed before its expected block"};
    }
    return *block;
}

[[nodiscard]] EngineSessionBlockView
require_first_audible_block(EngineSession &session,
                            const EngineSessionDescriptor &descriptor,
                            const std::string_view context) {
    for (std::uint64_t block_ordinal = 0U;
         block_ordinal < descriptor.preparation_block_count; ++block_ordinal) {
        const auto block = require_next_block(session, context);
        gate::expect(block.block_ordinal() == block_ordinal &&
                         block.phase() == EngineSessionBlockPhase::preparation &&
                         block.telemetry().size() == 1U &&
                         !block.telemetry().front().held_dyno.has_value() &&
                         !block.telemetry().front().free_vehicle.has_value(),
                     "preparation exposed a released motion sidecar");
    }
    const auto block = require_next_block(session, context);
    gate::expect(
        block.block_ordinal() == descriptor.preparation_block_count &&
            block.phase() == EngineSessionBlockPhase::audible &&
            block.telemetry().size() == 1U,
        "session did not publish its first released block at the audible boundary");
    return block;
}

void run(const std::filesystem::path &repository_root) {
    const auto scenario = gate::compile_authored_scenario(repository_root);
    constexpr auto kInertialDynoLiveControls =
        kEngineLiveControlCapabilityThrottle |
        kEngineLiveControlCapabilityIgnitionEnabled |
        kEngineLiveControlCapabilityFuelEnabled;
    constexpr auto kFreeEngineLiveControls =
        kInertialDynoLiveControls | kEngineLiveControlCapabilityLimiterEnabled |
        kEngineLiveControlCapabilityExternalResistingTorque |
        kEngineLiveControlCapabilityStarterEnabled;
    constexpr auto kHeldDynoLiveControls =
        kInertialDynoLiveControls |
        kEngineLiveControlCapabilityHeldDynoTargetEngineSpeed |
        kEngineLiveControlCapabilityHeldDynoMaximumAbsorbingTorque |
        kEngineLiveControlCapabilityHeldDynoMaximumDrivingTorque;
    constexpr auto kFreeVehicleLiveControls =
        kInertialDynoLiveControls | kEngineLiveControlCapabilityLimiterEnabled |
        kEngineLiveControlCapabilityStarterEnabled |
        kEngineLiveControlCapabilityVehicleSelectedForwardGear |
        kEngineLiveControlCapabilityVehicleClutchEngagement |
        kEngineLiveControlCapabilityVehicleServiceBrakeApplication;

    // The complete simulation -> capture -> excitation -> presentation quantum
    // must use only session-owned bounded storage once construction is complete.
    // One unmeasured quantum permits no lazy hot-path setup to hide in the guard.
    auto allocation_session = require_session(scenario);
    auto warmup = allocation_session.process_block();
    gate::expect(std::holds_alternative<EngineSessionBlockView>(warmup),
                 "allocation-guard session did not produce its warm-up block");
    allocation_probe::allocation_count = 0;
    allocation_probe::count_allocations = true;
    auto allocation_guarded = allocation_session.process_block();
    allocation_probe::count_allocations = false;
    gate::expect(std::holds_alternative<EngineSessionBlockView>(allocation_guarded),
                 "allocation-guarded EngineSession quantum did not complete");
    gate::expect(allocation_probe::allocation_count == 0U,
                 "EngineSession allocated while processing one complete quantum");

    auto session = require_session(scenario);
    const auto descriptor = session.descriptor();
    gate::expect(
        descriptor.engine_id == "bmw-m52b28" &&
            descriptor.scenario_id == "bmw-m52b28-inertial-dyno-1500-6500rpm" &&
            descriptor.physics_rate == kEngineSessionPhysicsRateHz &&
            descriptor.delivery_rate == kEngineSessionDeliveryRateHz &&
            descriptor.physics_frames_per_block ==
                kEngineSessionPhysicsFramesPerBlock &&
            descriptor.delivery_frames_per_block ==
                kEngineSessionDeliveryFramesPerBlock &&
            descriptor.total_block_count == 1072U &&
            descriptor.preparation_block_count == 322U &&
            descriptor.audio_buses.size() == 8U &&
            descriptor.live_control_capabilities == kInertialDynoLiveControls &&
            descriptor.execution_kind == EngineSessionExecutionKind::finite_scenario &&
            descriptor.motion_mode == EngineMotionMode::inertial_dyno &&
            descriptor.forward_gears.empty() &&
            descriptor.capacities ==
                compile::CompiledSessionCapacities{3840U, 3800U, 1U},
        "session descriptor differs from the compiled BMW contract");

    const auto first_live_delivery_frame =
        descriptor.preparation_block_count * kEngineSessionDeliveryFramesPerBlock;
    auto capability_rejection_session = require_session(scenario);
    const auto empty_rejection = capability_rejection_session.enqueue_controls({});
    gate::expect(empty_rejection.has_value() &&
                     empty_rejection->code ==
                         EngineControlRejectionCode::invalid_payload,
                 "EngineSession admitted an empty live-control batch");
    const std::array unsupported_batch{
        EngineControlCommand{first_live_delivery_frame, 1U, SetEngineThrottle{0.5}},
        EngineControlCommand{first_live_delivery_frame, 2U,
                             SetEngineLimiterEnabled{true}},
        EngineControlCommand{first_live_delivery_frame, 3U, SetEngineFuelEnabled{true}},
    };
    const auto limiter_rejection =
        capability_rejection_session.enqueue_controls(unsupported_batch);
    gate::expect(limiter_rejection.has_value() &&
                     limiter_rejection->code ==
                         EngineControlRejectionCode::unsupported_for_operating_mode &&
                     limiter_rejection->command_index == 1U,
                 "inertial-dyno control capability rejection lost the command index");
    const EngineControlCommand unsupported_resistance{
        first_live_delivery_frame,
        1U,
        SetEngineExternalResistingTorque{12.0},
    };
    const auto resistance_rejection = capability_rejection_session.enqueue_controls(
        std::span{&unsupported_resistance, 1U});
    gate::expect(resistance_rejection.has_value() &&
                     resistance_rejection->code ==
                         EngineControlRejectionCode::unsupported_for_operating_mode &&
                     resistance_rejection->command_index == 0U,
                 "inertial dyno admitted external resisting-torque ownership");

    const auto free_scenario =
        gate::compile_authored_free_engine_scenario(repository_root);
    auto free_session = require_session(free_scenario);
    const auto free_descriptor = free_session.descriptor();
    gate::expect(
        free_descriptor.scenario_id == "bmw-m52b28-warm-running-free-rev-1500rpm" &&
            free_descriptor.live_control_capabilities == kFreeEngineLiveControls &&
            free_descriptor.motion_mode == EngineMotionMode::free_engine &&
            free_descriptor.forward_gears.empty(),
        "free-engine session did not advertise its exact live-control surface");
    const auto free_first_live_frame =
        free_descriptor.preparation_block_count * kEngineSessionDeliveryFramesPerBlock;
    const std::array free_controls{
        EngineControlCommand{free_first_live_frame, 1U, SetEngineThrottle{0.75}},
        EngineControlCommand{free_first_live_frame, 2U, SetEngineIgnitionEnabled{true}},
        EngineControlCommand{free_first_live_frame, 3U, SetEngineFuelEnabled{true}},
        EngineControlCommand{free_first_live_frame, 4U, SetEngineLimiterEnabled{false}},
        EngineControlCommand{free_first_live_frame, 5U,
                             SetEngineExternalResistingTorque{18.0}},
    };
    gate::expect(!free_session.enqueue_controls(free_controls).has_value(),
                 "free-engine session rejected an advertised live control");

    const auto starter_scenario =
        gate::compile_authored_bmw_m52tub28_cold_start_scenario(repository_root);
    auto starter_session = require_session(starter_scenario);
    const auto starter_descriptor = starter_session.descriptor();
    gate::expect(starter_descriptor.preparation_block_count == 0U &&
                     (starter_descriptor.live_control_capabilities &
                      kEngineLiveControlCapabilityStarterEnabled) != 0U,
                 "crank/catch session did not expose immediate starter control");
    auto live_starter_session = require_session(starter_scenario);
    const EngineControlCommand live_starter_release{
        0U,
        1U,
        SetEngineStarterEnabled{false},
    };
    gate::expect(
        !live_starter_session.enqueue_controls(std::span{&live_starter_release, 1U})
             .has_value(),
        "cranking-capable FreeEngine rejected its advertised starter control");
    const auto live_starter_result = live_starter_session.process_block();
    const auto *live_starter_block =
        std::get_if<EngineSessionBlockView>(&live_starter_result);
    gate::expect(live_starter_block != nullptr &&
                     !live_starter_block->telemetry().front().engine.starter_enabled,
                 "first-boundary starter release did not reach executed mechanics");

    bool observed_cranking = false;
    bool observed_ignition = false;
    bool observed_starter_release = false;
    for (std::uint64_t block_index = 0; block_index < 75U; ++block_index) {
        auto result = starter_session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&result)) {
            throw std::runtime_error{"starter session failed: " +
                                     session_error_text(*error)};
        }
        const auto *block = std::get_if<EngineSessionBlockView>(&result);
        gate::expect(block != nullptr && !block->telemetry().empty(),
                     "starter session completed before its crank/catch interval");
        const auto &engine = block->telemetry().front().engine;
        observed_cranking = observed_cranking ||
                            (engine.starter_enabled && engine.engine_speed_rpm > 100.0);
        observed_ignition =
            observed_ignition || (engine.ignition_enabled && engine.starter_enabled &&
                                  engine.engine_speed_rpm > 0.0);
        observed_starter_release =
            observed_starter_release ||
            (!engine.starter_enabled && engine.ignition_enabled &&
             engine.engine_speed_rpm > 0.0);
    }
    gate::expect(observed_cranking && observed_ignition && observed_starter_release,
                 "crank/catch session did not crank, energize ignition, and release "
                 "the starter while the engine remained rotating");

    auto rejected_open_dyno =
        create_engine_session(scenario, EngineSessionExecutionKind::open_ended);
    gate::expect(std::holds_alternative<EngineSessionError>(rejected_open_dyno),
                 "open-ended execution was admitted for a capture-only scenario");

    auto open_free_session =
        require_session(free_scenario, EngineSessionExecutionKind::open_ended);
    const auto open_descriptor = open_free_session.descriptor();
    gate::expect(
        open_descriptor.execution_kind == EngineSessionExecutionKind::open_ended &&
            open_descriptor.total_block_count == 0U &&
            open_descriptor.preparation_block_count ==
                free_descriptor.preparation_block_count &&
            open_descriptor.live_control_capabilities == kFreeEngineLiveControls,
        "open FreeEngine descriptor does not identify a continuous session");

    const auto held_dyno_scenario =
        gate::compile_authored_bmw_m52tub28_held_dyno_scenario(repository_root);
    auto held_dyno_session =
        require_session(held_dyno_scenario, EngineSessionExecutionKind::open_ended);
    const auto held_dyno_descriptor = held_dyno_session.descriptor();
    gate::expect(held_dyno_descriptor.motion_mode == EngineMotionMode::held_dyno &&
                     held_dyno_descriptor.live_control_capabilities ==
                         kHeldDynoLiveControls &&
                     held_dyno_descriptor.forward_gears.empty() &&
                     held_dyno_descriptor.execution_kind ==
                         EngineSessionExecutionKind::open_ended &&
                     held_dyno_descriptor.total_block_count == 0U,
                 "held-dyno descriptor did not expose its exact native contract");
    const auto held_dyno_first_live_frame =
        held_dyno_descriptor.preparation_block_count *
        kEngineSessionDeliveryFramesPerBlock;
    constexpr double kCommandedDynoTargetRpm = 1750.0;
    constexpr double kCommandedMaximumAbsorbingTorqueNm = 333.0;
    constexpr double kCommandedMaximumDrivingTorqueNm = 17.0;
    const std::array held_dyno_controls{
        EngineControlCommand{held_dyno_first_live_frame, 1U,
                             SetHeldDynoTargetEngineSpeed{kCommandedDynoTargetRpm}},
        EngineControlCommand{
            held_dyno_first_live_frame, 2U,
            SetHeldDynoMaximumAbsorbingTorque{kCommandedMaximumAbsorbingTorqueNm}},
        EngineControlCommand{
            held_dyno_first_live_frame, 3U,
            SetHeldDynoMaximumDrivingTorque{kCommandedMaximumDrivingTorqueNm}},
    };
    gate::expect(!held_dyno_session.enqueue_controls(held_dyno_controls).has_value(),
                 "held dyno rejected its same-frame target and limit commands");
    const auto held_dyno_live_block = require_first_audible_block(
        held_dyno_session, held_dyno_descriptor, "held-dyno session");
    const auto &held_dyno_telemetry = held_dyno_live_block.telemetry().front();
    gate::expect(
        held_dyno_telemetry.held_dyno.has_value() &&
            !held_dyno_telemetry.free_vehicle.has_value() &&
            held_dyno_telemetry.held_dyno->target_engine_speed_rpm ==
                kCommandedDynoTargetRpm &&
            held_dyno_telemetry.held_dyno->maximum_absorbing_torque_nm ==
                kCommandedMaximumAbsorbingTorqueNm &&
            held_dyno_telemetry.held_dyno->maximum_driving_torque_nm ==
                kCommandedMaximumDrivingTorqueNm &&
            std::isfinite(held_dyno_telemetry.held_dyno->required_actuator_torque_nm) &&
            std::isfinite(held_dyno_telemetry.held_dyno->applied_actuator_torque_nm),
        "held-dyno telemetry did not atomically reflect its same-frame commands");

    const auto free_vehicle_scenario =
        gate::compile_authored_bmw_m52tub28_free_vehicle_scenario(repository_root);
    auto free_vehicle_session =
        require_session(free_vehicle_scenario, EngineSessionExecutionKind::open_ended);
    const auto free_vehicle_descriptor = free_vehicle_session.descriptor();
    constexpr std::array<std::string_view, 5U> kExpectedGearIds{
        "gear-1", "gear-2", "gear-3", "gear-4", "gear-5"};
    constexpr std::array<double, 5U> kExpectedGearRatios{4.21, 2.49, 1.66, 1.24, 1.0};
    gate::expect(
        free_vehicle_descriptor.motion_mode == EngineMotionMode::free_vehicle &&
            free_vehicle_descriptor.live_control_capabilities ==
                kFreeVehicleLiveControls &&
            free_vehicle_descriptor.forward_gears.size() == kExpectedGearIds.size() &&
            free_vehicle_descriptor.execution_kind ==
                EngineSessionExecutionKind::open_ended &&
            free_vehicle_descriptor.total_block_count == 0U,
        "FreeVehicle descriptor did not expose its exact native contract");
    for (std::size_t index = 0U; index < kExpectedGearIds.size(); ++index) {
        const auto &gear = free_vehicle_descriptor.forward_gears[index];
        gate::expect(gear.authored_ordinal == index + 1U &&
                         gear.semantic_id == kExpectedGearIds[index] &&
                         gear.ratio == kExpectedGearRatios[index],
                     "FreeVehicle forward-gear descriptor order changed");
    }

    auto invalid_gear_session = require_session(free_vehicle_scenario);
    const auto free_vehicle_first_live_frame =
        free_vehicle_descriptor.preparation_block_count *
        kEngineSessionDeliveryFramesPerBlock;
    const EngineControlCommand invalid_gear_command{
        free_vehicle_first_live_frame,
        1U,
        SetVehicleSelectedForwardGear{6U},
    };
    const auto invalid_gear_rejection =
        invalid_gear_session.enqueue_controls(std::span{&invalid_gear_command, 1U});
    gate::expect(invalid_gear_rejection.has_value() &&
                     invalid_gear_rejection->code ==
                         EngineControlRejectionCode::invalid_payload &&
                     invalid_gear_rejection->command_index == 0U,
                 "FreeVehicle admitted a forward-gear ordinal outside its descriptor");

    constexpr std::uint32_t kCommandedForwardGearOrdinal = 2U;
    constexpr double kCommandedClutchEngagement = 0.0;
    constexpr double kCommandedServiceBrakeApplication = 0.5;
    const std::array free_vehicle_controls{
        EngineControlCommand{
            free_vehicle_first_live_frame, 1U,
            SetVehicleSelectedForwardGear{kCommandedForwardGearOrdinal}},
        EngineControlCommand{free_vehicle_first_live_frame, 2U,
                             SetVehicleClutchEngagement{kCommandedClutchEngagement}},
        EngineControlCommand{
            free_vehicle_first_live_frame, 3U,
            SetVehicleServiceBrakeApplication{kCommandedServiceBrakeApplication}},
    };
    gate::expect(
        !free_vehicle_session.enqueue_controls(free_vehicle_controls).has_value(),
        "FreeVehicle rejected its same-frame drivetrain commands");
    const auto free_vehicle_live_block = require_first_audible_block(
        free_vehicle_session, free_vehicle_descriptor, "FreeVehicle session");
    const auto &free_vehicle_telemetry = free_vehicle_live_block.telemetry().front();
    gate::expect(
        !free_vehicle_telemetry.held_dyno.has_value() &&
            free_vehicle_telemetry.free_vehicle.has_value() &&
            free_vehicle_telemetry.free_vehicle->selected_forward_gear_ordinal ==
                kCommandedForwardGearOrdinal &&
            free_vehicle_telemetry.free_vehicle->clutch_engagement_01 ==
                kCommandedClutchEngagement &&
            free_vehicle_telemetry.free_vehicle->service_brake_application_01 ==
                kCommandedServiceBrakeApplication &&
            free_vehicle_telemetry.free_vehicle->clutch_disposition ==
                EngineClutchDisposition::disengaged &&
            std::isfinite(free_vehicle_telemetry.free_vehicle->vehicle_speed_m_s) &&
            std::isfinite(free_vehicle_telemetry.free_vehicle->vehicle_distance_m),
        "FreeVehicle telemetry did not atomically reflect its drivetrain commands");

    auto finite_held_dyno = require_session(held_dyno_scenario);
    auto finite_free_vehicle = require_session(free_vehicle_scenario);
    gate::expect(finite_held_dyno.descriptor().execution_kind ==
                         EngineSessionExecutionKind::finite_scenario &&
                     finite_held_dyno.descriptor().total_block_count > 0U &&
                     finite_free_vehicle.descriptor().execution_kind ==
                         EngineSessionExecutionKind::finite_scenario &&
                     finite_free_vehicle.descriptor().total_block_count > 0U,
                 "finite authored dyno or drivetrain capture lost its exact horizon");

    auto authored_open_held =
        require_session(held_dyno_scenario, EngineSessionExecutionKind::open_ended);
    const auto authored_held_block =
        require_first_audible_block(authored_open_held, authored_open_held.descriptor(),
                                    "authored open held-dyno session");
    gate::expect(authored_held_block.telemetry().front().held_dyno.has_value(),
                 "open HeldDyno published no sidecar at its handoff");
    const auto authored_handoff_target =
        authored_held_block.telemetry().front().held_dyno->target_engine_speed_rpm;
    gate::expect(authored_handoff_target >= 1500.0 && authored_handoff_target < 1500.1,
                 "open HeldDyno did not retain its authored handoff target");
    const auto authored_held_next = authored_open_held.process_block();
    const auto *authored_held_next_block =
        std::get_if<EngineSessionBlockView>(&authored_held_next);
    gate::expect(
        authored_held_next_block != nullptr &&
            authored_held_next_block->telemetry().front().held_dyno.has_value() &&
            authored_held_next_block->telemetry()
                    .front()
                    .held_dyno->target_engine_speed_rpm == authored_handoff_target,
        "open HeldDyno did not hold its target after the authored handoff");

    auto authored_open_vehicle =
        require_session(free_vehicle_scenario, EngineSessionExecutionKind::open_ended);
    auto authored_vehicle_block = require_first_audible_block(
        authored_open_vehicle, authored_open_vehicle.descriptor(),
        "authored open FreeVehicle session");
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        const auto next = authored_open_vehicle.process_block();
        const auto *block = std::get_if<EngineSessionBlockView>(&next);
        gate::expect(block != nullptr,
                     "open FreeVehicle completed after its audible handoff");
        authored_vehicle_block = *block;
    }
    const auto &authored_vehicle_telemetry = authored_vehicle_block.telemetry().front();
    gate::expect(
        authored_vehicle_telemetry.engine.requested_throttle_01 == 0.1 &&
            authored_vehicle_telemetry.free_vehicle.has_value() &&
            !authored_vehicle_telemetry.free_vehicle->selected_forward_gear_ordinal
                 .has_value() &&
            authored_vehicle_telemetry.free_vehicle->clutch_engagement_01 == 0.0 &&
            authored_vehicle_telemetry.free_vehicle->service_brake_application_01 ==
                1.0,
        "open FreeVehicle executed finite-procedure events after the authored "
        "handoff");

    const auto command_beyond_authored_horizon =
        (free_descriptor.total_block_count + 10U) *
        kEngineSessionDeliveryFramesPerBlock;
    const EngineControlCommand open_future_command{
        command_beyond_authored_horizon,
        1U,
        SetEngineThrottle{0.2},
    };
    gate::expect(
        !open_free_session.enqueue_controls(std::span{&open_future_command, 1U})
             .has_value(),
        "open FreeEngine rejected a control beyond the authored recording horizon");

    constexpr auto maximum_open_block_count =
        std::numeric_limits<std::uint64_t>::max() /
        kEngineSessionDeliveryFramesPerBlock;
    constexpr auto maximum_open_delivery_end =
        maximum_open_block_count * kEngineSessionDeliveryFramesPerBlock;
    constexpr auto delivery_frames_spanning_one_physics_step =
        (kEngineSessionDeliveryFramesPerBlock + kEngineSessionPhysicsFramesPerBlock -
         1U) /
        kEngineSessionPhysicsFramesPerBlock;
    constexpr auto last_open_executable_delivery_frame =
        maximum_open_delivery_end - delivery_frames_spanning_one_physics_step;
    const EngineControlCommand last_open_executable_command{
        last_open_executable_delivery_frame,
        2U,
        SetEngineThrottle{0.2},
    };
    gate::expect(
        !open_free_session
             .enqueue_controls(std::span{&last_open_executable_command, 1U})
             .has_value(),
        "open FreeEngine rejected its last causally executable control timestamp");
    const EngineControlCommand first_open_unexecutable_command{
        last_open_executable_delivery_frame + 1U,
        3U,
        SetEngineThrottle{0.2},
    };
    const auto open_clock_rejection = open_free_session.enqueue_controls(
        std::span{&first_open_unexecutable_command, 1U});
    gate::expect(open_clock_rejection.has_value() &&
                     open_clock_rejection->code ==
                         EngineControlRejectionCode::outside_session_horizon,
                 "open FreeEngine accepted a control its block clock can never reach");

    const auto open_block_count = free_descriptor.total_block_count + 12U;
    for (std::uint64_t block_index = 0; block_index < open_block_count; ++block_index) {
        auto open_result = open_free_session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&open_result)) {
            throw std::runtime_error{"open FreeEngine session failed: " +
                                     session_error_text(*error)};
        }
        gate::expect(std::holds_alternative<EngineSessionBlockView>(open_result),
                     "open FreeEngine completed at the authored recording horizon");
        const auto &block = std::get<EngineSessionBlockView>(open_result);
        gate::expect(block.block_ordinal() == block_index,
                     "open FreeEngine block clock became discontinuous");
        if (block_index == open_descriptor.preparation_block_count + 30U) {
            gate::expect(block.telemetry().front().engine.requested_throttle_01 == 0.1,
                         "open FreeEngine executed the finite recipe's post-release "
                         "free-rev trajectory");
        }
    }

    const auto oracle = gate::read_bytes(
        repository_root /
        "reference/oracles/bmw-m52b28/"
        "bmw-m52b28-canonical-flow-coupled-20khz-4eafff8-dyno-1500-6500rpm.wav");
    const auto pcm = wave_data(oracle);
    constexpr std::uint64_t kAudibleFrames = UINT64_C(2880000);
    gate::expect(pcm.size() == kAudibleFrames * 3U,
                 "canonical 20 kHz oracle PCM extent changed");
    const presentation::MasteringSettings mastering{
        kAudibleFrames,
        kEngineSessionDeliveryFramesPerBlock,
        kEngineSessionDeliveryFramesPerBlock,
        1.0F,
    };

    std::uint64_t preparation_blocks = 0;
    std::uint64_t audible_blocks = 0;
    std::uint64_t audible_frames = 0;
    std::uint64_t completed_cycle_count = 0;
    std::optional<std::int64_t> preceding_cycle_end_ordinal;
    while (true) {
        auto result = session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&result)) {
            throw std::runtime_error{"session processing failed: " +
                                     session_error_text(*error)};
        }
        if (const auto *completed = std::get_if<EngineSessionCompleted>(&result)) {
            gate::expect(completed->physics_frame_count == 428800U &&
                             completed->delivery_frame_count == 4116480U &&
                             completed->block_count == 1072U &&
                             !completed->live_controls_accepted &&
                             !completed->held_speed_operating_point.has_value() &&
                             completed->inertial_dyno.has_value(),
                         "session completion evidence differs from the BMW horizon");
            break;
        }

        const auto &block = std::get<EngineSessionBlockView>(result);
        gate::expect(
            block.block_ordinal() == preparation_blocks + audible_blocks &&
                block.first_physics_frame() ==
                    block.block_ordinal() * kEngineSessionPhysicsFramesPerBlock &&
                block.first_delivery_frame() ==
                    block.block_ordinal() * kEngineSessionDeliveryFramesPerBlock &&
                block.physics_frame_count() == kEngineSessionPhysicsFramesPerBlock &&
                block.delivery_frame_count() == kEngineSessionDeliveryFramesPerBlock &&
                block.telemetry().size() == 1U &&
                block.telemetry().front().physics_step_end ==
                    block.first_physics_frame() + kEngineSessionPhysicsFramesPerBlock &&
                std::isfinite(
                    block.telemetry().front().mean_intake_manifold_pressure_pa_abs) &&
                block.telemetry().front().mean_intake_manifold_pressure_pa_abs > 0.0,
            "session block clocks or telemetry diverged");

        for (const auto &cycle : block.cycle_evidence()) {
            gate::expect(
                cycle.end_boundary.cycle_ordinal ==
                        cycle.start_boundary.cycle_ordinal + 1 &&
                    cycle.end_boundary.right_physics_frame >=
                        block.first_physics_frame() &&
                    cycle.end_boundary.right_physics_frame <
                        block.first_physics_frame() + block.physics_frame_count() &&
                    (!preceding_cycle_end_ordinal.has_value() ||
                     cycle.start_boundary.cycle_ordinal ==
                         *preceding_cycle_end_ordinal),
                "session cycle evidence was partial, duplicated, or published by "
                "the wrong block");
            preceding_cycle_end_ordinal = cycle.end_boundary.cycle_ordinal;
            ++completed_cycle_count;
        }

        if (block.phase() == EngineSessionBlockPhase::preparation) {
            ++preparation_blocks;
        } else {
            require_pcm_block(block, pcm, audible_frames, mastering);
            audible_frames += block.delivery_frame_count();
            ++audible_blocks;
        }
    }
    gate::expect(preparation_blocks == 322U && audible_blocks == 750U &&
                     audible_frames == kAudibleFrames && completed_cycle_count > 0U,
                 "session preparation/audible partition changed");

    // Completion is stable and does not advance any clock.
    const auto repeated = session.process_block();
    gate::expect(std::holds_alternative<EngineSessionCompleted>(repeated),
                 "session completion was not stable");

    // A timestamp must project to a physics step the finite session will actually
    // execute. The last few delivery frames can lie beyond that causal boundary.
    auto horizon_controlled = require_session(scenario);
    const auto terminal_physics_frame =
        descriptor.total_block_count * kEngineSessionPhysicsFramesPerBlock;
    const auto last_executable_delivery_frame =
        ((terminal_physics_frame - 1U) * kEngineSessionDeliveryRateHz.numerator) /
        kEngineSessionPhysicsRateHz.numerator;
    const EngineControlCommand last_executable_command{
        last_executable_delivery_frame,
        1U,
        SetEngineThrottle{0.5},
    };
    gate::expect(
        !horizon_controlled.enqueue_controls(std::span{&last_executable_command, 1U})
             .has_value(),
        "last causally executable live-control timestamp was rejected");
    const EngineControlCommand first_unexecutable_command{
        last_executable_delivery_frame + 1U,
        2U,
        SetEngineThrottle{0.5},
    };
    const auto first_unexecutable_rejection =
        horizon_controlled.enqueue_controls(std::span{&first_unexecutable_command, 1U});
    gate::expect(first_unexecutable_rejection.has_value() &&
                     first_unexecutable_rejection->code ==
                         EngineControlRejectionCode::outside_session_horizon,
                 "first causally unexecutable live-control timestamp was not rejected");

    // Preparation is evidence-producing authored state, so live controls begin
    // at the first audible frame rather than silently falsifying that evidence.
    auto controlled = require_session(scenario);
    const EngineControlCommand preparation_command{
        0U,
        1U,
        SetEngineThrottle{0.5},
    };
    const auto preparation_rejection =
        controlled.enqueue_controls(std::span{&preparation_command, 1U});
    gate::expect(preparation_rejection.has_value() &&
                     preparation_rejection->code ==
                         EngineControlRejectionCode::unavailable_during_preparation,
                 "live control inside held preparation was not rejected");

    const EngineControlCommand command{
        descriptor.preparation_block_count * kEngineSessionDeliveryFramesPerBlock,
        2U,
        SetEngineThrottle{0.5},
    };
    gate::expect(!controlled.enqueue_controls(std::span{&command, 1U}).has_value(),
                 "valid first-audible throttle command was rejected");

    EngineSessionBlockView controlled_block = [&]() {
        for (std::uint64_t block_index = 0;
             block_index <= descriptor.preparation_block_count; ++block_index) {
            auto controlled_result = controlled.process_block();
            if (const auto *error =
                    std::get_if<EngineSessionError>(&controlled_result)) {
                throw std::runtime_error{"controlled session failed: " +
                                         session_error_text(*error)};
            }
            if (block_index == descriptor.preparation_block_count) {
                return std::get<EngineSessionBlockView>(controlled_result);
            }
        }
        throw std::runtime_error{"controlled session did not reach audible output"};
    }();
    gate::expect(controlled_block.phase() == EngineSessionBlockPhase::audible,
                 "first live control did not coincide with the first audible block");
    gate::expect(controlled_block.telemetry().front().engine.requested_throttle_01 ==
                     0.5,
                 "first-audible throttle command did not reach executed mechanics");

    EngineSessionCompleted controlled_completion;
    while (true) {
        auto controlled_result = controlled.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&controlled_result)) {
            throw std::runtime_error{"controlled session failed: " +
                                     session_error_text(*error)};
        }
        if (const auto *completed =
                std::get_if<EngineSessionCompleted>(&controlled_result)) {
            controlled_completion = *completed;
            break;
        }
    }
    gate::expect(controlled_completion.live_controls_accepted &&
                     !controlled_completion.held_speed_operating_point.has_value() &&
                     !controlled_completion.inertial_dyno.has_value(),
                 "live-controlled session exposed evidence bound only to the authored "
                 "trajectory");

    const EngineControlCommand completed_command{
        descriptor.preparation_block_count * kEngineSessionDeliveryFramesPerBlock,
        1U,
        SetEngineThrottle{0.5},
    };
    const auto completed_rejection =
        session.enqueue_controls(std::span{&completed_command, 1U});
    gate::expect(completed_rejection.has_value() &&
                     completed_rejection->code ==
                         EngineControlRejectionCode::session_terminal,
                 "completed session accepted a live control it can never execute");
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        gate::expect(argc == 2, "usage: engine_session_test <repository-root>");
        run(std::filesystem::canonical(argv[1]));
    } catch (const std::exception &error) {
        std::cerr << "EngineSession test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
