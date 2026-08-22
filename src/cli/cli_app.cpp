#include "cli_app.hpp"

#include "bake_crankwave_command.hpp"
#include "native_input_files.hpp"
#include "crankwave_cli_support.hpp"

#include "crankwave/artifacts/directory_render_sink.hpp"
#include "crankwave/artifacts/simulation_manifest_encoder.hpp"
#include "crankwave/bake.hpp"
#include "crankwave/compile.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <new>
#include <ostream>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#ifndef CRANKWAVE_VERSION_LABEL
#error "CRANKWAVE_VERSION_LABEL must be supplied by the product build"
#endif

namespace crankwave::cli {
namespace {

constexpr std::string_view kProgramName = "crankwave";
constexpr std::string_view kMachineResultSchema = "crankwave.cli-result.v1";

enum class CommandOptionKind : std::uint8_t {
    string,
    result_format,
    deadline_unix_ms,
};

template <class Command> struct CommandOption {
    std::string_view spelling;
    std::string Command::*value = nullptr;
    bool required = true;
    bool seen = false;
    CommandOptionKind kind = CommandOptionKind::string;
};

struct CliOutput {
    CliResultFormat format = CliResultFormat::text;
    std::string_view command;
    std::ostream &standard_out;
    std::ostream &standard_error;
};

enum class InvocationStopReason : std::uint8_t {
    none,
    termination,
    deadline,
};

struct InvocationExecutionControl {
    RenderControl render;
    const std::atomic<InvocationStopReason> *reason = nullptr;
};

[[nodiscard]] CliParseResult usage_error(std::string message) {
    return CliUsageError{std::move(message)};
}

void print_help(std::ostream &stream) {
    stream << "Usage:\n"
              "  crankwave --help\n"
              "  crankwave --version\n"
              "  crankwave render --engine <engine.json> "
              "--scenario <scenario.json> \\\n"
              "      --output-directory <new-directory> "
              "[--asset-root <developer-directory>] \\\n"
              "      [--deadline-unix-ms <epoch-ms>] "
              "[--result-format <text|json>]\n"
              "  crankwave bake-crankwave --engine <engine.json> \\\n"
              "      --output <new.crankwave> "
              "[--asset-root <developer-directory>] \\\n"
              "      [--deadline-unix-ms <epoch-ms>] "
              "[--result-format <text|json>]\n"
              "  crankwave pack-crankwave "
              "--package-directory <directory> \\\n"
              "      --output <new.crankwave> [--deadline-unix-ms <epoch-ms>] "
              "[--result-format <text|json>]\n"
              "  crankwave inspect-crankwave --input <file.crankwave> "
              "[--deadline-unix-ms <epoch-ms>] [--result-format <text|json>]\n"
              "  crankwave verify-crankwave --input <file.crankwave> "
              "[--deadline-unix-ms <epoch-ms>] [--result-format <text|json>]\n"
              "  crankwave inspect-ir-catalog "
              "[--result-format <text|json>]\n"
              "\n"
              "Commands:\n"
              "  render  Compile declarative engine and scenario JSON, render the\n"
              "          admitted simulation, and atomically publish its artifacts.\n"
              "  bake-crankwave  Compile an engine, cook its complete responsive\n"
              "                  runtime, and atomically publish a verified carrier.\n"
              "  pack-crankwave     Pack a validated responsive package tree.\n"
              "  inspect-crankwave  Inspect structure and the authenticated index.\n"
              "  verify-crankwave   Verify every payload and package binding.\n"
              "  inspect-ir-catalog  Return the release-bound IR authoring palette.\n"
              "\n"
              "Render uses the bundled content-addressed asset catalog by default.\n"
              "--asset-root is an opt-in developer override for authored local URIs.\n"
              "--deadline-unix-ms requests command cancellation at an absolute Unix "
              "deadline.\n"
              "--result-format json emits one versioned result object on stdout for "
              "success or failure.\n"
              "Command options may appear in any order; required options occur "
              "exactly once.\n";
}

void print_usage_hint(std::ostream &stream) {
    stream << "Try '" << kProgramName << " --help' for usage.\n";
}

void write_json_string(std::ostream &stream, const std::string_view value) {
    stream.put('"');
    std::size_t index = 0;
    while (index < value.size()) {
        const auto byte = static_cast<unsigned char>(value[index]);
        switch (byte) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                constexpr std::string_view digits = "0123456789abcdef";
                stream << "\\u00" << digits[byte >> 4U] << digits[byte & 0x0fU];
            } else if (byte < 0x80U) {
                stream.put(static_cast<char>(byte));
            } else {
                const auto continuation = [&](const std::size_t offset) {
                    return index + offset < value.size() &&
                           (static_cast<unsigned char>(value[index + offset]) &
                            0xc0U) == 0x80U;
                };
                std::size_t length = 0;
                if (byte >= 0xc2U && byte <= 0xdfU && continuation(1U)) {
                    length = 2U;
                } else if (byte == 0xe0U && continuation(1U) && continuation(2U) &&
                           static_cast<unsigned char>(value[index + 1U]) >= 0xa0U) {
                    length = 3U;
                } else if (((byte >= 0xe1U && byte <= 0xecU) ||
                            (byte >= 0xeeU && byte <= 0xefU)) &&
                           continuation(1U) && continuation(2U)) {
                    length = 3U;
                } else if (byte == 0xedU && continuation(1U) && continuation(2U) &&
                           static_cast<unsigned char>(value[index + 1U]) <= 0x9fU) {
                    length = 3U;
                } else if (byte == 0xf0U && continuation(1U) && continuation(2U) &&
                           continuation(3U) &&
                           static_cast<unsigned char>(value[index + 1U]) >= 0x90U) {
                    length = 4U;
                } else if (byte >= 0xf1U && byte <= 0xf3U && continuation(1U) &&
                           continuation(2U) && continuation(3U)) {
                    length = 4U;
                } else if (byte == 0xf4U && continuation(1U) && continuation(2U) &&
                           continuation(3U) &&
                           static_cast<unsigned char>(value[index + 1U]) <= 0x8fU) {
                    length = 4U;
                }
                if (length == 0U) {
                    stream << "\\ufffd";
                } else {
                    stream.write(value.data() + index,
                                 static_cast<std::streamsize>(length));
                    index += length - 1U;
                }
            }
            break;
        }
        ++index;
    }
    stream.put('"');
}

