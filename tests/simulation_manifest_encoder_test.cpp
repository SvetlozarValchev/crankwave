#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include "contract_test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;
using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;
using namespace engine_sim_offline::identity;

constexpr std::string_view kExpectedCanonicalManifestSha256 =
    "d2552d85cd3b6ac7f32c14e110bece327e87c6bb2d34997bc7be7b83b394fea2";
constexpr std::string_view kExpectedCanonicalRequestIdentitySha256 =
    "23ab2a54df036f837289a8893ab2343a77b7eecfa7443f63b1c5395485c391c8";
constexpr std::string_view kExpectedCustomizedManifestSha256 =
    "8ede36d9aefe8d14004f7591e808b7e351796efc7ba3189ae127e4ad82e05db8";
constexpr std::string_view kExpectedCustomizedRequestIdentitySha256 =
    "96ce077744030ed8e6034473e8fcfc51c894891d165f9ac79c9b37275dd3ea7d";

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void require_valid(const ValidationReport &report, std::string_view message) {
    if (report.ok()) {
        return;
    }
    std::cerr << message << ":\n";
    for (const auto &issue : report.issues) {
        std::cerr << "  " << issue.path << ": " << issue.message << '\n';
    }
    throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::string digest_hex(const Sha256Digest &digest) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::string as_string(const std::vector<std::byte> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] ExecutionFacts deterministic_execution() {
    return {
        "simulation-manifest-encoder-test-v10",
        "2026-07-28T12:34:56Z",
        std::chrono::nanoseconds{UINT64_C(1234567890)},
        "linux",
        "test-cpu",
        16,
        1,
        1,
        UINT64_C(123456),
    };
}

struct SimulationFixture {
    InputBuilder builder;
    SourceMatrixContract source_matrix = make_source_matrix();
    RenderManifest manifest{
        make_manifest_content(builder),
        deterministic_execution(),
    };
};

LegacyCamShape &only_cam_profile(LegacyCamshaftProfile &camshaft) {
    expect(camshaft.profiles.size() == 1U,
           "fixture camshaft did not contain exactly one profile");
    return camshaft.profiles.front();
}

const LegacyCamShape &only_cam_profile(const LegacyCamshaftProfile &camshaft) {
    expect(camshaft.profiles.size() == 1U,
           "fixture camshaft did not contain exactly one profile");
    return camshaft.profiles.front();
}

LegacyCamshaftProfile make_vtec_camshaft(InputBuilder &builder,
                                         const LegacyCamshaftProfile &source,
                                         std::string_view role, double maximum_lift_m) {
    const auto base =
        std::string{"engine.physics.low-order-operating-point-v1.valvetrain."
                    "alternate."} +
        std::string{role};
    const auto profile_base = base + ".profiles.profile-0";
    const auto &shape = std::get<LegacyHarmonicCamShape>(only_cam_profile(source));
    LegacyCamshaftProfile result;
    result.profiles.push_back(LegacyHarmonicCamShape{
        builder.resolved(maximum_lift_m, profile_base + ".shape.maximum_lift_m"),
        builder.resolved(shape.duration_at_reference_lift_rad.value,
                         profile_base + ".shape.duration_at_reference_lift_rad"),
        builder.resolved(shape.exponent.value, profile_base + ".shape.exponent"),
        builder.resolved(shape.construction_steps.value,
                         profile_base + ".shape.construction_steps"),
        builder.resolved(shape.advance_rad.value, profile_base + ".shape.advance_rad"),
        builder.resolved(shape.base_radius_m.value,
                         profile_base + ".shape.base_radius_m"),
    });
    for (const auto &lobe : source.lobes) {
        result.lobes.push_back({
            lobe.cylinder_id,
            lobe.port_id,
            lobe.profile_index,
            builder.resolved(lobe.crank_center_rad.value,
                             base + ".lobes.cylinder-1.crank_center_rad"),
        });
    }
    return result;
}

LegacyVtecAlternateCamProfile
make_vtec_alternate(InputBuilder &builder, const LegacyValvetrainProfile &valvetrain) {
    constexpr std::string_view kActivationBase =
        "engine.physics.low-order-operating-point-v1.valvetrain.alternate.selectors."
        "selector-0.activation";
    return {
        make_vtec_camshaft(builder, valvetrain.intake, "intake", 0.0115),
        make_vtec_camshaft(builder, valvetrain.exhaust, "exhaust", 0.0105),
        {{
            BankId{1},
            {
                builder.resolved(607.3745796940267, std::string{kActivationBase} +
                                                        ".minimum_engine_speed_rad_s"),
                builder.resolved(84393.05666666667,
                                 std::string{kActivationBase} +
                                     ".minimum_mean_manifold_pressure_pa_abs"),
                builder.resolved(0.3, std::string{kActivationBase} +
                                          ".minimum_throttle_linkage_opening_01"),
            },
        }},
    };
}

[[nodiscard]] std::vector<std::byte>
require_manifest_encoding(const RenderManifest &manifest) {
    auto result = encode_simulation_manifest_v10(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<ManifestEncoding>(result).bytes);
}

[[nodiscard]] SimulationRequestIdentityEncoding require_request_identity_encoding(
    const EngineSpec &engine, const RenderScenario &scenario,
    const RandomPlan &random_plan, const ProvenanceBundleRef &provenance) {
    auto result = encode_simulation_request_identity_v7(engine, scenario, random_plan,
                                                        provenance);
    if (const auto *error = std::get_if<SimulationRequestIdentityError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<SimulationRequestIdentityEncoding>(result));
}

void expect_manifest_error(const RenderManifest &manifest,
                           std::string_view detail_code) {
    const auto result = encode_simulation_manifest_v10(manifest);
    const auto *error = std::get_if<RenderSinkError>(&result);
    expect(error != nullptr, "invalid simulation manifest unexpectedly encoded");
    expect(error->kind == RenderSinkErrorKind::protocol_violation,
           "invalid simulation manifest returned the wrong error kind");
    expect(error->detail_code == detail_code,
           "invalid simulation manifest returned the wrong detail code");
}

void expect_request_identity_error(const EngineSpec &engine,
                                   const RenderScenario &scenario,
                                   const RandomPlan &random_plan,
                                   const ProvenanceBundleRef &provenance,
                                   std::string_view detail_code) {
    const auto result = encode_simulation_request_identity_v7(engine, scenario,
                                                              random_plan, provenance);
    const auto *error = std::get_if<SimulationRequestIdentityError>(&result);
    expect(error != nullptr,
           "invalid simulation request identity unexpectedly encoded");
    expect(error->detail_code == detail_code,
           "invalid simulation request identity returned the wrong detail code");
}

struct GoldenHashes {
    std::string manifest;
    std::string request_identity;
};

[[nodiscard]] GoldenHashes test_deterministic_roots() {
    SimulationFixture fixture;
    require_valid(
        validate(fixture.manifest, fixture.builder.provenance, fixture.source_matrix),
        "simulation encoder fixture is not a valid completed manifest");

    const auto first_manifest = require_manifest_encoding(fixture.manifest);
    const auto second_manifest = require_manifest_encoding(fixture.manifest);
    expect(first_manifest == second_manifest,
           "identical simulation manifests produced different bytes");

    const auto manifest_document = as_string(first_manifest);
    constexpr std::string_view kManifestPrefix =
        "{\"wire_schema\":\"engine-sim-offline.render-manifest.simulation.v10\","
        "\"content\":{\"schema_version\":10,\"inputs\":{\"kind\":\"simulation_v9\","
        "\"value\":{\"resolved\":{\"engine\":";
    expect(manifest_document.starts_with(kManifestPrefix),
           "simulation manifest root, discriminator, or member order changed");
    expect(manifest_document.ends_with("}}\n"),
           "simulation manifest canonical suffix changed");
    expect(std::count(manifest_document.begin(), manifest_document.end(), '\n') == 1,
           "simulation manifest contains non-terminal whitespace");

    const auto engine_key = manifest_document.find("\"engine\":");
    const auto presentation_key = manifest_document.find("\"presentation\":");
    const auto randomness_key = manifest_document.find("\"randomness\":");
    const auto scenario_key = manifest_document.find("\"scenario\":");
    const auto execution_key = manifest_document.rfind("\"execution\":");
    expect(engine_key != std::string::npos && presentation_key != std::string::npos &&
               randomness_key != std::string::npos &&
               scenario_key != std::string::npos &&
               execution_key != std::string::npos && engine_key < presentation_key &&
               presentation_key < randomness_key && randomness_key < scenario_key &&
               scenario_key < execution_key,
           "simulation manifest resolved-input or root member order changed");
    const auto crankshafts_key = manifest_document.find("\"crankshafts\":", engine_key);
    const auto output_crankshaft_key =
        manifest_document.find("\"output_crankshaft_id\":", crankshafts_key);
    const auto banks_key = manifest_document.find("\"banks\":", output_crankshaft_key);
    const auto intakes_key = manifest_document.find("\"intakes\":", banks_key);
    const auto cylinders_key = manifest_document.find("\"cylinders\":", intakes_key);
    const auto public_cylinder_crankshaft_id =
        manifest_document.find("\"crankshaft_id\":", cylinders_key);
    const auto public_cylinder_intake_id =
        manifest_document.find("\"intake_id\":", public_cylinder_crankshaft_id);
    expect(crankshafts_key != std::string::npos &&
               output_crankshaft_key != std::string::npos &&
               banks_key != std::string::npos && intakes_key != std::string::npos &&
               cylinders_key != std::string::npos &&
               public_cylinder_crankshaft_id != std::string::npos &&
               public_cylinder_intake_id != std::string::npos &&
               crankshafts_key < output_crankshaft_key &&
               output_crankshaft_key < banks_key && banks_key < intakes_key &&
               intakes_key < cylinders_key &&
               cylinders_key < public_cylinder_crankshaft_id &&
               public_cylinder_crankshaft_id < public_cylinder_intake_id,
           "engine crank/intake identities or cylinder bindings were omitted or "
           "reordered");
    expect(manifest_document.find(
               "\"seed_namespace_id\":{\"value\":\"baked.loaded_acceleration\","
               "\"resolution_id\":") != std::string::npos,
           "resolved seed namespace was omitted or flattened");
    expect(manifest_document.find("\"presentation\":{\"schema_version\":2,") !=
               std::string::npos,
           "presentation-calibration v2 was not emitted");
    expect(manifest_document.find("\"algorithm_record\":") == std::string::npos,
           "retired presentation algorithm record leaked into manifest v10");

    constexpr std::string_view kProfilePrefix =
        "\"physics_profile\":{\"kind\":\"low_order_operating_point_v1\",\"value\":{"
        "\"core\":{\"mechanism\":{\"output_crankshaft_id\":";
    const auto profile = manifest_document.find(kProfilePrefix);
    const auto core_cranks = manifest_document.find("\"cranks\":[", profile);
    const auto core_crankshaft_id =
        manifest_document.find("\"crankshaft_id\":", core_cranks);
    const auto crank_tdc_reference =
        manifest_document.find("\"crank_tdc_reference_rad\":", core_crankshaft_id);
    const auto core_cylinders =
        manifest_document.find("\"cylinders\":[", crank_tdc_reference);
    const auto topology_crankshaft_id =
        manifest_document.find("\"crankshaft_id\":", core_cylinders);
    const auto cylinder_blowby =
        manifest_document.find("\"piston_blowby\":", topology_crankshaft_id);
    const auto gas_path = manifest_document.find("\"gas_path\":", profile);
    const auto gas_intakes = manifest_document.find("\"intakes\":[", gas_path);
    const auto intake_topology =
        manifest_document.find("\"topology\":{\"intake_id\":", gas_intakes);
    const auto intake_parameters = manifest_document.find(
        "\"parameters\":{\"plenum_volume_m3\":", intake_topology);
    const auto running_crank_friction =
        manifest_document.find("\"running_friction_torque_magnitude_nm\":", profile);
    const auto aggregate_loss = manifest_document.find("\"aggregate_loss\":{", profile);
    const auto accessory_configuration =
        manifest_document.find("\"accessory_configuration\":{", aggregate_loss);
    const auto starter =
        manifest_document.find("\"starter\":{", accessory_configuration);
    const auto cycle_quadrature =
        manifest_document.find("\"cycle_quadrature\":{", starter);
    const auto torque_capability =
        manifest_document.find("\"torque_capability\":", cycle_quadrature);
    expect(profile != std::string::npos && core_cranks != std::string::npos &&
               core_crankshaft_id != std::string::npos &&
               crank_tdc_reference != std::string::npos &&
               core_cylinders != std::string::npos &&
               topology_crankshaft_id != std::string::npos &&
               cylinder_blowby != std::string::npos && gas_path != std::string::npos &&
               gas_intakes != std::string::npos &&
               intake_topology != std::string::npos &&
               intake_parameters != std::string::npos &&
               running_crank_friction != std::string::npos &&
               aggregate_loss != std::string::npos &&
               accessory_configuration != std::string::npos &&
               starter != std::string::npos && cycle_quadrature != std::string::npos &&
               torque_capability != std::string::npos && profile < core_cranks &&
               core_cranks < core_crankshaft_id &&
               core_crankshaft_id < crank_tdc_reference &&
               crank_tdc_reference < running_crank_friction &&
               running_crank_friction < core_cylinders &&
               core_cylinders < topology_crankshaft_id &&
               topology_crankshaft_id < cylinder_blowby && cylinder_blowby < gas_path &&
               gas_path < gas_intakes && gas_intakes < intake_topology &&
               intake_topology < intake_parameters &&
               running_crank_friction < aggregate_loss &&
               aggregate_loss < accessory_configuration &&
               accessory_configuration < starter && starter < cycle_quadrature &&
               cycle_quadrature < torque_capability,
           "operating-point profile was flattened or its member order changed");
    expect(manifest_document.find("\"piston_blowby\":", gas_path) == std::string::npos,
           "per-cylinder blowby collapsed back into the shared gas path");
    expect(manifest_document.find("\"mechanism\":{\"crank\":", profile) ==
               std::string::npos,
           "retired singular crank assembly leaked into manifest v10");
    expect(manifest_document.find("\"intake_topology\":", gas_path) ==
               std::string::npos,
           "retired singular intake topology leaked into manifest v10");
    expect(manifest_document.find("\"instantaneous_net_shaft\":{"
                                  "\"availability\":\"available\","
                                  "\"completeness\":\"complete\","
                                  "\"included_terms\":\"0x00000000000000ff\","
                                  "\"omitted_terms\":\"0x0000000000000000\"}") !=
               std::string::npos,
           "instantaneous torque capability was projected or reordered");
    expect(manifest_document.find("\"cycle_mean_net_shaft\":{"
                                  "\"availability\":\"available\","
                                  "\"completeness\":\"complete\","
                                  "\"included_terms\":\"0x00000000000000ff\","
                                  "\"omitted_terms\":\"0x0000000000000000\"}") !=
               std::string::npos,
           "cycle-mean torque capability was projected or reordered");
    expect(manifest_document.find("\"physical_net_complete\":") == std::string::npos &&
               manifest_document.find("\"cycle_integration_available\":") ==
                   std::string::npos,
           "retired torque projection leaked into manifest v10");

    const auto &resolved = simulation_inputs(fixture.manifest.content);
    const auto first_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    const auto second_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(first_identity == second_identity,
           "identical simulation requests produced different identity encodings");
    expect(first_identity.sha256 == sha256(first_identity.bytes),
           "request identity reported a digest for different bytes");

    const auto identity_document = as_string(first_identity.bytes);
    constexpr std::string_view kIdentityPrefix =
        "{\"wire_schema\":\"engine-sim-offline.simulation-request-identity.v7\","
        "\"engine\":";
    expect(identity_document.starts_with(kIdentityPrefix),
           "request identity root or member order changed");
    expect(identity_document.ends_with("}}\n"),
           "request identity canonical suffix changed");
    expect(std::count(identity_document.begin(), identity_document.end(), '\n') == 1,
           "request identity contains non-terminal whitespace");
    const auto identity_engine_key = identity_document.find("\"engine\":");
    const auto identity_scenario_key = identity_document.find("\"scenario\":");
    const auto identity_random_plan_key = identity_document.find("\"random_plan\":");
    const auto identity_provenance_key = identity_document.find("\"provenance\":");
    expect(identity_engine_key < identity_scenario_key &&
               identity_scenario_key < identity_random_plan_key &&
               identity_random_plan_key < identity_provenance_key &&
               identity_document.find("\"presentation\":") == std::string::npos,
           "request identity member order or excluded presentation changed");
    const auto identity_crankshafts =
        identity_document.find("\"crankshafts\":", identity_engine_key);
    const auto identity_output_crankshaft =
        identity_document.find("\"output_crankshaft_id\":", identity_crankshafts);
    const auto identity_cylinder =
        identity_document.find("\"cylinders\":", identity_output_crankshaft);
    const auto identity_cylinder_crankshaft =
        identity_document.find("\"crankshaft_id\":", identity_cylinder);
    const auto identity_core_output_crankshaft = identity_document.find(
        "\"mechanism\":{\"output_crankshaft_id\":", identity_cylinder_crankshaft);
    const auto identity_core_cranks =
        identity_document.find("\"cranks\":[", identity_core_output_crankshaft);
    const auto identity_core_cylinders =
        identity_document.find("\"cylinders\":[", identity_core_cranks);
    const auto identity_cylinder_blowby =
        identity_document.find("\"piston_blowby\":", identity_core_cylinders);
    const auto identity_gas_path =
        identity_document.find("\"gas_path\":", identity_cylinder_blowby);
    expect(identity_crankshafts != std::string::npos &&
               identity_output_crankshaft != std::string::npos &&
               identity_cylinder != std::string::npos &&
               identity_cylinder_crankshaft != std::string::npos &&
               identity_core_output_crankshaft != std::string::npos &&
               identity_core_cranks != std::string::npos &&
               identity_core_cylinders != std::string::npos &&
               identity_cylinder_blowby != std::string::npos &&
               identity_gas_path != std::string::npos &&
               identity_crankshafts < identity_output_crankshaft &&
               identity_output_crankshaft < identity_cylinder &&
               identity_cylinder < identity_cylinder_crankshaft &&
               identity_cylinder_crankshaft < identity_core_output_crankshaft &&
               identity_core_output_crankshaft < identity_core_cranks &&
               identity_core_cranks < identity_core_cylinders &&
               identity_core_cylinders < identity_cylinder_blowby &&
               identity_cylinder_blowby < identity_gas_path &&
               identity_document.find("\"piston_blowby\":", identity_gas_path) ==
                   std::string::npos,
           "request identity omitted or reordered mechanism ownership");

    auto mutated_plan = fixture.manifest.content.randomness;
    ++mutated_plan.component_seeds.front().initial_state;
    const auto mutated_seed_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, mutated_plan,
        fixture.manifest.content.provenance);
    expect(mutated_seed_identity.sha256 != first_identity.sha256,
           "executed component-seed mutation did not change request identity");

    auto changed_randomness = resolved.randomness;
    changed_randomness.seed_namespace_id.value += ".identity-variant";
    auto changed_plan_result = compile_random_plan(
        changed_randomness, resolved.engine, resolved.presentation, resolved.scenario);
    const auto *changed_plan = std::get_if<RandomPlan>(&changed_plan_result);
    expect(changed_plan != nullptr &&
               *changed_plan != fixture.manifest.content.randomness,
           "changed seed namespace did not derive a distinct canonical random plan");
    const auto changed_namespace_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, *changed_plan,
        fixture.manifest.content.provenance);
    expect(changed_namespace_identity.sha256 != first_identity.sha256,
           "seed-namespace-derived plan change did not change request identity");

    auto changed_engine_crank_identity = resolved.engine;
    changed_engine_crank_identity.crankshafts.front().semantic_id.value += ".variant";
    const auto changed_engine_crank_identity_encoding =
        require_request_identity_encoding(
            changed_engine_crank_identity, resolved.scenario,
            fixture.manifest.content.randomness, fixture.manifest.content.provenance);
    expect(changed_engine_crank_identity_encoding.sha256 != first_identity.sha256,
           "public crankshaft identity mutation did not change request identity");

    auto changed_core_crank_binding = resolved.engine;
    auto &changed_core = std::get<LowOrderOperatingPointV1Profile>(
                             changed_core_crank_binding.physics_profile)
                             .core;
    changed_core.mechanism.cranks.front().crankshaft_id = CrankshaftId{2};
    const auto changed_core_crank_binding_encoding = require_request_identity_encoding(
        changed_core_crank_binding, resolved.scenario,
        fixture.manifest.content.randomness, fixture.manifest.content.provenance);
    expect(changed_core_crank_binding_encoding.sha256 != first_identity.sha256,
           "executable crankshaft binding mutation did not change request identity");

    return {
        digest_hex(sha256(first_manifest)),
        digest_hex(first_identity.sha256),
    };
}

