#include "render/compiled_presentation_job.hpp"

#include "determinism/renderer_determinism_envelope.hpp"
#include "excitation/captured_exhaust_excitation.hpp"
#include "presentation/presentation_asset_compiler.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "render/compiled_presentation_job_impl.hpp"
#include "render/render_job_derivation.hpp"
#include "render/render_job_failure.hpp"
#include "render/render_request.hpp"
#include "simulation/legacy_low_order_simulation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::render_detail {
namespace {

[[nodiscard]] contract::RenderFailure
compiler_failure(contract::RenderRequestRecord request, contract::FailureKind kind,
                 std::string detail_code, std::string state_summary) {
    return make_job_failure(std::move(request), kind, std::move(detail_code),
                            "compiled-presentation-job-v1", std::move(state_summary));
}

[[nodiscard]] bool
kernel_matches(const presentation::CompiledPresentationConvolutionKernel &kernel,
               const presentation::CompiledPresentationAsset &asset,
               const contract::MethodIdentity &convolution_method) {
    const auto &key = kernel.key();
    return key.raw_payload_identity == asset.raw_payload_identity() &&
           key.conversion_method == asset.conversion_method() &&
           key.configured_gain_binary64_bits ==
               std::bit_cast<std::uint64_t>(asset.configured_gain().value) &&
           key.coefficient_f64le_identity == asset.coefficient_f64le_identity() &&
           key.convolution_method == convolution_method;
}

[[nodiscard]] std::optional<presentation::RouteConditioningSeeds>
route_seeds(const contract::RandomPlan &plan, contract::RouteId route_id) {
    const contract::ComponentSeed *jitter = nullptr;
    const contract::ComponentSeed *air_noise = nullptr;
    for (const auto &seed : plan.component_seeds) {
        if (seed.route_id != route_id) {
            continue;
        }
        if (seed.kind == contract::RandomComponentKind::presentation_jitter) {
            if (jitter != nullptr) {
                return std::nullopt;
            }
            jitter = &seed;
        } else if (seed.kind == contract::RandomComponentKind::presentation_air_noise) {
            if (air_noise != nullptr) {
                return std::nullopt;
            }
            air_noise = &seed;
        }
    }
    if (jitter == nullptr || air_noise == nullptr) {
        return std::nullopt;
    }
    return presentation::RouteConditioningSeeds{
        {jitter->initial_state, jitter->stream},
        {air_noise->initial_state, air_noise->stream},
    };
}

[[nodiscard]] const contract::AudioAssetSpec *
find_asset(const contract::PresentationCalibration &presentation,
           contract::AudioAssetId id) {
    const auto found =
        std::ranges::find(presentation.assets, id, &contract::AudioAssetSpec::id);
    return found == presentation.assets.end() ? nullptr : &*found;
}

[[nodiscard]] const RenderAssetPayload *
find_payload(const RenderSpecification &specification, contract::AudioAssetId id) {
    const auto found =
        std::ranges::find(specification.asset_payloads, id, &RenderAssetPayload::id);
    return found == specification.asset_payloads.end() ? nullptr : &*found;
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

    if (!std::holds_alternative<contract::LegacyLowOrderV1Profile>(
            inputs.engine.physics_profile)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "simulation-profile-not-admitted",
            "the resolved engine profile has no complete capture producer");
    }

    // Admit and retain the numeric identity before any IR conversion, FFT
    // construction, or simulation compilation can perform floating-point work.
    // Source/runtime identity remains below so a structurally unsupported pipeline
    // keeps its more specific failure precedence.
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

    auto calibration_result = presentation::compile_presentation_calibration(
        inputs.presentation, inputs.engine, inputs.scenario, request.provenance);
    if (std::holds_alternative<presentation::PresentationCalibrationCompileError>(
            calibration_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "render-pipeline-not-admitted",
            "the resolved presentation calibration is structurally valid but not "
            "supported by the complete executable route");
    }
    auto calibration = std::get<presentation::AdmittedPresentationCalibration>(
        std::move(calibration_result));

    auto random_result = contract::compile_random_plan(
        inputs.randomness, inputs.engine, inputs.presentation, inputs.scenario);
    if (std::holds_alternative<contract::ValidationReport>(random_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "compiled-random-plan-disagreed",
            "random-plan recompilation disagreed after successful render admission");
    }
    auto random_plan = std::get<contract::RandomPlan>(std::move(random_result));

    auto projection_result = derive_render_job_projection(request, calibration);
    if (std::holds_alternative<RenderJobDerivationError>(projection_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "presentation-output-contract-not-admitted",
            std::get<RenderJobDerivationError>(projection_result).message);
    }
    auto projection = std::get<RenderJobProjection>(std::move(projection_result));

    std::vector<presentation::CompiledPresentationAsset> compiled_assets;
    std::vector<presentation::CompiledPresentationConvolutionKernel> compiled_kernels;
    compiled_assets.reserve(calibration.route_count);
    compiled_kernels.reserve(calibration.route_count);
    std::array<std::shared_ptr<const dsp::FixedConvolutionKernel>,
               presentation::AdmittedPresentationCalibration::route_count>
        route_kernels;