[[nodiscard]] bool write_json_value(std::ostream &stream,
                                    const authoring::JsonValue value) {
    switch (value.kind()) {
    case authoring::JsonKind::null_value:
        stream << "null";
        return true;
    case authoring::JsonKind::boolean: {
        const auto parsed = value.boolean();
        if (!parsed) {
            return false;
        }
        stream << (*parsed ? "true" : "false");
        return true;
    }
    case authoring::JsonKind::number: {
        const auto parsed = value.number();
        if (!parsed) {
            return false;
        }
        std::array<char, 128> buffer{};
        const auto converted = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), *parsed,
            std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (converted.ec != std::errc{}) {
            return false;
        }
        stream.write(buffer.data(), converted.ptr - buffer.data());
        return true;
    }
    case authoring::JsonKind::string: {
        const auto parsed = value.string();
        if (!parsed) {
            return false;
        }
        write_json_string(stream, *parsed);
        return true;
    }
    case authoring::JsonKind::array:
        stream.put('[');
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index != 0U) {
                stream.put(',');
            }
            if (!write_json_value(stream, value.at(index))) {
                return false;
            }
        }
        stream.put(']');
        return true;
    case authoring::JsonKind::object:
        stream.put('{');
        for (std::size_t index = 0; index < value.size(); ++index) {
            const auto member = value.member_at(index);
            if (!member) {
                return false;
            }
            if (index != 0U) {
                stream.put(',');
            }
            write_json_string(stream, member.key);
            stream.put(':');
            if (!write_json_value(stream, member.value)) {
                return false;
            }
        }
        stream.put('}');
        return true;
    case authoring::JsonKind::invalid:
        return false;
    }
    return false;
}

void write_machine_prefix(CliOutput &output, const bool ok, const std::string_view code,
                          const int exit_code) {
    auto &stream = output.standard_out;
    stream << "{\"schema\":";
    write_json_string(stream, kMachineResultSchema);
    stream << ",\"release_identity\":";
    write_json_string(stream, CRANKWAVE_VERSION_LABEL);
    stream << ",\"command\":";
    write_json_string(stream, output.command);
    stream << ",\"ok\":" << (ok ? "true" : "false") << ",\"code\":";
    write_json_string(stream, code);
    stream << ",\"exit_code\":" << exit_code;
}

[[nodiscard]] int report_error(CliOutput &output, const int exit_code,
                               const std::string_view code,
                               const std::string_view message) {
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, false, code, exit_code);
        output.standard_out << ",\"message\":";
        write_json_string(output.standard_out, message);
        output.standard_out << "}\n";
        return exit_code;
    }
    output.standard_error << "error: " << message << '\n';
    return exit_code;
}

[[nodiscard]] int report_usage_error(CliOutput &output,
                                     const std::string_view message) {
    const auto result = report_error(output, kExitUsage, "usage-error", message);
    if (output.format == CliResultFormat::text) {
        print_usage_hint(output.standard_error);
    }
    return result;
}

[[nodiscard]] int report_controlled_stop(CliOutput &output,
                                         const InvocationExecutionControl &control,
                                         std::string_view message);

[[nodiscard]] int crankwave_cli_exit_code(const CrankwaveCliErrorKind kind) noexcept {
    switch (kind) {
    case CrankwaveCliErrorKind::data_error:
        return kExitDataError;
    case CrankwaveCliErrorKind::no_input:
        return kExitNoInput;
    case CrankwaveCliErrorKind::cant_create:
        return kExitCantCreate;
    case CrankwaveCliErrorKind::unavailable:
        return kExitUnavailable;
    case CrankwaveCliErrorKind::cancelled:
        return kExitTemporaryFailure;
    }
    return kExitSoftware;
}

[[nodiscard]] std::string_view
crankwave_cli_error_code(const CrankwaveCliErrorKind kind) noexcept {
    switch (kind) {
    case CrankwaveCliErrorKind::data_error:
        return "crankwave-data-error";
    case CrankwaveCliErrorKind::no_input:
        return "crankwave-input-unavailable";
    case CrankwaveCliErrorKind::cant_create:
        return "crankwave-output-unavailable";
    case CrankwaveCliErrorKind::unavailable:
        return "crankwave-operation-unavailable";
    case CrankwaveCliErrorKind::cancelled:
        return "crankwave-cancelled";
    }
    return "software-error";
}

[[nodiscard]] int report_crankwave_error(CliOutput &output,
                                         const CrankwaveCliError &error,
                                         const InvocationExecutionControl &control) {
    if (error.kind == CrankwaveCliErrorKind::cancelled) {
        return report_controlled_stop(output, control, error.message);
    }
    return report_error(output, crankwave_cli_exit_code(error.kind),
                        crankwave_cli_error_code(error.kind), error.message);
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

[[nodiscard]] std::string_view
diagnostic_severity_name(const authoring::DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case authoring::DiagnosticSeverity::error:
        return "error";
    case authoring::DiagnosticSeverity::warning:
        return "warning";
    }
    return "unknown";
}

