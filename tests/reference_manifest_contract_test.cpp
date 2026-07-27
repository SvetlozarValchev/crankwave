#include "reference_manifest_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::contract::test {
namespace {

bool has_issue(const ValidationReport &report, ContractIssueCode code,
               std::string_view path_fragment) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.code == code &&
               issue.path.find(path_fragment) != std::string::npos;
    });
}

void require_rejected(const ValidationReport &report, std::string_view mutation) {
    if (report.ok()) {
        throw std::runtime_error("reference manifest accepted mutation: " +
                                 std::string(mutation));
    }
}

void flip_digest(Sha256Digest &digest_value) {
    digest_value.bytes[0] ^= UINT8_C(0x80);
}

ValidationReport validate_reference(const ReferenceManifestFixture &fixture,
                                    const RenderManifestContent &content) {
    return validate(content, fixture.builder.provenance,
                    bmw_m52b28_reference_source_matrix_v1());
}

void test_exact_fixture_is_admitted() {
    ReferenceManifestFixture fixture;
    const auto &source_matrix = bmw_m52b28_reference_source_matrix_v1();

    expect(validate(reference_inputs(fixture.content), fixture.builder.provenance,
                    source_matrix)
               .ok(),
           "exact ReferencePresentationInputsV1 fixture was rejected");
    expect(validate_reference(fixture, fixture.content).ok(),
           "exact reference RenderManifestContent fixture was rejected");
    expect(fixture.content.artifacts.size() == 8,
           "reference manifest test fixture lost an artifact");
}

