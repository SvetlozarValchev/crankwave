#pragma once

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::cli {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitDataError = 65;
inline constexpr int kExitNoInput = 66;
inline constexpr int kExitUnavailable = 69;
inline constexpr int kExitSoftware = 70;
inline constexpr int kExitCantCreate = 73;
inline constexpr int kExitTemporaryFailure = 75;

struct HelpCommand {
    friend bool operator==(const HelpCommand &, const HelpCommand &) = default;
};

struct VersionCommand {
    friend bool operator==(const VersionCommand &, const VersionCommand &) = default;
};

struct RenderCommand {
    std::string engine_path;
    std::string scenario_path;
    std::string asset_root;
    std::string output_directory;

    friend bool operator==(const RenderCommand &, const RenderCommand &) = default;
};

using CliCommand = std::variant<HelpCommand, VersionCommand, RenderCommand>;

struct CliUsageError {
    std::string message;

    friend bool operator==(const CliUsageError &, const CliUsageError &) = default;
};

using CliParseResult = std::variant<CliCommand, CliUsageError>;

[[nodiscard]] std::string_view version_label() noexcept;

// Parses the current command grammar. The program name is not part of arguments.
// Command option order is arbitrary, but every current option occurs exactly once
// and uses a separate, non-empty value token.
[[nodiscard]] CliParseResult
parse_cli_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] int run_cli(std::span<const std::string_view> arguments,
                          std::ostream &standard_out, std::ostream &standard_error);

} // namespace engine_sim_offline::cli
