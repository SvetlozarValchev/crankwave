#include "render/compiled_presentation_job_impl.hpp"

#include "render/render_job_failure.hpp"

#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace engine_sim_offline::render_detail {
namespace {

enum class ExecutionStage : std::uint8_t {
    presentation_transaction,
    simulation,
    excitation,
    presentation,
    finalization,
    manifest_completion,
    commit,
};

struct DeferredException {
    ExecutionStage stage = ExecutionStage::simulation;
    std::exception_ptr exception;
};

struct CoordinatorFailure {
    contract::FailureKind kind = contract::FailureKind::contract_violation;
    std::string_view detail_code;
    std::string_view state_summary;
    std::uint64_t sample_index = 0;
};

struct NumericEnvironmentFailure {
    std::string_view boundary;
    std::optional<determinism::RendererNumericEnvironmentError> error;
    std::uint64_t sample_index = 0;
};

using FirstCauseValue =
    std::variant<std::monostate, contract::FailureContext, DeferredException,
                 CoordinatorFailure, NumericEnvironmentFailure>;

class FirstCause final {
  public:
    [[nodiscard]] bool present() const noexcept {
        return !std::holds_alternative<std::monostate>(value_);
    }

    void record(contract::FailureContext failure) {
        if (!present()) {
            value_.emplace<contract::FailureContext>(std::move(failure));
        }
    }

    void record(ExecutionStage stage, std::exception_ptr exception) noexcept {
        if (!present()) {
            value_.emplace<DeferredException>(stage, std::move(exception));
        }
    }

    void record(CoordinatorFailure failure) noexcept {
        if (!present()) {
            value_.emplace<CoordinatorFailure>(failure);
        }
    }

    void record(NumericEnvironmentFailure failure) noexcept {
        if (!present()) {
            value_.emplace<NumericEnvironmentFailure>(failure);
        }
    }

    [[nodiscard]] FirstCauseValue take() noexcept {
        return std::move(value_);
    }

  private:
    FirstCauseValue value_;
};

class NumericControlRecovery final {
  public:
    NumericControlRecovery() = default;
    NumericControlRecovery(const NumericControlRecovery &) = delete;
    NumericControlRecovery &operator=(const NumericControlRecovery &) = delete;

