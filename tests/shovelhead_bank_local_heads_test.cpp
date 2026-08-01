#include "authored_engine_fixture_support.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/session.hpp"
#include "simulation/legacy_fixed_valvetrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
namespace simulation = engine_sim_offline::simulation;
namespace test = engine_sim_offline::test;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kDegreesToRadians = kLegacyPi / 180.0;

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

struct ShovelheadSource {
    authoring::EnginePackageDocument engine_document;
    authoring::ScenarioDocument scenario_document;
    compile::CompiledScenario compiled_scenario;
};

[[nodiscard]] ShovelheadSource
compile_source(authoring::EnginePackageDocument engine_document,
               authoring::ScenarioDocument scenario_document,
               const std::filesystem::path &engine_path) {
    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{"Shovelhead cross-document validation failed: " +
                                 diagnostics(references)};
    }

    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto engine = require(compile::compile_engine(engine_document, views),
                          "Shovelhead engine compilation failed");
    auto scenario = require(compile::compile_scenario(engine, scenario_document),
                            "Shovelhead prescribed scenario compilation failed");
    return {std::move(engine_document), std::move(scenario_document),
            std::move(scenario)};
}

[[nodiscard]] ShovelheadSource
load_source(const std::filesystem::path &repository_root,
            const std::filesystem::path &engine_relative_path) {
    const auto engine_path = repository_root / engine_relative_path;
    const auto scenario_path = repository_root /
                               "data/engines/shovelhead-bank-local-heads/scenarios/"
                               "prescribed-1500rpm.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "Shovelhead authored engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(read_text(scenario_path)),
                "Shovelhead authored scenario parse failed");
    return compile_source(std::move(engine_document), std::move(scenario_document),
                          engine_path);
}

[[nodiscard]] authoring::CurveDefinition &
find_curve(authoring::EnginePackageDocument &document,
           const std::string_view curve_id) {
    const auto found = std::ranges::find(
        document.engine.curves, curve_id,
        [](const auto &curve) -> std::string_view { return curve.id.value; });
    expect(found != document.engine.curves.end(),
           "Shovelhead diagnostic control could not find its selected curve");
    return *found;
}

[[nodiscard]] authoring::IntakeDefinition &
find_intake(authoring::EnginePackageDocument &document,
            const std::string_view intake_id) {
    const auto found = std::ranges::find(
        document.engine.intakes, intake_id,
        [](const auto &intake) -> std::string_view { return intake.id.value; });
    expect(found != document.engine.intakes.end(),
           "Shovelhead fixture could not find its selected intake");
    return *found;
}

[[nodiscard]] const authoring::IntakeDefinition &
find_intake(const authoring::EnginePackageDocument &document,
            const std::string_view intake_id) {
    const auto found = std::ranges::find(
        document.engine.intakes, intake_id,
        [](const auto &intake) -> std::string_view { return intake.id.value; });
    expect(found != document.engine.intakes.end(),
           "Shovelhead fixture could not find its selected intake");
    return *found;
}

[[nodiscard]] authoring::CylinderDefinition &
find_cylinder(authoring::EnginePackageDocument &document,
              const std::string_view cylinder_id) {
    const auto found = std::ranges::find(
        document.engine.cylinders, cylinder_id,
        [](const auto &cylinder) -> std::string_view { return cylinder.id.value; });
    expect(found != document.engine.cylinders.end(),
           "Shovelhead fixture could not find its selected cylinder");
    return *found;
}

[[nodiscard]] ShovelheadSource
make_uniform_flow_control(const std::filesystem::path &repository_root,
                          const ShovelheadSource &source, const bool one_times) {
    auto document = source.engine_document;
    if (one_times) {
        find_curve(document, "intake-valve-flow-2x").samples =
            find_curve(document, "intake-valve-flow-1x").samples;
        find_curve(document, "exhaust-valve-flow-2x").samples =
            find_curve(document, "exhaust-valve-flow-1x").samples;
    } else {
        find_curve(document, "intake-valve-flow-1x").samples =
            find_curve(document, "intake-valve-flow-2x").samples;
        find_curve(document, "exhaust-valve-flow-1x").samples =
            find_curve(document, "exhaust-valve-flow-2x").samples;
    }
    return compile_source(std::move(document), source.scenario_document,
                          repository_root /
                              "data/engines/shovelhead-bank-local-heads/engine.json");
}

