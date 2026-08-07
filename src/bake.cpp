#include "engine_sim_offline/bake.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "determinism/renderer_numeric_environment.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "engine_sim_offline/session.hpp"
#include "presentation/presentation_calibration_compiler.hpp"
#include "render/native_bake_plan.hpp"
#include "render/native_presentation_publisher.hpp"
#include "render/render_job_failure.hpp"
#include "render/render_request.hpp"

#include <exception>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace engine_sim_offline {
namespace {

using contract::FailureKind;
using contract::RenderFailure;
using contract::RenderRequestRecord;
using contract::RenderResult;

class NumericControlRecovery final {
  public:
    NumericControlRecovery() = default;
    NumericControlRecovery(const NumericControlRecovery &) = delete;
    NumericControlRecovery &operator=(const NumericControlRecovery &) = delete;

    ~NumericControlRecovery() noexcept {
        determinism::detail::restore_admitted_renderer_numeric_controls();
    }
};

[[nodiscard]] double scenario_time(std::uint64_t physics_frame,
                                   contract::RationalRateHz physics_rate) noexcept {
    if (physics_rate.numerator == 0U || physics_rate.denominator == 0U) {
        return 0.0;
    }
    return static_cast<double>(physics_frame) *
           static_cast<double>(physics_rate.denominator) /
           static_cast<double>(physics_rate.numerator);
}

[[nodiscard]] RenderFailure failure(RenderRequestRecord request, FailureKind kind,
                                    std::string detail_code, std::string state_summary,
                                    std::uint64_t physics_frame = 0) {
    const auto physics_rate = request.resolved_inputs.scenario.rates.physics;
    return render_detail::make_job_failure(
        std::move(request), kind, std::move(detail_code), "native-engine-bake-v1",
        std::move(state_summary), physics_frame, physics_frame,
        scenario_time(physics_frame, physics_rate));
}

[[nodiscard]] RenderFailure preflight_failure(RenderRequestRecord request,
                                              FailureKind kind, std::string detail_code,
                                              std::string state_summary,
                                              contract::ValidationReport validation) {
    auto result = failure(std::move(request), kind, std::move(detail_code),
                          std::move(state_summary));
    result.validation = std::move(validation);
    return result;
}

[[nodiscard]] RenderFailure session_failure(RenderRequestRecord request,
                                            const EngineSessionError &error,
                                            std::uint64_t physics_frame) {
    if (error.simulation_failure.has_value()) {
        return {
            *error.simulation_failure,
            std::move(request),
            {},
        };
    }
    const auto kind = error.code == EngineSessionErrorCode::unsupported_configuration
                          ? FailureKind::incomplete_source_route
                          : FailureKind::contract_violation;
    return failure(std::move(request), kind, error.detail_code, error.message,
                   physics_frame);
}

[[nodiscard]] RenderFailure exception_failure(RenderRequestRecord request,
                                              std::string_view stage,
                                              std::exception_ptr exception,
                                              std::uint64_t physics_frame) {
    try {
        std::rethrow_exception(std::move(exception));
    } catch (const render_detail::NativePresentationSinkFailure &error) {
        const auto &sink = error.sink_error();
        const auto kind = sink.kind == RenderSinkErrorKind::publication_failure
                              ? FailureKind::artifact_publication_failure
                              : FailureKind::contract_violation;
        const auto detail_code =
            contract::is_valid_semantic_id(sink.detail_code)
                ? sink.detail_code
                : std::string{sink.kind == RenderSinkErrorKind::publication_failure
                                  ? "bake-sink-publication-failed"
                                  : "bake-sink-protocol-violated"};
        return failure(std::move(request), kind, detail_code,
                       std::string(stage) + " failed: " + sink.message, physics_frame);
    } catch (const std::bad_alloc &) {
        return failure(std::move(request), FailureKind::contract_violation,
                       "bake-resource-exhausted",
                       std::string(stage) + " exhausted process memory", physics_frame);
    } catch (const std::domain_error &error) {
        return failure(std::move(request), FailureKind::numerical_failure,
                       "bake-numerical-failure",
                       std::string(stage) + " failed: " + error.what(), physics_frame);
    } catch (const std::exception &error) {
        return failure(std::move(request), FailureKind::contract_violation,
                       "bake-execution-failed",
                       std::string(stage) + " failed: " + error.what(), physics_frame);
    } catch (...) {
        return failure(std::move(request), FailureKind::contract_violation,
                       "bake-execution-failed",
                       std::string(stage) + " failed with a non-standard exception",
                       physics_frame);
    }
}

[[nodiscard]] std::optional<RenderFailure>
check_numeric_environment(const determinism::RendererNumericEnvironment &expected,
                          const RenderRequestRecord &request, std::string_view boundary,
                          std::uint64_t physics_frame) {
    const auto observed = determinism::renderer_numeric_environment();
    const auto *environment =
        std::get_if<determinism::RendererNumericEnvironment>(&observed);
    if (environment != nullptr && *environment == expected) {
        return std::nullopt;
    }
    return failure(request, FailureKind::contract_violation,
                   "renderer-numeric-environment-changed",
                   "the renderer numeric environment changed at " +
                       std::string(boundary),
                   physics_frame);
}

void append_prefixed(contract::ValidationReport &destination,
                     contract::ValidationReport source, std::string_view prefix) {
    for (auto &issue : source.issues) {
        issue.path = issue.path.empty() ? std::string(prefix)
                                        : std::string(prefix) + "." + issue.path;
        destination.issues.push_back(std::move(issue));
    }
}

} // namespace

