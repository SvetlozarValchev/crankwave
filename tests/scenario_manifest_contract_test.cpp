#include "contract_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

void configure_stopped_free_engine(InputBuilder &builder, RenderScenario &scenario) {
    std::erase_if(builder.provenance.resolutions, [](const auto &resolution) {
        return resolution.parameter_path.starts_with("scenario.mode.");
    });

    scenario.scenario_id = "stopped-free-engine-contract";
    scenario.mode = FreeEngine{
        builder.resolved(0.0, "scenario.mode.initial_engine_speed_rpm"),
        builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        builder.resolved(0.20, "scenario.mode.engine_baseline_inertia_kg_m2"),
        builder.resolved(0.0, "scenario.mode.attached_inertia_kg_m2"),
        builder.resolved(0.20, "scenario.mode.total_equivalent_inertia_kg_m2"),
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.0}},
            builder.add_resolution("scenario.mode.throttle_01"),
        },
        {
            TrajectoryInterpolation::right_continuous_hold,
            {{0.0, 0.0}},
            builder.add_resolution("scenario.mode.external_resisting_torque_nm"),
        },
        builder.resolved(method("free-engine-contract-test-v1", 73),
                         "scenario.mode.crank_dynamics_method"),
    };
    scenario.mode_resolution_id = builder.add_resolution("scenario.mode.kind");
    scenario.preparation = FixedSettling{
        builder.resolved(0.0, "scenario.preparation.warm_up_duration_s"),
        builder.resolved(0.0, "scenario.preparation.settling_duration_s"),
    };
    scenario.audible_start_s.value = 0.0;
    scenario.audible_duration_s.value = scenario.total_duration_s.value;
    scenario.operating_state.value = {
        {
            "stopped",
            0.0,
            OperatingState{false, false, false, false, false},
        },
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

    const std::array canonical_hash_samples{0.0, 1.0, -0.0, 3000.5};
    const Sha256Digest expected_canonical_hash{{
        0x3f, 0x13, 0x22, 0x14, 0xc2, 0xed, 0xab, 0xb5, 0xc5, 0xff, 0x74,
        0xbd, 0x43, 0x06, 0x50, 0x3c, 0xb1, 0xf7, 0x04, 0x90, 0x71, 0x2a,
        0x98, 0xad, 0x23, 0xb3, 0xcd, 0x03, 0xe7, 0x06, 0xa6, 0x1c,
    }};
    expect(canonical_binary64_le_sha256(canonical_hash_samples) ==
               expected_canonical_hash,
           "canonical binary64 little-endian RPM hash changed");

    InputBuilder fixed_rpm_builder;
    auto fixed_rpm_content = make_manifest_content(fixed_rpm_builder);
    auto fixed_rpm_scenario = simulation_inputs(fixed_rpm_content).scenario;
    std::erase_if(fixed_rpm_builder.provenance.resolutions, [](const auto &resolution) {
        return resolution.parameter_path.starts_with("scenario.mode.");
    });
    const auto throttle_resolution_id =
        fixed_rpm_builder.add_resolution("scenario.mode.throttle_01");
    const auto rpm_resolution_id =
        fixed_rpm_builder.add_resolution("scenario.mode.trajectory.rpm");
    const auto fixed_rpm_frame_count = resolve_frame_index(
        fixed_rpm_scenario.total_duration_s.value, fixed_rpm_scenario.rates.physics);
    expect(fixed_rpm_frame_count.has_value(),
           "fixed-rate RPM test horizon did not resolve to physics frames");

    FixedRateRpmTrajectory fixed_rpm{
        fixed_rpm_scenario.rates.physics,
        0,
        RpmSampleSemantics::post_step_rpm,
        std::vector<double>(static_cast<std::size_t>(*fixed_rpm_frame_count), 3000.0),
        {},
        rpm_resolution_id,
    };
    fixed_rpm.samples_f64le_sha256 =
        canonical_binary64_le_sha256(fixed_rpm.post_step_rpm);
    PrescribedKinematicSweep fixed_sweep;
    fixed_sweep.trajectory.rpm = std::move(fixed_rpm);
    fixed_sweep.trajectory.initial_theta_rad =
        fixed_rpm_builder.resolved(0.0, "scenario.mode.trajectory.initial_theta_rad");
    fixed_sweep.trajectory.kinematic_resolution = fixed_rpm_builder.resolved(
        fixed_rate_rpm_method(), "scenario.mode.trajectory.kinematic_resolution");
    fixed_sweep.throttle_01 = {
        TrajectoryInterpolation::right_continuous_hold,
        {{0.0, 0.85}},
        throttle_resolution_id,
    };
    fixed_rpm_scenario.mode = std::move(fixed_sweep);
    fixed_rpm_scenario.mode_resolution_id =
        fixed_rpm_builder.add_resolution("scenario.mode.kind");
    fixed_rpm_scenario.preparation = FixedSettling{
        fixed_rpm_builder.resolved(1.0, "scenario.preparation.warm_up_duration_s"),
        fixed_rpm_builder.resolved(1.0, "scenario.preparation.settling_duration_s"),
    };
    expect(validate(fixed_rpm_scenario, fixed_rpm_builder.provenance).ok(),
           "valid owned fixed-rate RPM trajectory was rejected");

    auto keyframed_rpm_scenario = fixed_rpm_scenario;
    auto &keyframed_rpm =
        std::get<PrescribedKinematicSweep>(keyframed_rpm_scenario.mode).trajectory.rpm;
    const auto trajectory_resolution_id =
        std::get<FixedRateRpmTrajectory>(keyframed_rpm).resolution_id;
    keyframed_rpm = ScalarTrajectory{
        TrajectoryInterpolation::linear,
        {{0.0, 1000.0}, {3.0, 3000.0}},
        trajectory_resolution_id,
    };
    expect(validate(keyframed_rpm_scenario, fixed_rpm_builder.provenance).ok(),
           "existing keyframed RPM trajectory alternative was rejected");

    auto nonfinite_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(nonfinite_fixed_rpm.mode).trajectory.rpm)
        .post_step_rpm[0] = std::numeric_limits<double>::quiet_NaN();
    auto fixed_rpm_report = validate(nonfinite_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::invalid_value,
                         "post_step_rpm[0]"),
           "non-finite fixed-rate RPM sample was accepted");

    auto negative_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(negative_fixed_rpm.mode).trajectory.rpm)
        .post_step_rpm[0] = -1.0;
    fixed_rpm_report = validate(negative_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::invalid_value,
                         "post_step_rpm[0]"),
           "negative fixed-rate RPM sample was accepted");

    auto unhashed_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(unhashed_fixed_rpm.mode).trajectory.rpm)
        .post_step_rpm[0] += 1.0;
    fixed_rpm_report = validate(unhashed_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::inconsistent_semantics,
                         "samples_f64le_sha256"),
           "fixed-rate RPM samples were allowed to disagree with their hash");

    auto offset_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(offset_fixed_rpm.mode).trajectory.rpm)
        .first_step_index = 1;
    fixed_rpm_report = validate(offset_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::inconsistent_semantics,
                         "first_step_index"),
           "fixed-rate RPM trajectory with a nonzero first step was accepted");

    auto wrong_semantics_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(wrong_semantics_fixed_rpm.mode)
            .trajectory.rpm)
        .semantics = static_cast<RpmSampleSemantics>(255);
    fixed_rpm_report =
        validate(wrong_semantics_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::unsupported_value,
                         "semantics"),
           "unsupported fixed-rate RPM sample semantics were accepted");

    auto wrong_rate_fixed_rpm = fixed_rpm_scenario;
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(wrong_rate_fixed_rpm.mode).trajectory.rpm)
        .rate = {5000, 1};
    fixed_rpm_report = validate(wrong_rate_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::inconsistent_semantics,
                         "trajectory.rpm.rate"),
           "fixed-rate RPM trajectory rate was allowed to differ from physics");

    auto short_fixed_rpm = fixed_rpm_scenario;
    auto &short_samples =
        std::get<FixedRateRpmTrajectory>(
            std::get<PrescribedKinematicSweep>(short_fixed_rpm.mode).trajectory.rpm)
            .post_step_rpm;
    short_samples.pop_back();
    std::get<FixedRateRpmTrajectory>(
        std::get<PrescribedKinematicSweep>(short_fixed_rpm.mode).trajectory.rpm)
        .samples_f64le_sha256 = canonical_binary64_le_sha256(short_samples);
    fixed_rpm_report = validate(short_fixed_rpm, fixed_rpm_builder.provenance);
    expect(!fixed_rpm_report.ok() &&
               has_issue(fixed_rpm_report, ContractIssueCode::inconsistent_shape,
                         "post_step_rpm"),
           "fixed-rate RPM sample count was allowed to differ from physics frames");

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
    discrete_fixed_gap.preparation = FixedSettling{
        {1.0, ""},
        {0.9999999999995, ""},
    };
    std::get<FixedSettling>(discrete_fixed_gap.preparation).settling_duration_s.value =
        0.9999999999995;
    const auto fixed_grid_report = validate_clock_grid(discrete_fixed_gap);
    expect(!fixed_grid_report.ok() &&
               has_issue(fixed_grid_report, ContractIssueCode::inconsistent_semantics,
                         "physics.preparation"),
           "binary64-near fixed preparation left an undeclared frame gap");

    auto mismatched_sampling_grid = simulation_inputs(content).scenario;
    FixedHorizonCycleSampling grid_sampling;
    grid_sampling.fixed_preparation_horizon_s.value = 1.5;
    grid_sampling.trailing_complete_cycle_count.value = 1;
    mismatched_sampling_grid.preparation = grid_sampling;
    const auto sampling_grid_report = validate_clock_grid(mismatched_sampling_grid);
    expect(!sampling_grid_report.ok() &&
               has_issue(sampling_grid_report,
                         ContractIssueCode::inconsistent_semantics,
                         "physics.preparation.fixed_preparation_horizon_s"),
           "fixed-horizon sampling accepted a horizon away from audible start");

    InputBuilder sampling_builder;
    auto sampling_content = make_manifest_content(sampling_builder);
    auto sampling_scenario = simulation_inputs(sampling_content).scenario;
    std::erase_if(sampling_builder.provenance.resolutions, [](const auto &resolution) {
        return resolution.parameter_path.starts_with("scenario.preparation.") ||
               resolution.parameter_path.starts_with("scenario.mode.");
    });
    sampling_scenario.preparation = FixedHorizonCycleSampling{
        sampling_builder.resolved(fixed_horizon_cycle_sampling_method_identity(),
                                  "scenario.preparation.method"),
        sampling_builder.resolved(2.0,
                                  "scenario.preparation.fixed_preparation_horizon_s"),
        sampling_builder.resolved<std::uint32_t>(
            32, "scenario.preparation.trailing_complete_cycle_count"),
    };
    sampling_scenario.mode = HeldSpeed{
        sampling_builder.resolved(3000.0, "scenario.mode.engine_speed_rpm"),
        sampling_builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        sampling_builder.resolved(0.85, "scenario.mode.throttle_01"),
    };
    sampling_scenario.mode_resolution_id =
        sampling_builder.add_resolution("scenario.mode.kind");
    expect(validate(sampling_scenario, sampling_builder.provenance).ok(),
           "valid identified fixed-horizon preparation was rejected");
    auto wrong_sampling_method = sampling_scenario;
    std::get<FixedHorizonCycleSampling>(wrong_sampling_method.preparation)
        .method.value.id = "other-sampling-v1";
    expect(!validate(wrong_sampling_method, sampling_builder.provenance).ok(),
           "unimplemented fixed-horizon method was accepted");
    auto wrong_sampling_version = sampling_scenario;
    std::get<FixedHorizonCycleSampling>(wrong_sampling_version.preparation)
        .method.value.version = 2;
    expect(!validate(wrong_sampling_version, sampling_builder.provenance).ok(),
           "wrong fixed-horizon method version was accepted");
    auto wrong_sampling_digest = sampling_scenario;
    auto &wrong_digest =
        std::get<FixedHorizonCycleSampling>(wrong_sampling_digest.preparation)
            .method.value.configuration_sha256;
    wrong_digest.bytes[0] =
        static_cast<std::uint8_t>(wrong_digest.bytes[0] ^ UINT8_C(1));
    expect(!validate(wrong_sampling_digest, sampling_builder.provenance).ok(),
           "wrong fixed-horizon method configuration digest was accepted");
    auto empty_sampling = sampling_scenario;
    std::get<FixedHorizonCycleSampling>(empty_sampling.preparation)
        .trailing_complete_cycle_count.value = 0;
    expect(!validate(empty_sampling, sampling_builder.provenance).ok(),
           "zero trailing complete-cycle count was accepted");
    auto short_sampling = sampling_scenario;
    std::get<FixedHorizonCycleSampling>(short_sampling.preparation)
        .fixed_preparation_horizon_s.value = 1.0;
    short_sampling.audible_start_s.value = 1.0;
    short_sampling.audible_duration_s.value = 2.0;
    expect(!validate(short_sampling, sampling_builder.provenance).ok(),
           "horizon shorter than the conservative complete-cycle bound was accepted");

    expect(validate_for_engine(simulation_inputs(content).scenario,
                               simulation_inputs(content).engine)
               .ok(),
           "valid scenario and engine pairing was rejected");
    auto wrong_fuel_scenario = simulation_inputs(content).scenario;
    wrong_fuel_scenario.fuel.lower_heating_value_j_per_kg.value += 1.0;
    expect(!validate_for_engine(wrong_fuel_scenario, simulation_inputs(content).engine)
                .ok(),
           "scenario fuel was allowed to contradict executable engine fuel");

    auto operating_engine = simulation_inputs(content).engine;
    auto operating_scenario = sampling_scenario;
    operating_scenario.operating_state.value = {
        {
            "fired",
            0.0,
            OperatingState{true, true, false, true, false},
        },
    };
    expect(validate_for_engine(operating_scenario, operating_engine).ok(),
           "valid operating-profile held-speed scenario was rejected");

    expect(validate_for_engine(fixed_rpm_scenario, operating_engine).ok(),
           "operating profile rejected a direct prescribed sweep with fixed "
           "settling");
    auto master_rod_mode_engine = operating_engine;
    master_rod_mode_engine.cylinders.front().master_rod_attachment =
        MasterRodAttachmentSpec{
            CylinderId{2},
            master_rod_mode_engine.cylinders.front().bore_m,
        };
    const auto master_rod_held_report =
        validate_for_engine(operating_scenario, master_rod_mode_engine);
    expect(has_issue(master_rod_held_report, ContractIssueCode::unsupported_value,
                     "mode"),
           "programmatic master-rod engine admitted non-prescribed motion");
    expect(validate_for_engine(fixed_rpm_scenario, master_rod_mode_engine).ok(),
           "programmatic master-rod mode gate rejected prescribed motion");
    auto sampled_prescribed_scenario = fixed_rpm_scenario;
    sampled_prescribed_scenario.preparation = operating_scenario.preparation;
    expect(!validate_for_engine(sampled_prescribed_scenario, operating_engine).ok(),
           "operating profile accepted fixed-horizon evidence for a prescribed "
           "sweep");
    auto fixed_operating_preparation = operating_scenario;
    fixed_operating_preparation.preparation = FixedSettling{};
    expect(!validate_for_engine(fixed_operating_preparation, operating_engine).ok(),
           "operating profile accepted fixed preparation");
    auto wrong_operating_oil = operating_scenario;
    wrong_operating_oil.initial_thermal_state.oil_temperature_k.value =
        std::nextafter(370.0, 371.0);
    expect(!validate_for_engine(wrong_operating_oil, operating_engine).ok(),
           "operating profile accepted a nonidentical oil condition");
    auto engaged_operating_starter = operating_scenario;
    engaged_operating_starter.operating_state.value.front().state.starter_enabled =
        true;
    expect(!validate_for_engine(engaged_operating_starter, operating_engine).ok(),
           "operating profile accepted an enabled starter");
    auto active_operating_limiter = operating_scenario;
    active_operating_limiter.operating_state.value.front().state.limiter_enabled = true;
    expect(!validate_for_engine(active_operating_limiter, operating_engine).ok(),
           "operating profile accepted an enabled limiter");
    auto missing_operating_state = operating_scenario;
    missing_operating_state.operating_state.value.clear();
    expect(!validate_for_engine(missing_operating_state, operating_engine).ok(),
           "operating profile accepted an empty operating-state journal");

    InputBuilder stopped_free_engine_builder;
    auto stopped_free_engine_content =
        make_manifest_content(stopped_free_engine_builder);
    const auto stopped_free_engine =
        simulation_inputs(stopped_free_engine_content).engine;
    auto stopped_free_engine_scenario =
        simulation_inputs(stopped_free_engine_content).scenario;
    const auto fixed_horizon_preparation = stopped_free_engine_scenario.preparation;
    configure_stopped_free_engine(stopped_free_engine_builder,
                                  stopped_free_engine_scenario);

    expect(
        validate(stopped_free_engine_scenario, stopped_free_engine_builder.provenance)
            .ok(),
        "canonical zero-speed free engine with immediate zero settling was "
        "rejected");
    expect(validate_for_engine(stopped_free_engine_scenario, stopped_free_engine).ok(),
           "free engine rejected ignition and fuel off while starter and dyno were "
           "off");

    auto warm_free_engine = stopped_free_engine_scenario;
    std::get<FreeEngine>(warm_free_engine.mode).initial_engine_speed_rpm.value = 3000.0;
    warm_free_engine.preparation = fixed_horizon_preparation;
    std::get<FixedHorizonCycleSampling>(warm_free_engine.preparation)
        .fixed_preparation_horizon_s.value = 1.5;
    warm_free_engine.audible_start_s.value = 2.0;
    warm_free_engine.audible_duration_s.value = 1.0;
    warm_free_engine.total_duration_s.value = 3.0;
    expect(validate(warm_free_engine, stopped_free_engine_builder.provenance).ok(),
           "positive-speed free engine rejected release before audible start");

    auto equal_release_and_audition = warm_free_engine;
    std::get<FixedHorizonCycleSampling>(equal_release_and_audition.preparation)
        .fixed_preparation_horizon_s.value = 2.0;
    expect(validate(equal_release_and_audition, stopped_free_engine_builder.provenance)
               .ok(),
           "positive-speed free engine rejected release at audible start");

    auto late_warm_release = warm_free_engine;
    std::get<FixedHorizonCycleSampling>(late_warm_release.preparation)
        .fixed_preparation_horizon_s.value = 2.5;
    auto free_engine_report =
        validate(late_warm_release, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "preparation.fixed_preparation_horizon_s.value"),
           "positive-speed free engine admitted release after audible start");

    auto negative_zero_initial_speed = stopped_free_engine_scenario;
    std::get<FreeEngine>(negative_zero_initial_speed.mode)
        .initial_engine_speed_rpm.value = -0.0;
    free_engine_report =
        validate(negative_zero_initial_speed, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::invalid_value, "mode"),
           "free engine accepted negative-zero initial speed");

    auto negative_zero_warm_up = stopped_free_engine_scenario;
    std::get<FixedSettling>(negative_zero_warm_up.preparation)
        .warm_up_duration_s.value = -0.0;
    free_engine_report =
        validate(negative_zero_warm_up, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "preparation"),
           "stopped free engine accepted negative-zero warm-up duration");

    auto negative_zero_settling = stopped_free_engine_scenario;
    std::get<FixedSettling>(negative_zero_settling.preparation)
        .settling_duration_s.value = -0.0;
    free_engine_report =
        validate(negative_zero_settling, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "preparation"),
           "stopped free engine accepted negative-zero settling duration");

    auto negative_zero_audible_start = stopped_free_engine_scenario;
    negative_zero_audible_start.audible_start_s.value = -0.0;
    free_engine_report =
        validate(negative_zero_audible_start, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "preparation"),
           "stopped free engine accepted negative-zero audible start");

    auto zero_speed_fixed_horizon = stopped_free_engine_scenario;
    zero_speed_fixed_horizon.preparation = fixed_horizon_preparation;
    zero_speed_fixed_horizon.audible_start_s.value = 2.0;
    zero_speed_fixed_horizon.audible_duration_s.value = 1.0;
    free_engine_report =
        validate(zero_speed_fixed_horizon, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "preparation"),
           "zero-speed free engine accepted fixed-horizon preparation");

    auto positive_speed_fixed_settling = stopped_free_engine_scenario;
    std::get<FreeEngine>(positive_speed_fixed_settling.mode)
        .initial_engine_speed_rpm.value = 1500.0;
    free_engine_report =
        validate(positive_speed_fixed_settling, stopped_free_engine_builder.provenance);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::unsupported_value,
                         "preparation"),
           "positive-speed free engine accepted fixed settling");

    auto starter_enabled_free_engine = stopped_free_engine_scenario;
    starter_enabled_free_engine.operating_state.value.front().state.starter_enabled =
        true;
    free_engine_report =
        validate_for_engine(starter_enabled_free_engine, stopped_free_engine);
    expect(!free_engine_report.ok() &&
               has_issue(free_engine_report, ContractIssueCode::inconsistent_semantics,
                         "operating_state.value"),
           "free engine admitted starter engagement before starter capability");

    auto cranking_engine = stopped_free_engine;
    auto &cranking_profile =
        std::get<LowOrderOperatingPointV1Profile>(cranking_engine.physics_profile);
    cranking_profile.starter.type.value = StarterCapabilityType::cranking;
    cranking_profile.starter.maximum_torque_nm.value = 150.0;
    cranking_profile.starter.target_speed_rad_s.value = 27.2271363;
    expect(validate_for_engine(starter_enabled_free_engine, cranking_engine).ok(),
           "stopped free engine rejected an engaged compiled cranking starter");

    expect(validate(content, builder.provenance, source_matrix).ok(),
           "valid render manifest content was rejected");

    ValidationReport report;
    for (const auto schema_version : {UINT32_C(4), UINT32_C(5), UINT32_C(7)}) {
        auto unsupported_manifest_schema = content;
        unsupported_manifest_schema.schema_version = schema_version;
        report =
            validate(unsupported_manifest_schema, builder.provenance, source_matrix);
        expect(!report.ok() && has_issue(report, ContractIssueCode::unsupported_value,
                                         "schema_version"),
               "unsupported simulation manifest schema was accepted");
    }

    auto mismatched_asset_evidence = content;
    simulation_inputs(mismatched_asset_evidence)
        .presentation.assets[0]
        .content_sha256.value = digest(31);
    report = validate(mismatched_asset_evidence, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "content_sha256"),
           "presentation asset digest was allowed to disagree with its evidence");

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
    const RenderResult held_success =
        RenderSuccess{first, std::nullopt, std::nullopt, std::nullopt};
    expect(!validate(held_success, simulation_inputs(content).scenario, Sha256Digest{},
                     builder.provenance, source_matrix)
                .ok(),
           "held-speed success without operating-point evidence was accepted");

    const RenderFailure runtime_failure{
        FailureContext{
            FailureKind::incomplete_source_route,
            "render-pipeline-not-admitted",
            "render-session-v1",
            simulation_inputs(content).scenario.engine_profile_id,
            0,
            0,
            0.0,
            0.0,
            simulation_inputs(content).engine.id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "no complete capture-to-artifact route is admitted",
            "none",
            {},
        },
        {
            simulation_inputs(content),
            builder.provenance,
            source_matrix,
            {},
        },
        {},
    };
    const RenderResult runtime_failure_result = runtime_failure;
    expect(validate(runtime_failure_result, simulation_inputs(content).scenario,
                    Sha256Digest{}, builder.provenance, source_matrix)
               .ok(),
           "valid runtime failure did not retain an admitted request");
    auto forged_randomness_failure = runtime_failure;
    forged_randomness_failure.request.resolved_inputs.randomness.seed_namespace_id
        .value += ".forged";
    report = validate(RenderResult{forged_randomness_failure},
                      simulation_inputs(content).scenario, Sha256Digest{},
                      builder.provenance, source_matrix);
    expect(report.ok(),
           "lower-level result validation rejected a structurally admitted retained "
           "randomness policy without an external policy argument");

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
    std::erase_if(load_builder.provenance.resolutions, [](const auto &resolution) {
        return resolution.parameter_path.starts_with("scenario.mode.");
    });
    load_scenario.mode = LoadTargetHeldCapture{
        load_builder.resolved(3000.0, "scenario.mode.engine_speed_rpm"),
        load_builder.resolved(0.0, "scenario.mode.initial_theta_rad"),
        load_builder.resolved(-100000.0, "scenario.mode.target_net_bmep_pa"),
        load_builder.resolved(100.0, "scenario.mode.target_tolerance_pa"),
        load_builder.resolved(0.0, "scenario.mode.throttle_lower_bound_01"),
        load_builder.resolved(1.0, "scenario.mode.throttle_upper_bound_01"),
        load_builder.resolved(method("load-search-v1", 22),
                              "scenario.mode.search_method"),
    };
    load_scenario.mode_resolution_id =
        load_builder.add_resolution("scenario.mode.kind");
    expect(validate(load_scenario, load_builder.provenance).ok(),
           "finite signed negative net-BMEP target was rejected");
    expect(!validate_for_engine(load_scenario, load_engine).ok(),
           "operating-point profile accepted an unsupported load-target capture");
    simulation_inputs(load_content).scenario = load_scenario;

    auto inverted_bounds = load_scenario;
    std::get<LoadTargetHeldCapture>(inverted_bounds.mode)
        .throttle_lower_bound_01.value = 0.9;
    std::get<LoadTargetHeldCapture>(inverted_bounds.mode)
        .throttle_upper_bound_01.value = 0.1;
    expect(!validate(inverted_bounds, load_builder.provenance).ok(),
           "inverted load-search throttle bounds were accepted");

    auto incomplete_torque_engine = load_engine;
    incomplete_torque_engine.torque_capability.value.cycle_mean_net_shaft = {
        Availability::available,
        Completeness::incomplete,
        indicated_gas_torque_term_mask(),
        known_torque_term_mask() & ~indicated_gas_torque_term_mask(),
    };
    expect(!validate_for_engine(load_scenario, incomplete_torque_engine).ok(),
           "load-target capture accepted an incomplete net-torque model");

    auto inertial_scenario = load_scenario;
    inertial_scenario.mode = InertialDyno{
        {1000.0, ""},
        {0.0, ""},
        {0.25, ""},
        ScalarTrajectory{
            TrajectoryInterpolation::linear,
            {
                {0.0, 0.5},
                {inertial_scenario.total_duration_s.value, 1.0},
            },
            "",
        },
        {
            {0.0, 0.0},
            {1000.0, 10.0},
        },
        "",
        {method("inertial-dyno-v1", 23), ""},
        {1800.0, ""},
        {method("piecewise-linear-passive-brake-v1", 24), ""},
    };
    auto inertial_capable_engine = load_engine;
    expect(validate_for_engine(inertial_scenario, inertial_capable_engine).ok(),
           "synthetic complete instantaneous capability rejected an inertial dyno");

    auto cycle_complete_instantaneous_incomplete = inertial_capable_engine;
    cycle_complete_instantaneous_incomplete.torque_capability.value
        .instantaneous_net_shaft = {
        Availability::available,
        Completeness::incomplete,
        indicated_gas_torque_term_mask(),
        known_torque_term_mask() & ~indicated_gas_torque_term_mask(),
    };
    expect(
        !validate_for_engine(inertial_scenario, cycle_complete_instantaneous_incomplete)
             .ok(),
        "inertial dyno accepted cycle-complete but instantaneous-incomplete "
        "net torque");
    auto inertia_missing = inertial_capable_engine;
    inertia_missing.torque_capability.value.equivalent_inertia_available = false;
    expect(!validate_for_engine(inertial_scenario, inertia_missing).ok(),
           "inertial dyno accepted missing equivalent inertia");

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
    const RenderResult reached_result =
        RenderSuccess{load_manifest, requested_reached, std::nullopt, std::nullopt};
    expect(!validate(reached_result, load_scenario, Sha256Digest{},
                     load_builder.provenance, source_matrix)
                .ok(),
           "unsupported operating-point mode published a load-target result");

    auto wrong_reached_search_result = reached_result;
    std::get<RenderSuccess>(wrong_reached_search_result)
        .reached_target->search.requested_throttle_upper_bound_01 = 0.9;
    report = validate(wrong_reached_search_result, load_scenario, Sha256Digest{},
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
    report = validate(almost_matching_reached_result, load_scenario, Sha256Digest{},
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
    expect(!validate(unreachable_result, load_scenario, Sha256Digest{},
                     load_builder.provenance, source_matrix)
                .ok(),
           "unsupported operating-point mode published a load-target failure");

    auto wrong_unreachable_search_interval = matching_unreachable;
    wrong_unreachable_search_interval.search.requested_throttle_lower_bound_01 = 0.25;
    const RenderResult wrong_unreachable_search_result =
        wrong_unreachable_search_interval;
    report = validate(wrong_unreachable_search_result, load_scenario, Sha256Digest{},
                      load_builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "unreachable.search."
                                     "requested_throttle_lower_bound_01"),
           "unreachable search interval was allowed to drift from its request");

    auto almost_matching_unreachable = matching_unreachable;
    almost_matching_unreachable.tolerance_pa =
        std::nextafter(almost_matching_unreachable.tolerance_pa, 0.0);
    const RenderResult almost_matching_unreachable_result = almost_matching_unreachable;
    report = validate(almost_matching_unreachable_result, load_scenario, Sha256Digest{},
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
                      Sha256Digest{}, builder.provenance, source_matrix);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "unreachable"),
           "non-load-target request accepted an unreachable load-target result");

    auto inconsistent_unreachable = unreachable;
    inconsistent_unreachable.nearest_feasible =
        ReachabilityCandidate{3, 1.0, -10.0, 80.0, true};
    report = validate(inconsistent_unreachable);
    expect(!report.ok() && has_issue(report, ContractIssueCode::dangling_reference,
                                     "search.selected"),
           "unreachable result accepted a nearest probe absent from evidence");
}

} // namespace engine_sim_offline::contract::test
