#include "engine_sim_offline/contract/result.hpp"

#include "validation_support.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

bool known(FailureKind value) noexcept {
    switch (value) {
    case FailureKind::invalid_specification:
    case FailureKind::unreachable_target:
    case FailureKind::cancelled:
    case FailureKind::event_schedule_violation:
    case FailureKind::nonphysical_state:
    case FailureKind::numerical_failure:
    case FailureKind::incomplete_source_route:
    case FailureKind::evidence_rights_failure:
    case FailureKind::artifact_publication_failure:
    case FailureKind::contract_violation:
    case FailureKind::preparation_not_converged:
        return true;
    }
    return false;
}

bool known(ContractIssueCode value) noexcept {
    switch (value) {
    case ContractIssueCode::missing_value:
    case ContractIssueCode::invalid_value:
    case ContractIssueCode::duplicate_identity:
    case ContractIssueCode::dangling_reference:
    case ContractIssueCode::inconsistent_shape:
    case ContractIssueCode::inconsistent_semantics:
    case ContractIssueCode::unsupported_value:
        return true;
    }
    return false;
}

bool known(ActiveBoundKind value) noexcept {
    switch (value) {
    case ActiveBoundKind::throttle_lower:
    case ActiveBoundKind::throttle_upper:
        return true;
    }
    return false;
}

bool valid_candidate(const ReachabilityCandidate &candidate) noexcept {
    return candidate.stable_candidate_id > 0 &&
           detail::unit_interval(candidate.throttle_01) &&
           detail::finite(candidate.actuator_torque_nm) &&
           detail::finite(candidate.achieved_net_bmep_pa);
}

ValidationReport validate_search(const ReachabilityEvidence &search,
                                 const ReachabilityCandidate &selected,
                                 double target_net_bmep_pa) {
    using detail::require;

    ValidationReport report;
    require(report, search.evaluated_candidate_count == search.probes.size(),
            ContractIssueCode::inconsistent_shape, "evaluated_candidate_count",
            "evaluated-candidate count must equal retained search evidence");
    require(report,
            search.evaluated_candidate_count > 0 &&
                search.evaluated_candidate_count <= kMaxRetainedReachabilityProbes &&
                search.iteration_count > 0 &&
                detail::unit_interval(search.requested_throttle_lower_bound_01) &&
                detail::unit_interval(search.requested_throttle_upper_bound_01) &&
                search.requested_throttle_lower_bound_01 <=
                    search.requested_throttle_upper_bound_01,
            ContractIssueCode::invalid_value, "",
            "reachability search bounds, counts, or probe budget are invalid");

    std::unordered_set<std::uint64_t> candidate_ids;
    bool selected_is_retained = false;
    for (std::size_t index = 0; index < search.probes.size(); ++index) {
        const auto &probe = search.probes[index];
        const auto path = "probes[" + std::to_string(index) + "]";
        require(report, valid_candidate(probe), ContractIssueCode::invalid_value, path,
                "reachability probe must be finite and identified");
        require(report,
                probe.throttle_01 >= search.requested_throttle_lower_bound_01 &&
                    probe.throttle_01 <= search.requested_throttle_upper_bound_01,
                ContractIssueCode::inconsistent_semantics, path + ".throttle_01",
                "reachability probe must lie inside the requested search interval");
        if (!candidate_ids.insert(probe.stable_candidate_id).second) {
            report.add(ContractIssueCode::duplicate_identity,
                       path + ".stable_candidate_id",
                       "reachability candidate IDs must be unique");
        }
        if (probe == selected) {
            selected_is_retained = true;
        }
        if (probe.settled &&
            is_better_nearest_candidate(probe, selected, target_net_bmep_pa)) {
            report.add(ContractIssueCode::inconsistent_semantics, "selected",
                       "selected candidate violates the deterministic tie-break "
                       "order");
        }
    }
    require(report, selected_is_retained, ContractIssueCode::dangling_reference,
            "selected", "selected candidate must be one of the retained search probes");
    return report;
}

void validate_requested_search_interval(ValidationReport &report,
                                        const ReachabilityEvidence &search,
                                        const LoadTargetHeldCapture &requested,
                                        std::string_view path) {
    using detail::require;

    const auto field_path = [path](std::string_view field) {
        return std::string(path) + "." + std::string(field);
    };
    require(report,
            search.requested_throttle_lower_bound_01 ==
                requested.throttle_lower_bound_01.value,
            ContractIssueCode::inconsistent_semantics,
            field_path("requested_throttle_lower_bound_01"),
            "search lower throttle bound must exactly match the requested lower "
            "bound");
    require(report,
            search.requested_throttle_upper_bound_01 ==
                requested.throttle_upper_bound_01.value,
            ContractIssueCode::inconsistent_semantics,
            field_path("requested_throttle_upper_bound_01"),
            "search upper throttle bound must exactly match the requested upper "
            "bound");
}

