#include "contract_test_support.hpp"
#include "reference_manifest_test_support.hpp"

#include "engine_sim_offline/render.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string_view>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;

struct CountingSink final : RenderSink {
    std::size_t calls = 0;

    RenderSinkStatus begin_transaction(const OutputContract &) override {
        ++calls;
        return std::nullopt;
    }

    RenderSinkStatus declare_artifact(const PendingArtifact &) override {
        ++calls;
        return std::nullopt;
    }

    RenderSinkStatus write_artifact_chunk(const ArtifactChunk &) override {
        ++calls;
        return std::nullopt;
    }

    RenderSinkStatus seal_artifact(const ArtifactRecord &) override {
        ++calls;
        return std::nullopt;
    }

    RenderSinkStatus commit(const RenderManifest &) override {
        ++calls;
        return std::nullopt;
    }

    void abort() noexcept override {
        ++calls;
    }
};

struct RequestFixture {
    InputBuilder builder;
    RenderSpecification specification;
    RenderScenario scenario;

    RequestFixture() {
        auto content = make_manifest_content(builder);
        auto &resolved = simulation_inputs(content);
        specification.engine = std::move(resolved.engine);
        specification.presentation = std::move(resolved.presentation);
        specification.provenance = builder.provenance;
        specification.source_matrix = make_source_matrix();
        const std::vector asset_bytes{
            std::byte{0x52},
            std::byte{0x49},
            std::byte{0x46},
            std::byte{0x46},
        };
        const auto asset_digest = sha256(asset_bytes);
        specification.presentation.assets.front().content_sha256.value = asset_digest;
        for (auto &evidence : specification.provenance.evidence) {
            if (evidence.id ==
                specification.presentation.assets.front().evidence_source_id.value) {
                evidence.content_sha256 = asset_digest;
            }
        }
        specification.asset_payloads.push_back(
            {specification.presentation.assets.front().id, asset_bytes});
        scenario = std::move(resolved.scenario);
    }
};

const RenderFailure &expect_failure(const RenderResult &result,
                                    FailureKind expected_kind, const char *message) {
    const auto *failure = std::get_if<RenderFailure>(&result);
    expect(failure != nullptr && failure->context.kind == expected_kind, message);
    return *failure;
}

void expect_request_valid_failure(const RenderResult &result,
                                  const RequestFixture &fixture, const char *message) {
    expect(engine_sim_offline::validate(result, fixture.specification, fixture.scenario)
               .ok(),
           message);
}

bool has_issue_path(const ValidationReport &report, std::string_view path_fragment) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.path.find(path_fragment) != std::string::npos;
    });
}

