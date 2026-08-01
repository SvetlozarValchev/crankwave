#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool exact_legacy_method(
    const contract::ResolvedValue<contract::MethodIdentity> &value) {
    return value.value.id == "legacy_low_order_v1" && value.value.version == 1;
}

} // namespace

detail::LowOrderEngineCoreV1RuntimeFactory::MechanicsCompileResult
detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core,
    const contract::RenderScenario &scenario,
    SharedMechanismKinematicsPlan mechanism_plan,
    const KinematicScenarioSchedule &schedule) {
    ValidationReport report;
    require(report,
            schedule.sample_semantics() == contract::RpmSampleSemantics::post_step_rpm,
            ContractIssueCode::unsupported_value, "schedule.sample_semantics",
            "legacy mechanics requires post-step RPM schedule samples");
    constexpr double kStepSeconds = 1.0 / 10000.0;
    for (std::uint64_t index = 0; index < schedule.sample_count(); ++index) {
        const auto rpm = schedule.rpm_at_sample_offset(index);
        if (!rpm.has_value()) {
            require(report, false, ContractIssueCode::inconsistent_shape, "schedule",
                    "kinematic schedule lost an admitted RPM sample");
            break;
        }
        const double step_rotation = std::abs(-*rpm * kLegacyRpmScale * kStepSeconds);
        if (!(step_rotation < 4.0 * kLegacyPi)) {
            require(report, false, ContractIssueCode::unsupported_value,
                    "schedule.rpm[" + std::to_string(index) + "]",
                    "legacy crossing schedule requires less than one engine cycle "
                    "of rotation per physics step");
            break;
        }
    }
    if (!report.ok()) {
        return report;
    }

    auto result = compile_mechanics_with_control(engine, core, scenario,
                                                 std::move(mechanism_plan),
                                                 schedule.control_schedule(), true);
    if (auto *session = std::get_if<LegacyLowOrderMechanicsSession>(&result)) {
        session->kinematic_cursor_.emplace(schedule.fresh_cursor());
    }
    return result;
}

detail::LowOrderEngineCoreV1RuntimeFactory::MechanicsCompileResult
detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core,
    const contract::RenderScenario &scenario,
    SharedMechanismKinematicsPlan mechanism_plan,
    const ScenarioControlSchedule &schedule) {
    return compile_mechanics_with_control(engine, core, scenario,
                                          std::move(mechanism_plan), schedule, false);
}

