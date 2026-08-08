#include "engine_sim_offline/responsive/directional_texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    using namespace engine_sim_offline::responsive;

    constexpr std::size_t block_frames = 3840U;
    constexpr std::size_t block_count = 100U;
    std::vector<ResponsiveCaptureEndpoint> endpoints;
    endpoints.reserve(block_count + 1U);
    for (std::size_t block = 0U; block <= block_count; ++block) {
        const auto frame = static_cast<std::uint64_t>(block * block_frames);
        endpoints.push_back(ResponsiveCaptureEndpoint{
            .delivery_frame = frame,
            .engine_speed_rpm = 1000.0 + static_cast<double>(block) * 10.0,
            .mean_intake_manifold_pressure_pa_abs = 70000.0,
            .requested_throttle_01 = 0.2,
            .resolved_engine_throttle_01 = 0.2,
            .unwrapped_crank_revolutions = static_cast<double>(block) * 0.5,
            .state_flags = 3U,
        });
    }
    std::vector<float> pcm(block_count * block_frames + 1U);
    for (std::size_t frame = 0U; frame < pcm.size(); ++frame) {
        const auto revolutions = static_cast<double>(frame) / 7680.0;
        pcm[frame] = static_cast<float>(
            0.7 * std::sin(std::numbers::pi * revolutions) +
            0.2 * std::sin(2.0 * std::numbers::pi * revolutions));
    }

    const DirectionalCaptureView view{0U, pcm, endpoints};
    const auto first = transform_directional_capture(
        view, 1300.0, DirectionalSweepDirection::rising);
    expect(std::holds_alternative<DirectionalTextureTransform>(first),
           "valid rising capture transforms");
    if (const auto *result = std::get_if<DirectionalTextureTransform>(&first)) {
        expect(result->crossing_revolutions == 15.0,
               "crossing interpolation is exact");
        expect(result->source_cycle_begin_revolutions == 16.0,
               "source begins at the next 720-degree boundary");
        expect(result->source_cycle_end_revolutions == 22.0,
               "source contains exactly three cycles");
        expect(result->source.size() == 3U * 4096U &&
                   result->seam_closed.size() == result->source.size(),
               "source and closure have exact shapes");
        expect(std::abs(result->source.front()) < 1.0e-6F,
               "phase-normalized source starts at cycle origin");
        expect(result->telemetry.endpoint_count > 0U &&
                   result->telemetry.state_masks ==
                       std::vector<engine_sim_offline::EngineCycleStateFlagMask>{3U},
               "telemetry interval and state masks are retained");
        expect(result->closure.seam_over_source_adjacent_derivative_rms < 3.0,
               "closed seam meets the established derivative bound");

        const auto repeated = transform_directional_capture(
            view, 1300.0, DirectionalSweepDirection::rising);
        expect(std::holds_alternative<DirectionalTextureTransform>(repeated) &&
                   std::get<DirectionalTextureTransform>(repeated).seam_closed ==
                       result->seam_closed,
               "repeat transform is byte-stable at Float32 payload authority");
    }

    auto malformed_endpoints = endpoints;
    malformed_endpoints[4U].unwrapped_crank_revolutions =
        malformed_endpoints[3U].unwrapped_crank_revolutions;
    const auto malformed = transform_directional_capture(
        DirectionalCaptureView{0U, pcm, malformed_endpoints}, 1300.0,
        DirectionalSweepDirection::rising);
    expect(std::holds_alternative<DirectionalTransformError>(malformed),
           "non-monotone crank evidence fails closed");

    auto non_finite = pcm;
    non_finite[7U] = std::numeric_limits<float>::infinity();
    const auto invalid_pcm = transform_directional_capture(
        DirectionalCaptureView{0U, non_finite, endpoints}, 1300.0,
        DirectionalSweepDirection::rising);
    expect(std::holds_alternative<DirectionalTransformError>(invalid_pcm),
           "non-finite PCM fails closed");

    return failures == 0 ? 0 : 1;
}