void test_schema_and_lineage_pins() {
    ReferenceManifestFixture fixture;

    auto mutated = fixture.content;
    mutated.schema_version = 1;
    require_rejected(validate_reference(fixture, mutated), "render-manifest schema v1");

    mutated = fixture.content;
    mutated.determinism.build.compiler_runtime_id.clear();
    require_rejected(validate_reference(fixture, mutated),
                     "missing compiler-runtime ID");

    mutated = fixture.content;
    mutated.determinism.build.compiler_runtime_identity.clear();
    require_rejected(validate_reference(fixture, mutated),
                     "missing compiler-runtime identity");

    mutated = fixture.content;
    mutated.determinism.build.git_commit_id = "not-a-git-object";
    require_rejected(validate_reference(fixture, mutated),
                     "malformed renderer Git commit ID");

    mutated = fixture.content;
    mutated.determinism.build.compiler_id = "gcc";
    require_rejected(validate_reference(fixture, mutated), "non-observer compiler ID");

    mutated = fixture.content;
    mutated.determinism.build.compiler_version = "13.3.";
    require_rejected(validate_reference(fixture, mutated),
                     "compiler version with trailing separator");

    mutated = fixture.content;
    mutated.determinism.build.compiler_runtime_identity = "test";
    require_rejected(validate_reference(fixture, mutated),
                     "malformed compiler-runtime identity");

    mutated = fixture.content;
    mutated.determinism.build.math_library_identity.insert(
        mutated.determinism.build.math_library_identity.find('+'), ".");
    require_rejected(validate_reference(fixture, mutated),
                     "glibc version with trailing separator");

    mutated = fixture.content;
    mutated.determinism.numeric_policy_id = "Not A Canonical Policy";
    require_rejected(validate_reference(fixture, mutated),
                     "noncanonical numeric-policy ID");

    mutated = fixture.content;
    mutated.determinism.build.standard_library_id = "libcxx";
    require_rejected(validate_reference(fixture, mutated),
                     "unsupported standard-library provider");

    mutated = fixture.content;
    mutated.determinism.build.math_library_id = "other-libm";
    require_rejected(validate_reference(fixture, mutated),
                     "unsupported math-library provider");

    mutated = fixture.content;
    mutated.determinism.build.compiler_runtime_id = "other-runtime";
    require_rejected(validate_reference(fixture, mutated),
                     "unsupported compiler-runtime provider");

    mutated = fixture.content;
    mutated.determinism.numeric_policy_id = "other-numeric-policy-v1";
    require_rejected(validate_reference(fixture, mutated),
                     "unsupported numeric-policy ID");

    mutated = fixture.content;
    mutated.determinism.instruction_set_profile = "x86-64-v2";
    require_rejected(validate_reference(fixture, mutated),
                     "numeric-policy ISA mismatch");

    mutated = fixture.content;
    mutated.determinism.deterministic_worker_count = 2;
    require_rejected(validate_reference(fixture, mutated),
                     "reference worker count greater than one");

    mutated = fixture.content;
    mutated.determinism.deterministic_reduction_topology = "pairwise-stable-v1";
    require_rejected(validate_reference(fixture, mutated),
                     "reference reduction topology mismatch");

    mutated = fixture.content;
    reference_inputs(mutated).schema_version = 2;
    require_rejected(validate_reference(fixture, mutated), "reference-input schema v2");

    mutated = fixture.content;
    reference_inputs(mutated).fixture.fixture_id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "fixture ID");

    mutated = fixture.content;
    ++reference_inputs(mutated).fixture.fixture_schema_version;
    require_rejected(validate_reference(fixture, mutated), "fixture schema");

    constexpr std::array payload_members{
        &ReferenceFixtureIdentityV1::manifest,
        &ReferenceFixtureIdentityV1::parity_evidence,
        &ReferenceFixtureIdentityV1::audit_input,
        &ReferenceFixtureIdentityV1::component_seed_input,
        &ReferenceFixtureIdentityV1::renderer_algorithm_record,
        &ReferenceFixtureIdentityV1::configured_ir_input,
        &ReferenceFixtureIdentityV1::kernel_oracle_comparator,
    };
    for (std::size_t index = 0; index < payload_members.size(); ++index) {
        mutated = fixture.content;
        auto &payload = reference_inputs(mutated).fixture.*payload_members[index];
        ++payload.byte_count;
        require_rejected(validate_reference(fixture, mutated),
                         "lineage payload byte count " + std::to_string(index));

        mutated = fixture.content;
        auto &hashed_payload =
            reference_inputs(mutated).fixture.*payload_members[index];
        flip_digest(hashed_payload.payload_sha256);
        require_rejected(validate_reference(fixture, mutated),
                         "lineage payload SHA-256 " + std::to_string(index));
    }

    mutated = fixture.content;
    reference_inputs(mutated).audit_reader.id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "audit reader ID");
    mutated = fixture.content;
    ++reference_inputs(mutated).audit_reader.version;
    require_rejected(validate_reference(fixture, mutated), "audit reader version");
    mutated = fixture.content;
    reference_inputs(mutated).audit_reader.configuration_sha256 = {};
    require_rejected(validate_reference(fixture, mutated),
                     "zero audit reader configuration digest");

    mutated = fixture.content;
    reference_inputs(mutated).excitation_adapter.id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "excitation adapter ID");
    mutated = fixture.content;
    ++reference_inputs(mutated).excitation_adapter.version;
    require_rejected(validate_reference(fixture, mutated),
                     "excitation adapter version");
    mutated = fixture.content;
    reference_inputs(mutated).excitation_adapter.configuration_sha256 = {};
    require_rejected(validate_reference(fixture, mutated),
                     "zero excitation adapter configuration digest");

    mutated = fixture.content;
    reference_inputs(mutated).audit_lane_semantic_id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "audit lane");

    mutated = fixture.content;
    reference_inputs(mutated).excitation_seam.id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "excitation seam ID");
    mutated = fixture.content;
    ++reference_inputs(mutated).excitation_seam.version;
    require_rejected(validate_reference(fixture, mutated), "excitation seam version");
    mutated = fixture.content;
    reference_inputs(mutated).excitation_seam.configuration_sha256 = {};
    require_rejected(validate_reference(fixture, mutated),
                     "zero excitation seam configuration digest");

    mutated = fixture.content;
    reference_inputs(mutated).engine.engine_id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "engine ID");
    mutated = fixture.content;
    reference_inputs(mutated).engine.engine_profile_id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "engine profile ID");
    mutated = fixture.content;
    reference_inputs(mutated).engine.routes.front().route_id = RouteId{3};
    require_rejected(validate_reference(fixture, mutated), "reference route ID");
    mutated = fixture.content;
    reference_inputs(mutated).engine.routes.front().semantic_id += "-changed";
    require_rejected(validate_reference(fixture, mutated),
                     "reference route semantic ID");
    mutated = fixture.content;
    reference_inputs(mutated).engine.routes.front().source_matrix_classification =
        SourceRouteKind::intake_inlet;
    require_rejected(validate_reference(fixture, mutated),
                     "reference route classification");
    mutated = fixture.content;
    std::ranges::swap(reference_inputs(mutated).engine.routes[0],
                      reference_inputs(mutated).engine.routes[1]);
    require_rejected(validate_reference(fixture, mutated), "reference route order");
}

