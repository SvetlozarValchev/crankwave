#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"

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

constexpr std::string_view kExpectedManifestSha256 =
    "8fb97dc325c9922cb26712bda6093652608649663e2f027e4f2b930352dc625b";
constexpr std::string_view kExpectedRequestIdentitySha256 =
    "b63c7200d10b4a63be22991b2aa99048f063053a882a1a4cc68fe4ae95c5e3ad";

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
        "simulation-manifest-encoder-test-v4",
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
    auto result = encode_simulation_manifest_v4(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<ManifestEncoding>(result).bytes);
}

[[nodiscard]] SimulationRequestIdentityEncoding
require_request_identity_encoding(const EngineSpec &engine,
                                  const RenderScenario &scenario,
                                  const ProvenanceBundleRef &provenance) {
    auto result = encode_simulation_request_identity_v1(engine, scenario, provenance);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<SimulationRequestIdentityEncoding>(result));
}

void expect_manifest_error(const RenderManifest &manifest,
                           std::string_view detail_code) {
    const auto result = encode_simulation_manifest_v4(manifest);
    const auto *error = std::get_if<RenderSinkError>(&result);
    expect(error != nullptr, "invalid simulation manifest unexpectedly encoded");
    expect(error->kind == RenderSinkErrorKind::protocol_violation,
           "invalid simulation manifest returned the wrong error kind");
    expect(error->detail_code == detail_code,
           "invalid simulation manifest returned the wrong detail code");
}

void expect_request_identity_error(const EngineSpec &engine,
                                   const RenderScenario &scenario,
                                   const ProvenanceBundleRef &provenance,
                                   std::string_view detail_code) {
    const auto result =
        encode_simulation_request_identity_v1(engine, scenario, provenance);
    const auto *error = std::get_if<RenderSinkError>(&result);
    expect(error != nullptr,
           "invalid simulation request identity unexpectedly encoded");
    expect(error->kind == RenderSinkErrorKind::protocol_violation,
           "invalid simulation request identity returned the wrong error kind");
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
        "{\"wire_schema\":\"engine-sim-offline.render-manifest.simulation.v4\","
        "\"content\":{\"schema_version\":4,\"inputs\":{\"kind\":\"simulation_v3\","
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
           "retired presentation algorithm record leaked into manifest v4");

    const auto &resolved = simulation_inputs(fixture.manifest.content);
    const auto first_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.provenance);
    const auto second_identity = require_request_identity_encoding(
        resolved.engine, resolved.scenario, fixture.manifest.content.provenance);
    expect(first_identity == second_identity,
           "identical simulation requests produced different identity encodings");
    expect(first_identity.sha256 == sha256(first_identity.bytes),
           "request identity reported a digest for different bytes");

    const auto identity_document = as_string(first_identity.bytes);
    constexpr std::string_view kIdentityPrefix =
        "{\"wire_schema\":\"engine-sim-offline.simulation-request-identity.v1\","
        "\"engine\":";
    expect(identity_document.starts_with(kIdentityPrefix),
           "request identity root or member order changed");
    expect(identity_document.ends_with("}}\n"),
           "request identity canonical suffix changed");
    expect(std::count(identity_document.begin(), identity_document.end(), '\n') == 1,
           "request identity contains non-terminal whitespace");
    const auto identity_engine_key = identity_document.find("\"engine\":");
    const auto identity_scenario_key = identity_document.find("\"scenario\":");
    const auto identity_provenance_key = identity_document.find("\"provenance\":");
    expect(identity_engine_key < identity_scenario_key &&
               identity_scenario_key < identity_provenance_key &&
               identity_document.find("\"presentation\":") == std::string::npos,
           "request identity member order or excluded presentation changed");

    return {
        digest_hex(sha256(first_manifest)),
        digest_hex(first_identity.sha256),
    };
}

void test_fail_closed_boundaries() {
    SimulationFixture fixture;

    for (const auto schema_version : {UINT32_C(3), UINT32_C(5)}) {
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

void set_compact_fixed_rate_sweep(SimulationFixture &fixture) {
    auto &scenario = simulation_inputs(fixture.manifest.content).scenario;
    const auto throttle_resolution_id =
        std::get<HeldSpeed>(scenario.mode).throttle_01.resolution_id;
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
        fixture.builder.add_resolution("scenario.mode.trajectory.rpm"),
    };
    fixed_rpm.samples_f64le_sha256 =
        canonical_binary64_le_sha256(fixed_rpm.post_step_rpm);

    RpmTrajectory trajectory{
        std::move(fixed_rpm),
        fixture.builder.resolved(0.0, "scenario.mode.trajectory.initial_theta_rad"),
        fixture.builder.resolved(method("fixed-rate-post-step-rpm-binary64-v1", 61),
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
    require_valid(
        validate(fixture.manifest, fixture.builder.provenance, fixture.source_matrix),
        "compact fixed-rate fixture is not a valid completed manifest");

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
                                  stale_digest.content.provenance,
                                  "simulation-request-identity-wire-unrepresentable");

    auto nonfinite_lane = fixture.manifest;
    fixed_rpm_trajectory(nonfinite_lane).post_step_rpm[1] =
        std::numeric_limits<double>::infinity();
    expect_manifest_error(nonfinite_lane, "simulation-manifest-wire-nonfinite");
    const auto &nonfinite_inputs = simulation_inputs(nonfinite_lane.content);
    expect_request_identity_error(nonfinite_inputs.engine, nonfinite_inputs.scenario,
                                  nonfinite_lane.content.provenance,
                                  "simulation-request-identity-wire-nonfinite");
}

void check_golden_hashes(const GoldenHashes &actual) {
    bool mismatch = false;
    if (actual.manifest != kExpectedManifestSha256) {
        std::cerr << "simulation manifest golden SHA-256: " << actual.manifest << '\n';
        mismatch = true;
    }
    if (actual.request_identity != kExpectedRequestIdentitySha256) {
        std::cerr << "simulation request identity golden SHA-256: "
                  << actual.request_identity << '\n';
        mismatch = true;
    }
    expect(!mismatch, "canonical simulation encoder golden hashes are not pinned");
}

} // namespace

int main() {
    try {
        const auto hashes = test_deterministic_roots();
        test_fail_closed_boundaries();
        test_compact_fixed_rate_scenario();
        check_golden_hashes(hashes);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