[[nodiscard]] ShovelheadSource
make_equal_split_intake_control(const std::filesystem::path &repository_root,
                                const ShovelheadSource &differentiated) {
    auto document = differentiated.engine_document;
    const auto &front = find_intake(document, "intake");
    auto &rear = find_intake(document, "intake.rear");
    rear.idle_throttle_position_01 = front.idle_throttle_position_01;
    rear.runner_velocity_decay_01 = front.runner_velocity_decay_01;
    return compile_source(
        std::move(document), differentiated.scenario_document,
        repository_root /
            "data/engines/shovelhead-bank-local-heads/engine-separate-intakes.json");
}

[[nodiscard]] const contract::EngineSpec &
resolved_engine(const ShovelheadSource &source) {
    return compile_detail::CompiledScenarioViewAccess::inputs(source.compiled_scenario)
        .engine.engine;
}

[[nodiscard]] simulation::LegacyFixedValvetrain
require_fixed_valvetrain(const contract::EngineSpec &engine) {
    auto result = simulation::compile_legacy_fixed_valvetrain(
        engine, test::low_order_core(engine));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string detail;
        for (const auto &issue : report->issues) {
            if (!detail.empty()) {
                detail += "; ";
            }
            detail += issue.path + ": " + issue.message;
        }
        throw std::runtime_error{"Shovelhead fixed-valvetrain compilation failed: " +
                                 detail};
    }
    return std::get<simulation::LegacyFixedValvetrain>(std::move(result));
}

void verify_only_four_flow_curve_references_differ(
    const authoring::EnginePackageDocument &source_assignment,
    const authoring::EnginePackageDocument &swapped_assignment) {
    expect(source_assignment.engine.ports.size() == 4U &&
               swapped_assignment.engine.ports.size() == 4U,
           "Shovelhead A/B fixture must contain exactly four head-local ports");

    auto normalized = swapped_assignment;
    std::size_t changed_references = 0U;
    for (std::size_t index = 0U; index < source_assignment.engine.ports.size();
         ++index) {
        auto &actual = normalized.engine.ports[index];
        const auto &expected = source_assignment.engine.ports[index];
        if (actual.flow_curve != expected.flow_curve) {
            actual.flow_curve = expected.flow_curve;
            ++changed_references;
        }
    }
    expect(changed_references == 4U && normalized == source_assignment,
           "Shovelhead variant B differs from A by more or less than the four "
           "ports[].flow_curve references");
}

void verify_uniform_control_delta(
    const authoring::EnginePackageDocument &source_assignment,
    const authoring::EnginePackageDocument &uniform_control,
    const std::string_view changed_suffix) {
    auto normalized = uniform_control;
    std::size_t changed_curves = 0U;
    for (std::size_t index = 0U; index < source_assignment.engine.curves.size();
         ++index) {
        auto &actual = normalized.engine.curves[index];
        const auto &expected = source_assignment.engine.curves[index];
        if (actual == expected) {
            continue;
        }
        expect(actual.id.value.ends_with(changed_suffix) &&
                   actual.samples != expected.samples,
               "Shovelhead uniform-flow control changed the wrong curve");
        actual.samples = expected.samples;
        ++changed_curves;
    }
    expect(changed_curves == 2U && normalized == source_assignment,
           "Shovelhead uniform-flow control differs outside one bank's two flow "
           "tables");
}