void test_new_determinism_fields_are_content_identity() {
    ReferenceManifestFixture fixture;
    const RenderManifest baseline{fixture.content, std::nullopt};

    auto changed_runtime = baseline;
    changed_runtime.content.determinism.build.compiler_runtime_identity += "-changed";
    expect(!same_content_identity(baseline, changed_runtime),
           "compiler-runtime identity was excluded from content identity");

    auto changed_policy = baseline;
    changed_policy.content.determinism.numeric_policy_id += "-changed";
    expect(!same_content_identity(baseline, changed_policy),
           "numeric-policy ID was excluded from content identity");
}

void test_capture_window_pins_and_malformed_shapes() {
    ReferenceManifestFixture fixture;
    auto mutated = fixture.content;

    constexpr std::array rate_members{
        &RenderRates::physics,  &RenderRates::capture,  &RenderRates::source_processing,
        &RenderRates::acoustic, &RenderRates::delivery,
    };
    for (std::size_t index = 0; index < rate_members.size(); ++index) {
        mutated = fixture.content;
        ++(reference_inputs(mutated).capture.rates.*rate_members[index]).numerator;
        require_rejected(validate_reference(fixture, mutated),
                         "capture rate " + std::to_string(index));
    }

    constexpr std::array window_members{
        &ReferenceCaptureWindow::record_count,
        &ReferenceCaptureWindow::consumed_start_record,
        &ReferenceCaptureWindow::consumed_end_record_exclusive,
        &ReferenceCaptureWindow::audible_start_record,
        &ReferenceCaptureWindow::audible_end_record_exclusive,
        &ReferenceCaptureWindow::block_frames,
        &ReferenceCaptureWindow::total_source_frames,
        &ReferenceCaptureWindow::audible_source_start_frame,
        &ReferenceCaptureWindow::audible_source_end_frame_exclusive,
        &ReferenceCaptureWindow::delivery_frame_count,
        &ReferenceCaptureWindow::public_seed,
    };
    for (std::size_t index = 0; index < window_members.size(); ++index) {
        mutated = fixture.content;
        ++(reference_inputs(mutated).capture.*window_members[index]);
        require_rejected(validate_reference(fixture, mutated),
                         "capture window field " + std::to_string(index));
    }

    mutated = fixture.content;
    reference_inputs(mutated).presentation.assets.clear();
    require_rejected(validate_reference(fixture, mutated),
                     "empty reference presentation assets");

    mutated = fixture.content;
    reference_inputs(mutated).presentation.routes.clear();
    require_rejected(validate_reference(fixture, mutated),
                     "empty reference presentation routes");

    mutated = fixture.content;
    reference_inputs(mutated).capture.rates = {};
    mutated.rates = {};
    require_rejected(validate_reference(fixture, mutated),
                     "zero-rate reference capture");
}

void test_default_route_classification_is_rejected_safely() {
    ReferenceManifestFixture fixture;
    const auto &inputs = reference_inputs(fixture.content);

    PresentationValidationContext context;
    context.engine_profile_id = inputs.engine.engine_profile_id;
    context.rates = inputs.capture.rates;
    context.total_duration_s = 17.0;
    context.audible_start_s = 2.0;
    context.audible_duration_s = 15.0;
    context.routes.resize(2);
    context.routes[0].route_id = RouteId{1};
    context.routes[0].semantic_id = "exhaust.reference.0";
    context.routes[1].route_id = RouteId{2};
    context.routes[1].semantic_id = "exhaust.reference.1";

    expect(!validate(inputs.presentation, context, fixture.builder.provenance).ok(),
           "default route classification was accepted by presentation validation");
}

