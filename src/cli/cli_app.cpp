#include "cli_app.hpp"

#include "native_input_files.hpp"
#include "revengine_cli_support.hpp"

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"
#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"
#include "engine_sim_offline/bake.hpp"
#include "engine_sim_offline/compile.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#ifndef ENGINE_SIM_OFFLINE_VERSION_LABEL
#define ENGINE_SIM_OFFLINE_VERSION_LABEL "development"
#endif

namespace engine_sim_offline::cli {
namespace {

constexpr std::string_view kProgramName = "engine-sim-offline";

template <class Command> struct CommandOption {
    std::string_view spelling;
    std::string Command::*value;
    bool required = true;
    bool seen = false;
};

[[nodiscard]] CliParseResult usage_error(std::string message) {
    return CliUsageError{std::move(message)};
}

void print_help(std::ostream &stream) {
    stream << "Usage:\n"
              "  engine-sim-offline --help\n"
              "  engine-sim-offline --version\n"
              "  engine-sim-offline render --engine <engine.json> "
              "--scenario <scenario.json> \\\n"
              "      --output-directory <new-directory> "
              "[--asset-root <developer-directory>]\n"
              "  engine-sim-offline pack-revengine "
              "--package-directory <directory> \\\n"
              "      --output <new.revengine>\n"
              "  engine-sim-offline inspect-revengine --input <file.revengine>\n"
              "  engine-sim-offline verify-revengine --input <file.revengine>\n"
              "\n"
              "Commands:\n"
              "  render  Compile declarative engine and scenario JSON, render the\n"
              "          admitted simulation, and atomically publish its artifacts.\n"
              "  pack-revengine     Pack a validated responsive package tree.\n"
              "  inspect-revengine  Inspect structure and the authenticated index.\n"
              "  verify-revengine   Verify every payload and package binding.\n"
              "\n"
              "Render uses the bundled content-addressed asset catalog by default.\n"
              "--asset-root is an opt-in developer override for authored local URIs.\n"
              "Command options may appear in any order; required options occur "
              "exactly once.\n";
}

void print_usage_hint(std::ostream &stream) {
    stream << "Try '" << kProgramName << " --help' for usage.\n";
}

[[nodiscard]] int report_error(std::ostream &stream, const int exit_code,
                               const std::string_view message) {
    stream << "error: " << message << '\n';
    return exit_code;
}

[[nodiscard]] int report_usage_error(std::ostream &stream,
                                     const std::string_view message) {
    const auto result = report_error(stream, kExitUsage, message);
    print_usage_hint(stream);
    return result;
}

[[nodiscard]] int
revengine_cli_exit_code(const RevengineCliErrorKind kind) noexcept {
    switch (kind) {
    case RevengineCliErrorKind::data_error:
        return kExitDataError;
    case RevengineCliErrorKind::no_input:
        return kExitNoInput;
    case RevengineCliErrorKind::cant_create:
        return kExitCantCreate;
    case RevengineCliErrorKind::unavailable:
        return kExitUnavailable;
    }
    return kExitSoftware;
}

[[nodiscard]] int report_revengine_error(std::ostream &stream,
                                         const RevengineCliError &error) {
    return report_error(stream, revengine_cli_exit_code(error.kind),
                        error.message);
}

[[nodiscard]] std::string_view
diagnostic_code_name(const authoring::DiagnosticCode code) noexcept {
    using enum authoring::DiagnosticCode;
    switch (code) {
    case malformed_document:
        return "malformed_document";
    case unsupported_schema:
        return "unsupported_schema";
    case missing_value:
        return "missing_value";
    case unknown_field:
        return "unknown_field";
    case invalid_type:
        return "invalid_type";
    case invalid_unit:
        return "invalid_unit";
    case invalid_value:
        return "invalid_value";
    case out_of_range:
        return "out_of_range";
    case duplicate_id:
        return "duplicate_id";
    case dangling_reference:
        return "dangling_reference";
    case forbidden_cycle:
        return "forbidden_cycle";
    case disconnected_object:
        return "disconnected_object";
    case inconsistent_value:
        return "inconsistent_value";
    case unsupported_capability:
        return "unsupported_capability";
    case missing_asset:
        return "missing_asset";
    case asset_hash_mismatch:
        return "asset_hash_mismatch";
    case resource_limit:
        return "resource_limit";
    case internal_failure:
        return "internal_failure";
    }
    return "unknown_diagnostic";
}

[[nodiscard]] int
diagnostic_exit_code(const authoring::DiagnosticReport &report) noexcept {
    bool unavailable = false;
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.severity != authoring::DiagnosticSeverity::error) {
            continue;
        }
        if (diagnostic.code == authoring::DiagnosticCode::internal_failure) {
            return kExitSoftware;
        }
        unavailable =
            unavailable ||
            diagnostic.code == authoring::DiagnosticCode::unsupported_capability;
    }
    return unavailable ? kExitUnavailable : kExitDataError;
}

