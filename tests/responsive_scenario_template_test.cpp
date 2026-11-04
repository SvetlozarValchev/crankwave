#include "crankwave/authoring/parse.hpp"
#include "crankwave/responsive/profile.hpp"
#include "crankwave/responsive/scenario_template.hpp"

#include <filesystem>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace crankwave;

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error{"could not open engine fixture"};
    }
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

} // namespace

int main(int argc, char **argv) try {
    expect(argc == 2, "usage: responsive_scenario_template_test <repo-root>");
    const auto engine_path =
        std::filesystem::path{argv[1]} /
        "data/engines/bmw-m52tub28-cleanroom/engine.json";
    auto parsed = authoring::parse_engine_document(read_text(engine_path));
    expect(std::holds_alternative<authoring::EnginePackageDocument>(parsed),
           "engine fixture parses");
    const auto &engine = std::get<authoring::EnginePackageDocument>(parsed);
    const auto selected =
        responsive::derive_engine_redline_affine_profile(engine);
    expect(std::holds_alternative<responsive::ResponsiveBakeProfile>(selected),
           "automatic profile selection succeeds");
    const auto &profile = std::get<responsive::ResponsiveBakeProfile>(selected);
    const auto result =
        responsive::make_responsive_scenario_template(engine, profile);
    expect(std::holds_alternative<responsive::ResponsiveScenarioTemplate>(result),
           "native scenario template is generated");
    const auto &value =
        std::get<responsive::ResponsiveScenarioTemplate>(result);
    const auto &scenario = value.document;
    expect(scenario.engine.value == "bmw-m52tub28-cleanroom" &&
               scenario.fuel.value == engine.engine.default_fuel.value,
           "template binds engine and default fuel");
    expect(scenario.rates.physics.numerator == 10000U &&
               scenario.rates.delivery.numerator == 192000U &&
               scenario.quality.process_block_capacity_frames == 3840U,
           "template fixes native 10 kHz / 192 kHz / 20 ms clocks");
    expect(scenario.output.buses.size() == 2U &&
               scenario.output.buses[0].value == "master-engine-raw" &&
               scenario.output.buses[1].value == "master-engine-audition",
           "template selects the established authored master buses");
    expect(scenario.public_seed == UINT64_C(12648430) &&
               scenario.initial_state.ignition_enabled &&
               scenario.initial_state.fuel_enabled &&
               scenario.initial_state.limiter_enabled,
           "template retains fixed seed and running state");
    const auto repeated =
        responsive::make_responsive_scenario_template(engine, profile);
    expect(std::holds_alternative<responsive::ResponsiveScenarioTemplate>(repeated) &&
               std::get<responsive::ResponsiveScenarioTemplate>(repeated) == value,
           "template and identity repeat exactly");
    const auto &original_preparation =
        std::get<authoring::FixedHorizonPreparation>(scenario.preparation);
    expect(original_preparation.preparation_duration.value == 1.0 &&
               scenario.audible_start.value == 1.0 &&
               scenario.total_duration.value == 7.0,
           "previously sufficient template duration remains unchanged");
    const contract::Sha256Digest legacy_identity{{
        0xe0, 0xdb, 0x44, 0x37, 0x1b, 0xdb, 0xca, 0x87,
        0x57, 0xd1, 0xb9, 0xa4, 0xab, 0xe4, 0x0c, 0x99,
        0xe4, 0xee, 0xb1, 0x7e, 0xa4, 0x45, 0x8b, 0xe0,
        0xce, 0x40, 0xbf, 0x09, 0x79, 0x94, 0x90, 0xc3}};
    expect(value.identity_sha256 == legacy_identity,
           "previously valid 6500 RPM template retains its legacy identity");
    for (const double redline : {2600.0, 800.0, 250.0}) {
        auto slow_engine = engine;
        slow_engine.engine.limits.redline = {redline, "rpm", std::nullopt};
        const auto slow_selection =
            responsive::derive_engine_redline_affine_profile(slow_engine);
        const auto &slow_profile =
            std::get<responsive::ResponsiveBakeProfile>(slow_selection);
        const auto slow_result =
            responsive::make_responsive_scenario_template(slow_engine, slow_profile);
        expect(std::holds_alternative<responsive::ResponsiveScenarioTemplate>(slow_result),
               "slow-engine template generation succeeds");
        const auto &slow =
            std::get<responsive::ResponsiveScenarioTemplate>(slow_result);
        const auto &preparation =
            std::get<authoring::FixedHorizonPreparation>(slow.document.preparation);
        const double seconds = preparation.preparation_duration.value;
        expect(preparation.trailing_complete_cycle_count == 4 &&
                   seconds >= 5.0 * 120.0 / slow_profile.rpm.anchors.front() &&
                   std::abs(seconds * 50.0 - std::round(seconds * 50.0)) < 1e-10 &&
                   slow.document.audible_start.value == seconds &&
                   slow.document.total_duration.value == seconds + 6.0 &&
                   slow.document.audible_duration.value == 6.0,
               "slow template retains all trailing cycles and the audible window on the block grid");
        if (redline == 2600.0) {
            expect(seconds == 1.16, "520 RPM template receives the required 1.16 seconds");
        }
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
