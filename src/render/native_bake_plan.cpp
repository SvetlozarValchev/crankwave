#include "render/native_bake_plan.hpp"

#include "render/render_job_derivation.hpp"
#include "render/render_request.hpp"

#include "crankwave/request_identity.hpp"

#include <utility>
#include <vector>

namespace crankwave::render_detail {

NativeBakePlanResult derive_native_bake_plan(
    const compile::detail::CompiledScenarioInputsView inputs,
    const contract::RandomPlan &random_plan,
    const presentation::AdmittedPresentationCalibration &calibration,
    const determinism::RendererDeterminismEnvelope &determinism) {
    auto request = make_render_request_record(inputs.engine, inputs.scenario);
    auto projection_result = derive_render_job_projection(request, calibration);
    if (const auto *error = std::get_if<RenderJobDerivationError>(&projection_result)) {
        return NativeBakePlanError{
            "native-bake-publication-plan-not-admitted",
            error->path + ": " + error->message,
        };
    }
    auto projection = std::get<RenderJobProjection>(std::move(projection_result));

    auto request_identity_result = identity::encode_simulation_request_identity_v7(
        request.resolved_inputs.engine, request.resolved_inputs.scenario, random_plan,
        request.provenance.bundle);
    if (const auto *error = std::get_if<identity::SimulationRequestIdentityError>(
            &request_identity_result)) {
        return NativeBakePlanError{
            error->detail_code,
            "native bake telemetry request identity could not be encoded: " +
                error->message,
        };
    }
    const auto request_identity = std::get<identity::SimulationRequestIdentityEncoding>(
        std::move(request_identity_result));

    std::vector<NativePresentationRoutePublicationPlan> routes;
    routes.reserve(calibration.route_count());
    for (std::size_t route = 0; route < calibration.route_count(); ++route) {
        routes.push_back({
            calibration.routes()[route].route_id(),
            projection.routes[route].semantic_id,
            std::move(projection.route_artifacts[route]),
        });
    }

    std::vector<contract::RouteId> audition_route_ids;
    audition_route_ids.reserve(calibration.audition_route_ids().size());
    for (const auto selected_id : calibration.audition_route_ids()) {
        audition_route_ids.push_back(selected_id);
    }
    const auto &mastering = calibration.mastering();
    NativePresentationPublicationPlan publication{
        projection.output_contract,
        {
            calibration.total_block_count(),
            calibration.pre_audible_block_count(),
            NativePresentationTailPolicy::truncate_at_timeline_end,
        },
        request_identity.sha256,
        calibration.methods(),
        std::move(projection.telemetry_artifact),
        std::move(routes),
        {
            std::move(audition_route_ids),
            {
                mastering.audible_frame_count(),
                mastering.fade_in_frame_count(),
                mastering.fade_out_frame_count(),
            },
            std::move(projection.audition_metadata),
            std::move(projection.raw_master_artifact),
            std::move(projection.audition_master_artifact),
        },
    };

    contract::RenderManifestContent manifest_basis;
    manifest_basis.schema_version = 10;
    manifest_basis.inputs = contract::SimulationManifestInputs{request.resolved_inputs};
    manifest_basis.provenance = request.provenance.bundle;
    manifest_basis.determinism = determinism.manifest_identity();
    manifest_basis.rates = inputs.scenario.scenario.rates;
    manifest_basis.randomness = random_plan;
    manifest_basis.output_contract = projection.output_contract;
    manifest_basis.routes = std::move(projection.routes);
    manifest_basis.output_buses = std::move(projection.output_buses);

    return NativeBakePlan{
        std::move(request),
        std::move(publication),
        std::move(manifest_basis),
    };
}

} // namespace crankwave::render_detail
