#include "session/session_build.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "crankwave/request_identity.hpp"
#include "presentation/presentation_asset_compiler.hpp"

#include <bit>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace crankwave::session_detail {
namespace {

[[nodiscard]] EngineSessionError
build_error(EngineSessionErrorCode code, std::string detail_code, std::string message) {
    return {code, std::move(detail_code), std::move(message), std::nullopt};
}

[[nodiscard]] std::string with_first_issue(std::string message,
                                           const contract::ValidationReport &report) {
    if (!report.issues.empty()) {
        message += " (";
        message += report.issues.front().path;
        message += ": ";
        message += report.issues.front().message;
        message += ')';
    }
    return message;
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

[[nodiscard]] const compile::detail::VerifiedEngineAsset *
find_payload(const compile::detail::ResolvedEnginePackage &engine,
             const contract::AudioAssetSpec &asset) {
    const auto found = std::ranges::find_if(engine.assets, [&](const auto &candidate) {
        return candidate.kind == compile::AssetKind::audio &&
               candidate.asset_id == asset.semantic_id.value;
    });
    return found == engine.assets.end() ? nullptr : &*found;
}

} // namespace

SessionBuildResult
build_session_components(const compile::CompiledScenario &compiled_scenario,
                         const EngineSessionExecutionKind execution_kind,
                         const std::span<const std::string_view>
                             projected_dry_bus_ids) {
    const auto inputs =
        compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario);
    const auto &engine_package = inputs.engine;
    const auto &scenario_contracts = inputs.scenario;
    const auto &engine = engine_package.engine;
    const auto &presentation_contract = engine_package.presentation;
    const auto &scenario = scenario_contracts.scenario;

    const bool supports_open_ended_execution =
        std::holds_alternative<contract::FreeEngine>(scenario.mode) ||
        std::holds_alternative<contract::HeldDyno>(scenario.mode) ||
        std::holds_alternative<contract::FreeVehicle>(scenario.mode);
    if (execution_kind == EngineSessionExecutionKind::open_ended &&
        !supports_open_ended_execution) {
        return build_error(
            EngineSessionErrorCode::unsupported_configuration,
            "open-session-requires-dynamic-bench-mode",
            "open-ended execution is admitted only for FreeEngine, HeldDyno, or "
            "FreeVehicle scenarios");
    }

    auto random_result = contract::compile_random_plan(
        engine_package.randomness, engine, presentation_contract, scenario);
    if (std::holds_alternative<contract::ValidationReport>(random_result)) {
        return build_error(
            EngineSessionErrorCode::invalid_compiled_scenario,
            "session-random-plan-disagreed",
            "the compiled scenario no longer produces an admitted random plan");
    }
    auto random_plan = std::get<contract::RandomPlan>(std::move(random_result));
    if (random_plan != scenario_contracts.random_plan) {
        return build_error(
            EngineSessionErrorCode::invalid_compiled_scenario,
            "session-random-plan-identity-disagreed",
            "the immutable scenario random plan differs from deterministic "
            "recompilation");
    }

    auto identity_result = identity::encode_simulation_request_identity_v7(
        engine, scenario, random_plan, scenario_contracts.combined_provenance.bundle);
    if (const auto *error =
            std::get_if<identity::SimulationRequestIdentityError>(&identity_result)) {
        return build_error(EngineSessionErrorCode::invalid_compiled_scenario,
                           error->detail_code, error->message);
    }
    const auto request_identity = std::get<identity::SimulationRequestIdentityEncoding>(
                                      std::move(identity_result))
                                      .sha256;

    auto calibration_result = presentation::compile_presentation_calibration(
        presentation_contract, engine, scenario,
        scenario_contracts.combined_provenance);
    if (std::holds_alternative<presentation::PresentationCalibrationCompileError>(
            calibration_result)) {
        return build_error(
            EngineSessionErrorCode::unsupported_configuration,
            "session-presentation-not-admitted",
            "the compiled scenario is unavailable to the executable presentation "
            "method");
    }
    auto calibration = std::get<presentation::AdmittedPresentationCalibration>(
        std::move(calibration_result));