void test_random_stream_pins() {
    ReferenceManifestFixture fixture;
    auto mutated = fixture.content;

    mutated.randomness.generator.id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "random generator");
    mutated = fixture.content;
    mutated.randomness.derivation.id += "-changed";
    require_rejected(validate_reference(fixture, mutated), "seed derivation");
    mutated = fixture.content;
    ++mutated.randomness.public_seed;
    require_rejected(validate_reference(fixture, mutated), "public seed");

    mutated = fixture.content;
    ++mutated.randomness.component_seeds.front().initial_state;
    require_rejected(validate_reference(fixture, mutated), "component initial state");
    mutated = fixture.content;
    ++mutated.randomness.component_seeds.front().stream;
    require_rejected(validate_reference(fixture, mutated), "component stream selector");

    mutated = fixture.content;
    std::ranges::swap(mutated.randomness.component_seeds[0],
                      mutated.randomness.component_seeds[1]);
    require_rejected(validate_reference(fixture, mutated), "four-stream vector order");

    mutated = fixture.content;
    mutated.randomness.component_seeds.push_back({
        RandomComponentKind::combustion,
        CylinderId{1},
        std::nullopt,
        1,
        1,
    });
    require_rejected(validate_reference(fixture, mutated),
                     "inherited combustion stream");

    mutated = fixture.content;
    mutated.randomness.component_seeds.push_back({
        RandomComponentKind::starter,
        std::nullopt,
        RouteId{1},
        1,
        1,
    });
    require_rejected(validate_reference(fixture, mutated), "inherited starter stream");
}

void test_exact_artifact_set_pins() {
    ReferenceManifestFixture fixture;

    for (std::size_t index = 0; index < fixture.content.artifacts.size(); ++index) {
        auto mutated = fixture.content;
        mutated.artifacts[index].relative_path += ".changed";
        require_rejected(validate_reference(fixture, mutated),
                         "artifact path " + std::to_string(index));

        mutated = fixture.content;
        ++mutated.artifacts[index].byte_count;
        require_rejected(validate_reference(fixture, mutated),
                         "artifact byte count " + std::to_string(index));

        mutated = fixture.content;
        flip_digest(mutated.artifacts[index].payload_sha256);
        require_rejected(validate_reference(fixture, mutated),
                         "artifact SHA-256 " + std::to_string(index));
    }

    auto mutated = fixture.content;
    mutated.artifacts.pop_back();
    require_rejected(validate_reference(fixture, mutated),
                     "seven-artifact reference manifest");

    mutated = fixture.content;
    mutated.artifacts.push_back(mutated.artifacts.front());
    require_rejected(validate_reference(fixture, mutated), "extra reference artifact");

    mutated = fixture.content;
    std::ranges::swap(mutated.artifacts[0], mutated.artifacts[1]);
    require_rejected(validate_reference(fixture, mutated), "reference artifact order");
}

void test_public_contract_rejects_reference_success() {
    ReferenceManifestFixture fixture;
    InputBuilder request_builder;
    const auto request_content = make_manifest_content(request_builder);
    const auto success = RenderResult{RenderSuccess{
        RenderManifest{fixture.content, std::nullopt},
        std::nullopt,
    }};

    const auto report =
        validate(success, simulation_inputs(request_content).scenario,
                 fixture.builder.provenance, bmw_m52b28_reference_source_matrix_v1());
    expect(!report.ok() && has_issue(report, ContractIssueCode::unsupported_value,
                                     "success.manifest.content.inputs"),
           "public RenderResult contract accepted reference manifest inputs");
}

} // namespace

void run_reference_manifest_contract_tests() {
    test_exact_fixture_is_admitted();
    test_schema_and_lineage_pins();
    test_new_determinism_fields_are_content_identity();
    test_capture_window_pins_and_malformed_shapes();
    test_default_route_classification_is_rejected_safely();
    test_random_stream_pins();
    test_exact_artifact_set_pins();
    test_public_contract_rejects_reference_success();
}

} // namespace engine_sim_offline::contract::test