void test_sampled_cam_request_identity_wire_shape() {
    SimulationFixture fixture;
    auto &resolved = simulation_inputs(fixture.manifest.content);
    auto &intake =
        std::get<LowOrderOperatingPointV1Profile>(resolved.engine.physics_profile)
            .core.valvetrain.intake;
    const auto harmonic = std::get<LegacyHarmonicCamShape>(only_cam_profile(intake));
    constexpr std::string_view kBase =
        "engine.physics.low-order-operating-point-v1.valvetrain.intake.profiles."
        "profile-0.shape";
    const auto point = [&](std::string id, double angle_rad, double lift_m) {
        const auto path = std::string{kBase} + ".samples." + id;
        return LegacySampledCamPoint{
            fixture.builder.resolved(id, path + ".sample_id"),
            fixture.builder.resolved(angle_rad, path + ".angle_rad"),
            fixture.builder.resolved(lift_m, path + ".lift_m"),
        };
    };
    only_cam_profile(intake) = LegacySampledCamShape{
        fixture.builder.resolved(0.01, std::string{kBase} + ".triangle_radius_rad"),
        {
            point("opening", -1.0, 0.0),
            point("peak", 0.0, 0.009),
            point("closing", 1.0, 0.0),
        },
        harmonic.advance_rad,
        harmonic.base_radius_m,
    };

    const auto first = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    const auto second = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(first == second, "sampled cam request identity was not deterministic");

    const auto document = as_string(first.bytes);
    const auto shape = document.find("\"profiles\":[{\"kind\":\"sampled\","
                                     "\"triangle_radius_rad\":");
    const auto samples = document.find("\"samples\":[{\"sample_id\":", shape);
    const auto angle = document.find("\"angle_rad\":", samples);
    const auto lift = document.find("\"lift_m\":", angle);
    const auto lobes = document.find("\"lobes\":", lift);
    const auto profile_index = document.find("\"profile_index\":0", lobes);
    expect(shape != std::string::npos && samples != std::string::npos &&
               angle != std::string::npos && lift != std::string::npos &&
               lobes != std::string::npos && profile_index != std::string::npos &&
               shape < samples && samples < angle && angle < lift && lift < lobes &&
               lobes < profile_index,
           "sampled cam request identity omitted or reordered its disjoint wire "
           "object");
    expect(document.find("\"maximum_lift_m\":", shape) > lobes,
           "sampled cam wire object leaked harmonic shape members");

    auto changed_engine = resolved.engine;
    std::get<LegacySampledCamShape>(
        only_cam_profile(
            std::get<LowOrderOperatingPointV1Profile>(changed_engine.physics_profile)
                .core.valvetrain.intake))
        .samples[1]
        .lift_m.value += 0.001;
    const auto changed = require_request_identity_encoding(
        changed_engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(changed.sha256 != first.sha256,
           "sampled cam lift mutation did not change request identity");
}

void test_vtec_request_identity_wire_shape() {
    SimulationFixture fixture;
    auto &resolved = simulation_inputs(fixture.manifest.content);
    auto &valvetrain =
        std::get<LowOrderOperatingPointV1Profile>(resolved.engine.physics_profile)
            .core.valvetrain;
    valvetrain.alternate = make_vtec_alternate(fixture.builder, valvetrain);
    require_valid(validate(resolved.engine, fixture.builder.provenance),
                  "VTEC request-identity fixture engine is invalid");

    const auto manifest_document =
        as_string(require_manifest_encoding(fixture.manifest));
    const auto valvetrain_position = manifest_document.find("\"valvetrain\":");
    const auto alternate_position =
        manifest_document.find("\"alternate\":{\"intake\":", valvetrain_position);
    const auto alternate_exhaust_position =
        manifest_document.find("\"exhaust\":", alternate_position);
    const auto selectors_position =
        manifest_document.find("\"selectors\":[", alternate_exhaust_position);
    const auto selector_bank_position =
        manifest_document.find("\"bank_id\":", selectors_position);
    const auto activation_position =
        manifest_document.find("\"activation\":", selector_bank_position);
    const auto speed_position =
        manifest_document.find("\"minimum_engine_speed_rad_s\":", activation_position);
    const auto pressure_position = manifest_document.find(
        "\"minimum_mean_manifold_pressure_pa_abs\":", speed_position);
    const auto throttle_position = manifest_document.find(
        "\"minimum_throttle_linkage_opening_01\":", pressure_position);
    expect(valvetrain_position != std::string::npos &&
               alternate_position != std::string::npos &&
               alternate_exhaust_position != std::string::npos &&
               selectors_position != std::string::npos &&
               selector_bank_position != std::string::npos &&
               activation_position != std::string::npos &&
               speed_position != std::string::npos &&
               pressure_position != std::string::npos &&
               throttle_position != std::string::npos &&
               valvetrain_position < alternate_position &&
               alternate_position < alternate_exhaust_position &&
               alternate_exhaust_position < selectors_position &&
               selectors_position < selector_bank_position &&
               selector_bank_position < activation_position &&
               activation_position < speed_position &&
               speed_position < pressure_position &&
               pressure_position < throttle_position,
           "manifest omitted or reordered VTEC alternate cams and activation "
           "thresholds");

    const auto first = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    const auto identity_document = as_string(first.bytes);
    expect(identity_document.find("\"alternate\":{\"intake\":") != std::string::npos &&
               identity_document.find("\"selectors\":[{\"bank_id\":") !=
                   std::string::npos &&
               identity_document.find("\"minimum_engine_speed_rad_s\":") !=
                   std::string::npos &&
               identity_document.find("\"minimum_mean_manifold_pressure_pa_abs\":") !=
                   std::string::npos &&
               identity_document.find("\"minimum_throttle_linkage_opening_01\":") !=
                   std::string::npos,
           "request identity omitted VTEC alternate cams or thresholds");

    auto changed_lift = resolved.engine;
    std::get<LegacyHarmonicCamShape>(
        only_cam_profile(
            std::get<LowOrderOperatingPointV1Profile>(changed_lift.physics_profile)
                .core.valvetrain.alternate->intake))
        .maximum_lift_m.value += 0.001;
    const auto lift_identity = require_request_identity_encoding(
        changed_lift, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(lift_identity.sha256 != first.sha256,
           "alternate VTEC cam-lift mutation did not change request identity");

    auto changed_threshold = resolved.engine;
    std::get<LowOrderOperatingPointV1Profile>(changed_threshold.physics_profile)
        .core.valvetrain.alternate->selectors.front()
        .activation.minimum_engine_speed_rad_s.value += 1.0;
    const auto threshold_identity = require_request_identity_encoding(
        changed_threshold, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(threshold_identity.sha256 != first.sha256,
           "VTEC activation-threshold mutation did not change request identity");

    auto changed_selector_bank = resolved.engine;
    std::get<LowOrderOperatingPointV1Profile>(changed_selector_bank.physics_profile)
        .core.valvetrain.alternate->selectors.front()
        .bank_id = BankId{2U};
    const auto selector_bank_identity = require_request_identity_encoding(
        changed_selector_bank, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(selector_bank_identity.sha256 != first.sha256,
           "VTEC selector bank mutation did not change request identity");
}

void test_fail_closed_boundaries() {
    SimulationFixture fixture;

    for (const auto schema_version :
         {UINT32_C(6), UINT32_C(7), UINT32_C(8), UINT32_C(9)}) {
        auto unsupported_schema = fixture.manifest;
        unsupported_schema.content.schema_version = schema_version;
        expect_manifest_error(unsupported_schema,
                              "simulation-manifest-wire-unrepresentable");
    }

    for (const auto schema_version : {UINT32_C(1), UINT32_C(3)}) {
        auto unsupported_presentation = fixture.manifest;
        simulation_inputs(unsupported_presentation.content)
            .presentation.schema_version = schema_version;
        expect_manifest_error(unsupported_presentation,
                              "simulation-manifest-wire-unrepresentable");
    }

    auto missing_execution = fixture.manifest;
    missing_execution.execution.reset();
    expect_manifest_error(missing_execution, "simulation-manifest-execution-missing");

    auto nonfinite = fixture.manifest;
    simulation_inputs(nonfinite.content).presentation.conditioning.jitter_scale.value =
        std::numeric_limits<double>::infinity();
    expect_manifest_error(nonfinite, "simulation-manifest-wire-nonfinite");
}

void test_temporally_distinct_torque_capability() {
    SimulationFixture fixture;
    auto temporally_distinct_torque = fixture.manifest;
    simulation_inputs(temporally_distinct_torque.content)
        .engine.torque_capability.value.instantaneous_net_shaft = {
        Availability::unavailable,
        Completeness::incomplete,
        0,
        0,
    };
    simulation_inputs(temporally_distinct_torque.content)
        .engine.torque_capability.value.cycle_mean_net_shaft = {
        Availability::available,
        Completeness::complete,
        known_torque_term_mask(),
        0,
    };
    simulation_inputs(temporally_distinct_torque.content)
        .engine.torque_capability.value.equivalent_inertia_available = false;

    const auto manifest_document =
        as_string(require_manifest_encoding(temporally_distinct_torque));
    constexpr std::string_view kDirectTemporalCapability =
        "\"torque_capability\":{\"value\":{"
        "\"instantaneous_net_shaft\":{\"availability\":\"unavailable\","
        "\"completeness\":\"incomplete\","
        "\"included_terms\":\"0x0000000000000000\","
        "\"omitted_terms\":\"0x0000000000000000\"},"
        "\"cycle_mean_net_shaft\":{\"availability\":\"available\","
        "\"completeness\":\"complete\","
        "\"included_terms\":\"0x00000000000000ff\","
        "\"omitted_terms\":\"0x0000000000000000\"},"
        "\"equivalent_inertia_available\":false},\"resolution_id\":";
    expect(manifest_document.find(kDirectTemporalCapability) != std::string::npos,
           "temporally distinct torque capability was not encoded directly");

    const auto &distinct_inputs = simulation_inputs(temporally_distinct_torque.content);
    const auto request_document =
        as_string(require_request_identity_encoding(
                      distinct_inputs.engine, distinct_inputs.scenario,
                      temporally_distinct_torque.content.randomness,
                      temporally_distinct_torque.content.provenance)
                      .bytes);
    expect(request_document.find(kDirectTemporalCapability) != std::string::npos,
           "request identity projected a temporally distinct torque capability");
}

void configure_customized_operating_point_wire_fixture(SimulationFixture &fixture) {
    auto &resolved = simulation_inputs(fixture.manifest.content);
    auto &engine = resolved.engine;
    auto &profile = std::get<LowOrderOperatingPointV1Profile>(engine.physics_profile);

    constexpr std::string_view kRoot =
        "encoder.customized.low-order-operating-point-v1";
    const auto path = [kRoot](std::string_view suffix) {
        return std::string{kRoot} + "." + std::string{suffix};
    };
    profile.aggregate_loss = {
        fixture.builder.resolved(1.25, path("aggregate_loss.constant_fmep_bar")),
        fixture.builder.resolved(0.004,
                                 path("aggregate_loss.peak_pressure_coefficient")),
        fixture.builder.resolved(
            0.03, path("aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m")),
        fixture.builder.resolved(
            0.002, path("aggregate_loss."
                        "mean_piston_speed_squared_coefficient_bar_s2_per_m2")),
        fixture.builder.resolved(370.0,
                                 path("aggregate_loss.required_oil_temperature_k")),
        fixture.builder.resolved(friction_pump_and_accessory_torque_term_mask(),
                                 path("aggregate_loss.included_terms")),
    };
    profile.accessory_configuration = {
        fixture.builder.resolved(std::string{"encoder-accessory-configuration-v1"},
                                 path("accessory_configuration.configuration_id")),
        fixture.builder.resolved(digest(1),
                                 path("accessory_configuration.content_sha256")),
    };
    profile.starter = {
        fixture.builder.resolved(StarterCapabilityType::cranking, path("starter.type")),
        fixture.builder.resolved(150.0, path("starter.maximum_torque_nm")),
        fixture.builder.resolved(27.2271363, path("starter.target_speed_rad_s")),
        fixture.builder.resolved(torque_term_mask(TorqueTerm::starter),
                                 path("starter.included_terms")),
    };
    profile.cycle_quadrature = fixture.builder.resolved(
        method("four-stroke-piecewise-linear-cycle-quadrature-v1", 61),
        path("cycle_quadrature"));
    engine.methods.losses.value = method("chen-flynn-cycle-mean-aggregate-loss-v1", 62);
    engine.torque_capability.value = {
        {
            Availability::unavailable,
            Completeness::incomplete,
            0,
            0,
        },
        {
            Availability::available,
            Completeness::complete,
            known_torque_term_mask(),
            0,
        },
        false,
    };
    engine.profile_id.value = "customized-operating-point-wire-test";
    resolved.presentation.engine_profile_id.value = engine.profile_id.value;

    auto &scenario = resolved.scenario;
    scenario.engine_profile_id = engine.profile_id.value;
    scenario.preparation = FixedHorizonCycleSampling{
        fixture.builder.resolved(fixed_horizon_cycle_sampling_method_identity(),
                                 "scenario.preparation.method"),
        fixture.builder.resolved(2.0,
                                 "scenario.preparation.fixed_preparation_horizon_s"),
        fixture.builder.resolved<std::uint32_t>(
            32, "scenario.preparation.trailing_complete_cycle_count"),
    };
    scenario.mode = HeldSpeed{
        fixture.builder.resolved(3000.0, "scenario.mode.engine_speed_rpm"),
        fixture.builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        fixture.builder.resolved(0.85, "scenario.mode.throttle_01"),
    };
    scenario.operating_state.value.front().state.limiter_enabled = false;
}

[[nodiscard]] GoldenHashes test_customized_direct_wire_shape() {
    // This fixture exercises direct wire enumeration with independently resolved
    // operating-point accounting inputs.
    SimulationFixture fixture;
    configure_customized_operating_point_wire_fixture(fixture);

    const auto first_manifest = require_manifest_encoding(fixture.manifest);
    const auto second_manifest = require_manifest_encoding(fixture.manifest);
    expect(first_manifest == second_manifest,
           "identical customized simulation manifests produced different bytes");
    const auto manifest_document = as_string(first_manifest);

    const auto profile = manifest_document.find(
        "\"physics_profile\":{\"kind\":\"low_order_operating_point_v1\","
        "\"value\":{");
    const auto core = manifest_document.find("\"core\":", profile);
    const auto aggregate_loss = manifest_document.find("\"aggregate_loss\":", core);
    const auto accessory =
        manifest_document.find("\"accessory_configuration\":", aggregate_loss);
    const auto starter = manifest_document.find("\"starter\":", accessory);
    const auto quadrature = manifest_document.find("\"cycle_quadrature\":", starter);
    const auto torque_capability =
        manifest_document.find("\"torque_capability\":", quadrature);
    expect(profile != std::string::npos && core != std::string::npos &&
               aggregate_loss != std::string::npos && accessory != std::string::npos &&
               starter != std::string::npos && quadrature != std::string::npos &&
               torque_capability != std::string::npos && profile < core &&
               core < aggregate_loss && aggregate_loss < accessory &&
               accessory < starter && starter < quadrature &&
               quadrature < torque_capability,
           "customized profile tag or five-member direct wire order changed");
    expect(
        manifest_document.find("\"aggregate_loss\":{\"constant_fmep_bar\":", profile) !=
                std::string::npos &&
            manifest_document.find("\"accessory_configuration\":{\"configuration_id\":",
                                   aggregate_loss) != std::string::npos &&
            manifest_document.find("\"starter\":{\"type\":{\"value\":\"cranking\",",
                                   accessory) != std::string::npos &&
            manifest_document.find("\"maximum_torque_nm\":", starter) !=
                std::string::npos &&
            manifest_document.find("\"target_speed_rad_s\":", starter) !=
                std::string::npos &&
            manifest_document.find(
                "\"cycle_quadrature\":{\"value\":{\"id\":"
                "\"four-stroke-piecewise-linear-cycle-quadrature-v1\"",
                starter) != std::string::npos,
        "one or more direct operating-point profile members were omitted or flattened");

    constexpr std::string_view kSamplingPrefix =
        "\"preparation\":{\"kind\":\"fixed_horizon_cycle_sampling\",\"value\":{"
        "\"method\":{\"value\":{\"id\":"
        "\"fixed-horizon-trailing-complete-cycle-sample-v1\"";
    expect(manifest_document.find(kSamplingPrefix) != std::string::npos,
           "fixed-horizon sampling did not encode its leading method identity");
    const auto sampling = manifest_document.find(kSamplingPrefix);
    const auto horizon =
        manifest_document.find("\"fixed_preparation_horizon_s\":", sampling);
    const auto cycle_count =
        manifest_document.find("\"trailing_complete_cycle_count\":", sampling);
    expect(sampling != std::string::npos && horizon != std::string::npos &&
               cycle_count != std::string::npos && sampling < horizon &&
               horizon < cycle_count,
           "fixed-horizon preparation value members changed order");

    const auto &resolved = simulation_inputs(fixture.manifest.content);
    const auto first_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    const auto second_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(first_identity == second_identity,
           "identical customized requests produced different identity encodings");
    expect(as_string(first_identity.bytes).find(kSamplingPrefix) != std::string::npos,
           "customized request identity omitted fixed-horizon sampling");
    expect(as_string(first_identity.bytes)
                   .find("\"starter\":{\"type\":{\"value\":\"cranking\",") !=
               std::string::npos,
           "customized request identity omitted cranking starter capability");

    auto changed_engine = resolved.engine;
    std::get<LowOrderOperatingPointV1Profile>(changed_engine.physics_profile)
        .starter.maximum_torque_nm.value += 1.0;
    const auto changed_identity = require_request_identity_encoding(
        changed_engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(changed_identity.sha256 != first_identity.sha256,
           "starter maximum torque mutation did not change request identity");

    return {
        digest_hex(sha256(first_manifest)),
        digest_hex(first_identity.sha256),
    };
}

void set_compact_fixed_rate_sweep(SimulationFixture &fixture) {
    auto &scenario = simulation_inputs(fixture.manifest.content).scenario;
    const auto &held = std::get<HeldSpeed>(scenario.mode);
    const auto initial_theta = held.initial_theta_rad;
    const auto throttle_resolution_id = held.throttle_01.resolution_id;
    auto rpm_resolution_id = held.engine_speed_rpm.resolution_id;
    const auto retarget_resolution = [&](const std::string &resolution_id,
                                         std::string parameter_path) {
        const auto resolution =
            std::ranges::find(fixture.builder.provenance.resolutions, resolution_id,
                              &ResolutionRecord::id);
        expect(resolution != fixture.builder.provenance.resolutions.end(),
               "canonical held-speed resolution was not retained");
        resolution->parameter_path = std::move(parameter_path);
    };
    retarget_resolution(rpm_resolution_id, "scenario.mode.trajectory.rpm");
    retarget_resolution(initial_theta.resolution_id,
                        "scenario.mode.trajectory.initial_theta_rad");
    retarget_resolution(throttle_resolution_id, "scenario.mode.throttle_01");
    scenario.scenario_id = "fixed-rate-encoder-smoke";
    scenario.rates.physics = {1, 1};
    scenario.rates.capture = {1, 1};
    fixture.manifest.content.rates = scenario.rates;

    std::vector<double> rpm_samples{1000.0, 1500.0, 2000.0};
    FixedRateRpmTrajectory fixed_rpm{
        {1, 1},
        0,
        RpmSampleSemantics::post_step_rpm,
        std::move(rpm_samples),
        {},
        rpm_resolution_id,
    };
    fixed_rpm.samples_f64le_sha256 =
        canonical_binary64_le_sha256(fixed_rpm.post_step_rpm);

    RpmTrajectory trajectory{
        std::move(fixed_rpm),
        initial_theta,
        fixture.builder.resolved(fixed_rate_rpm_method(),
                                 "scenario.mode.trajectory.kinematic_resolution"),
    };
    ScalarTrajectory throttle{
        TrajectoryInterpolation::right_continuous_hold,
        {
            {0.0, 0.25},
            {2.0, 0.75},
        },
        throttle_resolution_id,
    };
    scenario.mode =
        PrescribedKinematicSweep{std::move(trajectory), std::move(throttle)};
}

[[nodiscard]] FixedRateRpmTrajectory &fixed_rpm_trajectory(RenderManifest &manifest) {
    auto &scenario = simulation_inputs(manifest.content).scenario;
    auto &sweep = std::get<PrescribedKinematicSweep>(scenario.mode);
    return std::get<FixedRateRpmTrajectory>(sweep.trajectory.rpm);
}

void test_compact_fixed_rate_scenario() {
    SimulationFixture fixture;
    set_compact_fixed_rate_sweep(fixture);

    const auto bytes = require_manifest_encoding(fixture.manifest);
    const auto document = as_string(bytes);
    const auto &fixed_rpm = fixed_rpm_trajectory(fixture.manifest);
    expect(document.find("\"kind\":\"fixed_rate_rpm\"") != std::string::npos,
           "fixed-rate RPM variant tag was not emitted");
    expect(document.find("\"sample_count\":\"0x0000000000000003\"") !=
               std::string::npos,
           "fixed-rate RPM sample count was not derived from the owned vector");
    expect(document.find("\"samples_f64le_sha256\":\"" +
                         digest_hex(fixed_rpm.samples_f64le_sha256) + "\"") !=
               std::string::npos,
           "fixed-rate RPM canonical digest was not emitted");
    expect(document.find("\"post_step_rpm\":") == std::string::npos,
           "fixed-rate RPM samples were expanded into the manifest");

    auto stale_digest = fixture.manifest;
    fixed_rpm_trajectory(stale_digest).post_step_rpm[1] += 1.0;
    expect_manifest_error(stale_digest, "simulation-manifest-wire-unrepresentable");
    const auto &stale_inputs = simulation_inputs(stale_digest.content);
    expect_request_identity_error(stale_inputs.engine, stale_inputs.scenario,
                                  stale_digest.content.randomness,
                                  stale_digest.content.provenance,
                                  "simulation-request-identity-wire-unrepresentable");

    auto nonfinite_lane = fixture.manifest;
    fixed_rpm_trajectory(nonfinite_lane).post_step_rpm[1] =
        std::numeric_limits<double>::infinity();
    expect_manifest_error(nonfinite_lane, "simulation-manifest-wire-nonfinite");
    const auto &nonfinite_inputs = simulation_inputs(nonfinite_lane.content);
    expect_request_identity_error(nonfinite_inputs.engine, nonfinite_inputs.scenario,
                                  nonfinite_lane.content.randomness,
                                  nonfinite_lane.content.provenance,
                                  "simulation-request-identity-wire-nonfinite");
}

void test_free_engine_request_wire_shape() {
    SimulationFixture fixture;
    auto &resolved = simulation_inputs(fixture.manifest.content);

    const auto throttle_resolution =
        fixture.builder.add_resolution("scenario.mode.throttle_01");
    const auto resistance_resolution =
        fixture.builder.add_resolution("scenario.mode.external_resisting_torque_nm");
    resolved.scenario.mode = FreeEngine{
        fixture.builder.resolved(1500.0, "scenario.mode.initial_engine_speed_rpm"),
        fixture.builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        fixture.builder.resolved(0.20, "scenario.mode.engine_baseline_inertia_kg_m2"),
        fixture.builder.resolved(0.05, "scenario.mode.attached_inertia_kg_m2"),
        fixture.builder.resolved(0.25, "scenario.mode.total_equivalent_inertia_kg_m2"),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.10}, {2.0, 0.85}},
            throttle_resolution,
        },
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 8.0}, {2.5, 3.0}},
            resistance_resolution,
        },
        fixture.builder.resolved(method("rigid-crank-zoh-work-energy-v1", 73),
                                 "scenario.mode.crank_dynamics_method"),
    };

    const auto first = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    const auto second = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(first == second,
           "identical free-engine requests produced different identity bytes");

    const auto document = as_string(first.bytes);
    constexpr std::string_view kFreeEnginePrefix =
        "\"mode\":{\"kind\":\"free_engine\",\"value\":{"
        "\"initial_engine_speed_rpm\":";
    expect(
        document.find(kFreeEnginePrefix) != std::string::npos &&
            document.find("\"engine_baseline_inertia_kg_m2\":") != std::string::npos &&
            document.find("\"attached_inertia_kg_m2\":") != std::string::npos &&
            document.find("\"total_equivalent_inertia_kg_m2\":") != std::string::npos &&
            document.find("\"external_resisting_torque_nm\":") != std::string::npos &&
            document.find("\"crank_dynamics_method\":") != std::string::npos,
        "free-engine request fields were omitted or did not retain canonical "
        "mode order");

    auto changed_scenario = resolved.scenario;
    auto &changed_free_engine = std::get<FreeEngine>(changed_scenario.mode);
    changed_free_engine.attached_inertia_kg_m2.value += 0.01;
    changed_free_engine.total_equivalent_inertia_kg_m2.value += 0.01;
    const auto changed = require_request_identity_encoding(
        resolved.engine, changed_scenario, fixture.manifest.content.randomness,
        fixture.manifest.content.provenance);
    expect(changed.sha256 != first.sha256,
           "free-engine inertia mutation did not change request identity");
}

