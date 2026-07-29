#pragma once

#include "contract_test_support.hpp"

#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_render_specification.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace engine_sim_offline::contract::test {

struct CanonicalManifestFixture {
    RenderSpecification specification;
    RenderScenario scenario;
    RenderManifestContent content;

    [[nodiscard]] const ProvenanceLedger &provenance() const noexcept {
        return specification.provenance;
    }

    [[nodiscard]] const SourceMatrixContract &source_matrix() const noexcept {
        return specification.source_matrix;
    }
};

inline CanonicalManifestFixture make_canonical_manifest_fixture() {
    auto request_result =
        profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    auto *request =
        std::get_if<profiles::BmwM52b28InertialDynoListeningRequest>(
            &request_result);
    if (request == nullptr) {
        throw std::runtime_error{
            "canonical BMW manifest request did not construct"};
    }

    auto scenario = request->scenario;
    auto specification = profiles::make_bmw_m52b28_render_specification(
        std::move(request->engine), std::move(request->provenance));

    InputBuilder generic_builder;
    auto content = make_manifest_content(generic_builder);
    content.inputs.resolved = {
        specification.engine,
        specification.presentation,
        specification.randomness,
        scenario,
    };
    content.provenance = specification.provenance.bundle;
    content.rates = scenario.rates;

    auto random_plan = compile_random_plan(specification.randomness,
                                           specification.engine, scenario);
    if (!std::holds_alternative<RandomPlan>(random_plan)) {
        throw std::runtime_error{
            "canonical BMW manifest random plan did not compile"};
    }
    content.randomness = std::get<RandomPlan>(std::move(random_plan));
    content.output_contract = resolve_output_contract(specification.source_matrix);

    content.routes.clear();
    for (const auto &engine_route : specification.engine.routes) {
        const auto requirement = std::ranges::find(
            specification.source_matrix.required_source_routes,
            engine_route.semantic_id.value,
            &SourceRouteRequirement::semantic_id);
        if (requirement ==
            specification.source_matrix.required_source_routes.end()) {
            throw std::runtime_error{
                "canonical BMW route is absent from its source matrix"};
        }
        content.routes.push_back({
            engine_route.id,
            requirement->semantic_id,
            requirement->kind,
            requirement->disposition,
            requirement->disposition_reason,
            requirement->artifact_roles,
        });
    }

    content.output_buses.clear();
    for (const auto &bus : specification.source_matrix.required_output_buses) {
        content.output_buses.push_back(
            {bus.semantic_id, bus.kind, bus.artifact_roles});
    }

    content.artifacts.clear();
    std::uint8_t digest_byte = 80;
    for (const auto &artifact : specification.source_matrix.required_artifacts) {
        content.artifacts.push_back({
            artifact.role,
            artifact.kind,
            "audio/" + artifact.role + ".wav",
            artifact.audio,
            1,
            digest(digest_byte++),
            artifact.diagnostic,
        });
    }

    return {
        std::move(specification),
        std::move(scenario),
        std::move(content),
    };
}

} // namespace engine_sim_offline::contract::test
