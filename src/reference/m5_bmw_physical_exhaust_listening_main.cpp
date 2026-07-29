#include "experimental/bmw_physical_input_capture.hpp"
#include "experimental/physical_exhaust_network.hpp"
#include "experimental/physical_listening_wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using engine_sim_offline::experimental::BmwPhysicalInputHistory;
using engine_sim_offline::experimental::BmwPhysicalListeningMode;
using engine_sim_offline::experimental::capture_bmw_physical_input;
using engine_sim_offline::experimental::kPhysicalExhaustSampleRateHz;
using engine_sim_offline::experimental::PhysicalExhaustNetwork;
using engine_sim_offline::experimental::PhysicalListeningWavStats;
using engine_sim_offline::experimental::PhysicalValveBoundary;
using engine_sim_offline::experimental::write_physical_listening_wav;

constexpr double kMonitoringTargetPeak = 0.5;
constexpr double kMinimumPascalsPerFullScale = 20.0;

[[nodiscard]] BmwPhysicalListeningMode parse_mode(std::string_view value) {
    if (value == "held-rpm3000-throttle0p85") {
        return BmwPhysicalListeningMode::held_3000_throttle_0p85;
    }
    if (value == "inertial") {
        return BmwPhysicalListeningMode::inertial_dyno;
    }
    throw std::invalid_argument{"mode must be held-rpm3000-throttle0p85 or inertial"};
}

[[nodiscard]] std::array<PhysicalValveBoundary, 6U>
interpolate_boundary(const BmwPhysicalInputHistory &history,
                     std::uint64_t acoustic_sample_index) {
    const long double post_step_time_s =
        static_cast<long double>(acoustic_sample_index + 1U) /
        static_cast<long double>(kPhysicalExhaustSampleRateHz);
    const long double source_position = post_step_time_s * 10000.0L - 1.0L;
    const auto source_count = history.frames_10khz.size();
    if (source_count == 0U) {
        throw std::logic_error{"physical input history is empty"};
    }
    if (source_position <= 0.0L) {
        return history.frames_10khz.front();
    }
    if (source_position >= static_cast<long double>(source_count - 1U)) {
        return history.frames_10khz.back();
    }

    const auto lower = static_cast<std::size_t>(std::floor(source_position));
    const auto upper = lower + 1U;
    const double mix =
        static_cast<double>(source_position - static_cast<long double>(lower));
    std::array<PhysicalValveBoundary, 6U> result{};
    for (std::size_t cylinder = 0; cylinder < result.size(); ++cylinder) {
        const auto &a = history.frames_10khz[lower][cylinder];
        const auto &b = history.frames_10khz[upper][cylinder];
        result[cylinder] = {
            a.pressure_pa_abs + (b.pressure_pa_abs - a.pressure_pa_abs) * mix,
            a.temperature_k + (b.temperature_k - a.temperature_k) * mix,
            a.molar_flow_conductance +
                (b.molar_flow_conductance - a.molar_flow_conductance) * mix,
        };
    }
    return result;
}