[[nodiscard]] int report_diagnostics(std::ostream &stream, const std::string_view stage,
                                     const authoring::DiagnosticReport &report) {
    const auto exit_code = diagnostic_exit_code(report);
    stream << "error: " << stage << " failed\n";
    for (const auto &diagnostic : report.diagnostics) {
        stream << "  " << diagnostic_code_name(diagnostic.code);
        if (!diagnostic.json_pointer.empty()) {
            stream << " at " << diagnostic.json_pointer;
        }
        stream << ": " << diagnostic.message << '\n';
    }
    return exit_code;
}

[[nodiscard]] int native_input_exit_code(const NativeInputErrorKind kind) noexcept {
    switch (kind) {
    case NativeInputErrorKind::data_error:
        return kExitDataError;
    case NativeInputErrorKind::no_input:
        return kExitNoInput;
    case NativeInputErrorKind::unavailable:
        return kExitUnavailable;
    case NativeInputErrorKind::software:
        return kExitSoftware;
    }
    return kExitSoftware;
}

[[nodiscard]] int report_native_input_error(std::ostream &stream,
                                            const std::string_view stage,
                                            const NativeInputError &error) {
    if (error.diagnostics.has_value()) {
        return report_diagnostics(stream, stage, *error.diagnostics);
    }
    return report_error(stream, native_input_exit_code(error.kind), error.message);
}

[[nodiscard]] int native_output_exit_code(const NativeOutputErrorKind kind) noexcept {
    switch (kind) {
    case NativeOutputErrorKind::cant_create:
        return kExitCantCreate;
    case NativeOutputErrorKind::temp_fail:
        return kExitTemporaryFailure;
    }
    return kExitSoftware;
}

[[nodiscard]] int
render_failure_exit_code(const contract::FailureContext &context) noexcept {
    if (context.detail_code == "renderer-numeric-environment-not-admitted" ||
        context.detail_code == "renderer-identity-not-admitted" ||
        context.detail_code == "renderer-identity-not-production" ||
        context.detail_code == "atomic-noreplace-unavailable") {
        return kExitUnavailable;
    }

    using enum contract::FailureKind;
    switch (context.kind) {
    case invalid_specification:
    case unreachable_target:
    case event_schedule_violation:
        return kExitDataError;
    case incomplete_source_route:
    case evidence_rights_failure:
        return kExitUnavailable;
    case cancelled:
        return kExitTemporaryFailure;
    case artifact_publication_failure:
        return kExitCantCreate;
    case nonphysical_state:
    case numerical_failure:
    case contract_violation:
        return kExitSoftware;
    }
    return kExitSoftware;
}

[[nodiscard]] int report_render_failure(std::ostream &stream,
                                        const contract::FailureContext &context) {
    stream << "error: render failed";
    if (!context.detail_code.empty()) {
        stream << " [" << context.detail_code << ']';
    }
    if (!context.state_summary.empty()) {
        stream << ": " << context.state_summary;
    }
    stream << '\n';
    return render_failure_exit_code(context);
}

[[nodiscard]] int execute_render(const RenderCommand &command,
                                 std::ostream &standard_out,
                                 std::ostream &standard_error) {
    auto engine_input_result = command.asset_root.empty()
                                   ? load_native_engine_input_with_builtin_assets(
                                         std::filesystem::path{
                                             command.engine_path})
                                   : load_native_engine_input(
                                         std::filesystem::path{
                                             command.engine_path},
                                         std::filesystem::path{
                                             command.asset_root});
    if (const auto *error = std::get_if<NativeInputError>(&engine_input_result)) {
        return report_native_input_error(standard_error, "engine input", *error);
    }
    auto engine_input = std::get<NativeEngineInput>(std::move(engine_input_result));
    const auto asset_views = engine_input.asset_views();
    auto engine_result = compile::compile_engine(engine_input.document, asset_views);
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&engine_result)) {
        return report_diagnostics(standard_error, "engine compilation", *report);
    }
    auto engine = std::get<compile::CompiledEngine>(std::move(engine_result));

    auto scenario_input_result =
        load_native_scenario_input(std::filesystem::path{command.scenario_path});
    if (const auto *error = std::get_if<NativeInputError>(&scenario_input_result)) {
        return report_native_input_error(standard_error, "scenario input", *error);
    }
    auto scenario_document =
        std::get<authoring::ScenarioDocument>(std::move(scenario_input_result));
    auto scenario_result = compile::compile_scenario(engine, scenario_document);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&scenario_result)) {
        return report_diagnostics(standard_error, "scenario compilation", *report);
    }
    auto scenario = std::get<compile::CompiledScenario>(std::move(scenario_result));

    const auto output_result = preflight_native_output_directory(
        std::filesystem::path{command.output_directory});
    if (const auto *error = std::get_if<NativeOutputError>(&output_result)) {
        return report_error(standard_error, native_output_exit_code(error->kind),
                            error->message);
    }
    const auto &output = std::get<NativeOutputDirectory>(output_result);
    artifacts::DirectoryRenderSink sink{output.publication_root,
                                        output.publication_name};
    const auto result = bake(scenario, sink);
    if (const auto *failure = std::get_if<contract::RenderFailure>(&result)) {
        return report_render_failure(standard_error, failure->context);
    }
    if (const auto *unreachable = std::get_if<contract::UnreachableTarget>(&result)) {
        return report_render_failure(standard_error, unreachable->context);
    }
    if (sink.state() != artifacts::DirectoryRenderSinkState::committed) {
        return report_error(
            standard_error, kExitSoftware,
            "renderer reported success without committing the output transaction");
    }

    const auto publication_path = sink.publication_path();
    standard_out
        << "output_directory=" << publication_path.string() << '\n'
        << "manifest="
        << (publication_path / artifacts::kSimulationManifestRelativePathV10).string()
        << '\n';
    return kExitSuccess;
}

