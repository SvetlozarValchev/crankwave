#include "cli_app.hpp"

#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using engine_sim_offline::cli::CliCommand;
using engine_sim_offline::cli::CliParseResult;
using engine_sim_offline::cli::CliUsageError;
using engine_sim_offline::cli::RenderCommand;

static_assert(engine_sim_offline::cli::kExitSuccess == 0);
static_assert(engine_sim_offline::cli::kExitUsage == 64);
static_assert(engine_sim_offline::cli::kExitDataError == 65);
static_assert(engine_sim_offline::cli::kExitNoInput == 66);
static_assert(engine_sim_offline::cli::kExitUnavailable == 69);
static_assert(engine_sim_offline::cli::kExitSoftware == 70);
static_assert(engine_sim_offline::cli::kExitCantCreate == 73);
static_assert(engine_sim_offline::cli::kExitTemporaryFailure == 75);

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] CliParseResult
parse(const std::initializer_list<std::string_view> arguments) {
    return engine_sim_offline::cli::parse_cli_arguments(
        std::span<const std::string_view>{arguments.begin(), arguments.size()});
}

[[nodiscard]] const RenderCommand &require_render(const CliParseResult &result) {
    const auto *command = std::get_if<CliCommand>(&result);
    expect(command != nullptr, "valid render syntax was rejected");
    const auto *render = std::get_if<RenderCommand>(command);
    expect(render != nullptr, "valid render syntax selected the wrong command");
    return *render;
}

void test_exact_render_grammar() {
    const auto canonical =
        parse({"render", "--engine", "engine.json", "--scenario", "scenario.json",
               "--asset-root", "assets", "--output-directory", "result"});
    const auto &render = require_render(canonical);
    expect(render.engine_path == "engine.json", "engine value was not retained");
    expect(render.scenario_path == "scenario.json", "scenario value was not retained");
    expect(render.asset_root == "assets", "asset-root value was not retained");
    expect(render.output_directory == "result",
           "output-directory value was not retained");

    const auto reordered =
        parse({"render", "--output-directory", "out", "--asset-root", "root",
               "--scenario", "drive.json", "--engine", "motor.json"});
    const auto &other = require_render(reordered);
    expect(other.engine_path == "motor.json" && other.scenario_path == "drive.json" &&
               other.asset_root == "root" && other.output_directory == "out",
           "render flags must be order-independent");
}

void test_strict_render_rejections() {
    const std::vector<std::vector<std::string_view>> invalid{
        {},
        {"render"},
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "a"},
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o", "extra"},
        {"render", "--engine=e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "-e", "e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "--engine", "e", "--scenario", "s", "--assets", "a",
         "--output-directory", "o"},
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "a", "--output",
         "o"},
        {"render", "--engine", "first", "--engine", "second", "--scenario", "s",
         "--asset-root", "a", "--output-directory", "o"},
        {"render", "--engine", "", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "--engine", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "--help", "--engine", "e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"unknown"},
        {"--help", "extra"},
        {"--version", "extra"},
    };

    for (const auto &arguments : invalid) {
        const auto result = engine_sim_offline::cli::parse_cli_arguments(arguments);
        expect(std::holds_alternative<CliUsageError>(result),
               "non-current or malformed syntax was admitted");
        expect(!std::get<CliUsageError>(result).message.empty(),
               "usage rejection must carry a diagnostic");
    }
}

struct Invocation {
    int exit_code = -1;
    std::string standard_out;
    std::string standard_error;
};

[[nodiscard]] Invocation
invoke(const std::initializer_list<std::string_view> arguments) {
    std::ostringstream standard_out;
    std::ostringstream standard_error;
    const auto exit_code = engine_sim_offline::cli::run_cli(
        std::span<const std::string_view>{arguments.begin(), arguments.size()},
        standard_out, standard_error);
    return {exit_code, standard_out.str(), standard_error.str()};
}

void test_standalone_help_and_version() {
    const auto help = invoke({"--help"});
    expect(help.exit_code == engine_sim_offline::cli::kExitSuccess,
           "--help must succeed");
    expect(help.standard_out.find(
               "render --engine <engine.json> --scenario <scenario.json>") !=
               std::string::npos,
           "--help must document the exact current render syntax");
    expect(help.standard_error.empty(), "--help must not write stderr");

    const auto version = invoke({"--version"});
    expect(version.exit_code == engine_sim_offline::cli::kExitSuccess,
           "--version must succeed");
    expect(version.standard_error.empty(), "--version must not write stderr");
    expect(version.standard_out ==
               "engine-sim-offline " +
                   std::string{engine_sim_offline::cli::version_label()} + "\n",
           "--version output must be stable");
}

void test_usage_output_channels() {
    const auto missing = invoke({});
    expect(missing.exit_code == engine_sim_offline::cli::kExitUsage,
           "missing command must return EX_USAGE");
    expect(missing.standard_out.empty(), "a usage failure must not write stdout");
    expect(missing.standard_error.find("error:") != std::string::npos &&
               missing.standard_error.find("Try 'engine-sim-offline --help'") !=
                   std::string::npos,
           "a usage failure must carry the stable usage hint on stderr");

    const auto obsolete = invoke({"render", "--profile", "bmw"});
    expect(obsolete.exit_code == engine_sim_offline::cli::kExitUsage,
           "an obsolete profile surface must return EX_USAGE");
    expect(obsolete.standard_out.empty(),
           "an obsolete profile rejection must not write stdout");
}

} // namespace

int main() {
    try {
        test_exact_render_grammar();
        test_strict_render_rejections();
        test_standalone_help_and_version();
        test_usage_output_channels();
    } catch (const std::exception &error) {
        std::cerr << "CLI application test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