[[nodiscard]] int report_diagnostics(CliOutput &output, const std::string_view stage,
                                     const authoring::DiagnosticReport &report) {
    const auto exit_code = diagnostic_exit_code(report);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, false, "authoring-diagnostics", exit_code);
        auto &stream = output.standard_out;
        stream << ",\"message\":";
        write_json_string(stream, std::string{stage} + " failed");
        stream << ",\"stage\":";
        write_json_string(stream, stage);
        stream << ",\"diagnostics\":[";
        for (std::size_t index = 0; index < report.diagnostics.size(); ++index) {
            const auto &diagnostic = report.diagnostics[index];
            if (index != 0U) {
                stream.put(',');
            }
            stream << "{\"severity\":";
            write_json_string(stream, diagnostic_severity_name(diagnostic.severity));
            stream << ",\"code\":";
            write_json_string(stream, diagnostic_code_name(diagnostic.code));
            stream << ",\"json_pointer\":";
            write_json_string(stream, diagnostic.json_pointer);
            stream << ",\"message\":";
            write_json_string(stream, diagnostic.message);
            stream.put('}');
        }
        stream << "]}\n";
        return exit_code;
    }

    auto &stream = output.standard_error;
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

[[nodiscard]] int report_native_input_error(CliOutput &output,
                                            const std::string_view stage,
                                            const NativeInputError &error) {
    if (error.diagnostics.has_value()) {
        return report_diagnostics(output, stage, *error.diagnostics);
    }
    const auto exit_code = native_input_exit_code(error.kind);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, false, native_input_error_code_label(error.code),
                             exit_code);
        auto &stream = output.standard_out;
        stream << ",\"message\":";
        write_json_string(stream, error.message);
        stream << ",\"stage\":";
        write_json_string(stream, stage);
        stream << ",\"path\":";
        write_json_string(stream, error.path.string());
        stream << ",\"asset_id\":";
        if (error.asset_id.empty()) {
            stream << "null";
        } else {
            write_json_string(stream, error.asset_id);
        }
        stream << "}\n";
        return exit_code;
    }
    return report_error(output, exit_code, native_input_error_code_label(error.code),
                        error.message);
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

[[nodiscard]] int report_native_output_error(CliOutput &output,
                                             const NativeOutputError &error) {
    const auto exit_code = native_output_exit_code(error.kind);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, false, native_output_error_code_label(error.code),
                             exit_code);
        output.standard_out << ",\"message\":";
        write_json_string(output.standard_out, error.message);
        output.standard_out << ",\"path\":";
        write_json_string(output.standard_out, error.path.string());
        output.standard_out << "}\n";
        return exit_code;
    }
    return report_error(output, exit_code, native_output_error_code_label(error.code),
                        error.message);
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

[[nodiscard]] std::string_view
render_failure_code(const contract::FailureKind kind) noexcept {
    using enum contract::FailureKind;
    switch (kind) {
    case invalid_specification:
        return "render-invalid-specification";
    case unreachable_target:
        return "render-unreachable-target";
    case cancelled:
        return "render-cancelled";
    case event_schedule_violation:
        return "render-event-schedule-violation";
    case nonphysical_state:
        return "render-nonphysical-state";
    case numerical_failure:
        return "render-numerical-failure";
    case incomplete_source_route:
        return "render-incomplete-source-route";
    case evidence_rights_failure:
        return "render-evidence-rights-failure";
    case artifact_publication_failure:
        return "render-artifact-publication-failure";
    case contract_violation:
        return "render-contract-violation";
    }
    return "software-error";
}

[[nodiscard]] std::string_view
render_failure_kind_name(const contract::FailureKind kind) noexcept {
    using enum contract::FailureKind;
    switch (kind) {
    case invalid_specification:
        return "invalid_specification";
    case unreachable_target:
        return "unreachable_target";
    case cancelled:
        return "cancelled";
    case event_schedule_violation:
        return "event_schedule_violation";
    case nonphysical_state:
        return "nonphysical_state";
    case numerical_failure:
        return "numerical_failure";
    case incomplete_source_route:
        return "incomplete_source_route";
    case evidence_rights_failure:
        return "evidence_rights_failure";
    case artifact_publication_failure:
        return "artifact_publication_failure";
    case contract_violation:
        return "contract_violation";
    }
    return "unknown";
}

[[nodiscard]] std::string operation_stop_code(const std::string_view command,
                                              const InvocationStopReason reason) {
    auto result = std::string{command};
    switch (reason) {
    case InvocationStopReason::termination:
        result += "-terminated";
        break;
    case InvocationStopReason::deadline:
        result += "-deadline-exceeded";
        break;
    case InvocationStopReason::none:
        result += "-cancelled";
        break;
    }
    return result;
}

[[nodiscard]] InvocationStopReason
observed_stop_reason(const InvocationExecutionControl &control) noexcept {
    return control.reason == nullptr ? InvocationStopReason::none
                                     : control.reason->load(std::memory_order_acquire);
}

[[nodiscard]] int report_controlled_stop(CliOutput &output,
                                         const InvocationExecutionControl &control,
                                         const std::string_view message) {
    const auto code =
        operation_stop_code(output.command, observed_stop_reason(control));
    return report_error(output, kExitTemporaryFailure, code, message);
}

