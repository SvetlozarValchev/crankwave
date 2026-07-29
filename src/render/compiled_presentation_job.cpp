#include "render/compiled_presentation_job.hpp"

#include "acoustics/exhaust_acoustic_session.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "presentation/mastering.hpp"
#include "presentation/presentation_method_registry.hpp"
#include "presentation/presentation_render_session.hpp"
#include "render/compiled_presentation_job_impl.hpp"
#include "render/render_job_derivation.hpp"
#include "render/render_job_failure.hpp"
#include "render/render_request.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

namespace engine_sim_offline::render_detail {
namespace {

constexpr std::uint32_t kExhaustSourceIntervalsPerCaptureFrame = 8U;

[[nodiscard]] contract::RenderFailure
compiler_failure(contract::RenderRequestRecord request, contract::FailureKind kind,
                 std::string detail_code, std::string state_summary) {
    return make_job_failure(std::move(request), kind, std::move(detail_code),
                            "compiled-presentation-job-v1", std::move(state_summary));
}

[[nodiscard]] const contract::RouteSpec *
find_engine_route(const contract::EngineSpec &engine,
                  contract::RouteId route_id) noexcept {
    const auto found =
        std::ranges::find(engine.routes, route_id, &contract::RouteSpec::id);
    return found == engine.routes.end() ? nullptr : &*found;
}

} // namespace

CompiledPresentationJob::CompiledPresentationJob(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}

CompiledPresentationJob::~CompiledPresentationJob() = default;

CompiledPresentationJob::CompiledPresentationJob(CompiledPresentationJob &&) noexcept =
    default;

CompiledPresentationJobResult
compile_presentation_job(const RenderSpecification &specification,
                         const contract::RenderScenario &scenario) {
    auto request = make_render_request_record(specification, scenario);
    const auto &inputs = request.resolved_inputs;

    auto request_identity_result = identity::encode_simulation_request_identity_v3(
        inputs.engine, inputs.scenario, request.provenance.bundle);
    if (const auto *error = std::get_if<identity::SimulationRequestIdentityError>(
            &request_identity_result)) {
        return compiler_failure(std::move(request),
                                contract::FailureKind::contract_violation,
                                error->detail_code, error->message);
    }
    const auto simulation_request_identity_v3_sha256 =
        std::get<identity::SimulationRequestIdentityEncoding>(
            std::move(request_identity_result))
            .sha256;

    // Admit and retain the numeric identity before simulation or acoustic session
    // construction can perform floating-point work.
    const auto numeric_before_compilation = determinism::renderer_numeric_environment();
    const auto *admitted_numeric = std::get_if<determinism::RendererNumericEnvironment>(
        &numeric_before_compilation);
    if (admitted_numeric == nullptr) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "renderer-numeric-environment-not-admitted",
            "the calling thread cannot compile a presentation job under the "
            "required renderer numeric environment");
    }
    const auto compiled_numeric_identity = *admitted_numeric;

    const auto presentation_method_report =
        presentation::admit_implemented_presentation_methods(
            inputs.presentation.methods);
    if (!presentation_method_report.ok()) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "physical-presentation-methods-not-admitted",
            "the resolved presentation selects a method identity other than the "
            "exact implemented physical-pressure publication path");
    }

    const auto *operating_profile =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &inputs.engine.physics_profile);
    if (operating_profile == nullptr ||
        operating_profile->exhaust_acoustics.outlets.size() !=
            presentation::kPresentationExhaustOutletCount) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "exhaust-acoustic-profile-not-admitted",
            "the production renderer requires one operating profile with exactly "
            "two physical exhaust outlets");
    }
    const auto capture_block_capacity =
        inputs.scenario.quality.value.capture_block_capacity_frames;
    if (capture_block_capacity == 0U ||
        capture_block_capacity >
            dsp::SixChannelCausalResampler::maximum_input_frames_per_call /
                kExhaustSourceIntervalsPerCaptureFrame) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "exhaust-acoustic-block-capacity-not-admitted",
            "the capture block capacity exceeds the bounded physical exhaust "
            "reconstruction call");
    }

    std::array<contract::RouteId, presentation::kPresentationExhaustOutletCount>
        outlet_route_ids{};
    for (std::size_t index = 0; index < outlet_route_ids.size(); ++index) {
        outlet_route_ids[index] =
            operating_profile->exhaust_acoustics.outlets[index].route_id;
    }
    if (!outlet_route_ids[0].valid() ||
        outlet_route_ids[0].value >= outlet_route_ids[1].value) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "exhaust-acoustic-outlet-order-not-admitted",
            "the physical pressure publication method requires two outlets in "
            "strictly ascending stable route-ID order");
    }

    auto random_result = contract::compile_random_plan(
        inputs.randomness, inputs.engine, inputs.scenario);
    if (std::holds_alternative<contract::ValidationReport>(random_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "compiled-random-plan-disagreed",
            "random-plan recompilation disagreed after successful render admission");
    }
    auto random_plan = std::get<contract::RandomPlan>(std::move(random_result));

    auto projection_result = derive_render_job_projection(request, outlet_route_ids);
    if (std::holds_alternative<RenderJobDerivationError>(projection_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "presentation-output-contract-not-admitted",
            std::get<RenderJobDerivationError>(projection_result).message);
    }
    auto projection = std::get<RenderJobProjection>(std::move(projection_result));

    const auto total_acoustic_frame_count = contract::resolve_frame_index(
        inputs.scenario.total_duration_s.value, inputs.scenario.rates.acoustic);
    const auto pre_audible_frame_count = contract::resolve_frame_index(
        inputs.scenario.audible_start_s.value, inputs.scenario.rates.acoustic);
    const auto audible_frame_count = contract::resolve_frame_index(
        inputs.scenario.audible_duration_s.value, inputs.scenario.rates.acoustic);
    const auto fade_in_frame_count = contract::resolve_frame_index(
        inputs.presentation.monitoring.fade_in_duration_s.value,
        inputs.scenario.rates.acoustic);
    const auto fade_out_frame_count = contract::resolve_frame_index(
        inputs.presentation.monitoring.fade_out_duration_s.value,
        inputs.scenario.rates.acoustic);
    const auto capture_frame_count = contract::resolve_frame_index(
        inputs.scenario.total_duration_s.value, inputs.scenario.rates.capture);
    const auto source_interval_count = contract::resolve_frame_index(
        inputs.scenario.total_duration_s.value,
        operating_profile->exhaust_acoustics.source_interval_rate.value);
    if (!total_acoustic_frame_count.has_value() ||
        !pre_audible_frame_count.has_value() || !audible_frame_count.has_value() ||
        !fade_in_frame_count.has_value() || !fade_out_frame_count.has_value() ||
        !capture_frame_count.has_value() || !source_interval_count.has_value() ||
        *pre_audible_frame_count > *total_acoustic_frame_count ||
        *audible_frame_count !=
            *total_acoustic_frame_count - *pre_audible_frame_count) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "physical-presentation-timeline-not-admitted",
            "the resolved scenario does not form one exact capture/acoustic "
            "presentation timeline");
    }

    std::optional<presentation::MasteringSettings> mastering;
    std::optional<acoustics::ExhaustAcousticSession> acoustic_session;
    try {
        mastering.emplace(
            *audible_frame_count, *fade_in_frame_count, *fade_out_frame_count,
            static_cast<float>(
                inputs.presentation.monitoring.gain_linear.value));
        acoustic_session.emplace(operating_profile->exhaust_acoustics,
                                 acoustics::ExhaustAcousticEnvironment{
                                     inputs.scenario.ambient.pressure_pa_abs.value,
                                     inputs.scenario.ambient.temperature_k.value,
                                 });
    } catch (const std::exception &error) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "exhaust-acoustic-session-not-admitted",
            "the resolved physical exhaust or mastering session is not executable: " +
                std::string{error.what()});
    }

    std::array<presentation::PresentationOutletRenderPlan,
               presentation::kPresentationExhaustOutletCount>
        outlet_plans{};
    for (std::size_t index = 0; index < outlet_plans.size(); ++index) {
        const auto *route = find_engine_route(inputs.engine, outlet_route_ids[index]);
        if (route == nullptr) {
            return compiler_failure(
                std::move(request), contract::FailureKind::contract_violation,
                "exhaust-outlet-route-binding-lost",
                "an admitted acoustic outlet lost its engine route binding");
        }
        outlet_plans[index] = {
            outlet_route_ids[index],
            route->semantic_id.value,
            std::move(projection.outlet_pressure_artifacts[index]),
        };
    }

    presentation::PresentationRenderPlan presentation_plan{
        projection.output_contract,
        {
            *total_acoustic_frame_count,
            *pre_audible_frame_count,
            presentation::PresentationTailPolicy::truncate_at_timeline_end,
        },
        std::move(outlet_plans),
        acoustic_session->pa_per_full_scale(),
        {
            std::move(*mastering),
            projection.audition_metadata,
            projection.raw_master_artifact,
            projection.audition_master_artifact,
        },
    };

    auto simulation_result = simulation::compile_low_order_capture_session(
        inputs.engine, inputs.scenario, simulation_request_identity_v3_sha256);
    if (std::holds_alternative<contract::ValidationReport>(simulation_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "simulation-profile-not-admitted",
            "the resolved engine and scenario are valid but unavailable to the "
            "complete simulation executor");
    }
    auto simulation =
        std::get<simulation::LowOrderCaptureSession>(std::move(simulation_result));

    // Compilation itself is inside the numeric identity boundary. A control change
    // during simulation/acoustic/session construction invalidates the job.
    const auto numeric_after_compilation = determinism::renderer_numeric_environment();
    const auto *observed_numeric = std::get_if<determinism::RendererNumericEnvironment>(
        &numeric_after_compilation);
    if (observed_numeric == nullptr || *observed_numeric != compiled_numeric_identity) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "renderer-numeric-environment-changed-during-compilation",
            "the renderer numeric environment changed after identity admission and "
            "before the opaque job was sealed");
    }

    auto determinism_result = determinism::renderer_determinism_envelope();
    if (!std::holds_alternative<determinism::RendererDeterminismEnvelope>(
            determinism_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "renderer-identity-not-admitted",
            "the current build, runtime providers, or numeric environment cannot "
            "publish a deterministic render identity");
    }
    auto determinism = std::get<determinism::RendererDeterminismEnvelope>(
        std::move(determinism_result));
    if (!determinism.production_observation()) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "renderer-identity-not-production",
            "a non-production renderer observation cannot authorize publication");
    }
    if (determinism.numeric_environment() != compiled_numeric_identity) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "renderer-numeric-identity-disagreed",
            "the renderer identity disagreed with the numeric environment retained "
            "across presentation-job compilation");
    }

    contract::RenderManifestContent manifest_basis;
    manifest_basis.schema_version = 6;
    manifest_basis.inputs = contract::SimulationManifestInputs{inputs};
    manifest_basis.provenance = request.provenance.bundle;
    manifest_basis.determinism = determinism.manifest_identity();
    manifest_basis.rates = inputs.scenario.rates;
    manifest_basis.randomness = random_plan;
    manifest_basis.output_contract = projection.output_contract;
    manifest_basis.routes = std::move(projection.routes);
    manifest_basis.output_buses = std::move(projection.output_buses);

    return CompiledPresentationJob{
        std::make_unique<CompiledPresentationJob::Implementation>(
            std::move(request), simulation_request_identity_v3_sha256,
            std::move(determinism), std::move(presentation_plan),
            std::move(manifest_basis), std::move(simulation),
            std::move(*acoustic_session), *capture_frame_count, *source_interval_count,
            *total_acoustic_frame_count, *pre_audible_frame_count)};
}

} // namespace engine_sim_offline::render_detail