void test_free_vehicle_request_wire_shape() {
    SimulationFixture fixture;
    auto &resolved = simulation_inputs(fixture.manifest.content);

    FreeVehicle free_vehicle;
    free_vehicle.initial_engine_speed_rpm =
        fixture.builder.resolved(1500.0, "scenario.mode.initial_engine_speed_rpm");
    free_vehicle.initial_theta_rad =
        fixture.builder.resolved(0.0, "scenario.mode.initial_theta_rad");
    free_vehicle.engine_baseline_inertia_kg_m2 =
        fixture.builder.resolved(0.20, "scenario.mode.engine_baseline_inertia_kg_m2");
    free_vehicle.initial_vehicle_speed_m_s =
        fixture.builder.resolved(8.0, "scenario.mode.initial_vehicle_speed_m_s");
    free_vehicle.rig.id = RigId{20U};
    free_vehicle.rig.semantic_id =
        fixture.builder.resolved(std::string{"fixture-rig"}, "rig.semantic_id");
    free_vehicle.rig.vehicle.id = VehicleId{21U};
    free_vehicle.rig.vehicle.semantic_id = fixture.builder.resolved(
        std::string{"fixture-vehicle"}, "rig.vehicle.semantic_id");
    free_vehicle.rig.vehicle.mass_kg =
        fixture.builder.resolved(1500.0, "rig.vehicle.mass_kg");
    free_vehicle.rig.vehicle.drag_coefficient =
        fixture.builder.resolved(0.30, "rig.vehicle.drag_coefficient");
    free_vehicle.rig.vehicle.frontal_area_m2 =
        fixture.builder.resolved(2.10, "rig.vehicle.frontal_area_m2");
    free_vehicle.rig.vehicle.differential_ratio =
        fixture.builder.resolved(3.25, "rig.vehicle.differential_ratio");
    free_vehicle.rig.vehicle.tire_radius_m =
        fixture.builder.resolved(0.33, "rig.vehicle.tire_radius_m");
    free_vehicle.rig.vehicle.rolling_resistance_force_n =
        fixture.builder.resolved(180.0, "rig.vehicle.rolling_resistance_force_n");
    free_vehicle.rig.vehicle.maximum_service_brake_force_n =
        fixture.builder.resolved(12000.0, "rig.vehicle.maximum_service_brake_force_n");
    free_vehicle.rig.transmission.id = TransmissionId{22U};
    free_vehicle.rig.transmission.semantic_id = fixture.builder.resolved(
        std::string{"fixture-transmission"}, "rig.transmission.semantic_id");
    free_vehicle.rig.transmission.maximum_clutch_torque_nm =
        fixture.builder.resolved(450.0, "rig.transmission.maximum_clutch_torque_nm");
    free_vehicle.rig.transmission.gears = {
        {
            GearId{23U},
            fixture.builder.resolved(1U,
                                     "rig.transmission.gears.gear-1.authored_ordinal"),
            fixture.builder.resolved(std::string{"gear-1"},
                                     "rig.transmission.gears.gear-1.semantic_id"),
            fixture.builder.resolved(3.50, "rig.transmission.gears.gear-1.ratio"),
        },
        {
            GearId{24U},
            fixture.builder.resolved(2U,
                                     "rig.transmission.gears.gear-2.authored_ordinal"),
            fixture.builder.resolved(std::string{"gear-2"},
                                     "rig.transmission.gears.gear-2.semantic_id"),
            fixture.builder.resolved(2.10, "rig.transmission.gears.gear-2.ratio"),
        },
    };
    free_vehicle.throttle_01 = {
        TrajectoryInterpolation::right_continuous_hold,
        {{0.0, 0.2}, {2.0, 0.8}},
        fixture.builder.add_resolution("scenario.mode.throttle_01"),
    };
    free_vehicle.selected_gear = fixture.builder.resolved(
        std::vector<GearSelectionPoint>{{"initial-gear", 0.0, GearId{23U}},
                                        {"select-second", 2.25, GearId{24U}}},
        "scenario.mode.selected_gear");
    free_vehicle.clutch_engagement_01 = fixture.builder.resolved(
        std::vector<ScalarControlPoint>{{"initial-clutch-engagement", 0.0, 0.25},
                                        {"engage-clutch", 2.5, 0.8}},
        "scenario.mode.clutch_engagement_01");
    free_vehicle.service_brake_application_01 = fixture.builder.resolved(
        std::vector<ScalarControlPoint>{{"initial-service-brake-application", 0.0, 0.0},
                                        {"apply-brake", 2.75, 0.5}},
        "scenario.mode.service_brake_application_01");
    free_vehicle.crank_dynamics_method = fixture.builder.resolved(
        method("free-crank-v1", 70), "scenario.mode.crank_dynamics_method");
    free_vehicle.road_load_method = fixture.builder.resolved(
        method("road-load-v1", 71), "scenario.mode.road_load_method");
    free_vehicle.clutch_coupling_method = fixture.builder.resolved(
        method("clutch-coupling-v1", 72), "scenario.mode.clutch_coupling_method");
    free_vehicle.drivetrain_dynamics_method =
        fixture.builder.resolved(method("drivetrain-dynamics-v1", 73),
                                 "scenario.mode.drivetrain_dynamics_method");
    resolved.scenario.mode = std::move(free_vehicle);

    const auto encode = [&](const RenderScenario &scenario) {
        return require_request_identity_encoding(resolved.engine, scenario,
                                                 fixture.manifest.content.randomness,
                                                 fixture.manifest.content.provenance);
    };
    const auto first = encode(resolved.scenario);
    const auto second = encode(resolved.scenario);
    expect(first == second,
           "identical free-vehicle requests produced different identity bytes");

    const auto document = as_string(first.bytes);
    constexpr std::string_view kFreeVehiclePrefix =
        "\"mode\":{\"kind\":\"free_vehicle\",\"value\":{"
        "\"initial_engine_speed_rpm\":";
    expect(
        document.find(kFreeVehiclePrefix) != std::string::npos &&
            document.find("\"maximum_service_brake_force_n\":") != std::string::npos &&
            document.find("\"selected_gear\":") != std::string::npos &&
            document.find("\"clutch_engagement_01\":") != std::string::npos &&
            document.find("\"service_brake_application_01\":") != std::string::npos &&
            document.find("\"drivetrain_dynamics_method\":") != std::string::npos,
        "free-vehicle request fields were omitted or reordered");

    const auto expect_mutation_changes_identity = [&](auto mutate,
                                                      std::string_view message) {
        auto scenario = resolved.scenario;
        mutate(std::get<FreeVehicle>(scenario.mode));
        expect(encode(scenario).sha256 != first.sha256, message);
    };
    expect_mutation_changes_identity(
        [](FreeVehicle &mode) {
            mode.rig.transmission.gears.front().ratio.value += 0.01;
        },
        "free-vehicle gear-ratio mutation did not change request identity");
    expect_mutation_changes_identity(
        [](FreeVehicle &mode) {
            mode.selected_gear.value.back().gear_id = GearId{23U};
        },
        "free-vehicle gear-selection mutation did not change request identity");
    expect_mutation_changes_identity(
        [](FreeVehicle &mode) { mode.clutch_engagement_01.value.back().value = 0.7; },
        "free-vehicle clutch mutation did not change request identity");
    expect_mutation_changes_identity(
        [](FreeVehicle &mode) {
            mode.service_brake_application_01.value.back().value = 0.4;
        },
        "free-vehicle service-brake mutation did not change request identity");
}