[[nodiscard]] int report_render_failure(CliOutput &output,
                                        const contract::FailureContext &context,
                                        const InvocationExecutionControl &control) {
    const auto exit_code = render_failure_exit_code(context);
    const auto code =
        context.kind == contract::FailureKind::cancelled
            ? operation_stop_code(output.command, observed_stop_reason(control))
            : std::string{render_failure_code(context.kind)};
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, false, code, exit_code);
        auto &stream = output.standard_out;
        stream << ",\"message\":";
        write_json_string(stream, context.state_summary.empty()
                                      ? std::string_view{"render failed"}
                                      : std::string_view{context.state_summary});
        stream << ",\"failure_kind\":";
        write_json_string(stream, render_failure_kind_name(context.kind));
        stream << ",\"detail_code\":";
        if (context.detail_code.empty()) {
            stream << "null";
        } else {
            write_json_string(stream, context.detail_code);
        }
        stream << ",\"sample_index\":\"" << context.sample_index
               << "\",\"step_end_index\":\"" << context.step_end_index << "\"}\n";
        return exit_code;
    }

    auto &stream = output.standard_error;
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

[[nodiscard]] int execute_render(const RenderCommand &command, CliOutput &output,
                                 const InvocationExecutionControl &control) {
    if (control.render.stop_token.stop_requested()) {
        return report_controlled_stop(output, control,
                                      "render stopped before input processing");
    }
    auto engine_input_result =
        command.asset_root.empty()
            ? load_native_engine_input_with_builtin_assets(
                  std::filesystem::path{command.engine_path})
            : load_native_engine_input(std::filesystem::path{command.engine_path},
                                       std::filesystem::path{command.asset_root});
    if (const auto *error = std::get_if<NativeInputError>(&engine_input_result)) {
        return report_native_input_error(output, "engine input", *error);
    }
    auto engine_input = std::get<NativeEngineInput>(std::move(engine_input_result));
    if (control.render.stop_token.stop_requested()) {
        return report_controlled_stop(output, control,
                                      "render stopped after engine input");
    }
    const auto asset_views = engine_input.asset_views();
    auto engine_result = compile::compile_engine(engine_input.document, asset_views);
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&engine_result)) {
        return report_diagnostics(output, "engine compilation", *report);
    }
    auto engine = std::get<compile::CompiledEngine>(std::move(engine_result));
    if (control.render.stop_token.stop_requested()) {
        return report_controlled_stop(output, control,
                                      "render stopped after engine compilation");
    }

    auto scenario_input_result =
        load_native_scenario_input(std::filesystem::path{command.scenario_path});
    if (const auto *error = std::get_if<NativeInputError>(&scenario_input_result)) {
        return report_native_input_error(output, "scenario input", *error);
    }
    auto scenario_document =
        std::get<authoring::ScenarioDocument>(std::move(scenario_input_result));
    auto scenario_result = compile::compile_scenario(engine, scenario_document);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&scenario_result)) {
        return report_diagnostics(output, "scenario compilation", *report);
    }
    auto scenario = std::get<compile::CompiledScenario>(std::move(scenario_result));
    if (control.render.stop_token.stop_requested()) {
        return report_controlled_stop(output, control,
                                      "render stopped after scenario compilation");
    }

    const auto output_result = preflight_native_output_directory(
        std::filesystem::path{command.output_directory});
    if (const auto *error = std::get_if<NativeOutputError>(&output_result)) {
        return report_native_output_error(output, *error);
    }
    const auto &native_output = std::get<NativeOutputDirectory>(output_result);
    artifacts::DirectoryRenderSink sink{native_output.publication_root,
                                        native_output.publication_name};
    const auto result = bake(scenario, sink, control.render);
    if (const auto *failure = std::get_if<contract::RenderFailure>(&result)) {
        return report_render_failure(output, failure->context, control);
    }
    if (const auto *unreachable = std::get_if<contract::UnreachableTarget>(&result)) {
        return report_render_failure(output, unreachable->context, control);
    }
    if (sink.state() != artifacts::DirectoryRenderSinkState::committed) {
        return report_error(
            output, kExitSoftware, "render-commit-missing",
            "renderer reported success without committing the output transaction");
    }

    const auto publication_path = sink.publication_path();
    const auto manifest_path =
        publication_path / artifacts::kSimulationManifestRelativePathV10;
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, true, "success", kExitSuccess);
        output.standard_out << ",\"result\":{\"output_directory\":";
        write_json_string(output.standard_out, publication_path.string());
        output.standard_out << ",\"manifest\":";
        write_json_string(output.standard_out, manifest_path.string());
        output.standard_out << "}}\n";
    } else {
        output.standard_out << "output_directory=" << publication_path.string() << '\n'
                            << "manifest=" << manifest_path.string() << '\n';
    }
    return kExitSuccess;
}

[[nodiscard]] int bake_crankwave_exit_code(const BakeCrankwaveErrorKind kind) noexcept {
    switch (kind) {
    case BakeCrankwaveErrorKind::data_error:
        return kExitDataError;
    case BakeCrankwaveErrorKind::no_input:
        return kExitNoInput;
    case BakeCrankwaveErrorKind::unavailable:
        return kExitUnavailable;
    case BakeCrankwaveErrorKind::software:
        return kExitSoftware;
    case BakeCrankwaveErrorKind::cant_create:
        return kExitCantCreate;
    case BakeCrankwaveErrorKind::temporary_failure:
        return kExitTemporaryFailure;
    case BakeCrankwaveErrorKind::cancelled:
        return kExitTemporaryFailure;
    }
    return kExitSoftware;
}

[[nodiscard]] std::string_view
contract_issue_code_name(const contract::ContractIssueCode code) noexcept {
    using enum contract::ContractIssueCode;
    switch (code) {
    case missing_value:
        return "missing_value";
    case invalid_value:
        return "invalid_value";
    case duplicate_identity:
        return "duplicate_identity";
    case dangling_reference:
        return "dangling_reference";
    case inconsistent_shape:
        return "inconsistent_shape";
    case inconsistent_semantics:
        return "inconsistent_semantics";
    case unsupported_value:
        return "unsupported_value";
    }
    return "unknown_contract_issue";
}

