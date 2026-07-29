#include "canonical_manifest_test_support.hpp"

#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include <chrono>
#include <cstddef>
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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] ExecutionFacts deterministic_execution() {
    return {
        "simulation-manifest-encoder-test",
        "2026-07-29T08:00:00Z",
        std::chrono::nanoseconds{UINT64_C(1234567890)},
        "linux",
        "test-cpu",
        16,
        1,
        1,
        UINT64_C(123456),
    };
}

[[nodiscard]] std::vector<std::byte>
require_manifest_encoding(const RenderManifest &manifest) {
    auto result = encode_simulation_manifest_v6(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<ManifestEncoding>(result).bytes);
}

void expect_manifest_rejected(const RenderManifest &manifest,
                              std::string_view message) {
    const auto result = encode_simulation_manifest_v6(manifest);
    const auto *error = std::get_if<RenderSinkError>(&result);
    expect(error != nullptr &&
               error->kind == RenderSinkErrorKind::protocol_violation,
           message);
}

[[nodiscard]] std::string text(const std::vector<std::byte> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void test_canonical_wire_is_deterministic_and_current() {
    auto fixture = make_canonical_manifest_fixture();
    RenderManifest manifest{fixture.content, deterministic_execution()};

    const auto report =
        validate(manifest, fixture.provenance(), fixture.source_matrix());
    if (!report.ok()) {
        for (const auto &issue : report.issues) {
            std::cerr << issue.path << ": " << issue.message << '\n';
        }
        throw std::runtime_error{"canonical manifest fixture is invalid"};
    }

    const auto first = require_manifest_encoding(manifest);
    const auto second = require_manifest_encoding(manifest);
    expect(first == second, "identical manifests produced different wire bytes");

    const auto document = text(first);
    expect(document.starts_with(
               "{\"wire_schema\":\"engine-sim-offline.render-manifest.simulation.v6\""),
           "simulation manifest root identity changed");
    expect(document.find("\"schema_version\":1") != std::string::npos &&
               document.find("\"calibrated_pressure_publication\"") !=
                   std::string::npos &&
               document.find("\"coherent_two_outlet_audition\"") !=
                   std::string::npos &&
               document.find("\"monitoring\"") != std::string::npos,
           "current two-method presentation contract is absent from the wire");
    expect(document.find("\"combustion_seeds\"") != std::string::npos,
           "typed combustion seed inventory is absent from the wire");
    expect(document.find("\"conditioning\"") == std::string::npos &&
               document.find("\"assets\"") == std::string::npos &&
               document.find("master_reference") == std::string::npos &&
               document.find("p18") == std::string::npos,
           "retired presentation or reference vocabulary leaked into the wire");

    const auto &resolved = manifest.content.inputs.resolved;
    const auto first_identity = encode_simulation_request_identity_v3(
        resolved.engine, resolved.scenario, manifest.content.provenance);
    const auto second_identity = encode_simulation_request_identity_v3(
        resolved.engine, resolved.scenario, manifest.content.provenance);
    expect(std::holds_alternative<SimulationRequestIdentityEncoding>(first_identity) &&
               first_identity == second_identity,
           "canonical request identity is not deterministic");
}

void test_invalid_current_wire_is_rejected() {
    auto fixture = make_canonical_manifest_fixture();
    RenderManifest manifest{fixture.content, deterministic_execution()};

    auto obsolete_presentation = manifest;
    obsolete_presentation.content.inputs.resolved.presentation.schema_version = 2;
    expect_manifest_rejected(obsolete_presentation,
                             "obsolete presentation schema encoded");

    auto nonfinite_monitoring = manifest;
    nonfinite_monitoring.content.inputs.resolved.presentation.monitoring.gain_linear
        .value = std::numeric_limits<double>::infinity();
    expect_manifest_rejected(nonfinite_monitoring,
                             "nonfinite monitoring gain encoded");

}

void run_tests() {
    test_canonical_wire_is_deterministic_and_current();
    test_invalid_current_wire_is_rejected();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "simulation manifest encoder test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
