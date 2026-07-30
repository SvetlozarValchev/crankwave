#include "bmw_m52b28_render_gate_support.hpp"

#include "engine_sim_offline/session.hpp"
#include "presentation/mastering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
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

[[nodiscard]] std::span<const std::byte>
wave_data(std::span<const std::byte> wave) {
    gate::expect(wave.size() >= 12U &&
                     std::string_view{
                         reinterpret_cast<const char *>(wave.data()), 4U} ==
                         "RIFF" &&
                     std::string_view{
                         reinterpret_cast<const char *>(wave.data() + 8U), 4U} ==
                         "WAVE",
                 "oracle is not a RIFF/WAVE container");
    std::size_t offset = 12U;
    while (offset <= wave.size() && wave.size() - offset >= 8U) {
        const std::string_view id{
            reinterpret_cast<const char *>(wave.data() + offset), 4U};
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
require_session(const compile::CompiledScenario &scenario) {
    auto result = create_engine_session(scenario);
    if (const auto *error = std::get_if<EngineSessionError>(&result)) {
        throw std::runtime_error{"session creation failed: " +
                                 session_error_text(*error)};
    }
    return std::get<EngineSession>(std::move(result));
}

[[nodiscard]] const EngineAudioBusBlockView &
audition_bus(const EngineSessionBlockView &block) {
    const auto found = std::ranges::find(
        block.audio_buses(), EngineAudioBusKind::engine_audition_master,
        [](const auto &bus) { return bus.descriptor.kind; });
    if (found == block.audio_buses().end()) {
        throw std::runtime_error{"session block has no audition master"};
    }
    return *found;
}

void require_pcm_block(
    const EngineSessionBlockView &block, std::span<const std::byte> oracle_pcm,
    std::uint64_t audible_first_frame,
    const presentation::MasteringSettings &mastering) {
    const auto samples = audition_bus(block).samples;
    gate::expect(samples.size() == kEngineSessionDeliveryFramesPerBlock,
                 "session audition bus has the wrong quantum");
    const auto byte_offset =
        static_cast<std::size_t>(audible_first_frame) * 3U;
    gate::expect(byte_offset <= oracle_pcm.size() &&
                     samples.size() * 3U <= oracle_pcm.size() - byte_offset,
                 "session block exceeds the oracle PCM interval");

    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const auto absolute = audible_first_frame + frame;
        const auto fade = presentation::audition_fade_gain(absolute, mastering);
        const float faded =
            static_cast<float>(static_cast<double>(samples[frame]) * fade);
        const auto quantized = presentation::quantize_pcm24(faded);
        const auto encoded =
            presentation::serialize_pcm24le(quantized.pcm24);
        const auto expected = oracle_pcm.subspan(byte_offset + frame * 3U, 3U);
        if (!std::equal(encoded.begin(), encoded.end(), expected.begin())) {
            throw std::runtime_error{
                "session PCM differs from the accepted oracle at audible frame " +
                std::to_string(absolute)};
        }
    }
}

void run(const std::filesystem::path &repository_root) {
    const auto scenario = gate::compile_authored_scenario(repository_root);

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
            descriptor.scenario_id ==
                "bmw-m52b28-inertial-dyno-1500-6500rpm" &&
            descriptor.physics_rate == kEngineSessionPhysicsRateHz &&
            descriptor.delivery_rate == kEngineSessionDeliveryRateHz &&
            descriptor.physics_frames_per_block ==
                kEngineSessionPhysicsFramesPerBlock &&
            descriptor.delivery_frames_per_block ==
                kEngineSessionDeliveryFramesPerBlock &&
            descriptor.total_block_count == 1072U &&
            descriptor.preparation_block_count == 322U &&
            descriptor.audio_buses.size() == 8U &&
            descriptor.accepts_live_controls &&
            descriptor.capacities ==
                compile::CompiledSessionCapacities{3840U, 3800U, 1U},
        "session descriptor differs from the compiled BMW contract");

    const auto oracle = gate::read_bytes(
        repository_root /
        "reference/oracles/bmw-m52b28/"
        "bmw-m52b28-last-good-ffcc45c-dyno-1500-6500rpm.wav");
    const auto pcm = wave_data(oracle);
    constexpr std::uint64_t kAudibleFrames = UINT64_C(2880000);
    gate::expect(pcm.size() == kAudibleFrames * 3U,
                 "accepted oracle PCM extent changed");
    const presentation::MasteringSettings mastering{
        kAudibleFrames,
        kEngineSessionDeliveryFramesPerBlock,
        kEngineSessionDeliveryFramesPerBlock,
        128.0F,
    };

    std::uint64_t preparation_blocks = 0;
    std::uint64_t audible_blocks = 0;
    std::uint64_t audible_frames = 0;
    while (true) {
        auto result = session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&result)) {
            throw std::runtime_error{"session processing failed: " +
                                     session_error_text(*error)};
        }
        if (const auto *completed =
                std::get_if<EngineSessionCompleted>(&result)) {
            gate::expect(
                completed->physics_frame_count == 214400U &&
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
                    block.block_ordinal() *
                        kEngineSessionPhysicsFramesPerBlock &&
                block.first_delivery_frame() ==
                    block.block_ordinal() *
                        kEngineSessionDeliveryFramesPerBlock &&
                block.physics_frame_count() ==
                    kEngineSessionPhysicsFramesPerBlock &&
                block.delivery_frame_count() ==
                    kEngineSessionDeliveryFramesPerBlock &&
                block.telemetry().size() == 1U &&
                block.telemetry().front().physics_step_end ==
                    block.first_physics_frame() +
                        kEngineSessionPhysicsFramesPerBlock,
            "session block clocks or telemetry diverged");

        if (block.phase() == EngineSessionBlockPhase::preparation) {
            ++preparation_blocks;
        } else {
            require_pcm_block(block, pcm, audible_frames, mastering);
            audible_frames += block.delivery_frame_count();
            ++audible_blocks;
        }
    }
    gate::expect(preparation_blocks == 322U && audible_blocks == 750U &&
                     audible_frames == kAudibleFrames,
                 "session preparation/audible partition changed");

    // Completion is stable and does not advance any clock.
    const auto repeated = session.process_block();
    gate::expect(std::holds_alternative<EngineSessionCompleted>(repeated),
                 "session completion was not stable");

    // A timestamp must project to a physics step the finite session will actually
    // execute. The last few delivery frames can lie beyond that causal boundary.
    auto horizon_controlled = require_session(scenario);
    const auto terminal_physics_frame =
        descriptor.total_block_count *
        kEngineSessionPhysicsFramesPerBlock;
    const auto last_executable_delivery_frame =
        ((terminal_physics_frame - 1U) *
         kEngineSessionDeliveryRateHz.numerator) /
        kEngineSessionPhysicsRateHz.numerator;
    const EngineControlCommand last_executable_command{
        last_executable_delivery_frame,
        1U,
        SetEngineThrottle{0.5},
    };
    gate::expect(
        !horizon_controlled
             .enqueue_controls(
                 std::span{&last_executable_command, 1U})
             .has_value(),
        "last causally executable live-control timestamp was rejected");
    const EngineControlCommand first_unexecutable_command{
        last_executable_delivery_frame + 1U,
        2U,
        SetEngineThrottle{0.5},
    };
    const auto first_unexecutable_rejection =
        horizon_controlled.enqueue_controls(
            std::span{&first_unexecutable_command, 1U});
    gate::expect(
        first_unexecutable_rejection.has_value() &&
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
    gate::expect(
        preparation_rejection.has_value() &&
            preparation_rejection->code ==
                EngineControlRejectionCode::unavailable_during_preparation,
        "live control inside held preparation was not rejected");

    const EngineControlCommand command{
        descriptor.preparation_block_count *
            kEngineSessionDeliveryFramesPerBlock,
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
    gate::expect(
        controlled_block.phase() == EngineSessionBlockPhase::audible,
        "first live control did not coincide with the first audible block");
    gate::expect(
        controlled_block.telemetry().front().engine.requested_throttle_01 == 0.5,
        "first-audible throttle command did not reach executed mechanics");

    EngineSessionCompleted controlled_completion;
    while (true) {
        auto controlled_result = controlled.process_block();
        if (const auto *error =
                std::get_if<EngineSessionError>(&controlled_result)) {
            throw std::runtime_error{"controlled session failed: " +
                                     session_error_text(*error)};
        }
        if (const auto *completed =
                std::get_if<EngineSessionCompleted>(&controlled_result)) {
            controlled_completion = *completed;
            break;
        }
    }
    gate::expect(
        controlled_completion.live_controls_accepted &&
            !controlled_completion.held_speed_operating_point.has_value() &&
            !controlled_completion.inertial_dyno.has_value(),
        "live-controlled session exposed evidence bound only to the authored "
        "trajectory");

    const EngineControlCommand completed_command{
        descriptor.preparation_block_count *
            kEngineSessionDeliveryFramesPerBlock,
        1U,
        SetEngineThrottle{0.5},
    };
    const auto completed_rejection =
        session.enqueue_controls(std::span{&completed_command, 1U});
    gate::expect(
        completed_rejection.has_value() &&
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
