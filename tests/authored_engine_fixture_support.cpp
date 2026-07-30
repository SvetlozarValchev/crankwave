#include "authored_engine_fixture_support.hpp"

#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "render/compiled_scenario_projection.hpp"

#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::test {
namespace {

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string diagnostics(const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

[[nodiscard]] std::string diagnostics(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

template <class Value, class Report>
[[nodiscard]] Value require(std::variant<Value, Report> result,
                            std::string_view context) {
    if (const auto *report = std::get_if<Report>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> result;
    result.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    for (const auto &asset : document.presentation.assets) {
        result.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back({compile::AssetKind::accessory_configuration, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return result;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

} // namespace

AuthoredEngineFixture
load_canonical_authored_engine_fixture(const std::filesystem::path &repository_root) {
    const auto engine_path = repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_path = repository_root / "data/engines/bmw-m52b28/scenarios/"
                                                 "inertial-dyno-1500-6500rpm.json";

    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "authored engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(read_text(scenario_path)),
                "authored scenario parse failed");
    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{"authored cross-document validation failed: " +
                                 diagnostics(references)};
    }

    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto engine = require(compile::compile_engine(engine_document, views),
                          "authored engine compilation failed");
    auto scenario = require(compile::compile_scenario(engine, scenario_document),
                            "authored scenario compilation failed");
    auto projection = render_detail::CompiledScenarioAccess::project(scenario);
    return {
        std::move(projection.specification.engine),
        std::move(projection.specification.presentation),
        std::move(projection.specification.randomness),
        std::move(projection.scenario),
        projection.specification.provenance.bundle,
    };
}

AuthoredEngineFixture make_prescribed_fixture(const AuthoredEngineFixture &canonical,
                                              std::vector<double> post_step_rpm) {
    auto result = canonical;
    const auto *inertial = std::get_if<contract::InertialDyno>(&result.scenario.mode);
    if (inertial == nullptr) {
        throw std::runtime_error{
            "canonical authored fixture lacks inertial-dyno ownership"};
    }
    const auto initial_theta = inertial->initial_theta_rad;
    const auto throttle = inertial->throttle_01;
    contract::FixedRateRpmTrajectory rpm{
        result.scenario.rates.physics, 0U, contract::RpmSampleSemantics::post_step_rpm,
        std::move(post_step_rpm),      {}, "authored-fixture.fixed-rate-rpm",
    };
    rpm.samples_f64le_sha256 =
        contract::canonical_binary64_le_sha256(rpm.post_step_rpm);
    result.scenario.mode = contract::PrescribedKinematicSweep{
        {
            std::move(rpm),
            initial_theta,
            {compile::detail::fixed_rate_post_step_rpm_method_identity(),
             "authored-fixture.fixed-rate-method"},
        },
        throttle,
    };
    result.scenario.mode_resolution_id = "authored-fixture.prescribed-kinematic-sweep";
    return result;
}

contract::RandomPlan compile_fixture_random_plan(const AuthoredEngineFixture &fixture) {
    return compile_fixture_random_plan(fixture, fixture.engine, fixture.scenario);
}

contract::RandomPlan
compile_fixture_random_plan(const AuthoredEngineFixture &fixture,
                            const contract::EngineSpec &engine,
                            const contract::RenderScenario &scenario) {
    return require(contract::compile_random_plan(fixture.randomness, engine,
                                                 fixture.presentation, scenario),
                   "authored random-plan compilation failed");
}

contract::LowOrderEngineCoreV1 &low_order_core(contract::EngineSpec &engine) {
    return std::visit(
        [](auto &profile) -> contract::LowOrderEngineCoreV1 & { return profile.core; },
        engine.physics_profile);
}

const contract::LowOrderEngineCoreV1 &
low_order_core(const contract::EngineSpec &engine) {
    return std::visit(
        [](const auto &profile) -> const contract::LowOrderEngineCoreV1 & {
            return profile.core;
        },
        engine.physics_profile);
}

contract::LowOrderOperatingPointV1Profile &
operating_profile(contract::EngineSpec &engine) {
    auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    if (profile == nullptr) {
        throw std::runtime_error{
            "authored engine lacks the operating-point physics profile"};
    }
    return *profile;
}

const contract::LowOrderOperatingPointV1Profile &
operating_profile(const contract::EngineSpec &engine) {
    const auto *profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    if (profile == nullptr) {
        throw std::runtime_error{
            "authored engine lacks the operating-point physics profile"};
    }
    return *profile;
}

} // namespace engine_sim_offline::test