void validate_request_binding(ValidationReport &report,
                              const RenderRequestRecord &request,
                              const RenderScenario &requested_scenario,
                              const ProvenanceLedger &provenance,
                              const SourceMatrixContract &source_matrix,
                              std::string_view path) {
    const auto field_path = [path](std::string_view field) {
        return std::string(path) + "." + std::string(field);
    };
    detail::require(report, request.resolved_inputs.scenario == requested_scenario,
                    ContractIssueCode::inconsistent_semantics,
                    field_path("resolved_inputs.scenario"),
                    "result must retain the exact requested scenario");
    detail::require(report, request.provenance == provenance,
                    ContractIssueCode::inconsistent_semantics, field_path("provenance"),
                    "result must retain the complete supplied provenance");
    detail::require(report, request.source_matrix == source_matrix,
                    ContractIssueCode::inconsistent_semantics,
                    field_path("source_matrix"),
                    "result must retain the complete selected source matrix");
}

[[nodiscard]] bool
request_is_structurally_admitted(const RenderRequestRecord &request) {
    return validate_render_admission(
               request.resolved_inputs.engine, request.resolved_inputs.presentation,
               request.resolved_inputs.randomness, request.resolved_inputs.scenario,
               request.provenance, request.source_matrix)
        .ok();
}

} // namespace

bool is_better_nearest_candidate(const ReachabilityCandidate &candidate,
                                 const ReachabilityCandidate &incumbent,
                                 double target_net_bmep_pa) noexcept {
    return std::tuple{
               std::abs(candidate.achieved_net_bmep_pa - target_net_bmep_pa),
               std::abs(candidate.actuator_torque_nm),
               candidate.throttle_01,
               candidate.stable_candidate_id,
           } < std::tuple{
                   std::abs(incumbent.achieved_net_bmep_pa - target_net_bmep_pa),
                   std::abs(incumbent.actuator_torque_nm),
                   incumbent.throttle_01,
                   incumbent.stable_candidate_id,
               };
}

ValidationReport validate(const FailureContext &context) {
    using detail::finite;
    using detail::finite_nonnegative;
    using detail::require;

    ValidationReport report;
    require(report, known(context.kind), ContractIssueCode::unsupported_value, "kind",
            "failure kind is not recognized");
    require(report, is_valid_semantic_id(context.detail_code),
            ContractIssueCode::invalid_value, "detail_code",
            "failure detail code must be a canonical semantic ID");
    require(report, is_valid_semantic_id(context.model_id),
            ContractIssueCode::invalid_value, "model_id",
            "failure model ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(context.profile_id),
            ContractIssueCode::invalid_value, "profile_id",
            "failure profile ID must be a canonical semantic ID");
    require(report,
            finite_nonnegative(context.scenario_time_s) && finite(context.theta_rad),
            ContractIssueCode::invalid_value, "scenario_time_s",
            "failure time must be nonnegative and angle must be finite");
    if (context.engine_id.has_value()) {
        require(report, context.engine_id->valid(), ContractIssueCode::invalid_value,
                "engine_id", "present engine ID must be nonzero");
    }
    const auto validate_optional_id = [&](const auto &id, const char *path) {
        if (id.has_value()) {
            require(report, id->valid(), ContractIssueCode::invalid_value, path,
                    "present component ID must be nonzero");
        }
    };
    validate_optional_id(context.cylinder_id, "cylinder_id");
    validate_optional_id(context.port_id, "port_id");
    validate_optional_id(context.gas_volume_id, "gas_volume_id");
    validate_optional_id(context.flow_edge_id, "flow_edge_id");
    validate_optional_id(context.route_id, "route_id");
    require(report, !context.state_summary.empty(), ContractIssueCode::missing_value,
            "state_summary", "failure must summarize the relevant state");
    require(report, !context.attempted_recovery.empty(),
            ContractIssueCode::missing_value, "attempted_recovery",
            "failure must state the attempted recovery, including none");
    std::unordered_set<std::string> quantity_ids;
    for (std::size_t index = 0; index < context.tolerances.size(); ++index) {
        const auto &tolerance = context.tolerances[index];
        const auto path = "tolerances[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(tolerance.quantity_id),
                ContractIssueCode::invalid_value, path + ".quantity_id",
                "tolerance quantity ID must be canonical");
        if (!quantity_ids.insert(tolerance.quantity_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".quantity_id",
                       "tolerance quantity IDs must be unique");
        }
        require(report,
                finite(tolerance.attempted_value) &&
                    finite_nonnegative(tolerance.tolerance),
                ContractIssueCode::invalid_value, path,
                "attempted value and tolerance must be finite");
    }
    return report;
}