[[nodiscard]] int
report_bake_crankwave_error(CliOutput &output, const BakeCrankwaveError &failure,
                            const InvocationExecutionControl &control) {
    if (failure.kind == BakeCrankwaveErrorKind::cancelled) {
        return report_controlled_stop(output, control, failure.message);
    }
    if (failure.diagnostics.has_value()) {
        return report_diagnostics(output, failure.stage, *failure.diagnostics);
    }
    const auto exit_code = bake_crankwave_exit_code(failure.kind);
    if (output.format != CliResultFormat::json) {
        return report_error(output, exit_code, failure.code,
                            failure.stage.empty()
                                ? failure.message
                                : failure.stage + ": " + failure.message);
    }
    write_machine_prefix(output, false, failure.code, exit_code);
    auto &stream = output.standard_out;
    stream << ",\"message\":";
    write_json_string(stream, failure.message);
    stream << ",\"stage\":";
    write_json_string(stream, failure.stage);
    stream << ",\"path\":";
    if (failure.path.empty()) {
        stream << "null";
    } else {
        write_json_string(stream, failure.path.string());
    }
    stream << ",\"issues\":[";
    if (failure.validation.has_value()) {
        for (std::size_t index = 0U; index < failure.validation->issues.size();
             ++index) {
            if (index != 0U) {
                stream.put(',');
            }
            const auto &issue = failure.validation->issues[index];
            stream << "{\"code\":";
            write_json_string(stream, contract_issue_code_name(issue.code));
            stream << ",\"path\":";
            write_json_string(stream, issue.path);
            stream << ",\"message\":";
            write_json_string(stream, issue.message);
            stream.put('}');
        }
    }
    stream << "]}\n";
    return exit_code;
}

[[nodiscard]] int execute_bake_crankwave(const BakeCrankwaveCommand &command,
                                         CliOutput &output,
                                         const InvocationExecutionControl &control) {
    BakeCrankwaveRequest request;
    request.engine_path = command.engine_path;
    request.output_file = command.output_file;
    if (!command.asset_root.empty()) {
        request.asset_root = command.asset_root;
    }
    request.release_identity = std::string{version_label()};
    auto result = bake_crankwave_native(request, control.render.stop_token);
    if (const auto *failure = std::get_if<BakeCrankwaveError>(&result)) {
        return report_bake_crankwave_error(output, *failure, control);
    }
    const auto &baked = std::get<BakedCrankwaveFile>(result);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, true, "success", kExitSuccess);
        auto &stream = output.standard_out;
        stream << ",\"result\":{\"output_file\":";
        write_json_string(stream, baked.output_path.string());
        stream << ",\"engine_id\":";
        write_json_string(stream, baked.engine_id);
        stream << ",\"profile_id\":";
        write_json_string(stream, baked.profile_id);
        stream << ",\"verified\":" << (baked.verified ? "true" : "false")
               << ",\"container_bytes\":\"" << baked.container_byte_count
               << "\",\"entry_count\":" << baked.entry_count
               << ",\"held_cell_count\":" << baked.held_cell_count
               << ",\"directional_capture_count\":" << baked.directional_capture_count
               << ",\"lifecycle_capture_count\":" << baked.lifecycle_capture_count
               << ",\"container_sha256\":";
        write_json_string(stream, sha256_lower_hex(baked.container_sha256));
        stream << ",\"cache_identity_sha256\":";
        write_json_string(stream, sha256_lower_hex(baked.cache_identity_sha256));
        stream << "}}\n";
    } else {
        output.standard_out
            << "output_file=" << baked.output_path.string() << '\n'
            << "engine_id=" << baked.engine_id << '\n'
            << "profile_id=" << baked.profile_id << '\n'
            << "verified=" << (baked.verified ? "true" : "false") << '\n'
            << "container_bytes=" << baked.container_byte_count << '\n'
            << "entry_count=" << baked.entry_count << '\n'
            << "held_cell_count=" << baked.held_cell_count << '\n'
            << "directional_capture_count=" << baked.directional_capture_count << '\n'
            << "lifecycle_capture_count=" << baked.lifecycle_capture_count << '\n'
            << "container_sha256=" << sha256_lower_hex(baked.container_sha256) << '\n'
            << "cache_identity_sha256=" << sha256_lower_hex(baked.cache_identity_sha256)
            << '\n';
    }
    return kExitSuccess;
}

[[nodiscard]] int execute_pack_crankwave(const PackCrankwaveCommand &command,
                                         CliOutput &output,
                                         const InvocationExecutionControl &control) {
    auto result = pack_crankwave_package_directory(
        std::filesystem::path{command.package_directory},
        std::filesystem::path{command.output_file}, control.render.stop_token);
    if (const auto *error = std::get_if<CrankwaveCliError>(&result)) {
        return report_crankwave_error(output, *error, control);
    }
    const auto &packed = std::get<PackedCrankwaveFile>(result);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, true, "success", kExitSuccess);
        output.standard_out << ",\"result\":{\"output_file\":";
        write_json_string(output.standard_out, packed.output_path.string());
        output.standard_out << ",\"container_bytes\":\"" << packed.container_byte_count
                            << "\",\"entry_count\":" << packed.entry_count
                            << ",\"container_sha256\":";
        write_json_string(output.standard_out,
                          sha256_lower_hex(packed.container_sha256));
        output.standard_out << "}}\n";
    } else {
        output.standard_out << "output_file=" << packed.output_path.string() << '\n'
                            << "container_bytes=" << packed.container_byte_count << '\n'
                            << "entry_count=" << packed.entry_count << '\n'
                            << "container_sha256="
                            << sha256_lower_hex(packed.container_sha256) << '\n';
    }
    return kExitSuccess;
}