    ~NumericControlRecovery() noexcept {
        determinism::detail::restore_admitted_renderer_numeric_controls();
    }
};

[[nodiscard]] double scenario_time(std::uint64_t sample_index) noexcept {
    return static_cast<double>(sample_index) / 10000.0;
}

[[nodiscard]] std::string_view stage_name(ExecutionStage stage) noexcept {
    switch (stage) {
    case ExecutionStage::presentation_transaction:
        return "presentation transaction";
    case ExecutionStage::simulation:
        return "simulation";
    case ExecutionStage::excitation:
        return "excitation";
    case ExecutionStage::presentation:
        return "presentation";
    case ExecutionStage::finalization:
        return "presentation finalization";
    case ExecutionStage::manifest_completion:
        return "manifest completion";
    case ExecutionStage::commit:
        return "publication commit";
    }
    return "render execution";
}

[[nodiscard]] std::string_view exception_detail_code(ExecutionStage stage) noexcept {
    switch (stage) {
    case ExecutionStage::presentation_transaction:
        return "presentation-transaction-failed";
    case ExecutionStage::simulation:
        return "simulation-execution-threw";
    case ExecutionStage::excitation:
        return "excitation-execution-threw";
    case ExecutionStage::presentation:
        return "presentation-execution-threw";
    case ExecutionStage::finalization:
        return "presentation-finalization-failed";
    case ExecutionStage::manifest_completion:
        return "simulation-manifest-completion-failed";
    case ExecutionStage::commit:
        return "presentation-commit-failed";
    }
    return "render-execution-threw";
}

[[nodiscard]] std::string_view numeric_error_detail_code(
    determinism::RendererNumericEnvironmentErrorCode code) noexcept {
    using Code = determinism::RendererNumericEnvironmentErrorCode;
    switch (code) {
    case Code::unsupported_platform:
        return "renderer-numeric-platform-changed";
    case Code::unsupported_abi:
        return "renderer-numeric-abi-changed";
    case Code::build_policy_unmarked:
        return "renderer-numeric-build-policy-changed";
    case Code::cpuid_query_unavailable:
        return "renderer-numeric-cpuid-query-changed";
    case Code::cpuid_disabled:
        return "renderer-numeric-cpuid-disabled";
    case Code::cpuid_leaf_unavailable:
        return "renderer-numeric-cpuid-leaf-changed";
    case Code::cpu_capability_missing:
        return "renderer-numeric-cpu-capability-changed";
    case Code::floating_point_format_unsupported:
        return "renderer-numeric-format-changed";
    case Code::floating_point_evaluation_unsupported:
        return "renderer-numeric-evaluation-changed";
    case Code::rounding_mode_mismatch:
        return "renderer-numeric-rounding-changed";
    case Code::mxcsr_control_mismatch:
        return "renderer-numeric-mxcsr-changed";
    case Code::x87_control_mismatch:
        return "renderer-numeric-x87-control-changed";
    }
    return "renderer-numeric-environment-changed";
}

[[nodiscard]] std::optional<NumericEnvironmentFailure>
revalidate_numeric_environment(const determinism::RendererNumericEnvironment &expected,
                               std::string_view boundary,
                               std::uint64_t sample_index) noexcept {
    auto observed = determinism::renderer_numeric_environment();
    if (auto *error =
            std::get_if<determinism::RendererNumericEnvironmentError>(&observed)) {
        const auto failure = NumericEnvironmentFailure{boundary, *error, sample_index};
        determinism::detail::restore_admitted_renderer_numeric_controls();
        return failure;
    }
    if (std::get<determinism::RendererNumericEnvironment>(observed) != expected) {
        const auto failure =
            NumericEnvironmentFailure{boundary, std::nullopt, sample_index};
        determinism::detail::restore_admitted_renderer_numeric_controls();
        return failure;
    }
    return std::nullopt;
}

[[nodiscard]] contract::RenderFailure
numeric_environment_failure(contract::RenderRequestRecord request,
                            const NumericEnvironmentFailure &failure) {
    if (failure.error.has_value()) {
        const auto &error = *failure.error;
        return make_job_failure(
            std::move(request), contract::FailureKind::contract_violation,
            std::string(numeric_error_detail_code(error.code)),
            "compiled-presentation-job-v1",
            "renderer numeric environment changed at " + std::string(failure.boundary) +
                ": component=" + std::string(error.component) +
                "; observed=" + std::to_string(error.observed) + "; required=" +
                std::to_string(error.required) + "; " + std::string(error.message),
            failure.sample_index, failure.sample_index,
            scenario_time(failure.sample_index));
    }
    return make_job_failure(
        std::move(request), contract::FailureKind::contract_violation,
        "renderer-numeric-environment-identity-changed", "compiled-presentation-job-v1",
        "the validated renderer numeric environment at " +
            std::string(failure.boundary) +
            " differed from the identity retained by the opaque job",
        failure.sample_index, failure.sample_index,
        scenario_time(failure.sample_index));
}

[[nodiscard]] contract::RenderFailure
exception_failure(contract::RenderRequestRecord request, ExecutionStage stage,
                  std::exception_ptr exception, std::uint64_t sample_index) {
    try {
        std::rethrow_exception(std::move(exception));
    } catch (const presentation::PresentationSinkFailure &failure) {
        const auto &sink_error = failure.sink_error();
        const auto kind = sink_error.kind == RenderSinkErrorKind::publication_failure
                              ? contract::FailureKind::artifact_publication_failure
                              : contract::FailureKind::contract_violation;
        const auto detail_code =
            contract::is_valid_semantic_id(sink_error.detail_code)
                ? sink_error.detail_code
                : std::string{sink_error.kind ==
                                      RenderSinkErrorKind::publication_failure
                                  ? "render-sink-publication-failed"
                                  : "render-sink-protocol-violated"};
        const auto invalid_detail =
            contract::is_valid_semantic_id(sink_error.detail_code)
                ? std::string{}
                : "; rejected sink detail code=" + sink_error.detail_code;
        return make_job_failure(
            std::move(request), kind, detail_code, "compiled-presentation-job-v1",
            std::string(stage_name(stage)) + " sink failure: " + sink_error.message +
                invalid_detail,
            sample_index, sample_index, scenario_time(sample_index));
    } catch (const std::bad_alloc &) {
        return make_job_failure(
            std::move(request), contract::FailureKind::contract_violation,
            "render-resource-exhausted", "compiled-presentation-job-v1",
            std::string(stage_name(stage)) +
                " exhausted process memory; no retry or fallback was attempted",
            sample_index, sample_index, scenario_time(sample_index));
    } catch (const std::domain_error &failure) {
        const auto kind = stage == ExecutionStage::presentation
                              ? contract::FailureKind::numerical_failure
                              : contract::FailureKind::contract_violation;
        return make_job_failure(
            std::move(request), kind,
            std::string(stage == ExecutionStage::presentation
                            ? std::string_view{"presentation-numerical-failure"}
                            : exception_detail_code(stage)),
            "compiled-presentation-job-v1",
            std::string(stage_name(stage)) + " failed: " + failure.what(), sample_index,
            sample_index, scenario_time(sample_index));
    } catch (const std::exception &failure) {
        return make_job_failure(
            std::move(request), contract::FailureKind::contract_violation,
            std::string(exception_detail_code(stage)), "compiled-presentation-job-v1",
            std::string(stage_name(stage)) + " failed: " + failure.what(), sample_index,
            sample_index, scenario_time(sample_index));
    } catch (...) {
        return make_job_failure(
            std::move(request), contract::FailureKind::contract_violation,
            std::string(exception_detail_code(stage)), "compiled-presentation-job-v1",
            std::string(stage_name(stage)) + " failed with a non-standard exception",
            sample_index, sample_index, scenario_time(sample_index));
    }
}

[[nodiscard]] contract::RenderFailure
coordinator_failure(contract::RenderRequestRecord request,
                    const CoordinatorFailure &failure) {
    return make_job_failure(std::move(request), failure.kind,
                            std::string(failure.detail_code),
                            "compiled-presentation-job-v1",
                            std::string(failure.state_summary), failure.sample_index,
                            failure.sample_index, scenario_time(failure.sample_index));
}

[[nodiscard]] contract::RenderFailure
relayed_failure(contract::RenderRequestRecord request, FirstCauseValue cause,
                std::uint64_t sample_index) {
    if (auto *failure = std::get_if<contract::FailureContext>(&cause)) {
        return {std::move(*failure), std::move(request), {}};
    }
    if (const auto *failure = std::get_if<CoordinatorFailure>(&cause)) {
        return coordinator_failure(std::move(request), *failure);
    }
    if (const auto *failure = std::get_if<NumericEnvironmentFailure>(&cause)) {
        return numeric_environment_failure(std::move(request), *failure);
    }
    if (auto *failure = std::get_if<DeferredException>(&cause)) {
        return exception_failure(std::move(request), failure->stage,
                                 std::move(failure->exception), sample_index);
    }
    return make_job_failure(
        std::move(request), contract::FailureKind::contract_violation,
        "render-first-cause-missing", "compiled-presentation-job-v1",
        "the execution coordinator attempted to relay an empty failure cause",
        sample_index, sample_index, scenario_time(sample_index));
}

[[nodiscard]] contract::RenderFailure
cancellation_failure(contract::RenderRequestRecord request, std::string detail_code,
                     std::string state_summary, std::uint64_t sample_index) {
    return make_job_failure(std::move(request), contract::FailureKind::cancelled,
                            std::move(detail_code), "compiled-presentation-job-v1",
                            std::move(state_summary), sample_index, sample_index,
                            scenario_time(sample_index));
}

} // namespace

