#include "cli_shell.hpp"

#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Invocation {
    int exit_code = -1;
    std::string standard_out;
    std::string standard_error;
};

template <std::size_t Size>
Invocation invoke(const std::array<std::string_view, Size> &arguments) {
    std::ostringstream standard_out;
    std::ostringstream standard_error;
    const auto exit_code =
        engine_sim_offline::cli::run_cli(arguments, standard_out, standard_error);
    return {
        exit_code,
        standard_out.str(),
        standard_error.str(),
    };
}

void test_help_and_version() {
    const auto help = invoke(std::array<std::string_view, 1>{"--help"});
    expect(help.exit_code == engine_sim_offline::cli::kExitSuccess,
           "--help must succeed");
    expect(help.standard_out.find("Usage:") != std::string::npos,
           "--help must print usage to stdout");
    expect(help.standard_out.find("No serialized CLI input contract") !=
               std::string::npos,
           "--help must state the current input limitation");
    expect(help.standard_error.empty(), "--help must not write stderr");

    const auto version = invoke(std::array<std::string_view, 1>{"--version"});
    expect(version.exit_code == engine_sim_offline::cli::kExitSuccess,
           "--version must succeed");
    expect(version.standard_out ==
               "engine-sim-offline " +
                   std::string(engine_sim_offline::cli::version_label()) + "\n",
           "--version output must be stable");
    expect(version.standard_error.empty(), "--version must not write stderr");
}

void test_usage_errors() {
    const auto missing = invoke(std::array<std::string_view, 0>{});
    expect(missing.exit_code == engine_sim_offline::cli::kExitUsage,
           "missing command must return usage");
    expect(missing.standard_out.empty(), "usage errors must not write stdout");
    expect(missing.standard_error.find("missing command") != std::string::npos,
           "missing command must be diagnosed");

    const auto unknown = invoke(std::array<std::string_view, 1>{"unknown-command"});
    expect(unknown.exit_code == engine_sim_offline::cli::kExitUsage,
           "unknown command must return usage");
    expect(unknown.standard_error.find("unknown command") != std::string::npos,
           "unknown command must be diagnosed");

    const auto extra_help = invoke(std::array<std::string_view, 2>{"--help", "extra"});
    expect(extra_help.exit_code == engine_sim_offline::cli::kExitUsage,
           "extra help argument must return usage");

    const auto malformed_render =
        invoke(std::array<std::string_view, 2>{"render", "--output"});
    expect(malformed_render.exit_code == engine_sim_offline::cli::kExitUsage,
           "render arguments must be rejected before a loader contract exists");
    expect(malformed_render.standard_error.find("does not accept arguments") !=
               std::string::npos,
           "malformed render invocation must explain the admitted surface");
}

void test_render_is_unavailable() {
    const auto render = invoke(std::array<std::string_view, 1>{"render"});
    expect(render.exit_code == engine_sim_offline::cli::kExitUnavailable,
           "shipped render command must fail closed");
    expect(render.standard_out.empty(), "unavailable render must not claim output");
    expect(render.standard_error.find("no serialized CLI input contract") !=
               std::string::npos,
           "shipped unavailable render must explain why it cannot run");
}

} // namespace

int main() {
    try {
        test_help_and_version();
        test_usage_errors();
        test_render_is_unavailable();
    } catch (const std::exception &error) {
        std::cerr << "CLI shell test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