[[nodiscard]] int execute_load_crankwave(const std::string &input_file,
                                         const bool verify_payloads, CliOutput &output,
                                         const InvocationExecutionControl &control) {
    auto result = inspect_crankwave_file(std::filesystem::path{input_file},
                                         verify_payloads, control.render.stop_token);
    if (const auto *error = std::get_if<CrankwaveCliError>(&result)) {
        return report_crankwave_error(output, *error, control);
    }
    const auto &loaded = std::get<LoadedCrankwaveFile>(result);
    if (output.format == CliResultFormat::json) {
        write_machine_prefix(output, true, "success", kExitSuccess);
        auto &stream = output.standard_out;
        stream << ",\"result\":{\"crankwave_version\":" << loaded.index.version
               << ",\"verified\":" << (loaded.fully_verified ? "true" : "false")
               << ",\"container_bytes\":\"" << loaded.index.container_byte_count
               << "\",\"index_bytes\":\"" << loaded.index.index_byte_count
               << "\",\"payload_bytes\":\"" << loaded.index.payload_byte_count
               << "\",\"entry_count\":" << loaded.index.entries.size()
               << ",\"container_sha256\":";
        write_json_string(stream, sha256_lower_hex(loaded.container_sha256));
        stream << ",\"index_sha256\":";
        write_json_string(stream, sha256_lower_hex(loaded.index.index_sha256));
        stream << ",\"payload_sha256\":";
        write_json_string(stream, sha256_lower_hex(loaded.index.payload_sha256));
        stream << ",\"entries\":[";
        for (std::size_t index = 0; index < loaded.index.entries.size(); ++index) {
            const auto &entry = loaded.index.entries[index];
            if (index != 0U) {
                stream.put(',');
            }
            stream << "{\"path\":";
            write_json_string(stream, entry.path);
            stream << ",\"payload_bytes\":\"" << entry.payload_byte_count
                   << "\",\"payload_sha256\":";
            write_json_string(stream, sha256_lower_hex(entry.payload_sha256));
            stream.put('}');
        }
        stream << "],\"package\":";
        if (const auto *package =
                std::get_if<artifacts::CrankwavePackageDescriptor>(&loaded.package)) {
            stream << "{\"engine_id\":";
            write_json_string(stream, package->engine_id);
            stream << ",\"runtime_kind\":";
            write_json_string(stream, package->runtime.kind);
            stream << ",\"runtime_manifest_path\":";
            write_json_string(stream, package->runtime.manifest_path);
            stream << ",\"runtime_manifest_sha256\":";
            write_json_string(stream,
                              sha256_lower_hex(package->runtime.manifest_sha256));
            stream.put('}');
        } else {
            stream << "null";
        }
        stream << "}}\n";
    } else {
        output.standard_out
            << "crankwave_version=" << loaded.index.version << '\n'
            << "verified=" << (loaded.fully_verified ? "true" : "false") << '\n'
            << "container_bytes=" << loaded.index.container_byte_count << '\n'
            << "index_bytes=" << loaded.index.index_byte_count << '\n'
            << "payload_bytes=" << loaded.index.payload_byte_count << '\n'
            << "entry_count=" << loaded.index.entries.size() << '\n'
            << "container_sha256=" << sha256_lower_hex(loaded.container_sha256) << '\n'
            << "index_sha256=" << sha256_lower_hex(loaded.index.index_sha256) << '\n'
            << "payload_sha256=" << sha256_lower_hex(loaded.index.payload_sha256)
            << '\n';
        for (const auto &entry : loaded.index.entries) {
            output.standard_out << "entry=" << entry.path << '\t'
                                << entry.payload_byte_count << '\t'
                                << sha256_lower_hex(entry.payload_sha256) << '\n';
        }
        if (const auto *package =
                std::get_if<artifacts::CrankwavePackageDescriptor>(&loaded.package)) {
            output.standard_out
                << "engine_id=" << package->engine_id << '\n'
                << "runtime_kind=" << package->runtime.kind << '\n'
                << "runtime_manifest_path=" << package->runtime.manifest_path << '\n'
                << "runtime_manifest_sha256="
                << sha256_lower_hex(package->runtime.manifest_sha256) << '\n';
        }
    }
    return kExitSuccess;
}

[[nodiscard]] int execute_inspect_ir_catalog(CliOutput &output) {
    auto loaded =
        load_ir_authoring_catalog_with_builtin_assets(CRANKWAVE_VERSION_LABEL);
    if (const auto *error = std::get_if<NativeInputError>(&loaded)) {
        return report_native_input_error(output, "IR authoring catalog", *error);
    }
    const auto &catalog = std::get<IrAuthoringCatalogDocument>(loaded);
    auto parsed = authoring::parse_json(catalog.json);
    if (std::holds_alternative<authoring::JsonParseError>(parsed)) {
        return report_error(output, kExitSoftware, "software-error",
                            "validated IR authoring catalog could not be encoded");
    }
    auto document = std::get<authoring::JsonDocument>(std::move(parsed));
    if (output.format == CliResultFormat::json) {
        std::ostringstream encoded_catalog;
        if (!write_json_value(encoded_catalog, document.root())) {
            return report_error(output, kExitSoftware, "software-error",
                                "IR authoring catalog encoding failed");
        }
        write_machine_prefix(output, true, "success", kExitSuccess);
        auto &stream = output.standard_out;
        stream << ",\"result\":{\"catalog_sha256\":";
        write_json_string(stream, sha256_lower_hex(catalog.sha256));
        stream << ",\"entry_count\":" << catalog.entry_count << ",\"catalog\":";
        stream << encoded_catalog.str() << "}}\n";
    } else {
        output.standard_out << "release_identity=" << catalog.release_identity << '\n'
                            << "catalog_sha256=" << sha256_lower_hex(catalog.sha256)
                            << '\n'
                            << "entry_count=" << catalog.entry_count << '\n';
    }
    return kExitSuccess;
}

