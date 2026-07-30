#include "bmw_m52b28_migration_test_support.hpp"

#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "engine_sim_offline/render.hpp"

#include "reference/bmw_p18_render_specification.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

namespace migration = engine_sim_offline::test::bmw_m52b28_migration;
namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace contract = engine_sim_offline::contract;
namespace profiles = engine_sim_offline::profiles;
namespace reference = engine_sim_offline::reference;

template <class Value>
[[nodiscard]] Value require_parse(
    std::variant<Value, authoring::DiagnosticReport> result,
    const std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        std::string message{context};
        if (!report->diagnostics.empty()) {
            message += " at " + report->diagnostics.front().json_pointer + ": " +
                       report->diagnostics.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<Value>(std::move(result));
}

template <class Value>
[[nodiscard]] Value require_compile(compile::CompileResult<Value> result,
                                    const std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        std::string message{context};
        if (!report->diagnostics.empty()) {
            message += " at " + report->diagnostics.front().json_pointer + ": " +
                       report->diagnostics.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] profiles::BmwM52b28InertialDynoListeningRequest
make_oracle_request() {
    auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&result)) {
        std::string message{"temporary BMW oracle factory was rejected"};
        if (!report->issues.empty()) {
            message += " at " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<profiles::BmwM52b28InertialDynoListeningRequest>(
        std::move(result));
}

[[nodiscard]] contract::RandomPlan require_random_plan(
    contract::RandomPlanCompilationResult result,
    const std::string_view context) {
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message{context};
        if (!report->issues.empty()) {
            message += " at " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return std::get<contract::RandomPlan>(std::move(result));
}

void run_test(const std::filesystem::path &repository_root) {
    const auto engine_json =
        repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_json =
        repository_root /
        "data/engines/bmw-m52b28/scenarios/"
        "inertial-dyno-1500-6500rpm.json";

    const auto engine_document = require_parse(
        authoring::parse_engine_document(migration::read_text(engine_json)),
        "BMW engine JSON parse failed");
    const auto scenario_document = require_parse(
        authoring::parse_scenario_document(migration::read_text(scenario_json)),
        "BMW scenario JSON parse failed");
    const auto references =
        authoring::validate_scenario_references(scenario_document,
                                                engine_document);
    if (!references.ok()) {
        throw std::runtime_error{
            "BMW scenario cross-document references were rejected at " +
            references.diagnostics.front().json_pointer + ": " +
            references.diagnostics.front().message};
    }

    const auto owned_assets =
        migration::load_referenced_assets(engine_document, engine_json);
    const auto views = migration::asset_views(owned_assets);
    const auto compiled_engine = require_compile(
        compile::detail::resolve_engine_package(engine_document, views),
        "generic BMW engine resolution failed");
    const compile::detail::ScenarioResolverContext scenario_context{
        compiled_engine.engine,
        compiled_engine.presentation,
        compiled_engine.randomness,
        compiled_engine.provenance,
        compiled_engine.fuels,
        compiled_engine.audio_buses,
        {},
        contract::DistributionIntent::local_evaluation,
        {},
    };
    const auto compiled_scenario = require_compile(
        compile::detail::resolve_scenario_document(scenario_document,
                                                   scenario_context),
        "generic BMW scenario resolution failed");

    auto oracle_request = make_oracle_request();
    const auto ir = std::ranges::find(
        owned_assets, compile::AssetKind::audio, &migration::OwnedAsset::kind);
    if (ir == owned_assets.end()) {
        throw std::runtime_error{"BMW engine JSON did not reference an audio asset"};
    }
    const auto oracle_specification =
        reference::make_bmw_p18_render_specification(
            oracle_request.engine, oracle_request.provenance, ir->bytes);
    const auto oracle_random_plan = require_random_plan(
        contract::compile_random_plan(
            oracle_specification.randomness, oracle_request.engine,
            oracle_specification.presentation, oracle_request.scenario),
        "temporary BMW oracle random-plan compilation failed");

    migration::compare(migration::project_engine(compiled_engine.engine),
                       migration::project_engine(oracle_request.engine));
    migration::compare(
        migration::project_presentation(compiled_engine.presentation,
                                        compiled_engine.engine),
        migration::project_presentation(oracle_specification.presentation,
                                        oracle_request.engine));
    migration::compare(migration::project_scenario(compiled_scenario.scenario),
                       migration::project_scenario(oracle_request.scenario));
    migration::compare(
        migration::project_randomness(compiled_engine.randomness,
                                      compiled_scenario.random_plan,
                                      compiled_engine.engine),
        migration::project_randomness(oracle_specification.randomness,
                                      oracle_random_plan,
                                      oracle_request.engine));
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{
                "usage: bmw_m52b28_authored_json_migration_test "
                "<repository-root>"};
        }
        run_test(std::filesystem::canonical(argv[1]));
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