void verify_separate_intake_authored_deltas(
    const authoring::EnginePackageDocument &shared,
    const authoring::EnginePackageDocument &equal_split,
    const authoring::EnginePackageDocument &differentiated) {
    expect(shared.engine.intakes.size() == 1U &&
               equal_split.engine.intakes.size() == 2U &&
               differentiated.engine.intakes.size() == 2U,
           "Shovelhead A/B/C intake fixture has the wrong authored shape");

    const auto &source = find_intake(shared, "intake");
    const auto &equal_front = find_intake(equal_split, "intake");
    const auto &equal_rear = find_intake(equal_split, "intake.rear");
    const auto &different_front = find_intake(differentiated, "intake");
    const auto &different_rear = find_intake(differentiated, "intake.rear");
    auto normalized_equal_rear = equal_rear;
    normalized_equal_rear.id = source.id;
    auto normalized_different_rear = different_rear;
    normalized_different_rear.id = source.id;
    normalized_different_rear.idle_throttle_position_01 =
        source.idle_throttle_position_01;
    normalized_different_rear.runner_velocity_decay_01 =
        source.runner_velocity_decay_01;
    expect(equal_front == source && normalized_equal_rear == source &&
               different_front == source && normalized_different_rear == source,
           "split intake variants changed dimensions or restrictions outside the "
           "selected rear-lane controls");
    expect(source.idle_throttle_position_01 == 0.991 &&
               source.runner_velocity_decay_01 == 1.0 &&
               different_rear.idle_throttle_position_01 == 0.993 &&
               different_rear.runner_velocity_decay_01 == 0.5,
           "Shovelhead/TRX520 source intake values changed");

    auto normalized_differentiated = differentiated;
    normalized_differentiated.engine.identity = shared.engine.identity;
    std::erase_if(normalized_differentiated.engine.intakes,
                  [](const auto &intake) { return intake.id.value == "intake.rear"; });
    find_cylinder(normalized_differentiated, "cylinder.rear").intake.value = "intake";
    expect(normalized_differentiated == shared,
           "authored C differs from shared A outside its description, rear intake, "
           "and rear-cylinder binding");

    auto normalized_equal = equal_split;
    auto &normalized_rear = find_intake(normalized_equal, "intake.rear");
    normalized_rear.idle_throttle_position_01 =
        different_rear.idle_throttle_position_01;
    normalized_rear.runner_velocity_decay_01 = different_rear.runner_velocity_decay_01;
    expect(normalized_equal == differentiated,
           "derived B differs from authored C outside the two rear intake controls");
}

[[nodiscard]] bool same_restriction_values(const contract::LegacyRestriction &left,
                                           const contract::LegacyRestriction &right) {
    return left.calibration.value == right.calibration.value &&
           left.source_rating.value == right.source_rating.value &&
           left.resolved_k.value == right.resolved_k.value;
}

[[nodiscard]] bool
same_intake_base_values(const contract::LegacyIntakeParameters &left,
                        const contract::LegacyIntakeParameters &right) {
    return left.plenum_volume_m3.value == right.plenum_volume_m3.value &&
           left.plenum_cross_section_area_m2.value ==
               right.plenum_cross_section_area_m2.value &&
           left.runner_length_m.value == right.runner_length_m.value &&
           same_restriction_values(left.main_throttle, right.main_throttle) &&
           same_restriction_values(left.idle_bypass, right.idle_bypass) &&
           same_restriction_values(left.plenum_to_runner, right.plenum_to_runner);
}

[[nodiscard]] bool same_intake_values(const contract::LegacyIntakeParameters &left,
                                      const contract::LegacyIntakeParameters &right) {
    return same_intake_base_values(left, right) &&
           left.velocity_decay.value == right.velocity_decay.value &&
           left.idle_throttle_plate_position_01.value ==
               right.idle_throttle_plate_position_01.value;
}