void check_golden_hashes(const GoldenHashes &canonical,
                         const GoldenHashes &customized) {
    bool mismatch = false;
    if (canonical.manifest != kExpectedCanonicalManifestSha256) {
        std::cerr << "Canonical simulation manifest golden SHA-256: "
                  << canonical.manifest << '\n';
        mismatch = true;
    }
    if (canonical.request_identity != kExpectedCanonicalRequestIdentitySha256) {
        std::cerr << "Canonical simulation request identity golden SHA-256: "
                  << canonical.request_identity << '\n';
        mismatch = true;
    }
    if (customized.manifest != kExpectedCustomizedManifestSha256) {
        std::cerr << "Customized simulation manifest golden SHA-256: "
                  << customized.manifest << '\n';
        mismatch = true;
    }
    if (customized.request_identity != kExpectedCustomizedRequestIdentitySha256) {
        std::cerr << "Customized simulation request identity golden SHA-256: "
                  << customized.request_identity << '\n';
        mismatch = true;
    }
    expect(!mismatch, "canonical simulation encoder golden hashes are not pinned");
}

} // namespace

int main() {
    try {
        const auto canonical_hashes = test_deterministic_roots();
        test_sampled_cam_request_identity_wire_shape();
        test_vtec_request_identity_wire_shape();
        test_fail_closed_boundaries();
        test_temporally_distinct_torque_capability();
        const auto customized_hashes = test_customized_direct_wire_shape();
        test_compact_fixed_rate_scenario();
        test_free_engine_request_wire_shape();
        test_free_vehicle_request_wire_shape();
        check_golden_hashes(canonical_hashes, customized_hashes);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
