#include "compile/compiled_scenario_view.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "engine_sim_offline/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace compile_detail = engine_sim_offline::compile::detail;
namespace contract = engine_sim_offline::contract;
namespace identity = engine_sim_offline::identity;

constexpr std::string_view kFixtureDirectory =
    "data/engines/cocentered-split-crank-v-twin";

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const double left, const double right,
                        const double tolerance = 1.0e-12) noexcept {
    return std::abs(left - right) <= tolerance;
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

template <class Value, class Report>
[[nodiscard]] Value require(std::variant<Value, Report> result,
                            const std::string_view context) {
    if (const auto *report = std::get_if<Report>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

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

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

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

struct FixtureVariant {
    std::filesystem::path engine_path;
    authoring::EnginePackageDocument engine_document;
    authoring::ScenarioDocument scenario_document;
    compile::CompiledScenario compiled_scenario;
};

[[nodiscard]] compile::CompiledScenario
compile_variant(const authoring::EnginePackageDocument &engine_document,
                const authoring::ScenarioDocument &scenario_document,
                const std::filesystem::path &engine_path) {
    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{
            "multiple-crankshaft prescribed fixture cross-document validation "
            "failed: " +
            diagnostics(references)};
    }
    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto engine = require(compile::compile_engine(engine_document, views),
                          "multiple-crankshaft prescribed fixture engine compilation "
                          "failed");
    return require(compile::compile_scenario(engine, scenario_document),
                   "multiple-crankshaft prescribed fixture scenario compilation "
                   "failed");
}

[[nodiscard]] FixtureVariant load_variant(const std::filesystem::path &repository_root,
                                          const std::string_view engine_filename) {
    const auto fixture_root = repository_root / kFixtureDirectory;
    const auto engine_path = fixture_root / engine_filename;
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "multiple-crankshaft prescribed fixture engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(
                    read_text(fixture_root / "scenarios/prescribed-1500rpm.json")),
                "multiple-crankshaft prescribed fixture scenario parse failed");
    auto scenario = compile_variant(engine_document, scenario_document, engine_path);
    return {engine_path, std::move(engine_document), std::move(scenario_document),
            std::move(scenario)};
}

[[nodiscard]] compile_detail::CompiledScenarioInputsView
inputs(const compile::CompiledScenario &scenario) {
    return compile_detail::CompiledScenarioViewAccess::inputs(scenario);
}

[[nodiscard]] const contract::LowOrderOperatingPointV1Profile &
profile(const contract::EngineSpec &engine) {
    return std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
}

[[nodiscard]] contract::CrankshaftId crank_id(const contract::EngineSpec &engine,
                                              const std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.crankshafts, semantic_id,
                          [](const auto &crankshaft) -> std::string_view {
                              return crankshaft.semantic_id.value;
                          });
    if (found == engine.crankshafts.end()) {
        throw std::runtime_error{"resolved engine omitted crankshaft '" +
                                 std::string{semantic_id} + "'"};
    }
    return found->id;
}

[[nodiscard]] const contract::CylinderSpec &
cylinder(const contract::EngineSpec &engine, const std::string_view semantic_id) {
    const auto found = std::ranges::find(
        engine.cylinders, semantic_id,
        [](const auto &value) -> std::string_view { return value.semantic_id.value; });
    if (found == engine.cylinders.end()) {
        throw std::runtime_error{"resolved engine omitted cylinder '" +
                                 std::string{semantic_id} + "'"};
    }
    return *found;
}

[[nodiscard]] const contract::LegacyCylinderAssembly &
core_cylinder(const contract::LowOrderOperatingPointV1Profile &physics,
              const contract::CylinderId cylinder_id) {
    const auto found =
        std::ranges::find(physics.core.mechanism.cylinders, cylinder_id,
                          [](const auto &value) { return value.topology.cylinder_id; });
    if (found == physics.core.mechanism.cylinders.end()) {
        throw std::runtime_error{"resolved core omitted cylinder binding"};
    }
    return *found;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const compile::CompiledScenario &scenario) {
    const auto resolved = inputs(scenario);
    auto encoded = identity::encode_simulation_request_identity_v6(
        resolved.engine.engine, resolved.scenario.scenario,
        resolved.scenario.random_plan, resolved.scenario.combined_provenance.bundle);
    if (const auto *error =
            std::get_if<identity::SimulationRequestIdentityError>(&encoded)) {
        throw std::runtime_error{
            "multiple-crankshaft prescribed fixture request identity failed: " +
            error->detail_code + ": " + error->message};
    }
    return std::get<identity::SimulationRequestIdentityEncoding>(std::move(encoded));
}

[[nodiscard]] engine_sim_offline::EngineSession
require_session(const compile::CompiledScenario &scenario) {
    auto result = engine_sim_offline::create_engine_session(
        scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
        throw std::runtime_error{
            "multiple-crankshaft prescribed fixture session creation failed: " +
            error->detail_code + ": " + error->message};
    }
    return std::get<engine_sim_offline::EngineSession>(std::move(result));
}

