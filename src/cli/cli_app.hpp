#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

namespace crankwave::cli {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitDataError = 65;
inline constexpr int kExitNoInput = 66;
inline constexpr int kExitUnavailable = 69;
inline constexpr int kExitSoftware = 70;
inline constexpr int kExitCantCreate = 73;
inline constexpr int kExitTemporaryFailure = 75;

enum class CliResultFormat : std::uint8_t {
    text,
    json,
};

struct MachineResultOptions {
    CliResultFormat result_format = CliResultFormat::text;
    std::optional<std::uint64_t> deadline_unix_ms;

    friend bool operator==(const MachineResultOptions &,
                           const MachineResultOptions &) = default;
};

struct HelpCommand {
    friend bool operator==(const HelpCommand &, const HelpCommand &) = default;
};

struct VersionCommand {
    friend bool operator==(const VersionCommand &, const VersionCommand &) = default;
};

struct RenderCommand : MachineResultOptions {
    std::string engine_path;
    std::string scenario_path;
    std::string asset_root;
    std::string output_directory;
    friend bool operator==(const RenderCommand &, const RenderCommand &) = default;
};

struct PackCrankwaveCommand : MachineResultOptions {
    std::string package_directory;
    std::string output_file;

    friend bool operator==(const PackCrankwaveCommand &,
                           const PackCrankwaveCommand &) = default;
};

struct BakeCrankwaveCommand : MachineResultOptions {
    std::string engine_path;
    std::string asset_root;
    std::string output_file;

    friend bool operator==(const BakeCrankwaveCommand &,
                           const BakeCrankwaveCommand &) = default;
};

struct InspectCrankwaveCommand : MachineResultOptions {
    std::string input_file;

    friend bool operator==(const InspectCrankwaveCommand &,
                           const InspectCrankwaveCommand &) = default;
};

struct VerifyCrankwaveCommand : MachineResultOptions {
    std::string input_file;

    friend bool operator==(const VerifyCrankwaveCommand &,
                           const VerifyCrankwaveCommand &) = default;
};

struct InspectIrCatalogCommand : MachineResultOptions {
    friend bool operator==(const InspectIrCatalogCommand &,
                           const InspectIrCatalogCommand &) = default;
};

using CliCommand = std::variant<HelpCommand, VersionCommand, RenderCommand,
                                BakeCrankwaveCommand, PackCrankwaveCommand,
                                InspectCrankwaveCommand, VerifyCrankwaveCommand,
                                InspectIrCatalogCommand>;

struct CliUsageError {
    std::string message;

    friend bool operator==(const CliUsageError &, const CliUsageError &) = default;
};

using CliParseResult = std::variant<CliCommand, CliUsageError>;

[[nodiscard]] std::string_view version_label() noexcept;

// Parses the current command grammar. The program name is not part of arguments.
// Command option order is arbitrary. Required options occur exactly once; optional
// options occur at most once. Every supplied option uses a separate, non-empty
// value token.
[[nodiscard]] CliParseResult
parse_cli_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] int run_cli(std::span<const std::string_view> arguments,
                          std::ostream &standard_out, std::ostream &standard_error,
                          std::stop_token termination_token = {});

} // namespace crankwave::cli
