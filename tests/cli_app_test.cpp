#include "cli_app.hpp"
#include "native_responsive_bake_identity.hpp"

#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using engine_sim_offline::cli::BakeVehicleEngineCommand;
using engine_sim_offline::cli::CliCommand;
using engine_sim_offline::cli::CliParseResult;
using engine_sim_offline::cli::CliResultFormat;
using engine_sim_offline::cli::CliUsageError;
using engine_sim_offline::cli::InspectIrCatalogCommand;
using engine_sim_offline::cli::InspectVehicleEngineCommand;
using engine_sim_offline::cli::PackVehicleEngineCommand;
using engine_sim_offline::cli::RenderCommand;
using engine_sim_offline::cli::VerifyVehicleEngineCommand;

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
    const auto canonical = parse({"render", "--engine", "engine.json", "--scenario",
                                  "scenario.json", "--output-directory", "result"});
    const auto &render = require_render(canonical);
    expect(render.engine_path == "engine.json", "engine value was not retained");
    expect(render.scenario_path == "scenario.json", "scenario value was not retained");
    expect(render.asset_root.empty(),
           "omitted asset-root did not select built-in resolution");
    expect(render.output_directory == "result",
           "output-directory value was not retained");

    const auto reordered =
        parse({"render", "--output-directory", "out", "--asset-root", "root",
               "--scenario", "drive.json", "--engine", "motor.json",
               "--deadline-unix-ms", "1786057200000", "--result-format", "json"});
    const auto &other = require_render(reordered);
    expect(other.engine_path == "motor.json" && other.scenario_path == "drive.json" &&
               other.asset_root == "root" && other.output_directory == "out" &&
               other.deadline_unix_ms == 1786057200000ULL &&
               other.result_format == CliResultFormat::json,
           "render flags and the developer override must be order-independent");
}

void test_exact_vehicleengine_grammars() {
    const auto baked =
        parse({"bake-vehicleengine", "--output", "engine.vehicleengine", "--result-format",
               "json", "--asset-root", "assets", "--deadline-unix-ms", "1786057200000",
               "--engine", "engine.json"});
    const auto *baked_command = std::get_if<CliCommand>(&baked);
    expect(baked_command != nullptr, "valid bake-vehicleengine syntax was rejected");
    const auto *bake = std::get_if<BakeVehicleEngineCommand>(baked_command);
    expect(bake != nullptr && bake->engine_path == "engine.json" &&
               bake->asset_root == "assets" &&
               bake->output_file == "engine.vehicleengine" &&
               bake->deadline_unix_ms == 1786057200000ULL &&
               bake->result_format == CliResultFormat::json,
           "bake-vehicleengine options were not retained order-independently");

    const auto packed = parse({"pack-vehicleengine", "--output", "engine.vehicleengine",
                               "--result-format", "json", "--deadline-unix-ms",
                               "1786057200001", "--package-directory", "package"});
    const auto *packed_command = std::get_if<CliCommand>(&packed);
    expect(packed_command != nullptr, "valid pack-vehicleengine syntax was rejected");
    const auto *pack = std::get_if<PackVehicleEngineCommand>(packed_command);
    expect(pack != nullptr && pack->package_directory == "package" &&
               pack->output_file == "engine.vehicleengine" &&
               pack->deadline_unix_ms == 1786057200001ULL &&
               pack->result_format == CliResultFormat::json,
           "pack-vehicleengine options were not retained order-independently");

    const auto inspected =
        parse({"inspect-vehicleengine", "--deadline-unix-ms", "1786057200002", "--input",
               "a.vehicleengine", "--result-format", "json"});
    const auto *inspected_command = std::get_if<CliCommand>(&inspected);
    expect(inspected_command != nullptr, "valid inspect-vehicleengine syntax was rejected");
    const auto *inspect = std::get_if<InspectVehicleEngineCommand>(inspected_command);
    expect(inspect != nullptr && inspect->input_file == "a.vehicleengine" &&
               inspect->deadline_unix_ms == 1786057200002ULL &&
               inspect->result_format == CliResultFormat::json,
           "inspect-vehicleengine input was not retained");

    const auto verified = parse({"verify-vehicleengine", "--input", "b.vehicleengine",
                                 "--deadline-unix-ms", "1786057200003"});
    const auto *verified_command = std::get_if<CliCommand>(&verified);
    expect(verified_command != nullptr, "valid verify-vehicleengine syntax was rejected");
    const auto *verify = std::get_if<VerifyVehicleEngineCommand>(verified_command);
    expect(verify != nullptr && verify->input_file == "b.vehicleengine" &&
               verify->deadline_unix_ms == 1786057200003ULL,
           "verify-vehicleengine input was not retained");
}