    std::vector<std::string> dry_projection;
    dry_projection.reserve(projected_dry_bus_ids.size());
    if (!projected_dry_bus_ids.empty() &&
        execution_kind != EngineSessionExecutionKind::finite_scenario) {
        return build_error(
            EngineSessionErrorCode::unsupported_configuration,
            "session-dry-projection-requires-finite-execution",
            "source-route dry projection is admitted only for a finite session");
    }
    for (std::size_t selected_index = 0U;
         selected_index < projected_dry_bus_ids.size(); ++selected_index) {
        const auto selected_id = projected_dry_bus_ids[selected_index];
        if (selected_id.empty() ||
            std::ranges::find(projected_dry_bus_ids.begin(),
                              projected_dry_bus_ids.begin() +
                                  static_cast<std::ptrdiff_t>(selected_index),
                              selected_id) !=
                projected_dry_bus_ids.begin() +
                    static_cast<std::ptrdiff_t>(selected_index)) {
            return build_error(
                EngineSessionErrorCode::unsupported_configuration,
                "session-dry-projection-selection-invalid",
                "projected dry-bus identities must be nonempty and unique");
        }

        std::size_t dry_match_count = 0U;
        for (const auto &route : calibration.routes()) {
            const auto engine_route = std::ranges::find(
                engine.routes, route.route_id(), &contract::RouteSpec::id);
            if (engine_route == engine.routes.end()) {
                return build_error(
                    EngineSessionErrorCode::invalid_compiled_scenario,
                    "session-dry-projection-route-binding-lost",
                    "a calibrated presentation route is absent from the engine");
            }
            const auto requirement = std::ranges::find(
                scenario_contracts.source_matrix.required_source_routes,
                engine_route->semantic_id.value,
                &contract::SourceRouteRequirement::semantic_id);
            if (requirement ==
                    scenario_contracts.source_matrix.required_source_routes.end() ||
                requirement->kind != engine_route->kind.value ||
                requirement->artifact_roles.size() != 3U) {
                return build_error(
                    EngineSessionErrorCode::invalid_compiled_scenario,
                    "session-dry-projection-source-policy-lost",
                    "a calibrated presentation route lacks its three public signal "
                    "roles");
            }
            dry_match_count += requirement->artifact_roles.front() == selected_id
                                   ? 1U
                                   : 0U;
        }
        if (dry_match_count != 1U) {
            return build_error(
                EngineSessionErrorCode::unsupported_configuration,
                "session-dry-projection-requires-source-route-dry-buses",
                "every projected bus must identify exactly one source-route dry "
                "signal");
        }
        dry_projection.emplace_back(selected_id);
    }

    std::vector<presentation::CompiledPresentationAsset> compiled_assets;
    std::vector<presentation::CompiledPresentationConvolutionKernel> compiled_kernels;
    std::vector<dsp::RuntimeConvolutionKernel> route_kernels(
        calibration.route_count());
    if (dry_projection.empty()) {
        compiled_assets.reserve(calibration.route_count());
        compiled_kernels.reserve(calibration.route_count());
    }

    for (std::size_t route_index = 0;
         dry_projection.empty() && route_index < calibration.route_count();
         ++route_index) {
        const auto &route = calibration.routes()[route_index];
        if (!route.impulse_response_asset_id().has_value()) {
            return build_error(
                EngineSessionErrorCode::invalid_compiled_scenario,
                "session-presentation-route-transfer-missing",
                "an active exhaust presentation route lacks its transfer asset");
        }
        const auto *asset =
            find_asset(presentation_contract, *route.impulse_response_asset_id());
        if (asset == nullptr) {
            return build_error(EngineSessionErrorCode::invalid_compiled_scenario,
                               "session-presentation-asset-binding-lost",
                               "a presentation route references an absent asset");
        }
        const auto *payload = find_payload(engine_package, *asset);
        if (payload == nullptr) {
            return build_error(EngineSessionErrorCode::invalid_compiled_scenario,
                               "session-presentation-payload-binding-lost",
                               "a presentation asset has no verified payload");
        }

        auto asset_result = presentation::compile_presentation_asset(
            *asset, {asset->id, payload->bytes},
            presentation_contract.methods.impulse_response_conversion.value,
            route.impulse_response_gain_linear());
        if (std::holds_alternative<presentation::PresentationAssetCompileError>(
                asset_result)) {
            return build_error(
                EngineSessionErrorCode::unsupported_configuration,
                "session-presentation-asset-not-admitted",
                "a verified presentation asset is unavailable to the static-IR "
                "method");
        }
        auto compiled_asset =
            std::get<presentation::CompiledPresentationAsset>(std::move(asset_result));

        const auto &convolution_method =
            presentation_contract.methods.convolution.value;
        const auto existing =
            std::ranges::find_if(compiled_kernels, [&](const auto &kernel) {
                return kernel_matches(kernel, compiled_asset, convolution_method);
            });
        if (existing != compiled_kernels.end()) {
            route_kernels[route_index] = existing->runtime_kernel();
        } else {
            auto kernel_result = presentation::compile_presentation_convolution_kernel(
                compiled_asset, convolution_method);
            if (std::holds_alternative<
                    presentation::PresentationConvolutionKernelCompileError>(
                    kernel_result)) {
                return build_error(
                    EngineSessionErrorCode::unsupported_configuration,
                    "session-presentation-kernel-not-admitted",
                    "a verified presentation asset cannot produce the configured "
                    "convolution kernel");
            }
            compiled_kernels.push_back(
                std::get<presentation::CompiledPresentationConvolutionKernel>(
                    std::move(kernel_result)));
            route_kernels[route_index] =
                compiled_kernels.back().runtime_kernel();
        }
        compiled_assets.push_back(std::move(compiled_asset));
    }

