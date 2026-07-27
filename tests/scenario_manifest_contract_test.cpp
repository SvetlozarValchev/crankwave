#include "contract_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace engine_sim_offline::contract::test {
namespace {

bool has_issue(const ValidationReport &report, ContractIssueCode code,
               std::string_view path_fragment) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.code == code &&
               issue.path.find(path_fragment) != std::string::npos;
    });
}

FailureContext unreachable_context() {
    return {
        FailureKind::unreachable_target,
        "target-outside-feasible-domain",
        "load-controller-v1",
        "bmw-m52b28-parity",
        10,
        10,
        0.001,
        0.1,
        EngineId{1},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "throttle saturated at upper bound",
        "searched declared throttle interval",
        {FailureTolerance{"net-bmep-pa", 80.0, 1.0}},
    };
}

} // namespace

void run_scenario_manifest_contract_tests() {
    InputBuilder builder;
    auto content = make_manifest_content(builder);
    const auto source_matrix = make_source_matrix();

    expect(validate(builder.provenance).ok(), "valid provenance ledger was rejected");
    expect(validate(source_matrix).ok(), "valid source matrix was rejected");
    expect(validate(simulation_inputs(content).engine, builder.provenance).ok(),
           "valid resolved engine was rejected");
    expect(validate(simulation_inputs(content).scenario, builder.provenance).ok(),
           "valid tagged scenario was rejected");
    auto unbounded_event_journal = simulation_inputs(content).scenario;
    unbounded_event_journal.quality.value.event_journal_capacity_records = 0;
    expect(!validate(unbounded_event_journal, builder.provenance).ok(),
           "scenario accepted a zero event-journal transport capacity");

    const RationalRateHz high_rate{2'000'000'000'000ULL, 1};
    auto discrete_audible_gap = simulation_inputs(content).scenario;
    discrete_audible_gap.rates = {
        high_rate, high_rate, high_rate, high_rate, high_rate,
    };
    discrete_audible_gap.total_duration_s.value = 3.0;
    discrete_audible_gap.audible_start_s.value = 2.0;
    discrete_audible_gap.audible_duration_s.value = 0.9999999999995;
    const auto audible_grid_report = validate_clock_grid(discrete_audible_gap);
    expect(!audible_grid_report.ok() &&
               has_issue(audible_grid_report, ContractIssueCode::inconsistent_semantics,
                         "physics.audible_interval"),
           "binary64-near audible end left an undeclared frame gap");

    auto discrete_fixed_gap = simulation_inputs(content).scenario;
    discrete_fixed_gap.rates = discrete_audible_gap.rates;
    std::get<FixedSettling>(discrete_fixed_gap.preparation).settling_duration_s.value =
        0.9999999999995;
    const auto fixed_grid_report = validate_clock_grid(discrete_fixed_gap);
    expect(!fixed_grid_report.ok() &&
               has_issue(fixed_grid_report, ContractIssueCode::inconsistent_semantics,
                         "physics.preparation"),
           "binary64-near fixed preparation left an undeclared frame gap");

    auto invalid_convergence_grid = simulation_inputs(content).scenario;
    ConvergenceSettling convergence;
    convergence.minimum_warm_up_duration_s.value = 1.0;
    convergence.minimum_settling_duration_s.value = 1.0;
    convergence.maximum_preparation_duration_s.value = 1.5;
    invalid_convergence_grid.preparation = convergence;
    const auto convergence_grid_report = validate_clock_grid(invalid_convergence_grid);
    expect(!convergence_grid_report.ok() &&
               has_issue(convergence_grid_report,
                         ContractIssueCode::inconsistent_semantics,
                         "physics.preparation"),
           "convergence frame bounds accepted a maximum below their minimum");

    expect(validate_for_engine(simulation_inputs(content).scenario,
                               simulation_inputs(content).engine)
               .ok(),
           "valid scenario and engine pairing was rejected");
    auto wrong_fuel_scenario = simulation_inputs(content).scenario;
    wrong_fuel_scenario.fuel.lower_heating_value_j_per_kg.value += 1.0;
    expect(!validate_for_engine(wrong_fuel_scenario, simulation_inputs(content).engine)
                .ok(),
           "scenario fuel was allowed to contradict executable engine fuel");
    expect(validate(content, builder.provenance, source_matrix).ok(),
           "valid render manifest content was rejected");

    auto mismatched_asset_evidence = content;
    simulation_inputs(mismatched_asset_evidence)
        .presentation.assets[0]
        .content_sha256.value = digest(31);
    auto report =
        validate(mismatched_asset_evidence, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "content_sha256"),
           "presentation asset digest was allowed to disagree with its evidence");

    auto mismatched_algorithm_record = content;
    simulation_inputs(mismatched_algorithm_record)
        .presentation.algorithm_record.content_sha256.value = digest(28);
    report = validate(mismatched_algorithm_record, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "algorithm_record.content_sha256"),
           "presentation algorithm record was allowed to disagree with evidence");

    auto unconfigured_rendered_route = content;
    simulation_inputs(unconfigured_rendered_route).presentation.routes.clear();
    report = validate(unconfigured_rendered_route, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "routes[0].disposition"),
           "manifest claimed a rendered route without presentation configuration");

    RenderManifest first{
        content,
        ExecutionFacts{
            "run-a",
            "2026-07-27T00:00:00Z",
            std::chrono::seconds{1},
            "linux-a",
            "cpu-a",
            8,
            2,
            1,
            1024,
        },
    };
    RenderManifest second{
        content,
        ExecutionFacts{
            "run-b",
            "2026-07-27T00:00:01Z",
            std::chrono::seconds{9},
            "linux-b",
            "cpu-b",
            32,
            8,
            4,
            4096,
        },
    };
    expect(validate(first, builder.provenance, source_matrix).ok(),
           "valid completed render manifest was rejected");
    const RenderResult held_success = RenderSuccess{first, std::nullopt};
    expect(validate(held_success, simulation_inputs(content).scenario,
                    builder.provenance, source_matrix)
               .ok(),
           "held-speed success result was rejected");
    expect(same_content_identity(first, second),
           "execution facts changed deterministic render identity");
    second.content.randomness.public_seed += 1;
    expect(!same_content_identity(first, second),
           "public seed failed to change deterministic render identity");

    auto missing_execution = first;
    missing_execution.execution.reset();
    report = validate(missing_execution, builder.provenance, source_matrix);
    expect(!report.ok() &&
               has_issue(report, ContractIssueCode::missing_value, "execution"),
           "completed manifest without execution facts was accepted");

    auto invalid_execution = first;
    invalid_execution.execution->observed_process_threads = 0;
    report = validate(invalid_execution, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "execution.observed_process_threads"),
           "completed manifest accepted empty execution topology");

    invalid_execution = first;
    invalid_execution.execution->started_utc = "not-a-utc-time";
    report = validate(invalid_execution, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "execution.started_utc"),
           "completed manifest accepted a non-UTC execution timestamp");

    invalid_execution = first;
    invalid_execution.execution->started_utc = "2026-02-29T00:00:00Z";
    report = validate(invalid_execution, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "execution.started_utc"),
           "completed manifest accepted an impossible UTC calendar date");

    auto ownership_mismatch = content;
    ownership_mismatch.output_buses[0].artifact_roles = {
        "exhaust.outlet-1.selected",
    };
    report = validate(ownership_mismatch, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "output_buses[0]"),
           "source/output ownership mismatch was accepted");

    auto mismatched_policy = source_matrix;
    mismatched_policy.sha256 = digest(41);
    report = validate(content, builder.provenance, mismatched_policy);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "output_contract"),
           "manifest accepted an output contract from a different source matrix");

    auto wrong_provenance_bundle = content;
    wrong_provenance_bundle.provenance.sha256 = digest(99);
    report = validate(wrong_provenance_bundle, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "provenance"),
           "manifest accepted a provenance reference unrelated to the supplied "
           "ledger");

    auto seed_mismatch = content;
    ++seed_mismatch.randomness.public_seed;
    report = validate(seed_mismatch, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "randomness.public_seed"),
           "manifest seed was allowed to drift from the resolved scenario");

    auto reserved_generator = content;
    reserved_generator.randomness.generator.id = "p18_reference_pcg32_v1";
    report = validate(reserved_generator, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::unsupported_value,
                                     "randomness.generator"),
           "generic output contract was allowed to claim the frozen P1.8 RNG");

    auto missing_active_stream = content;
    missing_active_stream.randomness.component_seeds.pop_back();
    report = validate(missing_active_stream, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_shape,
                                     "randomness.component_seeds"),
           "active presentation air noise was accepted without its route stream");

    auto wrong_frame_count = content;
    --wrong_frame_count.artifacts[0].audio->frame_count;
    report = validate(wrong_frame_count, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "artifacts[0].audio.frame_count"),
           "audio artifact with the wrong frame count was accepted");

    auto wrong_rate = content;
    wrong_rate.artifacts[0].audio->sample_rate = {48000, 1};
    report = validate(wrong_rate, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "artifacts[0].audio.sample_rate"),
           "audio artifact with the wrong delivery rate was accepted");

    auto escaping_path = content;
    escaping_path.artifacts[0].relative_path = "../outside.wav";
    report = validate(escaping_path, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "artifacts[0].relative_path"),
           "artifact path traversal was accepted");

    auto colliding_path = content;
    colliding_path.artifacts[1].relative_path = "AUDIO/EXHAUST-OUTLET-1.WAV";
    report = validate(colliding_path, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::duplicate_identity,
                                     "artifacts[1].relative_path"),
           "portable case-insensitive artifact path collision was accepted");

    auto malformed_matrix = source_matrix;
    malformed_matrix.required_output_buses[0].artifact_roles = {
        "exhaust.outlet-1.selected",
    };
    report = validate(malformed_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "required_artifacts"),
           "source matrix allowed route and output bus to share audio ownership");

    malformed_matrix = source_matrix;
    malformed_matrix.required_source_routes[0].disposition =
        RouteDisposition::not_applicable;
    malformed_matrix.required_source_routes[0].artifact_roles.clear();
    report = validate(malformed_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "required_source_routes[0]"),
           "source-matrix not-applicable route was accepted without its policy "
           "reason");

    malformed_matrix = source_matrix;
    malformed_matrix.required_source_routes[0].kind = static_cast<SourceRouteKind>(255);
    report = validate(malformed_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "required_source_routes[0].kind"),
           "unknown route kind was accepted as a required physical source");

    malformed_matrix = source_matrix;
    malformed_matrix.declared_omissions.push_back({
        malformed_matrix.required_source_routes.front().semantic_id,
        OmissionKind::source_route,
        "Contradictory test omission.",
    });
    report = validate(malformed_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "declared_omissions"),
           "source matrix allowed the same source to be required and omitted");

    InputBuilder load_builder;
    auto load_content = make_manifest_content(load_builder);
    auto load_engine = simulation_inputs(load_content).engine;
    auto load_scenario = simulation_inputs(load_content).scenario;
    const auto held_speed = std::get<HeldSpeed>(load_scenario.mode);
    load_scenario.mode = LoadTargetHeldCapture{
        held_speed.engine_speed_rpm,
        held_speed.initial_theta_rad,
        load_builder.resolved(-100000.0, "scenario.mode.target_net_bmep_pa"),
        load_builder.resolved(100.0, "scenario.mode.target_tolerance_pa"),
        load_builder.resolved(0.0, "scenario.mode.throttle_lower_bound_01"),
        load_builder.resolved(1.0, "scenario.mode.throttle_upper_bound_01"),
        load_builder.resolved(method("load-search-v1", 22),
                              "scenario.mode.search_method"),
    };
    expect(validate(load_scenario, load_builder.provenance).ok(),
           "finite signed negative net-BMEP target was rejected");
    expect(validate_for_engine(load_scenario, load_engine).ok(),
           "complete torque model rejected a load-target capture");
    simulation_inputs(load_content).scenario = load_scenario;

    auto inverted_bounds = load_scenario;
    std::get<LoadTargetHeldCapture>(inverted_bounds.mode)
        .throttle_lower_bound_01.value = 0.9;
    std::get<LoadTargetHeldCapture>(inverted_bounds.mode)
        .throttle_upper_bound_01.value = 0.1;
    expect(!validate(inverted_bounds, load_builder.provenance).ok(),
           "inverted load-search throttle bounds were accepted");

    auto incomplete_torque_engine = load_engine;
    incomplete_torque_engine.torque_capability.value.physical_net_complete = false;
    expect(!validate_for_engine(load_scenario, incomplete_torque_engine).ok(),
           "load-target capture accepted an incomplete net-torque model");

    const ReachabilityCandidate reached_candidate{
        1, 0.5, -2.0, 100.5, true,
    };
    const ReachedTarget reached{
        100.0,
        100.5,
        0.5,
        1.0,
        reached_candidate,
        {
            2,
            2,
            0.0,
            1.0,
            {
                reached_candidate,
                ReachabilityCandidate{2, 0.75, -1.0, 110.0, true},
            },
        },
    };
    expect(validate(reached).ok(), "valid reached-target evidence was rejected");

    const ReachabilityCandidate requested_reached_candidate{
        1, 0.5, -2.0, -99950.0, true,
    };
    const ReachedTarget requested_reached{
        -100000.0,
        -99950.0,
        50.0,
        100.0,
        requested_reached_candidate,
        {
            2,
            2,
            0.0,
            1.0,
            {
                requested_reached_candidate,
                ReachabilityCandidate{2, 0.75, -1.0, -99500.0, true},
            },
        },
    };
    const RenderManifest load_manifest{
        load_content,
        ExecutionFacts{
            "load-run",
            "2026-07-27T00:00:02Z",
            std::chrono::seconds{1},
            "linux-a",
            "cpu-a",
            8,
            2,
            1,
            1024,
        },
    };
    const RenderResult reached_result = RenderSuccess{load_manifest, requested_reached};
    expect(
        validate(reached_result, load_scenario, load_builder.provenance, source_matrix)
            .ok(),
        "load-target reached result did not bind to its request");

    auto wrong_reached_search_result = reached_result;
    std::get<RenderSuccess>(wrong_reached_search_result)
        .reached_target->search.requested_throttle_upper_bound_01 = 0.9;
    report = validate(wrong_reached_search_result, load_scenario,
                      load_builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "success.reached_target.search."
                                     "requested_throttle_upper_bound_01"),
           "reached search interval was allowed to drift from its request");

    auto almost_matching_reached_result = reached_result;
    auto &almost_matching_reached =
        *std::get<RenderSuccess>(almost_matching_reached_result).reached_target;
    almost_matching_reached.target_net_bmep_pa =
        std::nextafter(almost_matching_reached.target_net_bmep_pa, 0.0);
    report = validate(almost_matching_reached_result, load_scenario,
                      load_builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "success.reached_target"),
           "reached result copied a numerically close but nonidentical request "
           "target");

    auto unretained_selection = reached;
    unretained_selection.selected.stable_candidate_id = 99;
    report = validate(unretained_selection);
    expect(!report.ok() && has_issue(report, ContractIssueCode::dangling_reference,
                                     "search.selected"),
           "reachability result accepted a selected probe absent from evidence");

    auto nonoptimal_selection = reached;
    nonoptimal_selection.search.probes[1].achieved_net_bmep_pa = 100.1;
    report = validate(nonoptimal_selection);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "search.selected"),
           "reachability result ignored the deterministic nearest-probe order");

    const ReachabilityCandidate nearest{
        1, 1.0, -10.0, 80.0, true,
    };
    const UnreachableTarget unreachable{
        100.0,
        80.0,
        -20.0,
        1.0,
        nearest,
        {
            {ActiveBoundKind::throttle_upper, 1.0},
        },
        {
            2,
            2,
            0.0,
            1.0,
            {
                nearest,
                ReachabilityCandidate{2, 0.5, -5.0, 50.0, true},
            },
        },
        unreachable_context(),
        {},
    };
    expect(validate(unreachable).ok(),
           "valid typed unreachable-target result was rejected");

    auto inexact_active_bound = unreachable;
    inexact_active_bound.active_bounds[0].throttle_01 =
        std::nextafter(inexact_active_bound.active_bounds[0].throttle_01, 0.0);
    report = validate(inexact_active_bound);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "active_bounds[0].throttle_01"),
           "active throttle bound accepted a numerically close non-boundary value");

    auto matching_unreachable = unreachable;
    matching_unreachable.target_net_bmep_pa = -100000.0;
    matching_unreachable.achieved_net_bmep_pa = -100200.0;
    matching_unreachable.signed_error_pa = -200.0;
    matching_unreachable.tolerance_pa = 100.0;
    matching_unreachable.nearest_feasible.achieved_net_bmep_pa = -100200.0;
    matching_unreachable.search.probes[0] = matching_unreachable.nearest_feasible;
    matching_unreachable.search.probes[1].achieved_net_bmep_pa = -100500.0;
    matching_unreachable.context.profile_id = load_scenario.engine_profile_id;
    matching_unreachable.context.tolerances = {
        FailureTolerance{"net-bmep-pa", -100200.0, 100.0},
    };
    matching_unreachable.request = {
        simulation_inputs(load_content),
        load_builder.provenance,
        source_matrix,
        {},
    };
    const RenderResult unreachable_result = matching_unreachable;
    expect(validate(unreachable_result, load_scenario, load_builder.provenance,
                    source_matrix)
               .ok(),
           "load-target unreachable result did not bind to its request");

    auto wrong_unreachable_search_interval = matching_unreachable;
    wrong_unreachable_search_interval.search.requested_throttle_lower_bound_01 = 0.25;
    const RenderResult wrong_unreachable_search_result =
        wrong_unreachable_search_interval;
    report = validate(wrong_unreachable_search_result, load_scenario,
                      load_builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "unreachable.search."
                                     "requested_throttle_lower_bound_01"),
           "unreachable search interval was allowed to drift from its request");

    auto almost_matching_unreachable = matching_unreachable;
    almost_matching_unreachable.tolerance_pa =
        std::nextafter(almost_matching_unreachable.tolerance_pa, 0.0);
    const RenderResult almost_matching_unreachable_result = almost_matching_unreachable;
    report = validate(almost_matching_unreachable_result, load_scenario,
                      load_builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "unreachable"),
           "unreachable result copied a numerically close but nonidentical request "
           "tolerance");

    auto wrong_mode_unreachable = matching_unreachable;
    wrong_mode_unreachable.context.profile_id =
        simulation_inputs(content).scenario.engine_profile_id;
    const RenderResult wrong_mode_result = wrong_mode_unreachable;
    report = validate(wrong_mode_result, simulation_inputs(content).scenario,
                      builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "unreachable"),
           "held-speed request accepted an unreachable load-target result");

    auto inconsistent_unreachable = unreachable;
    inconsistent_unreachable.nearest_feasible =
        ReachabilityCandidate{3, 1.0, -10.0, 80.0, true};
    report = validate(inconsistent_unreachable);
    expect(!report.ok() && has_issue(report, ContractIssueCode::dangling_reference,
                                     "search.selected"),
           "unreachable result accepted a nearest probe absent from evidence");
}

} // namespace engine_sim_offline::contract::test