[[nodiscard]] std::string
digest_hex(const engine_sim_offline::contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

void test_native_responsive_authority_golden() {
    const auto authority =
        engine_sim_offline::cli::native_responsive_bake_authority_v1();
    expect(authority.method_authority_preimage.size() == 2058U &&
               digest_hex(authority.method_authority_sha256) ==
                   "8b97b7ad80b9497cc375da53c41d7cc4b5557c77ac134d04e687404ed61b8760",
           "native responsive method-authority preimage or digest changed");
    const auto bake_recipe_digest = digest_hex(authority.bake_recipe_sha256);
    expect(authority.bake_recipe_preimage.size() == 3195U &&
               bake_recipe_digest ==
                   "a0ec4024df72c2df92c95855f70132381cdce8266bc061df15d07b0891a196df",
           std::string{"native responsive bake-recipe preimage or digest changed: size="} +
               std::to_string(authority.bake_recipe_preimage.size()) +
               " digest=" + bake_recipe_digest);
}

void test_ir_authoring_catalog_grammar() {
    const auto default_output = parse({"inspect-ir-catalog"});
    const auto *default_command = std::get_if<CliCommand>(&default_output);
    expect(default_command != nullptr &&
               std::holds_alternative<InspectIrCatalogCommand>(*default_command),
           "IR authoring catalog command was rejected");

    const auto machine = parse({"inspect-ir-catalog", "--result-format", "json"});
    const auto *machine_command = std::get_if<CliCommand>(&machine);
    const auto *catalog = machine_command == nullptr
                              ? nullptr
                              : std::get_if<InspectIrCatalogCommand>(machine_command);
    expect(catalog != nullptr && catalog->result_format == CliResultFormat::json,
           "IR authoring catalog machine format was not retained");
}

void test_strict_render_rejections() {
    const std::vector<std::vector<std::string_view>> invalid{
        {},
        {"render"},
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "a"},
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o", "extra"},
        {"render", "--engine", "e", "--scenario", "s", "--output-directory", "o",
         "--deadline-unix-ms", "0"},
        {"render", "--engine", "e", "--scenario", "s", "--output-directory", "o",
         "--deadline-unix-ms", "-1"},
        {"render", "--engine", "e", "--scenario", "s", "--output-directory", "o",
         "--deadline-unix-ms", "9223372036854775808"},
        {"render", "--engine", "e", "--scenario", "s", "--output-directory", "o",
         "--result-format", "yaml"},
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
        {"render", "--engine", "e", "--scenario", "s", "--asset-root", "first",
         "--asset-root", "second", "--output-directory", "o"},
        {"render", "--engine", "", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "--engine", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"render", "--help", "--engine", "e", "--scenario", "s", "--asset-root", "a",
         "--output-directory", "o"},
        {"pack-vehicleengine"},
        {"bake-vehicleengine"},
        {"bake-vehicleengine", "--engine", "engine.json"},
        {"bake-vehicleengine", "--output", "engine.vehicleengine"},
        {"bake-vehicleengine", "--engine", "engine.json", "--output", "one.vehicleengine",
         "--output", "two.vehicleengine"},
        {"bake-vehicleengine", "--engine", "engine.json", "--output", "engine.vehicleengine",
         "--deadline-unix-ms", "0"},
        {"pack-vehicleengine", "--package-directory", "package"},
        {"pack-vehicleengine", "--package-directory", "package", "--output",
         "one.vehicleengine", "--output", "two.vehicleengine"},
        {"pack-vehicleengine", "--package-directory", "package", "--output",
         "one.vehicleengine", "--deadline-unix-ms", "0"},
        {"pack-vehicleengine", "--package-directory=package", "--output",
         "engine.vehicleengine"},
        {"inspect-vehicleengine"},
        {"inspect-vehicleengine", "--input", "one", "extra"},
        {"inspect-vehicleengine", "--input", "one", "--deadline-unix-ms", "-1"},
        {"verify-vehicleengine", "--output", "one"},
        {"verify-vehicleengine", "--input", "one", "--deadline-unix-ms",
         "9223372036854775808"},
        {"inspect-ir-catalog", "extra"},
        {"inspect-ir-catalog", "--result-format", "yaml"},
        {"inspect-ir-catalog", "--result-format", "json", "--result-format", "json"},
        {"bake-atlas", "--engine", "e", "--atlas-bake", "a", "--asset-root", "assets",
         "--output-directory", "out"},
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

[[nodiscard]] Invocation invoke(const std::span<const std::string_view> arguments,
                                const std::stop_token termination_token = {}) {
    std::ostringstream standard_out;
    std::ostringstream standard_error;
    const auto exit_code = engine_sim_offline::cli::run_cli(
        arguments, standard_out, standard_error, termination_token);
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
    expect(help.standard_out.find("[--asset-root <developer-directory>]") !=
                   std::string::npos &&
               help.standard_out.find("bundled content-addressed asset catalog") !=
                   std::string::npos,
           "--help must distinguish default assets from the developer override");
    expect(help.standard_out.find("bake-atlas") == std::string::npos,
           "--help must not advertise the withdrawn atlas baker");
    expect(help.standard_out.find("pack-vehicleengine --package-directory <directory>") !=
                   std::string::npos &&
               help.standard_out.find("inspect-vehicleengine --input <file.vehicleengine>") !=
                   std::string::npos &&
               help.standard_out.find("verify-vehicleengine --input <file.vehicleengine>") !=
                   std::string::npos,
           "--help must advertise the exact VEHICLEENGINE command grammar");
    expect(help.standard_out.find("bake-vehicleengine --engine <engine.json>") !=
               std::string::npos,
           "--help must advertise the native responsive bake command");
    expect(help.standard_out.find("inspect-ir-catalog") != std::string::npos,
           "--help must advertise the installed IR authoring query");
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

    const auto machine_missing =
        invoke({"render", "--result-format", "json", "--engine", "only-engine"});
    expect(machine_missing.exit_code == engine_sim_offline::cli::kExitUsage,
           "machine usage failure must return EX_USAGE");
    expect(machine_missing.standard_error.empty(),
           "machine usage failure must reserve stderr for process diagnostics");
    expect(machine_missing.standard_out.find(
               "\"schema\":\"engine-sim-offline.cli-result.v1\"") !=
                   std::string::npos &&
               machine_missing.standard_out.find("\"command\":\"render\"") !=
                   std::string::npos &&
               machine_missing.standard_out.find("\"ok\":false") != std::string::npos &&
               machine_missing.standard_out.find("\"code\":\"usage-error\"") !=
                   std::string::npos &&
               machine_missing.standard_out.back() == '\n' &&
               machine_missing.standard_out.find('\n') ==
                   machine_missing.standard_out.size() - 1U,
           "machine usage failure must be one stable JSON line");
}

void test_machine_failures_and_external_stop() {
    const auto missing =
        invoke({"render", "--engine", "/definitely/missing/engine.json", "--scenario",
                "/definitely/missing/scenario.json", "--output-directory", "unused",
                "--result-format", "json"});
    expect(missing.exit_code != engine_sim_offline::cli::kExitSuccess,
           "machine missing input must fail");
    expect(missing.standard_error.empty(),
           "machine input failure must not write human diagnostics");
    expect(
        missing.standard_out.find("\"schema\":\"engine-sim-offline.cli-result.v1\"") !=
                std::string::npos &&
            missing.standard_out.find("\"stage\":\"engine input\"") !=
                std::string::npos,
        "machine input failure lost its stable code or stage");

    const std::string unicode_engine_path = "/definitely/missing/\xc3\xa9ngine.json";
    const std::array<std::string_view, 11> unicode_arguments{
        "render",
        "--engine",
        unicode_engine_path,
        "--scenario",
        "/definitely/missing/scenario.json",
        "--output-directory",
        "unused",
        "--asset-root",
        "/tmp",
        "--result-format",
        "json"};
    const auto unicode_failure = invoke(unicode_arguments);
    expect(unicode_failure.exit_code != engine_sim_offline::cli::kExitSuccess &&
               unicode_failure.standard_error.empty(),
           "Unicode machine failure did not use the machine channel");
    expect(unicode_failure.standard_out.find(unicode_engine_path) !=
                   std::string::npos &&
               unicode_failure.standard_out.find("\\u00c3\\u00a9") == std::string::npos,
           "machine JSON did not preserve valid UTF-8 path bytes");

    std::stop_source termination;
    termination.request_stop();
    std::ostringstream standard_out;
    std::ostringstream standard_error;
    const std::array<std::string_view, 10> arguments{"render",
                                                     "--engine",
                                                     "unused-engine",
                                                     "--scenario",
                                                     "unused-scenario",
                                                     "--output-directory",
                                                     "unused-output",
                                                     "--result-format",
                                                     "json",
                                                     ""};
    const auto exit_code = engine_sim_offline::cli::run_cli(
        std::span<const std::string_view>{arguments.data(), arguments.size() - 1U},
        standard_out, standard_error, termination.get_token());
    expect(exit_code == engine_sim_offline::cli::kExitTemporaryFailure,
           "pre-requested termination must return EX_TEMPFAIL");
    expect(standard_error.str().empty() &&
               standard_out.str().find("\"code\":\"render-terminated\"") !=
                   std::string::npos,
           "pre-requested termination must produce a machine cancellation result");

    const std::array<std::string_view, 7> pack_arguments{"pack-vehicleengine",
                                                         "--package-directory",
                                                         "unused-package",
                                                         "--output",
                                                         "unused.vehicleengine",
                                                         "--result-format",
                                                         "json"};
    const auto stopped_pack = invoke(pack_arguments, termination.get_token());
    expect(stopped_pack.exit_code == engine_sim_offline::cli::kExitTemporaryFailure &&
               stopped_pack.standard_error.empty() &&
               stopped_pack.standard_out.find(
                   "\"code\":\"pack-vehicleengine-terminated\"") != std::string::npos,
           "pre-requested pack termination lost its stable machine result");

    const std::array<std::string_view, 7> bake_arguments{
        "bake-vehicleengine",   "--engine",        "unused-engine", "--output",
        "unused.vehicleengine", "--result-format", "json"};
    const auto stopped_bake = invoke(bake_arguments, termination.get_token());
    expect(stopped_bake.exit_code == engine_sim_offline::cli::kExitTemporaryFailure &&
               stopped_bake.standard_error.empty() &&
               stopped_bake.standard_out.find(
                   "\"code\":\"bake-vehicleengine-terminated\"") != std::string::npos,
           "pre-requested native bake termination lost its stable machine result");

    const std::array<std::string_view, 5> verify_arguments{
        "verify-vehicleengine", "--input", "unused.vehicleengine", "--result-format", "json"};
    const auto stopped_verify = invoke(verify_arguments, termination.get_token());
    expect(stopped_verify.exit_code == engine_sim_offline::cli::kExitTemporaryFailure &&
               stopped_verify.standard_error.empty() &&
               stopped_verify.standard_out.find(
                   "\"code\":\"verify-vehicleengine-terminated\"") != std::string::npos,
           "pre-requested verify termination lost its stable machine result");

    const std::array<std::string_view, 7> expired_pack_arguments{"pack-vehicleengine",
                                                                 "--package-directory",
                                                                 "unused-package",
                                                                 "--output",
                                                                 "unused.vehicleengine",
                                                                 "--deadline-unix-ms",
                                                                 "1"};
    const auto expired_pack = invoke(expired_pack_arguments);
    expect(expired_pack.exit_code == engine_sim_offline::cli::kExitTemporaryFailure &&
               expired_pack.standard_out.empty() &&
               expired_pack.standard_error.find("error:") != std::string::npos,
           "expired pack deadline did not preserve human-mode diagnostics");

    const std::array<std::string_view, 7> expired_verify_arguments{
        "verify-vehicleengine", "--input", "unused.vehicleengine", "--deadline-unix-ms", "1",
        "--result-format",  "json"};
    const auto expired_verify = invoke(expired_verify_arguments);
    expect(expired_verify.exit_code == engine_sim_offline::cli::kExitTemporaryFailure &&
               expired_verify.standard_error.empty() &&
               expired_verify.standard_out.find(
                   "\"code\":\"verify-vehicleengine-deadline-exceeded\"") !=
                   std::string::npos,
           "expired verify deadline lost its stable machine result");
}

} // namespace

int main() {
    try {
        test_exact_render_grammar();
        test_exact_vehicleengine_grammars();
        test_native_responsive_authority_golden();
        test_ir_authoring_catalog_grammar();
        test_strict_render_rejections();
        test_standalone_help_and_version();
        test_usage_output_channels();
        test_machine_failures_and_external_stop();
    } catch (const std::exception &error) {
        std::cerr << "CLI application test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