    presentation::PresentationAudioPlan audio_plan;
    audio_plan.processing_projection =
        dry_projection.empty()
            ? presentation::PresentationAudioProcessingProjection::complete
            : presentation::PresentationAudioProcessingProjection::
                  source_route_dry_only;
    audio_plan.conditioning = calibration.conditioning();
    audio_plan.excitation_rate = calibration.capture_rate();
    audio_plan.excitation_frames_per_block =
        static_cast<std::size_t>(calibration.capture_frames_per_block());
    audio_plan.publication_calibration_gain_linear =
        calibration.publication_calibration_gain_linear().value;
    audio_plan.audition_volume_linear = calibration.mastering().volume_linear();
    if (dry_projection.empty()) {
        audio_plan.audition_route_ids.reserve(
            calibration.audition_route_ids().size());
        for (const auto selected_id : calibration.audition_route_ids()) {
            audio_plan.audition_route_ids.push_back(selected_id);
        }
    }
    audio_plan.routes.reserve(calibration.route_count());
    for (std::size_t route_index = 0; route_index < calibration.route_count();
         ++route_index) {
        const auto &route = calibration.routes()[route_index];
        const auto seeds = route_seeds(random_plan, route.route_id());
        if (!seeds.has_value()) {
            return build_error(
                EngineSessionErrorCode::invalid_compiled_scenario,
                "session-presentation-random-plan-incomplete",
                "a presentation route lacks unique jitter and air-noise streams");
        }
        audio_plan.routes.push_back({
            route.route_id(),
            route.source_route_kind(),
            seeds,
            std::nullopt,
            route_kernels[route_index],
            route.wet_mix_01(),
        });
    }

    const auto execution_extent =
        execution_kind == EngineSessionExecutionKind::open_ended
            ? simulation::LowOrderExecutionExtent::open_ended()
            : simulation::LowOrderExecutionExtent::finite_scenario(
                  calibration.total_block_count() *
                  calibration.capture_frames_per_block());
    auto simulation_result = simulation::compile_low_order_capture_session(
        engine, scenario, random_plan, request_identity, execution_extent);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&simulation_result)) {
        return build_error(
            EngineSessionErrorCode::unsupported_configuration,
            "session-simulation-profile-not-admitted",
            with_first_issue(
                "the compiled engine and scenario are unavailable to the simulation "
                "executor",
                *report));
    }
    auto simulation =
        std::get<simulation::LowOrderCaptureSession>(std::move(simulation_result));

    const auto *core = std::visit([](const auto &profile) { return &profile.core; },
                                  engine.physics_profile);
    auto excitation_result =
        excitation::compile_captured_source_excitation_session(engine, *core, scenario);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&excitation_result)) {
        return build_error(
            EngineSessionErrorCode::unsupported_configuration,
            "session-excitation-profile-not-admitted",
            with_first_issue(
                "the compiled engine is unavailable to the source excitation "
                "executor",
                *report));
    }
    auto excitation = std::get<excitation::CapturedSourceExcitationSession>(
        std::move(excitation_result));

    const auto excitation_route_ids = excitation.exhaust_route_ids();
    const auto reference_mass_flow =
        excitation.exhaust_valve_reference_mass_flow_kg_s();
    if (excitation_route_ids.size() != reference_mass_flow.size()) {
        return build_error(
            EngineSessionErrorCode::invalid_compiled_scenario,
            "session-exhaust-flow-reference-shape-disagreed",
            "the source excitation route and exhaust-valve reference-flow shapes "
            "differ");
    }
    std::size_t exhaust_route_index = 0U;
    for (auto &route : audio_plan.routes) {
        if (route.source_route_kind != contract::SourceRouteKind::exhaust_outlet) {
            continue;
        }
        if (exhaust_route_index >= excitation_route_ids.size() ||
            route.route_id != excitation_route_ids[exhaust_route_index]) {
            return build_error(
                EngineSessionErrorCode::invalid_compiled_scenario,
                "session-exhaust-flow-reference-route-disagreed",
                "presentation and excitation exhaust-route orders differ");
        }
        route.exhaust_valve_reference_mass_flow_kg_s =
            reference_mass_flow[exhaust_route_index];
        ++exhaust_route_index;
    }
    if (exhaust_route_index != excitation_route_ids.size()) {
        return build_error(EngineSessionErrorCode::invalid_compiled_scenario,
                           "session-exhaust-flow-reference-count-disagreed",
                           "presentation and excitation exhaust-route counts differ");
    }

    auto presentation =
        std::make_unique<presentation::PresentationAudioSession>(std::move(audio_plan));
    return BuiltSessionComponents{
        compiled_scenario,      execution_kind,          request_identity,
        std::move(random_plan), std::move(calibration),  std::move(simulation),
        std::move(excitation),  std::move(presentation), std::move(dry_projection),
    };
}

} // namespace crankwave::session_detail