template <class Command, std::size_t Size>
[[nodiscard]] CliParseResult
parse_command_options(const std::span<const std::string_view> arguments,
                      Command command,
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
            const auto classification =
                token.starts_with('-') ? "unknown option '" : "unexpected argument '";
            return usage_error(std::string{classification} + std::string{token} + "'");
        }
        if (option->seen) {
            return usage_error("duplicate option '" + std::string{token} + "'");
        }
        if (index + 1U >= arguments.size() || arguments[index + 1U].starts_with("--")) {
            return usage_error("option '" + std::string{token} +
                               "' requires a separate value");
        }
        const auto value = arguments[index + 1U];
        if (value.empty()) {
            return usage_error("option '" + std::string{token} +
                               "' requires a non-empty value");
        }
        switch (option->kind) {
        case CommandOptionKind::string:
            command.*(option->value) = value;
            break;
        case CommandOptionKind::result_format:
            if (value == "text") {
                command.result_format = CliResultFormat::text;
            } else if (value == "json") {
                command.result_format = CliResultFormat::json;
            } else {
                return usage_error(
                    "option '--result-format' requires 'text' or 'json'");
            }
            break;
        case CommandOptionKind::deadline_unix_ms:
            std::uint64_t parsed = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
            if (error != std::errc{} || end != value.data() + value.size() ||
                parsed == 0U ||
                parsed > static_cast<std::uint64_t>(
                             std::numeric_limits<std::int64_t>::max())) {
                return usage_error("option '--deadline-unix-ms' requires a positive "
                                   "base-10 int64 value");
            }
            command.deadline_unix_ms = parsed;
            break;
        }
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

[[nodiscard]] CliResultFormat
requested_result_format(const std::span<const std::string_view> arguments) noexcept {
    for (std::size_t index = 0; index + 1U < arguments.size(); ++index) {
        if (arguments[index] == "--result-format" && arguments[index + 1U] == "json") {
            return CliResultFormat::json;
        }
    }
    return CliResultFormat::text;
}

[[nodiscard]] std::string_view
requested_command(const std::span<const std::string_view> arguments) noexcept {
    if (arguments.empty() || arguments.front().starts_with('-')) {
        return "cli";
    }
    return arguments.front();
}

void request_invocation_stop(std::stop_source &source,
                             std::atomic<InvocationStopReason> &reason,
                             const InvocationStopReason requested_reason) noexcept {
    auto expected = InvocationStopReason::none;
    static_cast<void>(reason.compare_exchange_strong(expected, requested_reason,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire));
    static_cast<void>(source.request_stop());
}

template <class Operation>
[[nodiscard]] int execute_with_control(const MachineResultOptions &options,
                                       const std::stop_token termination_token,
                                       Operation &&operation) {
    std::stop_source invocation_stop_source;
    std::atomic reason{InvocationStopReason::none};
    std::stop_callback termination_callback{termination_token, [&] {
                                                request_invocation_stop(
                                                    invocation_stop_source, reason,
                                                    InvocationStopReason::termination);
                                            }};
    std::optional<std::jthread> deadline_thread;
    if (options.deadline_unix_ms.has_value()) {
        const auto deadline = *options.deadline_unix_ms;
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        if (now >= 0 && static_cast<std::uint64_t>(now) >= deadline) {
            request_invocation_stop(invocation_stop_source, reason,
                                    InvocationStopReason::deadline);
        } else {
            deadline_thread.emplace([&, deadline](const std::stop_token local_stop) {
                constexpr auto maximum_poll = std::chrono::milliseconds{10};
                while (!local_stop.stop_requested()) {
                    const auto now =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
                    if (now >= 0 && static_cast<std::uint64_t>(now) >= deadline) {
                        request_invocation_stop(invocation_stop_source, reason,
                                                InvocationStopReason::deadline);
                        return;
                    }
                    const auto remaining =
                        now < 0 ? deadline : deadline - static_cast<std::uint64_t>(now);
                    const auto delay = std::chrono::milliseconds{
                        static_cast<std::int64_t>(std::min<std::uint64_t>(
                            remaining,
                            static_cast<std::uint64_t>(maximum_poll.count())))};
                    std::this_thread::sleep_for(delay);
                }
            });
        }
    }

    return std::forward<Operation>(operation)(InvocationExecutionControl{
        RenderControl{invocation_stop_source.get_token()}, &reason});
}

} // namespace

