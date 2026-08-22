#include "crankwave/authoring/parse.hpp"
#include "crankwave/responsive/profile.hpp"
#include "crankwave/responsive/scenario_template.hpp"

#include <filesystem>
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
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