[[nodiscard]] std::vector<std::byte>
render_audition_pcm(const compile::CompiledScenario &scenario) {
    auto session = require_session(scenario);
    expect(session.descriptor().motion_mode ==
               engine_sim_offline::EngineMotionMode::prescribed_kinematic_sweep,
           "multiple-crankshaft prescribed fixture escaped its prescribed-kinematic "
           "execution boundary");

    std::vector<std::byte> pcm;
    bool observed_nonzero = false;
    std::uint64_t block_count = 0U;
    while (true) {
        auto result = session.process_block();
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            const auto bus = std::ranges::find(
                block->audio_buses(),
                engine_sim_offline::EngineAudioBusKind::engine_audition_master,
                [](const auto &value) { return value.descriptor.kind; });
            expect(bus != block->audio_buses().end() &&
                       std::ranges::all_of(
                           bus->samples,
                           [](const float sample) { return std::isfinite(sample); }),
                   "multiple-crankshaft prescribed fixture emitted an invalid "
                   "audition PCM block");
            observed_nonzero =
                observed_nonzero ||
                std::ranges::any_of(bus->samples,
                                    [](const float sample) { return sample != 0.0F; });
            const auto bytes = std::as_bytes(bus->samples);
            pcm.insert(pcm.end(), bytes.begin(), bytes.end());
            ++block_count;
            continue;
        }
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{
                "multiple-crankshaft prescribed fixture session faulted: " +
                error->detail_code + ": " + error->message};
        }
        const auto &completed =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(observed_nonzero && block_count != 0U &&
                   completed.block_count == block_count,
               "multiple-crankshaft prescribed fixture did not complete with "
               "nonzero PCM");
        return pcm;
    }
}

void verify_authored_ab_delta(const FixtureVariant &a, const FixtureVariant &b) {
    expect(a.scenario_document == b.scenario_document,
           "multiple-crankshaft prescribed fixture variants did not share one "
           "request");
    expect(a.engine_document.engine.crankshafts.size() == 1U &&
               b.engine_document.engine.crankshafts.size() == 2U &&
               a.engine_document.engine.journals.size() == 2U &&
               b.engine_document.engine.journals.size() == 2U &&
               a.engine_document.engine.output_crankshaft.value == "crank.output" &&
               b.engine_document.engine.output_crankshaft.value == "crank.output",
           "multiple-crankshaft prescribed fixture crank/journal shape changed");

    auto normalized_b = b.engine_document;
    normalized_b.engine.crankshafts = a.engine_document.engine.crankshafts;
    normalized_b.engine.journals = a.engine_document.engine.journals;
    expect(normalized_b == a.engine_document,
           "multiple-crankshaft prescribed fixture documents differ outside crank "
           "decomposition and journal "
           "ownership");
}

void verify_resolved_ab_invariants(const FixtureVariant &a, const FixtureVariant &b) {
    const auto a_inputs = inputs(a.compiled_scenario);
    const auto b_inputs = inputs(b.compiled_scenario);
    const auto &a_engine = a_inputs.engine.engine;
    const auto &b_engine = b_inputs.engine.engine;
    const auto &a_profile = profile(a_engine);
    const auto &b_profile = profile(b_engine);
    const auto output_id = crank_id(b_engine, "crank.output");
    const auto secondary_id = crank_id(b_engine, "crank.secondary");
    const auto &a_front = cylinder(a_engine, "cylinder.front");
    const auto &a_rear = cylinder(a_engine, "cylinder.rear");
    const auto &b_front = cylinder(b_engine, "cylinder.front");
    const auto &b_rear = cylinder(b_engine, "cylinder.rear");

    expect(a_engine.crankshafts.size() == 1U &&
               a_engine.output_crankshaft_id == a_engine.crankshafts.front().id &&
               a_front.crankshaft_id == a_engine.output_crankshaft_id &&
               a_rear.crankshaft_id == a_engine.output_crankshaft_id,
           "multiple-crankshaft prescribed fixture A lost its one-crank/two-journal "
           "ownership");
    expect(b_engine.crankshafts.size() == 2U &&
               b_engine.crankshafts[0].semantic_id.value == "crank.output" &&
               b_engine.crankshafts[1].semantic_id.value == "crank.secondary" &&
               b_engine.output_crankshaft_id == output_id &&
               output_id != secondary_id && b_front.crankshaft_id == output_id &&
               b_rear.crankshaft_id == secondary_id,
           "multiple-crankshaft prescribed fixture B collapsed explicit "
           "cylinder-to-crank identity");
    expect(core_cylinder(b_profile, b_front.id).topology.crankshaft_id == output_id &&
               core_cylinder(b_profile, b_rear.id).topology.crankshaft_id ==
                   secondary_id,
           "multiple-crankshaft prescribed fixture B core topology lost exact "
           "crankshaft bindings");

    const auto *a_crank = contract::find_output_crank(a_profile.core.mechanism);
    const auto *b_output = contract::find_crank(b_profile.core.mechanism, output_id);
    const auto *b_secondary =
        contract::find_crank(b_profile.core.mechanism, secondary_id);
    expect(a_crank != nullptr && b_output != nullptr && b_secondary != nullptr,
           "multiple-crankshaft prescribed fixture omitted an authored crank "
           "assembly");
    expect(near(a_crank->crankshaft_mass_kg.value,
                b_output->crankshaft_mass_kg.value +
                    b_secondary->crankshaft_mass_kg.value) &&
               near(a_crank->flywheel_mass_kg.value,
                    b_output->flywheel_mass_kg.value +
                        b_secondary->flywheel_mass_kg.value) &&
               near(a_crank->authored_crank_inertia_kg_m2.value,
                    b_output->authored_crank_inertia_kg_m2.value +
                        b_secondary->authored_crank_inertia_kg_m2.value) &&
               near(a_crank->running_friction_torque_magnitude_nm.value,
                    b_output->running_friction_torque_magnitude_nm.value +
                        b_secondary->running_friction_torque_magnitude_nm.value) &&
               b_output->crank_tdc_reference_rad.value ==
                   b_secondary->crank_tdc_reference_rad.value,
           "multiple-crankshaft prescribed fixture decomposition changed aggregate "
           "physical values or phase");

    expect(a_front.bore_m.value == b_front.bore_m.value &&
               a_front.stroke_m.value == b_front.stroke_m.value &&
               a_front.connecting_rod_length_m.value ==
                   b_front.connecting_rod_length_m.value &&
               a_front.journal_phase_rad.value == b_front.journal_phase_rad.value &&
               a_rear.bore_m.value == b_rear.bore_m.value &&
               a_rear.stroke_m.value == b_rear.stroke_m.value &&
               a_rear.connecting_rod_length_m.value ==
                   b_rear.connecting_rod_length_m.value &&
               a_rear.journal_phase_rad.value == b_rear.journal_phase_rad.value,
           "multiple-crankshaft prescribed fixture decomposition changed cylinder "
           "geometry or journal phase");
}

