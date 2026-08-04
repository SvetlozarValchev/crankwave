#include "bmw_m52b28_render_gate_support.hpp"
#include "package/package_source_capture.hpp"

#include "engine_sim_offline/session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace gate = engine_sim_offline::test::bmw_m52b28_render_gate;
using namespace engine_sim_offline;
using namespace engine_sim_offline::package_detail;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] PackageSourceLaneCapture
require_capture(PackageSourceCaptureResult result) {
    if (const auto *error = std::get_if<PackageSourceCaptureError>(&result)) {
        throw std::runtime_error{"source capture failed: " + error->detail_code +
                                 ": " + error->message};
    }
    return std::get<PackageSourceLaneCapture>(std::move(result));
}

[[nodiscard]] const PackageSourceCaptureError &
require_error(const PackageSourceCaptureResult &result,
              const PackageSourceCaptureErrorCode expected) {
    const auto *error = std::get_if<PackageSourceCaptureError>(&result);
    expect(error != nullptr && error->code == expected,
           "invalid source capture request returned the wrong result");
    return *error;
}

[[nodiscard]] EngineSession require_session(
    const compile::CompiledScenario &scenario) {
    auto result = create_engine_session(
        scenario, EngineSessionExecutionKind::finite_scenario);
    if (const auto *error = std::get_if<EngineSessionError>(&result)) {
        throw std::runtime_error{"direct session creation failed: " +
                                 error->detail_code + ": " + error->message};
    }
    return std::get<EngineSession>(std::move(result));
}

struct DirectAudibleCapture {
    std::uint64_t first_delivery_frame = 0U;
    std::uint64_t frame_count = 0U;
    std::vector<std::vector<float>> buses;
};

[[nodiscard]] DirectAudibleCapture direct_capture(
    const compile::CompiledScenario &scenario,
    const std::span<const std::string_view> bus_ids) {
    auto session = require_session(scenario);
    const auto descriptor = session.descriptor();
    DirectAudibleCapture result;
    result.first_delivery_frame =
        descriptor.preparation_block_count * descriptor.delivery_frames_per_block;
    result.buses.resize(bus_ids.size());

    while (true) {
        auto processed = session.process_block();
        if (const auto *error = std::get_if<EngineSessionError>(&processed)) {
            throw std::runtime_error{"direct session processing failed: " +
                                     error->detail_code + ": " + error->message};
        }
        if (std::holds_alternative<EngineSessionCompleted>(processed)) {
            break;
        }
        const auto &block = std::get<EngineSessionBlockView>(processed);
        if (block.phase() != EngineSessionBlockPhase::audible) {
            continue;
        }
        result.frame_count += block.delivery_frame_count();
        for (std::size_t index = 0; index < bus_ids.size(); ++index) {
            const auto found = std::ranges::find(
                block.audio_buses(), bus_ids[index],
                [](const EngineAudioBusBlockView &bus) { return bus.descriptor.id; });
            expect(found != block.audio_buses().end(),
                   "direct session omitted a selected bus");
            result.buses[index].insert(result.buses[index].end(),
                                       found->samples.begin(), found->samples.end());
        }
    }
    return result;
}

[[nodiscard]] bool byte_equal(const std::vector<float> &left,
                              const std::vector<float> &right) noexcept {
    return left.size() == right.size() &&
           std::memcmp(left.data(), right.data(),
                       left.size() * sizeof(float)) == 0;
}

void run(const std::filesystem::path &repository_root) {
    const auto scenario =
        gate::compile_authored_free_engine_scenario(repository_root);
    constexpr std::array<std::string_view, 2> kSelectedBuses{
        "master.engine.audition",
        "master.engine.raw",
    };

    const auto direct = direct_capture(scenario, kSelectedBuses);
    const auto capture = require_capture(
        capture_package_source_lane(scenario, kSelectedBuses));

    expect(capture.engine_id == "bmw-m52b28" &&
               capture.scenario_id == scenario.id() &&
               capture.sample_rate == kEngineSessionDeliveryRateHz &&
               capture.audible_first_delivery_frame ==
                   direct.first_delivery_frame &&
               capture.audible_delivery_frame_count == direct.frame_count &&
               capture.buses.size() == kSelectedBuses.size(),
           "source capture lost finite-session identity or audible extent");
    for (std::size_t index = 0; index < kSelectedBuses.size(); ++index) {
        expect(capture.buses[index].id == kSelectedBuses[index] &&
                   capture.buses[index].channel_count == 1U &&
                   capture.buses[index].sample_rate ==
                       kEngineSessionDeliveryRateHz &&
                   byte_equal(capture.buses[index].samples,
                              direct.buses[index]),
               "source capture changed selected-bus order, format, or Float32 PCM bytes");
    }

    expect(!capture.usable_cycles.empty() &&
               capture.usable_cycles.size() ==
                   capture.usable_cycle_lane_boundaries.size(),
           "source capture returned no complete audible 720-degree units");
    double preceding_end = -1.0;
    for (std::size_t index = 0; index < capture.usable_cycles.size(); ++index) {
        const auto &cycle = capture.usable_cycles[index];
        const auto &lane = capture.usable_cycle_lane_boundaries[index];
        const auto absolute_start = cycle.start_boundary.delivery_frame;
        const auto absolute_end = cycle.end_boundary.delivery_frame;
        expect(std::isfinite(lane.lane_relative_start_delivery_frame) &&
                   std::isfinite(lane.lane_relative_end_delivery_frame) &&
                   lane.lane_relative_start_delivery_frame ==
                       absolute_start -
                           static_cast<double>(
                               capture.audible_first_delivery_frame) &&
                   lane.lane_relative_end_delivery_frame ==
                       absolute_end -
                           static_cast<double>(
                               capture.audible_first_delivery_frame) &&
                   lane.lane_relative_start_delivery_frame >= 0.0 &&
                   lane.lane_relative_end_delivery_frame <=
                       static_cast<double>(
                           capture.audible_delivery_frame_count) &&
                   lane.lane_relative_start_delivery_frame >= preceding_end,
               "source capture shifted, truncated, or reordered exact cycle evidence");
        preceding_end = lane.lane_relative_end_delivery_frame;
    }

    constexpr std::array<std::string_view, 2> kDuplicateBuses{
        "master.engine.raw",
        "master.engine.raw",
    };
    const auto duplicate =
        capture_package_source_lane(scenario, kDuplicateBuses);
    expect(require_error(duplicate,
                         PackageSourceCaptureErrorCode::invalid_bus_selection)
               .detail_code == "package-source-duplicate-bus-id",
           "source capture did not reject a duplicate bus request before running");

    constexpr std::array<std::string_view, 1> kMissingBus{
        "master.engine.does-not-exist",
    };
    const auto missing = capture_package_source_lane(scenario, kMissingBus);
    expect(require_error(missing,
                         PackageSourceCaptureErrorCode::invalid_bus_selection)
               .detail_code == "package-source-bus-missing",
           "source capture did not reject a missing session bus");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"usage: package_source_capture_test <repo-root>"};
        }
        run(std::filesystem::path{argv[1]});
        std::cout << "package source capture tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