contract::RenderResult bake(const compile::CompiledScenario &compiled_scenario,
                            RenderSink &sink, RenderControl control) {
    NumericControlRecovery recover_numeric_controls_at_exit;
    const auto inputs =
        compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario);
    auto request =
        render_detail::make_render_request_record(inputs.engine, inputs.scenario);

    auto structural = contract::validate_render_admission(
        inputs.engine.engine, inputs.engine.presentation, inputs.engine.randomness,
        inputs.scenario.scenario, inputs.scenario.combined_provenance,
        inputs.scenario.source_matrix);
    if (!structural.ok()) {
        return preflight_failure(
            std::move(request), FailureKind::invalid_specification,
            "bake-preflight-invalid",
            "compiled scenario failed native bake structural preflight",
            std::move(structural));
    }
    auto rights =
        contract::validate_evidence_rights(inputs.scenario.combined_provenance,
                                           inputs.scenario.source_matrix.distribution);
    if (!rights.ok()) {
        return preflight_failure(
            std::move(request), FailureKind::evidence_rights_failure,
            "bake-evidence-not-admitted",
            "compiled scenario evidence rights do not admit publication",
            std::move(rights));
    }
    if (control.stop_token.stop_requested()) {
        return failure(std::move(request), FailureKind::cancelled,
                       "bake-cancelled-before-execution",
                       "cancellation was observed before session creation");
    }

    const auto numeric_before = determinism::renderer_numeric_environment();
    const auto *admitted_numeric =
        std::get_if<determinism::RendererNumericEnvironment>(&numeric_before);
    if (admitted_numeric == nullptr) {
        return failure(
            std::move(request), FailureKind::contract_violation,
            "renderer-numeric-environment-not-admitted",
            "the calling thread does not satisfy the renderer numeric contract");
    }
    const auto numeric_identity = *admitted_numeric;

    auto random_result = contract::compile_random_plan(
        inputs.engine.randomness, inputs.engine.engine, inputs.engine.presentation,
        inputs.scenario.scenario);
    if (const auto *report = std::get_if<contract::ValidationReport>(&random_result)) {
        return preflight_failure(std::move(request), FailureKind::contract_violation,
                                 "bake-random-plan-disagreed",
                                 "compiled scenario random-plan recompilation failed",
                                 *report);
    }
    auto random_plan = std::get<contract::RandomPlan>(std::move(random_result));
    if (random_plan != inputs.scenario.random_plan) {
        return failure(std::move(request), FailureKind::contract_violation,
                       "bake-random-plan-identity-disagreed",
                       "compiled scenario random plan changed before session creation");
    }

    auto calibration_result = presentation::compile_presentation_calibration(
        inputs.engine.presentation, inputs.engine.engine, inputs.scenario.scenario,
        inputs.scenario.combined_provenance);
    if (std::holds_alternative<presentation::PresentationCalibrationCompileError>(
            calibration_result)) {
        return failure(std::move(request), FailureKind::incomplete_source_route,
                       "bake-presentation-not-admitted",
                       "compiled scenario is unavailable to the presentation method");
    }
    auto calibration = std::get<presentation::AdmittedPresentationCalibration>(
        std::move(calibration_result));

    auto session_result = create_engine_session(
        compiled_scenario, EngineSessionExecutionKind::finite_scenario);
    if (const auto *error = std::get_if<EngineSessionError>(&session_result)) {
        return session_failure(std::move(request), *error, 0);
    }
    auto session = std::get<EngineSession>(std::move(session_result));

    if (const auto numeric_failure = check_numeric_environment(
            numeric_identity, request, "session creation", 0)) {
        return *numeric_failure;
    }
    auto determinism_result = determinism::renderer_determinism_envelope();
    const auto *determinism_envelope =
        std::get_if<determinism::RendererDeterminismEnvelope>(&determinism_result);
    if (determinism_envelope == nullptr ||
        !determinism_envelope->production_observation() ||
        determinism_envelope->numeric_environment() != numeric_identity) {
        return failure(
            std::move(request), FailureKind::contract_violation,
            "renderer-identity-not-admitted",
            "the current build or runtime cannot publish a deterministic bake "
            "identity");
    }

    auto plan_result = render_detail::derive_native_bake_plan(
        inputs, random_plan, calibration, *determinism_envelope);
    if (const auto *error =
            std::get_if<render_detail::NativeBakePlanError>(&plan_result)) {
        return failure(std::move(request), FailureKind::incomplete_source_route,
                       error->detail_code, error->message);
    }
    auto plan = std::get<render_detail::NativeBakePlan>(std::move(plan_result));

    const auto descriptor = session.descriptor();
    std::uint64_t physics_frame = 0;
    try {
        render_detail::NativePresentationPublisher publisher{
            sink, descriptor, std::move(plan.publication), control};

        EngineSessionCompleted completion;
        while (true) {
            if (control.stop_token.stop_requested()) {
                return failure(
                    std::move(plan.request), FailureKind::cancelled,
                    "bake-cancelled-between-blocks",
                    "cancellation was observed between complete session blocks",
                    physics_frame);
            }

            auto processed = session.process_block();
            if (const auto *block = std::get_if<EngineSessionBlockView>(&processed)) {
                publisher.process(*block);
                physics_frame =
                    block->first_physics_frame() + block->physics_frame_count();
                if (const auto numeric_failure =
                        check_numeric_environment(numeric_identity, plan.request,
                                                  "session block", physics_frame)) {
                    return *numeric_failure;
                }
                continue;
            }
            if (const auto *error = std::get_if<EngineSessionError>(&processed)) {
                return session_failure(std::move(plan.request), *error, physics_frame);
            }
            completion = std::get<EngineSessionCompleted>(std::move(processed));
            break;
        }

        if (control.stop_token.stop_requested()) {
            return failure(std::move(plan.request), FailureKind::cancelled,
                           "bake-cancelled-before-finalization",
                           "cancellation was observed before publication "
                           "finalization",
                           physics_frame);
        }

        auto evidence = publisher.finish();
        const auto &stats = evidence.stats();
        const auto expected_physics_frames =
            descriptor.total_block_count *
            static_cast<std::uint64_t>(descriptor.physics_frames_per_block);
        const auto expected_delivery_frames =
            descriptor.total_block_count *
            static_cast<std::uint64_t>(descriptor.delivery_frames_per_block);
        const auto expected_preparation_frames =
            descriptor.preparation_block_count *
            static_cast<std::uint64_t>(descriptor.delivery_frames_per_block);
        if (completion.block_count != descriptor.total_block_count ||
            completion.physics_frame_count != expected_physics_frames ||
            completion.delivery_frame_count != expected_delivery_frames ||
            stats.processed_block_count != descriptor.total_block_count ||
            stats.pre_audible_block_count != descriptor.preparation_block_count ||
            stats.input_frame_count != expected_physics_frames ||
            stats.processed_source_frame_count != expected_delivery_frames ||
            stats.pre_audible_source_frame_count != expected_preparation_frames) {
            return failure(std::move(plan.request), FailureKind::contract_violation,
                           "bake-completion-count-disagreed",
                           "session and native publisher completed different horizons",
                           physics_frame);
        }
        if (stats.audition_saturated_sample_count != 0U) {
            return failure(std::move(plan.request), FailureKind::contract_violation,
                           "native-audition-saturated",
                           "audition PCM24 quantization saturated " +
                               std::to_string(stats.audition_saturated_sample_count) +
                               " samples; successful publication requires zero",
                           physics_frame);
        }

        plan.manifest_basis.artifacts.assign(evidence.artifacts().begin(),
                                             evidence.artifacts().end());
        contract::RenderManifest manifest{
            std::move(plan.manifest_basis),
            evidence.execution().facts(),
        };
        RenderResult result{contract::RenderSuccess{
            std::move(manifest),
            std::nullopt,
            std::move(completion.held_speed_operating_point),
            std::move(completion.inertial_dyno),
        }};

        const auto result_report = validate_bake_result(result, compiled_scenario);
        if (!result_report.ok()) {
            std::string message =
                "complete request-bound bake result failed validation";
            if (!result_report.issues.empty()) {
                message += ": ";
                message += result_report.issues.front().path;
                message += ": ";
                message += result_report.issues.front().message;
            }
            return failure(std::move(plan.request), FailureKind::contract_violation,
                           "bake-result-validation-failed-before-commit",
                           std::move(message), physics_frame);
        }

        if (control.stop_token.stop_requested()) {
            return failure(std::move(plan.request), FailureKind::cancelled,
                           "bake-cancelled-before-commit",
                           "cancellation was observed before atomic publication",
                           physics_frame);
        }

        publisher.commit(evidence, std::get<contract::RenderSuccess>(result).manifest,
                         inputs.scenario.combined_provenance,
                         inputs.scenario.source_matrix);
        return result;
    } catch (const render_detail::NativePresentationCancellation &) {
        determinism::detail::restore_admitted_renderer_numeric_controls();
        return failure(std::move(plan.request), FailureKind::cancelled,
                       "bake-cancelled-during-publication",
                       "cancellation was observed during native publication",
                       physics_frame);
    } catch (...) {
        determinism::detail::restore_admitted_renderer_numeric_controls();
        return exception_failure(std::move(plan.request), "native bake",
                                 std::current_exception(), physics_frame);
    }
}