[[nodiscard]] int execute_pack_revengine(const PackRevengineCommand &command,
                                         std::ostream &standard_out,
                                         std::ostream &standard_error) {
    auto result = pack_revengine_package_directory(
        std::filesystem::path{command.package_directory},
        std::filesystem::path{command.output_file});
    if (const auto *error = std::get_if<RevengineCliError>(&result)) {
        return report_revengine_error(standard_error, *error);
    }
    const auto &packed = std::get<PackedRevengineFile>(result);
    standard_out << "output_file=" << packed.output_path.string() << '\n'
                 << "container_bytes=" << packed.container_byte_count << '\n'
                 << "entry_count=" << packed.entry_count << '\n'
                 << "container_sha256="
                 << sha256_lower_hex(packed.container_sha256) << '\n';
    return kExitSuccess;
}

[[nodiscard]] int execute_load_revengine(const std::string &input_file,
                                         const bool verify_payloads,
                                         std::ostream &standard_out,
                                         std::ostream &standard_error) {
    auto result = inspect_revengine_file(std::filesystem::path{input_file},
                                         verify_payloads);
    if (const auto *error = std::get_if<RevengineCliError>(&result)) {
        return report_revengine_error(standard_error, *error);
    }
    const auto &loaded = std::get<LoadedRevengineFile>(result);
    standard_out << "revengine_version=" << loaded.index.version << '\n'
                 << "verified=" << (loaded.fully_verified ? "true" : "false")
                 << '\n'
                 << "container_bytes=" << loaded.index.container_byte_count << '\n'
                 << "index_bytes=" << loaded.index.index_byte_count << '\n'
                 << "payload_bytes=" << loaded.index.payload_byte_count << '\n'
                 << "entry_count=" << loaded.index.entries.size() << '\n'
                 << "container_sha256="
                 << sha256_lower_hex(loaded.container_sha256) << '\n'
                 << "index_sha256="
                 << sha256_lower_hex(loaded.index.index_sha256) << '\n'
                 << "payload_sha256="
                 << sha256_lower_hex(loaded.index.payload_sha256) << '\n';
    for (const auto &entry : loaded.index.entries) {
        standard_out << "entry=" << entry.path << '\t'
                     << entry.payload_byte_count << '\t'
                     << sha256_lower_hex(entry.payload_sha256) << '\n';
    }
    if (const auto *package =
            std::get_if<artifacts::RevenginePackageDescriptor>(&loaded.package)) {
        standard_out << "engine_id=" << package->engine_id << '\n'
                     << "runtime_kind=" << package->runtime.kind << '\n'
                     << "runtime_manifest_path="
                     << package->runtime.manifest_path << '\n'
                     << "runtime_manifest_sha256="
                     << sha256_lower_hex(package->runtime.manifest_sha256) << '\n';
    }
    return kExitSuccess;
}