void verify_resolved_intake_bindings(const ShovelheadSource &equal_split,
                                     const ShovelheadSource &differentiated) {
    const auto verify_shape = [](const contract::EngineSpec &engine) {
        const auto &core = test::low_order_core(engine);
        expect(engine.intakes.size() == 2U && core.gas_path.intakes.size() == 2U &&
                   engine.intakes[0].semantic_id.value == "intake" &&
                   engine.intakes[1].semantic_id.value == "intake.rear",
               "split intakes did not resolve in stable semantic IntakeId order");
        const auto &front = core.gas_path.intakes[0].topology;
        const auto &rear = core.gas_path.intakes[1].topology;
        expect(front.intake_id == engine.intakes[0].id &&
                   rear.intake_id == engine.intakes[1].id &&
                   front.intake_id != rear.intake_id &&
                   front.plenum_volume_id != rear.plenum_volume_id &&
                   front.main_throttle_edge_id != rear.main_throttle_edge_id &&
                   front.idle_bypass_edge_id != rear.idle_bypass_edge_id,
               "equal-valued split intakes collapsed their runtime identities");

        const auto front_cylinder =
            std::ranges::find(engine.cylinders, "cylinder.front",
                              [](const auto &cylinder) -> std::string_view {
                                  return cylinder.semantic_id.value;
                              });
        const auto rear_cylinder =
            std::ranges::find(engine.cylinders, "cylinder.rear",
                              [](const auto &cylinder) -> std::string_view {
                                  return cylinder.semantic_id.value;
                              });
        expect(front_cylinder != engine.cylinders.end() &&
                   rear_cylinder != engine.cylinders.end() &&
                   front_cylinder->intake_id == front.intake_id &&
                   rear_cylinder->intake_id == rear.intake_id,
               "public cylinders lost their exact split-intake bindings");
        for (const auto &assembly : core.mechanism.cylinders) {
            const auto public_cylinder =
                std::ranges::find(engine.cylinders, assembly.topology.cylinder_id,
                                  &contract::CylinderSpec::id);
            expect(public_cylinder != engine.cylinders.end() &&
                       public_cylinder->intake_id == assembly.topology.intake_id,
                   "mechanism cylinders lost their public IntakeId binding");
        }
    };

    const auto &equal_engine = resolved_engine(equal_split);
    const auto &different_engine = resolved_engine(differentiated);
    verify_shape(equal_engine);
    verify_shape(different_engine);
    const auto &equal_intakes = test::low_order_core(equal_engine).gas_path.intakes;
    const auto &different_intakes =
        test::low_order_core(different_engine).gas_path.intakes;
    expect(same_intake_values(equal_intakes[0].parameters, equal_intakes[1].parameters),
           "equal-valued B resolved unequal intake parameters");
    expect(same_intake_values(equal_intakes[0].parameters,
                              different_intakes[0].parameters) &&
               same_intake_base_values(equal_intakes[1].parameters,
                                       different_intakes[1].parameters) &&
               different_intakes[1].parameters.idle_throttle_plate_position_01.value ==
                   0.993 &&
               different_intakes[1].parameters.velocity_decay.value == 0.5 &&
               !same_intake_values(equal_intakes[1].parameters,
                                   different_intakes[1].parameters),
           "differentiated C did not isolate its rear intake parameters");
}

void expect_two_times_source_flow(const contract::LegacyBankHeadProfile &two_times,
                                  const contract::LegacyBankHeadProfile &one_times,
                                  const std::string_view context) {
    const auto check = [&](const auto &twice, const auto &once,
                           const std::string_view port) {
        expect(twice.size() == 15U && once.size() == 15U,
               std::string{context} + " " + std::string{port} +
                   " flow-table extent changed");
        for (std::size_t index = 0U; index < twice.size(); ++index) {
            expect(twice[index].lift_m.value == once[index].lift_m.value &&
                       twice[index].source_cfm_at_28_inh2o.value ==
                           2.0 * once[index].source_cfm_at_28_inh2o.value,
                   std::string{context} + " " + std::string{port} +
                       " flow is not the exact source 2x/1x pair");
        }
    };
    check(two_times.intake_flow, one_times.intake_flow, "intake");
    check(two_times.exhaust_flow, one_times.exhaust_flow, "exhaust");
}

void expect_same_flow_data(const simulation::LegacyValvetrainFlowProfile &left,
                           const simulation::LegacyValvetrainFlowProfile &right,
                           const std::string_view context) {
    expect(
        left.intake_flow_table == right.intake_flow_table &&
            left.exhaust_flow_table == right.exhaust_flow_table &&
            left.intake_flow_triangle_radius_m == right.intake_flow_triangle_radius_m &&
            left.exhaust_flow_triangle_radius_m == right.exhaust_flow_triangle_radius_m,
        context);
}