    for (std::size_t route_index = 0; route_index < calibration.route_count;
         ++route_index) {
        const auto &route = calibration.routes()[route_index];
        const auto *asset =
            find_asset(inputs.presentation, route.impulse_response_asset_id());
        const auto *payload =
            find_payload(specification, route.impulse_response_asset_id());
        if (asset == nullptr || payload == nullptr) {
            return compiler_failure(
                std::move(request), contract::FailureKind::contract_violation,
                "presentation-asset-binding-lost",
                "an admitted presentation route lost its verified asset binding");
        }

        auto asset_result = presentation::compile_presentation_asset(
            *asset, {payload->id, payload->bytes},
            inputs.presentation.methods.impulse_response_conversion.value,
            route.impulse_response_gain_linear());
        if (std::holds_alternative<presentation::PresentationAssetCompileError>(
                asset_result)) {
            return compiler_failure(
                std::move(request), contract::FailureKind::incomplete_source_route,
                "presentation-asset-not-admitted",
                "a content-addressed presentation asset is not executable by the "
                "admitted static-IR method");
        }
        auto compiled_asset =
            std::get<presentation::CompiledPresentationAsset>(std::move(asset_result));

        const auto &convolution_method = inputs.presentation.methods.convolution.value;
        const auto existing =
            std::ranges::find_if(compiled_kernels, [&](const auto &kernel) {
                return kernel_matches(kernel, compiled_asset, convolution_method);
            });
        if (existing != compiled_kernels.end()) {
            route_kernels[route_index] = existing->kernel();
        } else {
            auto kernel_result = presentation::compile_presentation_convolution_kernel(
                compiled_asset, convolution_method);
            if (std::holds_alternative<
                    presentation::PresentationConvolutionKernelCompileError>(
                    kernel_result)) {
                return compiler_failure(
                    std::move(request), contract::FailureKind::incomplete_source_route,
                    "presentation-kernel-not-admitted",
                    "a verified presentation asset cannot produce the admitted "
                    "convolution kernel");
            }
            compiled_kernels.push_back(
                std::get<presentation::CompiledPresentationConvolutionKernel>(
                    std::move(kernel_result)));
            route_kernels[route_index] = compiled_kernels.back().kernel();
        }
        compiled_assets.push_back(std::move(compiled_asset));
    }

    std::array<presentation::PresentationRouteRenderPlan,
               presentation::AdmittedPresentationCalibration::route_count>
        route_plans;
    for (std::size_t route_index = 0; route_index < route_plans.size(); ++route_index) {
        const auto &route = calibration.routes()[route_index];
        const auto seeds = route_seeds(random_plan, route.route_id());
        if (!seeds.has_value()) {
            return compiler_failure(
                std::move(request), contract::FailureKind::contract_violation,
                "presentation-random-plan-incomplete",
                "the admitted random plan lacks one unique jitter and air-noise "
                "stream for a presentation route");
        }
        route_plans[route_index] = {
            route.route_id(),
            projection.routes[route_index].semantic_id,
            *seeds,
            route_kernels[route_index],
            route.wet_mix_01(),
            projection.route_artifacts[route_index],
        };
    }

    presentation::PresentationRenderPlan presentation_plan{
        projection.output_contract,
        {
            calibration.total_block_count(),
            calibration.pre_audible_block_count(),
            presentation::PresentationTailPolicy::truncate_at_timeline_end,
        },
        calibration.methods(),
        calibration.conditioning(),
        std::move(route_plans),
        calibration.publication_calibration_gain_linear().value,
        {
            calibration.audition_route_ids(),
            calibration.mastering(),
            projection.audition_metadata,
            projection.raw_master_artifact,
            projection.audition_master_artifact,
        },
    };

    auto simulation_result = simulation::compile_legacy_low_order_simulation_session(
        inputs.engine, inputs.scenario);
    if (std::holds_alternative<contract::ValidationReport>(simulation_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "simulation-profile-not-admitted",
            "the resolved engine and scenario are valid but unavailable to the "
            "complete simulation executor");
    }
    auto simulation = std::get<simulation::LegacyLowOrderSimulationSession>(
        std::move(simulation_result));

    const auto &legacy_profile =
        std::get<contract::LegacyLowOrderV1Profile>(inputs.engine.physics_profile);
    auto excitation_result = excitation::compile_captured_exhaust_excitation_session(
        inputs.engine, legacy_profile.core);
    if (std::holds_alternative<contract::ValidationReport>(excitation_result)) {
        return compiler_failure(
            std::move(request), contract::FailureKind::incomplete_source_route,
            "excitation-profile-not-admitted",
            "the resolved engine is valid but unavailable to the captured-exhaust "
            "executor");
    }
    auto excitation = std::get<excitation::CapturedExhaustExcitationSession>(
        std::move(excitation_result));

    // Compilation itself is inside the numeric identity boundary. A control change
    // during asset/kernel/session construction invalidates the job rather than
    // sealing values computed under two environments.
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
    manifest_basis.schema_version = 5;
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
            std::move(request), std::move(determinism), std::move(random_plan),
            std::move(calibration), std::move(compiled_assets),
            std::move(compiled_kernels), std::move(presentation_plan),
            std::move(manifest_basis), std::move(simulation), std::move(excitation))};
}

} // namespace engine_sim_offline::render_detail