ValidationReport validate(const ReachedTarget &reached) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    require(
        report,
        finite(reached.target_net_bmep_pa) && finite(reached.achieved_net_bmep_pa) &&
            finite(reached.signed_error_pa) && finite_positive(reached.tolerance_pa),
        ContractIssueCode::invalid_value, "",
        "reached target, result, error, and tolerance must be finite");
    require(
        report,
        detail::nearly_equal(reached.signed_error_pa,
                             reached.achieved_net_bmep_pa - reached.target_net_bmep_pa),
        ContractIssueCode::inconsistent_semantics, "signed_error_pa",
        "signed error must equal achieved minus target");
    require(report, std::abs(reached.signed_error_pa) <= reached.tolerance_pa,
            ContractIssueCode::inconsistent_semantics, "signed_error_pa",
            "successful load target must be inside tolerance");
    require(report, valid_candidate(reached.selected) && reached.selected.settled,
            ContractIssueCode::invalid_value, "selected",
            "selected successful candidate must be settled, finite, and identified");
    require(report,
            detail::nearly_equal(reached.selected.achieved_net_bmep_pa,
                                 reached.achieved_net_bmep_pa),
            ContractIssueCode::inconsistent_semantics, "selected.achieved_net_bmep_pa",
            "top-level achieved load must match selected candidate");
    append_prefixed(
        report,
        validate_search(reached.search, reached.selected, reached.target_net_bmep_pa),
        "search");
    return report;
}

ValidationReport validate(const UnreachableTarget &unreachable) {
    using detail::append_prefixed;
    using detail::finite;
    using detail::finite_positive;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate(unreachable.context), "context");
    require(report, unreachable.context.kind == FailureKind::unreachable_target,
            ContractIssueCode::inconsistent_semantics, "context.kind",
            "unreachable result must use unreachable-target failure kind");
    require(report,
            finite(unreachable.target_net_bmep_pa) &&
                finite(unreachable.achieved_net_bmep_pa) &&
                finite(unreachable.signed_error_pa) &&
                finite_positive(unreachable.tolerance_pa),
            ContractIssueCode::invalid_value, "",
            "reachability target, result, error, and tolerance must be finite");
    require(report,
            detail::nearly_equal(unreachable.signed_error_pa,
                                 unreachable.achieved_net_bmep_pa -
                                     unreachable.target_net_bmep_pa),
            ContractIssueCode::inconsistent_semantics, "signed_error_pa",
            "signed error must equal achieved minus target");
    require(report, std::abs(unreachable.signed_error_pa) > unreachable.tolerance_pa,
            ContractIssueCode::inconsistent_semantics, "signed_error_pa",
            "a target inside tolerance is reachable");
    require(report,
            valid_candidate(unreachable.nearest_feasible) &&
                unreachable.nearest_feasible.settled,
            ContractIssueCode::invalid_value, "nearest_feasible",
            "nearest feasible candidate must be settled, finite, and identified");
    require(report,
            detail::nearly_equal(unreachable.nearest_feasible.achieved_net_bmep_pa,
                                 unreachable.achieved_net_bmep_pa),
            ContractIssueCode::inconsistent_semantics,
            "nearest_feasible.achieved_net_bmep_pa",
            "top-level achieved load must match selected candidate");

    require(report, !unreachable.active_bounds.empty(),
            ContractIssueCode::missing_value, "active_bounds",
            "unreachable target must report every active limiting bound");
    std::unordered_set<std::uint8_t> active_bound_kinds;
    for (std::size_t index = 0; index < unreachable.active_bounds.size(); ++index) {
        const auto &bound = unreachable.active_bounds[index];
        const auto path = "active_bounds[" + std::to_string(index) + "]";
        require(report, known(bound.kind), ContractIssueCode::unsupported_value,
                path + ".kind", "active bound kind is not recognized");
        if (!active_bound_kinds.insert(static_cast<std::uint8_t>(bound.kind)).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".kind",
                       "active bound kinds must be unique");
        }
        require(report, finite(bound.throttle_01), ContractIssueCode::invalid_value,
                path + ".throttle_01", "active throttle bound must be finite");
        if (known(bound.kind)) {
            require(report, detail::unit_interval(bound.throttle_01),
                    ContractIssueCode::invalid_value, path + ".throttle_01",
                    "throttle bound must be in [0, 1]");
            const auto expected =
                bound.kind == ActiveBoundKind::throttle_lower
                    ? unreachable.search.requested_throttle_lower_bound_01
                    : unreachable.search.requested_throttle_upper_bound_01;
            require(report,
                    bound.throttle_01 == expected &&
                        unreachable.nearest_feasible.throttle_01 == expected,
                    ContractIssueCode::inconsistent_semantics, path + ".throttle_01",
                    "active throttle bound must match the search interval and "
                    "nearest feasible candidate");
        }
    }

    append_prefixed(report,
                    validate_search(unreachable.search, unreachable.nearest_feasible,
                                    unreachable.target_net_bmep_pa),
                    "search");
    return report;
}