void verify_resolved_head_bindings(const ShovelheadSource &source_assignment,
                                   const ShovelheadSource &swapped_assignment) {
    const auto &source_engine = resolved_engine(source_assignment);
    const auto &swapped_engine = resolved_engine(swapped_assignment);
    const auto &source_heads = test::low_order_core(source_engine).gas_path.heads;
    const auto &swapped_heads = test::low_order_core(swapped_engine).gas_path.heads;
    expect(source_engine.banks.size() == 2U && source_engine.cylinders.size() == 2U &&
               swapped_engine.banks.size() == 2U &&
               swapped_engine.cylinders.size() == 2U && source_heads.size() == 2U &&
               swapped_heads.size() == 2U,
           "Shovelhead did not resolve to two banks, cylinders, and head profiles");
    for (std::size_t index = 0U; index < 2U; ++index) {
        expect(source_heads[index].bank_id == source_engine.banks[index].id &&
                   swapped_heads[index].bank_id == swapped_engine.banks[index].id,
               "Shovelhead bank-local heads lost stable BankId order");
    }
    expect(
        source_engine.banks[0].angle_rad.has_value() &&
            source_engine.banks[1].angle_rad.has_value() &&
            near(source_engine.banks[0].angle_rad->value, -22.5 * kDegreesToRadians) &&
            near(source_engine.banks[1].angle_rad->value, 22.5 * kDegreesToRadians),
        "Shovelhead front/rear bank order or 45-degree geometry changed");
    expect_two_times_source_flow(source_heads[0], source_heads[1],
                                 "source-assigned front/rear");
    expect_two_times_source_flow(swapped_heads[1], swapped_heads[0],
                                 "swapped rear/front");

    const auto source_valvetrain = require_fixed_valvetrain(source_engine);
    const auto swapped_valvetrain = require_fixed_valvetrain(swapped_engine);
    const auto source_profiles = source_valvetrain.flow_profiles();
    const auto swapped_profiles = swapped_valvetrain.flow_profiles();
    const auto source_bindings = source_valvetrain.cylinder_bindings();
    const auto swapped_bindings = swapped_valvetrain.cylinder_bindings();
    expect(source_profiles.size() == 2U && swapped_profiles.size() == 2U &&
               source_profiles[0].bank_id == source_engine.banks[0].id &&
               source_profiles[1].bank_id == source_engine.banks[1].id &&
               swapped_profiles[0].bank_id == swapped_engine.banks[0].id &&
               swapped_profiles[1].bank_id == swapped_engine.banks[1].id,
           "Shovelhead fixed-valvetrain profiles lost ordered BankId ownership");
    expect(source_bindings.size() == 2U && swapped_bindings.size() == 2U &&
               source_bindings[0].flow_profile_index == 0U &&
               source_bindings[1].flow_profile_index == 1U &&
               swapped_bindings[0].flow_profile_index == 0U &&
               swapped_bindings[1].flow_profile_index == 1U,
           "Shovelhead cylinders did not bind fixed-valvetrain profiles 0/1");
    expect_same_flow_data(source_profiles[0], swapped_profiles[1],
                          "Shovelhead 2x runtime flow data did not follow the "
                          "swapped bank assignment");
    expect_same_flow_data(source_profiles[1], swapped_profiles[0],
                          "Shovelhead 1x runtime flow data did not follow the "
                          "swapped bank assignment");
}

[[nodiscard]] engine_sim_offline::EngineSession
require_session(const compile::CompiledScenario &scenario) {
    auto result = engine_sim_offline::create_engine_session(
        scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
        throw std::runtime_error{"Shovelhead session creation failed: " +
                                 error->detail_code + ": " + error->message};
    }
    return std::get<engine_sim_offline::EngineSession>(std::move(result));
}

[[nodiscard]] std::vector<std::byte>
render_audition_pcm(const compile::CompiledScenario &scenario) {
    auto session = require_session(scenario);
    const auto descriptor = session.descriptor();
    expect(descriptor.motion_mode ==
                   engine_sim_offline::EngineMotionMode::prescribed_kinematic_sweep &&
               descriptor.total_block_count == 4U &&
               descriptor.preparation_block_count == 0U,
           "Shovelhead regression scenario is not the bounded 0.08-second "
           "prescribed render");

    std::vector<std::byte> pcm;
    bool observed_nonzero = false;
    std::uint64_t block_count = 0U;
    while (true) {
        auto result = session.process_block();
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            const auto found = std::ranges::find(
                block->audio_buses(),
                engine_sim_offline::EngineAudioBusKind::engine_audition_master,
                [](const auto &bus) { return bus.descriptor.kind; });
            expect(found != block->audio_buses().end() &&
                       block->phase() ==
                           engine_sim_offline::EngineSessionBlockPhase::audible &&
                       found->samples.size() ==
                           engine_sim_offline::kEngineSessionDeliveryFramesPerBlock &&
                       std::ranges::all_of(
                           found->samples,
                           [](const float sample) { return std::isfinite(sample); }),
                   "Shovelhead session emitted an invalid audition PCM block");
            observed_nonzero =
                observed_nonzero ||
                std::ranges::any_of(found->samples,
                                    [](const float sample) { return sample != 0.0F; });
            const auto bytes = std::as_bytes(found->samples);
            pcm.insert(pcm.end(), bytes.begin(), bytes.end());
            ++block_count;
            continue;
        }
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{"Shovelhead session faulted: " +
                                     error->detail_code + ": " + error->message};
        }
        const auto &completed =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(observed_nonzero && block_count == 4U &&
                   completed.block_count == block_count &&
                   completed.physics_frame_count == 800U &&
                   completed.delivery_frame_count == 15360U,
               "Shovelhead bounded session did not complete with finite nonzero "
               "audition PCM");
        return pcm;
    }
}