detail::LowOrderEngineCoreV1RuntimeFactory::MechanicsCompileResult
detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics_with_control(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core,
    const contract::RenderScenario &scenario,
    SharedMechanismKinematicsPlan mechanism_plan,
    const ScenarioControlSchedule &schedule, const bool has_kinematic_schedule) {
    ValidationReport report;
    const auto *direct_plan = direct_mechanism_kinematics_plan(mechanism_plan);
    const auto *radial_plan =
        one_level_master_rod_mechanism_kinematics_plan(mechanism_plan);
    const bool plan_matches_source =
        mechanism_kinematics_plan_matches_source(mechanism_plan, engine, core);
    const bool prescribed_motion =
        std::holds_alternative<contract::PrescribedKinematicSweep>(scenario.mode);
    const bool exactly_one_crankshaft =
        engine.crankshafts.size() == 1U && core.mechanism.cranks.size() == 1U;
    require(report, prescribed_motion || exactly_one_crankshaft,
            ContractIssueCode::unsupported_value, "engine.crankshafts",
            "legacy torque-owning mechanics requires exactly one crankshaft; "
            "multiple crankshafts are admitted only with prescribed kinematics");
    if (radial_plan != nullptr) {
        require(report,
                has_kinematic_schedule &&
                    std::holds_alternative<contract::PrescribedKinematicSweep>(
                        scenario.mode),
                ContractIssueCode::unsupported_value, "mechanism_plan",
                "one-level master-rod mechanics requires a finite prescribed "
                "kinematic schedule");
    } else {
        require(report, direct_plan != nullptr, ContractIssueCode::unsupported_value,
                "mechanism_plan",
                "legacy mechanics requires one compiled direct mechanism plan");
    }
    require(report, plan_matches_source, ContractIssueCode::inconsistent_semantics,
            "mechanism_plan",
            direct_plan != nullptr
                ? "compiled direct mechanism plan does not exactly match its "
                  "resolved engine source"
                : "compiled one-level master-rod mechanism plan does not exactly "
                  "match its resolved engine source");
    if ((direct_plan == nullptr && radial_plan == nullptr) || !plan_matches_source ||
        (!prescribed_motion && !exactly_one_crankshaft) ||
        (radial_plan != nullptr &&
         (!has_kinematic_schedule ||
          !std::holds_alternative<contract::PrescribedKinematicSweep>(
              scenario.mode)))) {
        return report;
    }
    require(report, scenario.engine_profile_id == engine.profile_id.value,
            ContractIssueCode::inconsistent_semantics, "scenario.engine_profile_id",
            "mechanics session requires matching engine and scenario profiles");
    require(report, exact_legacy_method(engine.methods.mechanism),
            ContractIssueCode::unsupported_value, "engine.methods.mechanism",
            "mechanics session requires legacy_low_order_v1 version 1");
    require(report, exact_legacy_method(engine.methods.ignition),
            ContractIssueCode::unsupported_value, "engine.methods.ignition",
            "event scheduling requires legacy_low_order_v1 version 1");
    require(report, engine.cycle.value == contract::EngineCycle::four_stroke,
            ContractIssueCode::unsupported_value, "engine.cycle.value",
            "legacy mechanics requires a four-stroke engine");
    require(report, engine.ignition.value == contract::IgnitionKind::spark_ignition,
            ContractIssueCode::unsupported_value, "engine.ignition.value",
            "legacy event scheduling requires spark ignition");

    const auto *crank = contract::find_output_crank(core.mechanism);
    require(report, crank != nullptr, ContractIssueCode::dangling_reference,
            "engine.physics_profile.mechanism.output_crankshaft_id",
            "mechanics session requires one resolved output crankshaft");
    if (crank == nullptr) {
        return report;
    }
    require(report, schedule.rate() == scenario.rates.physics,
            ContractIssueCode::inconsistent_semantics, "schedule.rate",
            "compiled control schedule rate must equal the scenario physics rate");
    require(report, schedule.first_step_index() == 0,
            ContractIssueCode::inconsistent_semantics, "schedule.first_step_index",
            "legacy mechanics requires a control schedule beginning at physics "
            "step zero");
    const auto scenario_horizon = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    const auto finite_schedule =
        schedule.execution_extent().finite_physics_frame_count();
    if (finite_schedule.has_value()) {
        require(report,
                scenario_horizon.has_value() && *finite_schedule == *scenario_horizon,
                ContractIssueCode::inconsistent_shape, "schedule.execution_extent",
                "compiled finite control schedule length must equal the scenario "
                "physics horizon");
    } else {
        const bool supports_open_ended_execution =
            std::holds_alternative<contract::FreeEngine>(scenario.mode) ||
            std::holds_alternative<contract::HeldDyno>(scenario.mode) ||
            std::holds_alternative<contract::FreeVehicle>(scenario.mode);
        require(report, supports_open_ended_execution,
                ContractIssueCode::unsupported_value, "schedule.execution_extent",
                "open-ended mechanics execution is admitted only for FreeEngine, "
                "HeldDyno, or FreeVehicle");
    }
    require(report, scenario.rates.physics == contract::RationalRateHz{10000, 1},
            ContractIssueCode::unsupported_value, "scenario.rates.physics",
            "legacy_low_order_v1 mechanics requires exactly 10000 Hz");
    require(report,
            schedule.initial_theta_rad() == crank->crank_tdc_reference_rad.value,
            ContractIssueCode::unsupported_value, "schedule.initial_theta_rad",
            "legacy fresh state requires initial cycle angle equal to the crank TDC "
            "reference");
    bool limiter_enabled = false;
    if (!scenario.operating_state.value.empty()) {
        limiter_enabled = scenario.operating_state.value.front().state.limiter_enabled;
    }
    for (std::size_t index = 1; index < scenario.operating_state.value.size();
         ++index) {
        require(report,
                scenario.operating_state.value[index].state.limiter_enabled ==
                    limiter_enabled,
                ContractIssueCode::unsupported_value,
                "scenario.operating_state.value[" + std::to_string(index) +
                    "].state.limiter_enabled",
                "limiter_enabled must remain constant for the complete scenario; "
                "within-scenario limiter-policy transitions are not admitted");
    }

    require(report, std::isfinite(crank->crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.output_crankshaft.crank_tdc_reference_"
            "rad.value",
            "crank TDC reference must be finite");
    require(report, !core.gas_path.intakes.empty(), ContractIssueCode::missing_value,
            "engine.physics_profile.gas_path.intakes",
            "at least one intake profile is required");
    const auto *intake = core.gas_path.intakes.empty()
                             ? nullptr
                             : &core.gas_path.intakes.front().parameters;
    std::optional<LegacyThrottleControllerParameters> throttle_controller;
    std::visit(
        [&](const auto &controller) {
            if constexpr (requires { controller.minimum_engine_speed_rad_s; }) {
                const bool valid =
                    std::isfinite(controller.minimum_engine_speed_rad_s.value) &&
                    controller.minimum_engine_speed_rad_s.value >= 0.0 &&
                    finite_positive(controller.maximum_engine_speed_rad_s.value) &&
                    controller.minimum_engine_speed_rad_s.value <=
                        controller.maximum_engine_speed_rad_s.value &&
                    std::isfinite(controller.minimum_velocity_per_s.value) &&
                    std::isfinite(controller.maximum_velocity_per_s.value) &&
                    controller.minimum_velocity_per_s.value <=
                        controller.maximum_velocity_per_s.value &&
                    std::isfinite(controller.k_s.value) &&
                    controller.k_s.value >= 0.0 &&
                    std::isfinite(controller.k_d_per_s.value) &&
                    controller.k_d_per_s.value >= 0.0 &&
                    finite_positive(controller.gamma.value);
                require(report, valid, ContractIssueCode::invalid_value,
                        "engine.physics_profile.throttle_controller.governor",
                        "governor parameters are outside their admitted domain");
                if (valid) {
                    throttle_controller = LegacyGovernorControllerParameters{
                        controller.minimum_engine_speed_rad_s.value,
                        controller.maximum_engine_speed_rad_s.value,
                        controller.minimum_velocity_per_s.value,
                        controller.maximum_velocity_per_s.value,
                        controller.k_s.value,
                        controller.k_d_per_s.value,
                        controller.gamma.value,
                    };
                }
            } else {
                const bool valid = finite_positive(controller.gamma.value);
                require(report, valid, ContractIssueCode::invalid_value,
                        "engine.physics_profile.throttle_controller.direct.gamma",
                        "direct throttle gamma must be finite and positive");
                if (valid) {
                    throttle_controller = LegacyDirectThrottleControllerParameters{
                        controller.gamma.value};
                }
            }
        },
        core.throttle_controller);
    if (intake != nullptr) {
        require(report,
                std::isfinite(intake->idle_throttle_plate_position_01.value) &&
                    intake->idle_throttle_plate_position_01.value >= 0.0 &&
                    intake->idle_throttle_plate_position_01.value <= 1.0,
                ContractIssueCode::invalid_value,
                "engine.physics_profile.gas_path.intakes[0].parameters."
                "idle_throttle_plate_position_01",
                "idle plate position must be finite in [0,1]");
    }
    const auto &ignition = core.ignition;
    require(report, finite_positive(ignition.timing_curve_triangle_radius_rad_s.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.ignition.timing_curve_triangle_radius_rad_s.value",
            "ignition timing triangle radius must be finite and positive");
    require(report, !ignition.timing_curve.empty(), ContractIssueCode::missing_value,
            "engine.physics_profile.ignition.timing_curve",
            "ignition timing curve must be nonempty");
    std::vector<LegacyTrianglePoint> timing_curve;
    timing_curve.reserve(ignition.timing_curve.size());
    for (std::size_t index = 0; index < ignition.timing_curve.size(); ++index) {
        const auto &point = ignition.timing_curve[index];
        const bool valid =
            std::isfinite(point.angular_speed_rad_s.value) &&
            std::isfinite(point.timing_advance_rad.value) &&
            (index == 0 ||
             point.angular_speed_rad_s.value >
                 ignition.timing_curve[index - 1].angular_speed_rad_s.value);
        require(report, valid, ContractIssueCode::invalid_value,
                "engine.physics_profile.ignition.timing_curve[" +
                    std::to_string(index) + "]",
                "ignition timing points must be finite and strictly increasing");
        if (valid) {
            timing_curve.push_back(
                {point.angular_speed_rad_s.value, point.timing_advance_rad.value});
        }
    }
    require(report, finite_positive(ignition.limiter_speed_rpm.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.ignition.limiter_speed_rpm.value",
            "limiter speed must be finite and positive");
    require(report, finite_positive(ignition.limiter_hold_s.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.ignition.limiter_hold_s.value",
            "limiter hold must be finite and positive");

    if (!report.ok()) {
        return report;
    }

    auto cursor = schedule.fresh_cursor();
    return LegacyLowOrderMechanicsSession{
        std::move(cursor),
        std::nullopt,
        schedule.rate(),
        std::move(mechanism_plan),
        schedule.initial_theta_rad(),
        std::move(timing_curve),
        ignition.timing_curve_triangle_radius_rad_s.value,
        std::move(*throttle_controller),
        intake->idle_throttle_plate_position_01.value,
        ignition.limiter_speed_rpm.value,
        ignition.limiter_hold_s.value,
        engine.methods.mechanism.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
