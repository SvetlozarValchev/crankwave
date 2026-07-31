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
    "88e426d8f7cd81729a3f1a85620d03f7b644da05b7a4e2d0d1dce7e5f7153a48";
constexpr std::string_view kExpectedCanonicalRequestIdentitySha256 =
    "12983cacf3f34b17f058d2285e3104fb66ed9597a154107c001141e9d78f7710";
constexpr std::string_view kExpectedCustomizedManifestSha256 =
    "cd276c055b1c9f5b250699e260d2e7589a8c541d980997f8e2b8c786a1767da5";
constexpr std::string_view kExpectedCustomizedRequestIdentitySha256 =
    "75298fa47370eaf88af92a629f3a23276d1efc4717c17f000b1d2f41b881e37e";

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
        "simulation-manifest-encoder-test-v6",
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

[[nodiscard]] std::vector<std::byte>
require_manifest_encoding(const RenderManifest &manifest) {
    auto result = encode_simulation_manifest_v6(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<ManifestEncoding>(result).bytes);
}

[[nodiscard]] SimulationRequestIdentityEncoding require_request_identity_encoding(
    const EngineSpec &engine, const RenderScenario &scenario,
    const RandomPlan &random_plan, const ProvenanceBundleRef &provenance) {
    auto result = encode_simulation_request_identity_v3(engine, scenario, random_plan,
                                                        provenance);
    if (const auto *error = std::get_if<SimulationRequestIdentityError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<SimulationRequestIdentityEncoding>(result));
}

void expect_manifest_error(const RenderManifest &manifest,
                           std::string_view detail_code) {
    const auto result = encode_simulation_manifest_v6(manifest);
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
    const auto result = encode_simulation_request_identity_v3(engine, scenario,
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
        "{\"wire_schema\":\"engine-sim-offline.render-manifest.simulation.v6\","
        "\"content\":{\"schema_version\":6,\"inputs\":{\"kind\":\"simulation_v5\","
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
    expect(manifest_document.find(
               "\"seed_namespace_id\":{\"value\":\"baked.loaded_acceleration\","
               "\"resolution_id\":") != std::string::npos,
           "resolved seed namespace was omitted or flattened");
    expect(manifest_document.find("\"presentation\":{\"schema_version\":2,") !=
               std::string::npos,
           "presentation-calibration v2 was not emitted");
    expect(manifest_document.find("\"algorithm_record\":") == std::string::npos,
           "retired presentation algorithm record leaked into manifest v6");

    constexpr std::string_view kProfilePrefix =
        "\"physics_profile\":{\"kind\":\"low_order_operating_point_v1\",\"value\":{"
        "\"core\":{\"mechanism\":{\"crank\":{\"crank_tdc_reference_rad\":";
    const auto profile = manifest_document.find(kProfilePrefix);
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
    expect(profile != std::string::npos &&
               running_crank_friction != std::string::npos &&
               aggregate_loss != std::string::npos &&
               accessory_configuration != std::string::npos &&
               starter != std::string::npos && cycle_quadrature != std::string::npos &&
               torque_capability != std::string::npos &&
               profile < running_crank_friction &&
               running_crank_friction < aggregate_loss &&
               aggregate_loss < accessory_configuration &&
               accessory_configuration < starter && starter < cycle_quadrature &&
               cycle_quadrature < torque_capability,
           "operating-point profile was flattened or its member order changed");
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
           "retired torque projection leaked into manifest v6");

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
        "{\"wire_schema\":\"engine-sim-offline.simulation-request-identity.v3\","
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

    return {
        digest_hex(sha256(first_manifest)),
        digest_hex(first_identity.sha256),
    };
}

void test_fail_closed_boundaries() {
    SimulationFixture fixture;

    for (const auto schema_version : {UINT32_C(5), UINT32_C(7)}) {
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
        test_fail_closed_boundaries();
        test_temporally_distinct_torque_capability();
        const auto customized_hashes = test_customized_direct_wire_shape();
        test_compact_fixed_rate_scenario();
        test_free_engine_request_wire_shape();
        check_golden_hashes(canonical_hashes, customized_hashes);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