ValidationReport validate(const RenderFailure &failure) {
    auto report = validate(failure.context);
    detail::require(report, failure.context.kind != FailureKind::unreachable_target,
                    ContractIssueCode::inconsistent_semantics, "context.kind",
                    "unreachable targets use the typed UnreachableTarget result");
    const bool requires_validation =
        failure.context.kind == FailureKind::invalid_specification ||
        failure.context.kind == FailureKind::evidence_rights_failure;
    detail::require(
        report, !requires_validation || !failure.validation.ok(),
        ContractIssueCode::missing_value, "validation",
        "preflight and evidence-rights failures must retain their diagnostics");
    if (failure.context.kind != FailureKind::invalid_specification) {
        detail::require(
            report, request_is_structurally_admitted(failure.request),
            ContractIssueCode::inconsistent_semantics, "request.resolved_inputs",
            "non-preflight failure must retain a structurally admitted render "
            "request");
    }
    for (std::size_t index = 0; index < failure.validation.issues.size(); ++index) {
        const auto &issue = failure.validation.issues[index];
        const auto path = "validation.issues[" + std::to_string(index) + "]";
        detail::require(report, known(issue.code), ContractIssueCode::unsupported_value,
                        path + ".code",
                        "retained validation issue code must be recognized");
        detail::require(report, !issue.path.empty(), ContractIssueCode::missing_value,
                        path + ".path",
                        "retained validation issues must identify their input path");
        detail::require(report, !issue.message.empty(),
                        ContractIssueCode::missing_value, path + ".message",
                        "retained validation issues must explain the rejection");
    }
    return report;
}