void run(const std::filesystem::path &repository_root) {
    const auto source_assignment = load_source(
        repository_root, "data/engines/shovelhead-bank-local-heads/engine.json");
    const auto swapped_assignment = load_source(
        repository_root,
        "data/engines/shovelhead-bank-local-heads/engine-swapped-heads.json");
    const auto uniform_one_times =
        make_uniform_flow_control(repository_root, source_assignment, true);
    const auto uniform_two_times =
        make_uniform_flow_control(repository_root, source_assignment, false);
    const auto differentiated_intakes = load_source(
        repository_root,
        "data/engines/shovelhead-bank-local-heads/engine-separate-intakes.json");
    const auto equal_split_intakes =
        make_equal_split_intake_control(repository_root, differentiated_intakes);
    verify_only_four_flow_curve_references_differ(source_assignment.engine_document,
                                                  swapped_assignment.engine_document);
    verify_uniform_control_delta(source_assignment.engine_document,
                                 uniform_one_times.engine_document, "-2x");
    verify_uniform_control_delta(source_assignment.engine_document,
                                 uniform_two_times.engine_document, "-1x");
    verify_separate_intake_authored_deltas(source_assignment.engine_document,
                                           equal_split_intakes.engine_document,
                                           differentiated_intakes.engine_document);
    verify_resolved_head_bindings(source_assignment, swapped_assignment);
    verify_resolved_intake_bindings(equal_split_intakes, differentiated_intakes);

    const auto first_source_pcm =
        render_audition_pcm(source_assignment.compiled_scenario);
    const auto second_source_pcm =
        render_audition_pcm(source_assignment.compiled_scenario);
    const auto swapped_pcm = render_audition_pcm(swapped_assignment.compiled_scenario);
    const auto uniform_one_times_pcm =
        render_audition_pcm(uniform_one_times.compiled_scenario);
    const auto uniform_two_times_pcm =
        render_audition_pcm(uniform_two_times.compiled_scenario);
    const auto first_equal_split_pcm =
        render_audition_pcm(equal_split_intakes.compiled_scenario);
    const auto second_equal_split_pcm =
        render_audition_pcm(equal_split_intakes.compiled_scenario);
    const auto differentiated_intakes_pcm =
        render_audition_pcm(differentiated_intakes.compiled_scenario);
    expect(first_source_pcm == second_source_pcm,
           "repeated Shovelhead A renders were not byte-identical");
    expect(first_source_pcm.size() == swapped_pcm.size() &&
               first_source_pcm != swapped_pcm,
           "swapping only bank-local head flow curves did not change audition PCM");
    expect(first_source_pcm.size() == uniform_one_times_pcm.size() &&
               first_source_pcm != uniform_one_times_pcm,
           "changing only the front head to 1x flow did not change audition PCM; "
           "the executor may have collapsed to the rear profile");
    expect(first_source_pcm.size() == uniform_two_times_pcm.size() &&
               first_source_pcm != uniform_two_times_pcm,
           "changing only the rear head to 2x flow did not change audition PCM; "
           "the executor may have collapsed to the front profile");
    expect(first_equal_split_pcm == second_equal_split_pcm,
           "repeated equal-valued split-intake renders were not byte-identical");
    expect(first_source_pcm.size() == first_equal_split_pcm.size() &&
               first_source_pcm != first_equal_split_pcm,
           "identity-distinct equal-valued intakes behaved as one shared plenum");
    expect(first_equal_split_pcm.size() == differentiated_intakes_pcm.size() &&
               first_equal_split_pcm != differentiated_intakes_pcm,
           "TRX520 rear intake controls did not change Shovelhead audition PCM");
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Shovelhead topology failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