contract::ValidationReport
validate_bake_result(const contract::RenderResult &result,
                     const compile::CompiledScenario &compiled_scenario) {
    const auto inputs =
        compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario);
    const auto expected_request =
        render_detail::make_render_request_record(inputs.engine, inputs.scenario);

    contract::ValidationReport report;
    contract::Sha256Digest request_identity;
    auto identity_result = identity::encode_simulation_request_identity_v7(
        inputs.engine.engine, inputs.scenario.scenario, inputs.scenario.random_plan,
        inputs.scenario.combined_provenance.bundle);
    if (const auto *encoded = std::get_if<identity::SimulationRequestIdentityEncoding>(
            &identity_result)) {
        request_identity = encoded->sha256;
    } else {
        const auto &error =
            std::get<identity::SimulationRequestIdentityError>(identity_result);
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "simulation_request_identity_v7",
                   error.detail_code + ": " + error.message);
    }

    append_prefixed(report,
                    contract::validate(result, inputs.scenario.scenario,
                                       request_identity,
                                       inputs.scenario.combined_provenance,
                                       inputs.scenario.source_matrix),
                    "contract");

    std::visit(
        [&](const auto &outcome) {
            using Outcome = std::decay_t<decltype(outcome)>;
            if constexpr (std::is_same_v<Outcome, contract::RenderSuccess>) {
                if (outcome.manifest.content.inputs.resolved !=
                    expected_request.resolved_inputs) {
                    report.add(
                        contract::ContractIssueCode::inconsistent_semantics,
                        "success.manifest.content.inputs",
                        "bake manifest differs from the immutable compiled request");
                }
            } else {
                if (outcome.request != expected_request) {
                    report.add(
                        contract::ContractIssueCode::inconsistent_semantics,
                        "result.request",
                        "terminal bake result differs from the immutable compiled "
                        "request");
                }
            }
        },
        result);
    return report;
}

} // namespace engine_sim_offline