std::string_view version_label() noexcept {
    return CRANKWAVE_VERSION_LABEL;
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
                CommandOption<RenderCommand>{"--engine", &RenderCommand::engine_path},
                CommandOption<RenderCommand>{"--scenario",
                                             &RenderCommand::scenario_path},
                CommandOption<RenderCommand>{"--asset-root", &RenderCommand::asset_root,
                                             false},
                CommandOption<RenderCommand>{"--output-directory",
                                             &RenderCommand::output_directory},
                CommandOption<RenderCommand>{"--deadline-unix-ms", nullptr, false,
                                             false,
                                             CommandOptionKind::deadline_unix_ms},
                CommandOption<RenderCommand>{"--result-format", nullptr, false, false,
                                             CommandOptionKind::result_format},
            });
    }
    if (arguments.front() == "pack-crankwave") {
        return parse_command_options(
            arguments, PackCrankwaveCommand{},
            std::array{
                CommandOption<PackCrankwaveCommand>{
                    "--package-directory", &PackCrankwaveCommand::package_directory},
                CommandOption<PackCrankwaveCommand>{"--output",
                                                    &PackCrankwaveCommand::output_file},
                CommandOption<PackCrankwaveCommand>{
                    "--deadline-unix-ms", nullptr, false, false,
                    CommandOptionKind::deadline_unix_ms},
                CommandOption<PackCrankwaveCommand>{"--result-format", nullptr, false,
                                                    false,
                                                    CommandOptionKind::result_format},
            });
    }
    if (arguments.front() == "bake-crankwave") {
        return parse_command_options(
            arguments, BakeCrankwaveCommand{},
            std::array{
                CommandOption<BakeCrankwaveCommand>{"--engine",
                                                    &BakeCrankwaveCommand::engine_path},
                CommandOption<BakeCrankwaveCommand>{"--output",
                                                    &BakeCrankwaveCommand::output_file},
                CommandOption<BakeCrankwaveCommand>{
                    "--asset-root", &BakeCrankwaveCommand::asset_root, false},
                CommandOption<BakeCrankwaveCommand>{
                    "--deadline-unix-ms", nullptr, false, false,
                    CommandOptionKind::deadline_unix_ms},
                CommandOption<BakeCrankwaveCommand>{"--result-format", nullptr, false,
                                                    false,
                                                    CommandOptionKind::result_format},
            });
    }
    if (arguments.front() == "inspect-crankwave") {
        return parse_command_options(
            arguments, InspectCrankwaveCommand{},
            std::array{
                CommandOption<InspectCrankwaveCommand>{
                    "--input", &InspectCrankwaveCommand::input_file},
                CommandOption<InspectCrankwaveCommand>{
                    "--deadline-unix-ms", nullptr, false, false,
                    CommandOptionKind::deadline_unix_ms},
                CommandOption<InspectCrankwaveCommand>{
                    "--result-format", nullptr, false, false,
                    CommandOptionKind::result_format},
            });
    }
    if (arguments.front() == "verify-crankwave") {
        return parse_command_options(
            arguments, VerifyCrankwaveCommand{},
            std::array{
                CommandOption<VerifyCrankwaveCommand>{
                    "--input", &VerifyCrankwaveCommand::input_file},
                CommandOption<VerifyCrankwaveCommand>{
                    "--deadline-unix-ms", nullptr, false, false,
                    CommandOptionKind::deadline_unix_ms},
                CommandOption<VerifyCrankwaveCommand>{"--result-format", nullptr, false,
                                                      false,
                                                      CommandOptionKind::result_format},
            });
    }
    if (arguments.front() == "inspect-ir-catalog") {
        return parse_command_options(arguments, InspectIrCatalogCommand{},
                                     std::array{CommandOption<InspectIrCatalogCommand>{
                                         "--result-format", nullptr, false, false,
                                         CommandOptionKind::result_format}});
    }
    return usage_error("unknown command '" + std::string{arguments.front()} + "'");
}

int run_cli(const std::span<const std::string_view> arguments,
            std::ostream &standard_out, std::ostream &standard_error,
            const std::stop_token termination_token) {
    CliOutput output{requested_result_format(arguments), requested_command(arguments),
                     standard_out, standard_error};
    try {
        const auto parsed = parse_cli_arguments(arguments);
        if (const auto *error = std::get_if<CliUsageError>(&parsed)) {
            return report_usage_error(output, error->message);
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
            output.format = render->result_format;
            return execute_with_control(*render, termination_token,
                                        [&](const InvocationExecutionControl &control) {
                                            return execute_render(*render, output,
                                                                  control);
                                        });
        }
        if (const auto *bake_crankwave = std::get_if<BakeCrankwaveCommand>(&command)) {
            output.format = bake_crankwave->result_format;
            return execute_with_control(*bake_crankwave, termination_token,
                                        [&](const InvocationExecutionControl &control) {
                                            return execute_bake_crankwave(
                                                *bake_crankwave, output, control);
                                        });
        }
        if (const auto *pack = std::get_if<PackCrankwaveCommand>(&command)) {
            output.format = pack->result_format;
            return execute_with_control(*pack, termination_token,
                                        [&](const InvocationExecutionControl &control) {
                                            return execute_pack_crankwave(*pack, output,
                                                                          control);
                                        });
        }
        if (const auto *inspect = std::get_if<InspectCrankwaveCommand>(&command)) {
            output.format = inspect->result_format;
            return execute_with_control(*inspect, termination_token,
                                        [&](const InvocationExecutionControl &control) {
                                            return execute_load_crankwave(
                                                inspect->input_file, false, output,
                                                control);
                                        });
        }
        if (const auto *verify = std::get_if<VerifyCrankwaveCommand>(&command)) {
            output.format = verify->result_format;
            return execute_with_control(*verify, termination_token,
                                        [&](const InvocationExecutionControl &control) {
                                            return execute_load_crankwave(
                                                verify->input_file, true, output,
                                                control);
                                        });
        }
        const auto &catalog = std::get<InspectIrCatalogCommand>(command);
        output.format = catalog.result_format;
        return execute_inspect_ir_catalog(output);
    } catch (const std::bad_alloc &) {
        return report_error(output, kExitSoftware, "memory-exhausted",
                            "insufficient memory while processing the request");
    } catch (const std::exception &error) {
        return report_error(output, kExitSoftware, "software-error",
                            std::string{"unexpected application failure: "} +
                                error.what());
    } catch (...) {
        return report_error(output, kExitSoftware, "software-error",
                            "unexpected non-standard application failure");
    }
}

} // namespace crankwave::cli