template <class Command, std::size_t Size>
[[nodiscard]] CliParseResult parse_command_options(
    const std::span<const std::string_view> arguments, Command command,
    std::array<CommandOption<Command>, Size> options) {
    for (std::size_t index = 1U; index < arguments.size();) {
        const auto token = arguments[index];
        auto *option = static_cast<CommandOption<Command> *>(nullptr);
        for (auto &candidate : options) {
            if (candidate.spelling == token) {
                option = &candidate;
                break;
            }
        }
        if (option == nullptr) {
            const auto classification = token.starts_with('-')
                                            ? "unknown option '"
                                            : "unexpected argument '";
            return usage_error(std::string{classification} +
                               std::string{token} + "'");
        }
        if (option->seen) {
            return usage_error("duplicate option '" + std::string{token} + "'");
        }
        if (index + 1U >= arguments.size() ||
            arguments[index + 1U].starts_with("--")) {
            return usage_error("option '" + std::string{token} +
                               "' requires a separate value");
        }
        const auto value = arguments[index + 1U];
        if (value.empty()) {
            return usage_error("option '" + std::string{token} +
                               "' requires a non-empty value");
        }
        command.*(option->value) = value;
        option->seen = true;
        index += 2U;
    }

    for (const auto &option : options) {
        if (option.required && !option.seen) {
            return usage_error("missing required option '" +
                               std::string{option.spelling} + "'");
        }
    }
    return CliCommand{std::move(command)};
}

} // namespace

std::string_view version_label() noexcept {
    return ENGINE_SIM_OFFLINE_VERSION_LABEL;
}

CliParseResult parse_cli_arguments(const std::span<const std::string_view> arguments) {
    if (arguments.empty()) {
        return usage_error("missing command");
    }

    if (arguments.front() == "--help") {
        if (arguments.size() != 1U) {
            return usage_error("--help does not accept additional arguments");
        }
        return CliCommand{HelpCommand{}};
    }
    if (arguments.front() == "--version") {
        if (arguments.size() != 1U) {
            return usage_error("--version does not accept additional arguments");
        }
        return CliCommand{VersionCommand{}};
    }
    if (arguments.front() == "render") {
        return parse_command_options(
            arguments, RenderCommand{},
            std::array{
                CommandOption<RenderCommand>{"--engine",
                                             &RenderCommand::engine_path},
                CommandOption<RenderCommand>{"--scenario",
                                             &RenderCommand::scenario_path},
                CommandOption<RenderCommand>{"--asset-root",
                                             &RenderCommand::asset_root,
                                             false},
                CommandOption<RenderCommand>{
                    "--output-directory", &RenderCommand::output_directory},
            });
    }
    if (arguments.front() == "pack-revengine") {
        return parse_command_options(
            arguments, PackRevengineCommand{},
            std::array{
                CommandOption<PackRevengineCommand>{
                    "--package-directory",
                    &PackRevengineCommand::package_directory},
                CommandOption<PackRevengineCommand>{
                    "--output", &PackRevengineCommand::output_file},
            });
    }
    if (arguments.front() == "inspect-revengine") {
        return parse_command_options(
            arguments, InspectRevengineCommand{},
            std::array{CommandOption<InspectRevengineCommand>{
                "--input", &InspectRevengineCommand::input_file}});
    }
    if (arguments.front() == "verify-revengine") {
        return parse_command_options(
            arguments, VerifyRevengineCommand{},
            std::array{CommandOption<VerifyRevengineCommand>{
                "--input", &VerifyRevengineCommand::input_file}});
    }
    return usage_error("unknown command '" + std::string{arguments.front()} +
                       "'");
}

int run_cli(const std::span<const std::string_view> arguments,
            std::ostream &standard_out, std::ostream &standard_error) {
    try {
        const auto parsed = parse_cli_arguments(arguments);
        if (const auto *error = std::get_if<CliUsageError>(&parsed)) {
            return report_usage_error(standard_error, error->message);
        }
        const auto &command = std::get<CliCommand>(parsed);
        if (std::holds_alternative<HelpCommand>(command)) {
            print_help(standard_out);
            return kExitSuccess;
        }
        if (std::holds_alternative<VersionCommand>(command)) {
            standard_out << kProgramName << ' ' << version_label() << '\n';
            return kExitSuccess;
        }
        if (const auto *render = std::get_if<RenderCommand>(&command)) {
            return execute_render(*render, standard_out, standard_error);
        }
        if (const auto *pack = std::get_if<PackRevengineCommand>(&command)) {
            return execute_pack_revengine(*pack, standard_out, standard_error);
        }
        if (const auto *inspect =
                std::get_if<InspectRevengineCommand>(&command)) {
            return execute_load_revengine(inspect->input_file, false,
                                          standard_out, standard_error);
        }
        const auto &verify = std::get<VerifyRevengineCommand>(command);
        return execute_load_revengine(verify.input_file, true, standard_out,
                                      standard_error);
    } catch (const std::bad_alloc &) {
        return report_error(standard_error, kExitSoftware,
                            "insufficient memory while processing the request");
    } catch (const std::exception &error) {
        return report_error(standard_error, kExitSoftware,
                            std::string{"unexpected application failure: "} +
                                error.what());
    } catch (...) {
        return report_error(standard_error, kExitSoftware,
                            "unexpected non-standard application failure");
    }
}

} // namespace engine_sim_offline::cli