ValidationReport
validate(const RenderResult &result, const RenderScenario &requested_scenario,
         const Sha256Digest &expected_simulation_request_identity_v2_sha256,
         const ProvenanceLedger &provenance,
         const SourceMatrixContract &source_matrix) {
    using detail::append_prefixed;
    using detail::require;

    ValidationReport report;
    std::visit(
        [&](const auto &outcome) {
            using T = std::decay_t<decltype(outcome)>;
            if constexpr (std::is_same_v<T, RenderSuccess>) {
                append_prefixed(report,
                                validate(outcome.manifest, provenance, source_matrix),
                                "success.manifest");
                const auto &simulation_inputs = outcome.manifest.content.inputs;
                require(report,
                        simulation_inputs.resolved.scenario == requested_scenario,
                        ContractIssueCode::inconsistent_semantics,
                        "success.manifest.content.inputs.simulation.scenario",
                        "successful manifest must contain the requested scenario");
                const auto is_load_target =
                    std::holds_alternative<LoadTargetHeldCapture>(
                        requested_scenario.mode);
                require(report, outcome.reached_target.has_value() == is_load_target,
                        ContractIssueCode::inconsistent_semantics,
                        "success.reached_target",
                        "exactly load-target success requires reached-target evidence");
                if (outcome.reached_target.has_value()) {
                    append_prefixed(report, validate(*outcome.reached_target),
                                    "success.reached_target");
                    if (const auto *mode = std::get_if<LoadTargetHeldCapture>(
                            &requested_scenario.mode)) {
                        require(
                            report,
                            outcome.reached_target->target_net_bmep_pa ==
                                    mode->target_net_bmep_pa.value &&
                                outcome.reached_target->tolerance_pa ==
                                    mode->target_tolerance_pa.value,
                            ContractIssueCode::inconsistent_semantics,
                            "success.reached_target",
                            "reached-target evidence must match the requested target "
                            "and tolerance");
                        validate_requested_search_interval(
                            report, outcome.reached_target->search, *mode,
                            "success.reached_target.search");
                    }
                }
                const auto is_held_speed =
                    std::holds_alternative<HeldSpeed>(requested_scenario.mode);
                const auto is_operating_profile =
                    std::holds_alternative<LowOrderOperatingPointV1Profile>(
                        simulation_inputs.resolved.engine.physics_profile);
                const auto requires_operating_point =
                    is_held_speed && is_operating_profile;
                require(report,
                        outcome.held_speed_operating_point.has_value() ==
                            requires_operating_point,
                        ContractIssueCode::inconsistent_semantics,
                        "success.held_speed_operating_point",
                        "exactly a low-order operating-profile held-speed success "
                        "requires typed operating-point evidence");
                if (outcome.held_speed_operating_point.has_value()) {
                    append_prefixed(
                        report,
                        validate(*outcome.held_speed_operating_point,
                                 requested_scenario, simulation_inputs.resolved.engine,
                                 expected_simulation_request_identity_v2_sha256),
                        "success.held_speed_operating_point");
                }
            } else if constexpr (std::is_same_v<T, UnreachableTarget>) {
                append_prefixed(report, validate(outcome), "unreachable");
                validate_request_binding(report, outcome.request, requested_scenario,
                                         provenance, source_matrix,
                                         "unreachable.request");
                require(report, request_is_structurally_admitted(outcome.request),
                        ContractIssueCode::inconsistent_semantics,
                        "unreachable.request.resolved_inputs",
                        "unreachable result must retain a structurally admitted "
                        "render request");
                const auto *mode =
                    std::get_if<LoadTargetHeldCapture>(&requested_scenario.mode);
                require(report, mode != nullptr,
                        ContractIssueCode::inconsistent_semantics, "unreachable",
                        "only a load-target scenario may return unreachable target");
                if (mode != nullptr) {
                    require(report,
                            outcome.target_net_bmep_pa ==
                                    mode->target_net_bmep_pa.value &&
                                outcome.tolerance_pa == mode->target_tolerance_pa.value,
                            ContractIssueCode::inconsistent_semantics, "unreachable",
                            "unreachable evidence must match the requested target and "
                            "tolerance");
                    validate_requested_search_interval(report, outcome.search, *mode,
                                                       "unreachable.search");
                }
                require(report,
                        outcome.context.profile_id ==
                            requested_scenario.engine_profile_id,
                        ContractIssueCode::inconsistent_semantics,
                        "unreachable.context.profile_id",
                        "failure profile must match the requested engine profile");
            } else {
                append_prefixed(report, validate(outcome), "failure");
                validate_request_binding(report, outcome.request, requested_scenario,
                                         provenance, source_matrix, "failure.request");
                if (outcome.context.kind == FailureKind::evidence_rights_failure) {
                    ValidationReport expected_rights;
                    append_prefixed(expected_rights,
                                    validate_evidence_rights(
                                        provenance, source_matrix.distribution),
                                    "specification.provenance");
                    require(report, !expected_rights.ok(),
                            ContractIssueCode::inconsistent_semantics,
                            "failure.context.kind",
                            "evidence-rights failure requires an inadmissible evidence "
                            "and distribution combination");
                    require(report, outcome.validation.issues == expected_rights.issues,
                            ContractIssueCode::inconsistent_semantics,
                            "failure.validation",
                            "evidence-rights failure must retain the exact admission "
                            "diagnostics");
                }
                if (is_valid_semantic_id(requested_scenario.engine_profile_id)) {
                    require(report,
                            outcome.context.profile_id ==
                                requested_scenario.engine_profile_id,
                            ContractIssueCode::inconsistent_semantics,
                            "failure.context.profile_id",
                            "failure profile must match the requested engine "
                            "profile");
                } else {
                    require(report,
                            outcome.context.kind ==
                                    FailureKind::invalid_specification &&
                                outcome.context.profile_id == "unresolved",
                            ContractIssueCode::inconsistent_semantics,
                            "failure.context.profile_id",
                            "a malformed requested profile requires the canonical "
                            "unresolved preflight identity");
                }
            }
        },
        result);
    return report;
}

} // namespace engine_sim_offline::contract
