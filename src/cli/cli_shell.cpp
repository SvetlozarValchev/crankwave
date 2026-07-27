#include "cli_shell.hpp"

#include <ostream>

#ifndef ENGINE_SIM_OFFLINE_VERSION_LABEL
#define ENGINE_SIM_OFFLINE_VERSION_LABEL "development"
#endif

namespace engine_sim_offline::cli {
namespace {

constexpr std::string_view kProgramName = "engine-sim-offline";

void print_usage_hint(std::ostream &stream) {
    stream << "Try '" << kProgramName << " --help' for usage.\n";
}

int report_usage_error(std::ostream &standard_error, std::string_view detail) {
    standard_error << "error: " << detail << '\n';
    print_usage_hint(standard_error);
    return kExitUsage;
}

void print_help(std::ostream &stream) {
    stream << "Usage:\n"
              "  engine-sim-offline --help\n"
              "  engine-sim-offline --version\n"
              "  engine-sim-offline render\n"
              "\n"
              "Commands:\n"
              "  render  Run the admitted typed render route (currently unavailable).\n"
              "\n"
              "No serialized CLI input contract is admitted in this checkpoint.\n"
              "The shipped render command reports unavailable until a real input\n"
              "loader or built-in request route is added.\n";
}

} // namespace

std::string_view version_label() noexcept {
    return ENGINE_SIM_OFFLINE_VERSION_LABEL;
}

int run_cli(std::span<const std::string_view> arguments, std::ostream &standard_out,
            std::ostream &standard_error) {
    if (arguments.empty()) {
        return report_usage_error(standard_error, "missing command");
    }

    const auto command = arguments.front();
    if (command == "--help") {
        if (arguments.size() != 1) {
            return report_usage_error(standard_error,
                                      "--help does not accept additional arguments");
        }
        print_help(standard_out);
        return kExitSuccess;
    }

    if (command == "--version") {
        if (arguments.size() != 1) {
            return report_usage_error(standard_error,
                                      "--version does not accept additional arguments");
        }
        standard_out << kProgramName << ' ' << version_label() << '\n';
        return kExitSuccess;
    }

    if (command == "render") {
        if (arguments.size() != 1) {
            return report_usage_error(
                standard_error,
                "render does not accept arguments until a CLI input contract is "
                "admitted");
        }
        standard_error
            << "error: render unavailable: no serialized CLI input contract or "
               "built-in request route is available in this build\n";
        return kExitUnavailable;
    }

    standard_error << "error: unknown command '" << command << "'\n";
    print_usage_hint(standard_error);
    return kExitUsage;
}

} // namespace engine_sim_offline::cli