contract::RenderResult CompiledPresentationJob::execute(RenderSink &sink,
                                                        RenderControl control) && {
    if (implementation_ == nullptr) {
        throw std::logic_error{"compiled presentation job was already consumed"};
    }
    auto implementation = std::move(implementation_);
    NumericControlRecovery recover_numeric_controls_at_exit;

    if (control.stop_token.stop_requested()) {
        return cancellation_failure(
            std::move(implementation->request), "render-cancelled-before-execution",
            "cancellation was observed before the presentation transaction began", 0);
    }

    ExecutionStage stage = ExecutionStage::presentation_transaction;
    try {
        // Cancellation belongs to this job coordinator. Passing the caller token
        // into a nested session would allow a callback wrapper to relabel it as a
        // consumer rejection.
        presentation::PresentationRenderSession presentation{
            sink, std::move(implementation->presentation_plan), RenderControl{}};
        if (const auto failure = revalidate_numeric_environment(
                implementation->determinism.numeric_environment(),
                "presentation transaction setup", 0)) {
            return numeric_environment_failure(std::move(implementation->request),
                                               *failure);
        }

        const auto total_block_count = implementation->calibration.total_block_count();
        const auto pre_audible_block_count =
            implementation->calibration.pre_audible_block_count();
        const auto expected_input_frame_count =
            total_block_count *
            presentation::AdmittedPresentationCalibration::capture_frames_per_block;
        const auto expected_source_frame_count =
            total_block_count *
            presentation::AdmittedPresentationCalibration::source_frames_per_block;
        const auto expected_pre_audible_source_frame_count =
            pre_audible_block_count *
            presentation::AdmittedPresentationCalibration::source_frames_per_block;
        const auto expected_published_block_count =
            total_block_count - pre_audible_block_count;
        const auto expected_published_source_frame_count =
            expected_published_block_count *
            presentation::AdmittedPresentationCalibration::source_frames_per_block;

        while (true) {
            const auto sample_index =
                implementation->simulation.published_sample_count();
            if (control.stop_token.stop_requested()) {
                return cancellation_failure(
                    std::move(implementation->request),
                    "render-cancelled-between-blocks",
                    "cancellation was observed between complete 200-frame "
                    "simulation blocks",
                    sample_index);
            }

            const auto expected_block =
                implementation->simulation.published_block_count();
            const auto expected_first_frame =
                expected_block *
                presentation::AdmittedPresentationCalibration::capture_frames_per_block;
            FirstCause first_cause;
            std::optional<simulation::LowOrderCaptureAdvanceResult> simulation_result;

            stage = ExecutionStage::simulation;
            try {
                simulation_result.emplace(implementation->simulation.publish_next_block(
                    [&](const contract::CaptureBlockView &capture) -> bool {
                        if (first_cause.present()) {
                            return false;
                        }

                        if (capture.clock().first_sample_index !=
                                expected_first_frame ||
                            capture.frame_count() !=
                                presentation::AdmittedPresentationCalibration::
                                    capture_frames_per_block) {
                            first_cause.record(CoordinatorFailure{
                                contract::FailureKind::contract_violation,
                                "simulation-capture-extent-disagreed",
                                "the simulation callback extent disagreed with the "
                                "opaque job block identity",
                                expected_first_frame,
                            });
                            return false;
                        }

                        try {
                            auto excitation_result = implementation->excitation.process_block(
                                capture,
                                [&](const presentation::ExhaustExcitationBlockView
                                        &excitation_block,
                                    const excitation::
                                        ExhaustExcitationDiagnosticBlockView &)
                                    -> bool {
                                    try {
                                        presentation.process(excitation_block);
                                        if (const auto failure =
                                                revalidate_numeric_environment(
                                                    implementation->determinism
                                                        .numeric_environment(),
                                                    "presentation block callback",
                                                    expected_first_frame +
                                                        presentation::
                                                            AdmittedPresentationCalibration::
                                                                capture_frames_per_block)) {
                                            first_cause.record(*failure);
                                            return false;
                                        }
                                        return true;
                                    } catch (...) {
                                        determinism::detail::
                                            restore_admitted_renderer_numeric_controls();
                                        first_cause.record(ExecutionStage::presentation,
                                                           std::current_exception());
                                        return false;
                                    }
                                });

                            if (auto *failure = std::get_if<contract::FailureContext>(
                                    &excitation_result)) {
                                first_cause.record(std::move(*failure));
                                return false;
                            }

                            const auto &published =
                                std::get<excitation::ExhaustExcitationBlockPublished>(
                                    excitation_result);
                            const auto expected_end =
                                expected_first_frame +
                                presentation::AdmittedPresentationCalibration::
                                    capture_frames_per_block;
                            if (published.block_ordinal != expected_block ||
                                published.first_frame_index != expected_first_frame ||
                                published.frame_count !=
                                    presentation::AdmittedPresentationCalibration::
                                        capture_frames_per_block ||
                                published.published_frame_count != expected_end) {
                                first_cause.record(CoordinatorFailure{
                                    contract::FailureKind::contract_violation,
                                    "excitation-publication-extent-disagreed",
                                    "the excitation publication disagreed with the "
                                    "opaque job block identity",
                                    expected_first_frame,
                                });
                                return false;
                            }
                            return true;
                        } catch (...) {
                            determinism::detail::
                                restore_admitted_renderer_numeric_controls();
                            first_cause.record(ExecutionStage::excitation,
                                               std::current_exception());
                            return false;
                        }
                    }));
            } catch (...) {
                determinism::detail::restore_admitted_renderer_numeric_controls();
                first_cause.record(ExecutionStage::simulation,
                                   std::current_exception());
            }

            if (first_cause.present()) {
                return relayed_failure(
                    std::move(implementation->request), first_cause.take(),
                    implementation->simulation.published_sample_count());
            }

            if (auto *failure =
                    std::get_if<contract::FailureContext>(&*simulation_result)) {
                return contract::RenderFailure{
                    std::move(*failure), std::move(implementation->request), {}};
            }

            if (const auto *completed =
                    std::get_if<simulation::LowOrderCaptureCompleted>(
                        &*simulation_result)) {
                if (completed->sample_count != expected_input_frame_count ||
                    completed->block_count != total_block_count ||
                    implementation->simulation.published_sample_count() !=
                        expected_input_frame_count ||
                    implementation->simulation.published_block_count() !=
                        total_block_count ||
                    implementation->excitation.next_frame_index() !=
                        expected_input_frame_count ||
                    implementation->excitation.published_block_count() !=
                        total_block_count ||
                    implementation->excitation.faulted()) {
                    return coordinator_failure(
                        std::move(implementation->request),
                        {
                            contract::FailureKind::contract_violation,
                            "pipeline-completion-count-disagreed",
                            "simulation and excitation completion counts disagreed "
                            "with the opaque job timeline",
                            implementation->simulation.published_sample_count(),
                        });
                }
                break;
            }

            const auto &published =
                std::get<simulation::LowOrderCaptureBlockPublished>(*simulation_result);
            const auto expected_end =
                expected_first_frame +
                presentation::AdmittedPresentationCalibration::capture_frames_per_block;
            if (published.block_ordinal != expected_block ||
                published.first_sample_index != expected_first_frame ||
                published.frame_count != presentation::AdmittedPresentationCalibration::
                                             capture_frames_per_block ||
                published.published_sample_count != expected_end ||
                implementation->simulation.published_sample_count() != expected_end ||
                implementation->simulation.published_block_count() !=
                    expected_block + 1 ||
                implementation->excitation.next_frame_index() != expected_end ||
                implementation->excitation.published_block_count() !=
                    expected_block + 1) {
                return coordinator_failure(
                    std::move(implementation->request),
                    {
                        contract::FailureKind::contract_violation,
                        "pipeline-block-count-disagreed",
                        "simulation and excitation block progress disagreed with "
                        "the opaque job timeline",
                        expected_first_frame,
                    });
            }
        }

        if (control.stop_token.stop_requested()) {
            return cancellation_failure(
                std::move(implementation->request),
                "render-cancelled-before-finalization",
                "cancellation was observed after complete simulation and before "
                "presentation finalization",
                implementation->simulation.published_sample_count());
        }

        stage = ExecutionStage::finalization;
        auto evidence = presentation.finish();
        if (const auto failure = revalidate_numeric_environment(
                implementation->determinism.numeric_environment(),
                "presentation finalization",
                implementation->simulation.published_sample_count())) {
            return numeric_environment_failure(std::move(implementation->request),
                                               *failure);
        }
        const auto &stats = evidence.stats();
        if (stats.input_frame_count != expected_input_frame_count ||
            stats.processed_block_count != total_block_count ||
            stats.pre_audible_block_count != pre_audible_block_count ||
            stats.published_block_count != expected_published_block_count ||
            stats.processed_source_frame_count != expected_source_frame_count ||
            stats.pre_audible_source_frame_count !=
                expected_pre_audible_source_frame_count ||
            stats.published_source_frame_count !=
                expected_published_source_frame_count) {
            return coordinator_failure(
                std::move(implementation->request),
                {
                    contract::FailureKind::contract_violation,
                    "presentation-evidence-count-disagreed",
                    "sealed presentation evidence disagreed with the opaque job "
                    "timeline",
                    implementation->simulation.published_sample_count(),
                });
        }

        stage = ExecutionStage::manifest_completion;
        implementation->manifest_basis.artifacts.assign(evidence.artifacts().begin(),
                                                        evidence.artifacts().end());
        contract::RenderManifest manifest{
            std::move(implementation->manifest_basis),
            evidence.execution().facts(),
        };

        stage = ExecutionStage::commit;
        presentation.commit(evidence, manifest, implementation->request.provenance,
                            implementation->request.source_matrix);
        return contract::RenderSuccess{
            std::move(manifest),
            std::nullopt,
            std::nullopt,
        };
    } catch (...) {
        determinism::detail::restore_admitted_renderer_numeric_controls();
        return exception_failure(std::move(implementation->request), stage,
                                 std::current_exception(),
                                 implementation->simulation.published_sample_count());
    }
}

} // namespace engine_sim_offline::render_detail