void verify_explicit_output_selection_changes_identity(const FixtureVariant &b) {
    auto secondary_output = b.engine_document;
    secondary_output.engine.output_crankshaft.value = "crank.secondary";
    auto compiled =
        compile_variant(secondary_output, b.scenario_document, b.engine_path);
    const auto selected_inputs = inputs(compiled);
    const auto expected_output =
        crank_id(selected_inputs.engine.engine, "crank.secondary");
    expect(selected_inputs.engine.engine.output_crankshaft_id == expected_output &&
               profile(selected_inputs.engine.engine)
                       .core.mechanism.output_crankshaft_id == expected_output,
           "explicit secondary output selection did not reach both contracts");
    expect(request_identity(compiled).sha256 !=
               request_identity(b.compiled_scenario).sha256,
           "changing only explicit output selection did not change canonical "
           "request identity");
}

void verify_torque_owning_motion_remains_closed(const FixtureVariant &b) {
    const auto resolved = inputs(b.compiled_scenario);
    auto non_prescribed = resolved.scenario.scenario;
    non_prescribed.mode = contract::HeldSpeed{};
    const auto report =
        contract::validate_for_engine(non_prescribed, resolved.engine.engine);
    expect(std::ranges::any_of(
               report.issues,
               [](const auto &issue) {
                   return issue.path == "mode" &&
                          issue.message.starts_with(
                              "multiple-crankshaft engines currently admit only");
               }),
           "multiple-crankshaft engine crossed the prescribed-only motion gate");
}

void verify_multiple_crank_master_slave_is_rejected(
    const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / kFixtureDirectory / "engine-negative-cross-crank-master.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "multiple-crankshaft master/slave fixture engine parse failed");
    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    const auto result = compile::compile_engine(engine_document, views);
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(
                   report->diagnostics,
                   [](const auto &diagnostic) {
                       return diagnostic.code ==
                                  authoring::DiagnosticCode::unsupported_capability &&
                              diagnostic.json_pointer ==
                                  "/engine/physics_profile/mechanism/cranks" &&
                              diagnostic.message.starts_with(
                                  "one-level master-rod execution currently requires "
                                  "one output crankshaft");
                   }),
           "valid authored multiple-crankshaft master/slave mechanism crossed its "
           "execution gate");
}

void run(const std::filesystem::path &repository_root) {
    const auto a = load_variant(repository_root, "engine-a-one-crank.json");
    const auto b = load_variant(repository_root, "engine-b-two-cranks.json");
    verify_authored_ab_delta(a, b);
    verify_resolved_ab_invariants(a, b);
    verify_explicit_output_selection_changes_identity(b);
    verify_torque_owning_motion_remains_closed(b);
    verify_multiple_crank_master_slave_is_rejected(repository_root);

    const auto a_identity = request_identity(a.compiled_scenario);
    const auto b_identity = request_identity(b.compiled_scenario);
    expect(a_identity.sha256 != b_identity.sha256 &&
               a_identity.bytes != b_identity.bytes,
           "one-crank and two-crank fixtures produced one canonical request "
           "identity");

    const auto a_pcm = render_audition_pcm(a.compiled_scenario);
    const auto b_pcm = render_audition_pcm(b.compiled_scenario);
    expect(a_pcm == b_pcm,
           "co-centered prescribed crank decomposition changed audition PCM");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Multiple-crankshaft prescribed fixture failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