void run_tests() {
    {
        RequestFixture fixture;
        CountingSink sink;
        const auto first = render(fixture.specification, fixture.scenario, sink);
        const auto &failure =
            expect_failure(first, FailureKind::incomplete_source_route,
                           "valid request did not fail at the unadmitted route");
        expect(failure.context.detail_code == "render-pipeline-not-admitted",
               "unadmitted route did not use its stable detail code");
        expect(failure.validation.ok(),
               "unadmitted route unexpectedly carried preflight diagnostics");
        expect(sink.calls == 0,
               "valid fail-closed request touched the sink transaction");
        expect_request_valid_failure(
            first, fixture,
            "valid fail-closed result violated the request-aware result contract");

        const auto second = render(fixture.specification, fixture.scenario, sink);
        const auto &second_failure =
            expect_failure(second, FailureKind::incomplete_source_route,
                           "repeated preflight changed failure kind");
        expect(failure.context == second_failure.context &&
                   failure.validation.issues == second_failure.validation.issues,
               "repeated preflight did not return deterministic diagnostics");
        auto different_specification = fixture.specification;
        different_specification.engine.display_name.value = "Different engine name";
        expect(!engine_sim_offline::validate(first, different_specification,
                                             fixture.scenario)
                    .ok(),
               "failure validated against a different resolved engine");
        expect(sink.calls == 0, "repeated preflight touched the sink");
    }

    {
        RequestFixture fixture;
        CountingSink sink;
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto result = render(fixture.specification, fixture.scenario, sink,
                                   RenderControl{cancellation.get_token()});
        const auto &failure =
            expect_failure(result, FailureKind::cancelled,
                           "pre-requested cancellation did not return its typed kind");
        expect(failure.context.detail_code == "render-cancelled-before-execution",
               "pre-execution cancellation used the wrong stable detail code");
        expect_request_valid_failure(
            result, fixture,
            "pre-execution cancellation violated the request-aware result contract");
        expect(sink.calls == 0, "pre-execution cancellation touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.engine.total_displacement_m3.value = -1.0;
        CountingSink sink;
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto result = render(fixture.specification, fixture.scenario, sink,
                                   RenderControl{cancellation.get_token()});
        expect_failure(result, FailureKind::invalid_specification,
                       "cancellation concealed an invalid render request");
        expect(sink.calls == 0, "cancelled invalid request touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.engine.total_displacement_m3.value = -1.0;
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        const auto &failure =
            expect_failure(result, FailureKind::invalid_specification,
                           "invalid engine did not fail structural preflight");
        expect(!failure.validation.ok(),
               "invalid engine failure lost its validation diagnostics");
        expect_request_valid_failure(
            result, fixture,
            "invalid engine did not produce a contract-valid typed failure");
        RequestFixture corrected;
        expect(!engine_sim_offline::validate(result, corrected.specification,
                                             corrected.scenario)
                    .ok(),
               "invalid-engine diagnostics validated against a corrected request");
        expect(sink.calls == 0, "invalid engine touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.scenario.engine_profile_id.clear();
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        const auto &failure =
            expect_failure(result, FailureKind::invalid_specification,
                           "malformed scenario profile did not fail preflight");
        expect(failure.context.profile_id == "unresolved",
               "malformed request did not use the canonical unresolved profile");
        expect_request_valid_failure(
            result, fixture,
            "malformed scenario profile made its typed failure invalid");
        expect(sink.calls == 0, "malformed scenario touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.scenario.engine_profile_id = "different-profile";
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "engine/scenario profile mismatch passed preflight");
        expect_request_valid_failure(
            result, fixture,
            "engine/scenario mismatch did not produce a valid typed failure");
        expect(sink.calls == 0, "profile mismatch touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.presentation.calibration_id.clear();
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "invalid presentation passed structural preflight");
        expect_request_valid_failure(
            result, fixture,
            "invalid presentation did not produce a valid typed failure");
        expect(sink.calls == 0, "invalid presentation touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.provenance.schema_id.clear();
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "invalid provenance passed structural preflight");
        expect_request_valid_failure(
            result, fixture,
            "invalid provenance did not produce a valid typed failure");
        expect(sink.calls == 0, "invalid provenance touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.source_matrix.required_artifacts.clear();
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "invalid source matrix passed structural preflight");
        expect_request_valid_failure(
            result, fixture,
            "invalid source matrix did not produce a valid typed failure");
        expect(sink.calls == 0, "invalid source matrix touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.source_matrix.required_source_routes.front().semantic_id =
            "exhaust.unrelated";
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "unrelated source-matrix route passed admission");
        expect(sink.calls == 0, "unrelated source matrix touched the sink");
    }

    {
        RequestFixture fixture;
        --fixture.specification.source_matrix.required_artifacts.front()
              .audio->frame_count;
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "source-matrix frame mismatch passed admission");
        expect(sink.calls == 0, "media-shape mismatch touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.asset_payloads.front().bytes.front() = std::byte{0x00};
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "asset payload digest mismatch passed preflight");
        auto different_invalid_payload = fixture.specification;
        different_invalid_payload.asset_payloads.front().bytes.front() =
            std::byte{0x01};
        expect(!engine_sim_offline::validate(result, different_invalid_payload,
                                             fixture.scenario)
                    .ok(),
               "failure rebound to different bytes with identical diagnostics");
        expect(sink.calls == 0, "invalid asset payload touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.source_matrix.id =
            bmw_m52b28_reference_source_matrix_v1().id;
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::invalid_specification,
                       "mutated frozen BMW matrix identity passed preflight");
        expect(sink.calls == 0, "mutated frozen matrix touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.provenance.evidence.front().rights =
            RightsDisposition::prohibited;
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        const auto &failure =
            expect_failure(result, FailureKind::evidence_rights_failure,
                           "prohibited evidence was not rejected as a rights failure");
        expect(!failure.validation.ok(),
               "rights failure lost its validation diagnostics");
        expect_request_valid_failure(
            result, fixture,
            "rights rejection did not produce a contract-valid typed failure");
        auto rights_permitted = fixture.specification;
        rights_permitted.provenance.evidence.front().rights =
            RightsDisposition::permitted;
        expect(!engine_sim_offline::validate(result, rights_permitted, fixture.scenario)
                    .ok(),
               "rights failure remained valid after its rejection condition was "
               "removed");
        expect(sink.calls == 0, "rights failure touched the sink");
    }

    {
        RequestFixture fixture;
        fixture.specification.source_matrix.distribution =
            DistributionIntent::distributable;
        fixture.specification.provenance.evidence.front().rights =
            RightsDisposition::noassertion;
        CountingSink sink;
        const auto result = render(fixture.specification, fixture.scenario, sink);
        expect_failure(result, FailureKind::evidence_rights_failure,
                       "distribution-incompatible evidence was not rejected as rights");
        expect_request_valid_failure(
            result, fixture,
            "distribution-rights rejection produced an invalid typed result");
        expect(sink.calls == 0, "distribution-rights failure touched the sink");
    }

    {
        RequestFixture request;
        ReferenceManifestFixture reference;
        const RenderResult success = RenderSuccess{
            RenderManifest{reference.content, std::nullopt},
            std::nullopt,
        };
        const auto report = engine_sim_offline::validate(success, request.specification,
                                                         request.scenario);
        expect(!report.ok() &&
                   has_issue_path(report, "success.manifest.content.inputs"),
               "public render-layer validation accepted a reference replay as "
               "RenderSuccess");
    }
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "render API test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