[[nodiscard]] double maximum_absolute(std::span<const double> samples) {
    double peak = 0.0;
    for (const double sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::runtime_error{
                "physical exhaust network produced a non-finite listener sample"};
        }
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

[[nodiscard]] PhysicalListeningWavStats
require_written(engine_sim_offline::experimental::PhysicalListeningWavResult result,
                std::string_view role) {
    if (const auto *error = std::get_if<std::string>(&result)) {
        throw std::runtime_error{std::string{role} + " WAVE failed: " + *error};
    }
    return std::get<PhysicalListeningWavStats>(std::move(result));
}

int run(int argc, char **argv) {
    if (argc != 3) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-m5-bmw-physical-listening "
            "<held-rpm3000-throttle0p85|inertial> <new-output-directory>"};
    }
    const auto mode = parse_mode(argv[1]);
    const std::filesystem::path output_directory{argv[2]};
    if (output_directory.empty() || output_directory.filename().empty() ||
        output_directory.filename().string().front() == '-') {
        throw std::invalid_argument{
            "output directory must end in a normal publication-name component"};
    }
    std::error_code filesystem_error;
    const auto parent = output_directory.parent_path().empty()
                            ? std::filesystem::path{"."}
                            : output_directory.parent_path();
    if (!std::filesystem::is_directory(parent, filesystem_error) || filesystem_error ||
        std::filesystem::exists(output_directory)) {
        throw std::invalid_argument{
            "output parent must exist and the output directory must be new"};
    }

    auto capture_result = capture_bmw_physical_input(mode);
    if (const auto *error = std::get_if<std::string>(&capture_result)) {
        throw std::runtime_error{"BMW physical input capture failed: " + *error};
    }
    auto history = std::get<BmwPhysicalInputHistory>(std::move(capture_result));

    const auto total_acoustic_frames = static_cast<std::uint64_t>(std::llround(
        history.total_duration_s * static_cast<double>(kPhysicalExhaustSampleRateHz)));
    const auto audible_start_frame = static_cast<std::uint64_t>(std::llround(
        history.audible_start_s * static_cast<double>(kPhysicalExhaustSampleRateHz)));
    const auto audible_frame_count = static_cast<std::uint64_t>(
        std::llround(history.audible_duration_s *
                     static_cast<double>(kPhysicalExhaustSampleRateHz)));
    if (total_acoustic_frames == 0U || audible_frame_count == 0U ||
        audible_start_frame > total_acoustic_frames ||
        audible_frame_count > total_acoustic_frames - audible_start_frame) {
        throw std::runtime_error{
            "BMW scenario durations do not map to a valid acoustic interval"};
    }

    PhysicalExhaustNetwork network;
    network.initialize(history.ambient_pressure_pa, history.ambient_temperature_k);
    std::array<std::vector<double>, 2U> route_pressure;
    for (auto &route : route_pressure) {
        route.reserve(static_cast<std::size_t>(audible_frame_count));
    }
    std::vector<double> coherent_pressure;
    coherent_pressure.reserve(static_cast<std::size_t>(audible_frame_count));

    for (std::uint64_t frame = 0; frame < total_acoustic_frames; ++frame) {
        const auto boundary = interpolate_boundary(history, frame);
        if (!network.advance(boundary)) {
            throw std::runtime_error{
                "physical exhaust network failed at acoustic frame " +
                std::to_string(frame) + ": " + std::string{network.failure_reason()}};
        }
        if (frame < audible_start_frame ||
            frame >= audible_start_frame + audible_frame_count) {
            continue;
        }
        const auto &output = network.output();
        route_pressure[0].push_back(output.radiated_pressure_pa[0]);
        route_pressure[1].push_back(output.radiated_pressure_pa[1]);
        coherent_pressure.push_back(output.coherent_mono_pressure_pa);
    }

    const double pressure_peak = std::max({maximum_absolute(route_pressure[0]),
                                           maximum_absolute(route_pressure[1]),
                                           maximum_absolute(coherent_pressure)});
    if (!(pressure_peak > 0.0) || !std::isfinite(pressure_peak)) {
        throw std::runtime_error{
            "physical exhaust network produced no finite audible pressure"};
    }
    const double pascals_per_full_scale =
        std::max(kMinimumPascalsPerFullScale, pressure_peak / kMonitoringTargetPeak);

    if (!std::filesystem::create_directory(output_directory, filesystem_error) ||
        filesystem_error) {
        throw std::runtime_error{"could not create the new output directory"};
    }
    const std::string common_title =
        "BMW M52B28 experimental physical exhaust; scenario=" + history.scenario_id;
    const auto full = require_written(
        write_physical_listening_wav(output_directory / "physical-exhaust.full.wav",
                                     coherent_pressure, kPhysicalExhaustSampleRateHz,
                                     pascals_per_full_scale,
                                     common_title + "; monitor=coherent-full"),
        "full");
    const auto front = require_written(
        write_physical_listening_wav(output_directory / "physical-exhaust.front.wav",
                                     route_pressure[0], kPhysicalExhaustSampleRateHz,
                                     pascals_per_full_scale,
                                     common_title + "; monitor=front-route"),
        "front");
    const auto rear = require_written(
        write_physical_listening_wav(output_directory / "physical-exhaust.rear.wav",
                                     route_pressure[1], kPhysicalExhaustSampleRateHz,
                                     pascals_per_full_scale,
                                     common_title + "; monitor=rear-route"),
        "rear");

    std::cout << std::setprecision(17) << "scenario=" << history.scenario_id << '\n'
              << "output=" << output_directory.string() << '\n'
              << "full=" << (output_directory / "physical-exhaust.full.wav").string()
              << '\n'
              << "sample_rate_hz=" << kPhysicalExhaustSampleRateHz << '\n'
              << "audible_frames=" << coherent_pressure.size() << '\n'
              << "raw_pressure_peak_pa=" << pressure_peak << '\n'
              << "pascals_per_full_scale=" << pascals_per_full_scale << '\n'
              << "full_peak_normalized=" << full.peak_normalized << '\n'
              << "front_peak_normalized=" << front.peak_normalized << '\n'
              << "rear_peak_normalized=" << rear.peak_normalized << '\n'
              << "maximum_cfl=" << network.output().maximum_cfl << '\n';
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &exception) {
        std::cerr << "M5 BMW physical listening spike failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
