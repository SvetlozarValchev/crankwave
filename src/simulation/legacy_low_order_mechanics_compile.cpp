#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
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

    auto result =
        compile_mechanics(engine, core, scenario, schedule.control_schedule());
    if (auto *session = std::get_if<LegacyLowOrderMechanicsSession>(&result)) {
        session->kinematic_cursor_.emplace(schedule.fresh_cursor());
    }
    return result;
}

detail::LowOrderEngineCoreV1RuntimeFactory::MechanicsCompileResult
detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core,
    const contract::RenderScenario &scenario, const ScenarioControlSchedule &schedule) {
    ValidationReport report;
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

    const auto &crank = core.mechanism.crank;
    require(report, schedule.rate() == scenario.rates.physics,
            ContractIssueCode::inconsistent_semantics, "schedule.rate",
            "compiled control schedule rate must equal the scenario physics rate");
    require(report, schedule.first_step_index() == 0,
            ContractIssueCode::inconsistent_semantics, "schedule.first_step_index",
            "legacy mechanics requires a control schedule beginning at physics "
            "step zero");
    const auto scenario_horizon = contract::resolve_frame_index(
        scenario.total_duration_s.value, scenario.rates.physics);
    require(report,
            scenario_horizon.has_value() &&
                schedule.sample_count() == *scenario_horizon,
            ContractIssueCode::inconsistent_shape, "schedule.sample_count",
            "compiled control schedule length must equal the scenario physics "
            "horizon");
    require(report, scenario.rates.physics == contract::RationalRateHz{10000, 1},
            ContractIssueCode::unsupported_value, "scenario.rates.physics",
            "legacy_low_order_v1 mechanics requires exactly 10000 Hz");
    require(report, schedule.initial_theta_rad() == crank.crank_tdc_reference_rad.value,
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

    require(report, std::isfinite(crank.crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank.crank_tdc_reference_rad.value",
            "crank TDC reference must be finite");
    require(report,
            core.mechanism.cylinders.size() == engine.cylinders.size() &&
                !core.mechanism.cylinders.empty(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "mechanism cylinders must match the nonempty engine cylinder order");
    require(report,
            core.mechanism.cylinders.size() <= std::numeric_limits<std::uint8_t>::max(),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cylinders",
            "event ordinals support at most 255 cylinders per mechanics session");

    std::vector<LegacyLowOrderMechanicsSession::CylinderModel> cylinders;
    cylinders.reserve(core.mechanism.cylinders.size());
    std::unordered_set<std::uint32_t> cylinder_ids;
    for (std::size_t index = 0; index < core.mechanism.cylinders.size(); ++index) {
        const auto &assembly = core.mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto path =
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) + "]";
        const bool identity_valid =
            assembly.topology.cylinder_id.valid() && index < engine.cylinders.size() &&
            assembly.topology.cylinder_id == engine.cylinders[index].id &&
            cylinder_ids.insert(assembly.topology.cylinder_id.value).second;
        require(report, identity_valid, ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "mechanism cylinder identity/order must be unique and match the "
                "engine topology");
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == assembly.topology.exhaust_route_id;
            });
        const bool exhaust_route_valid =
            assembly.topology.exhaust_route_id.valid() &&
            route != engine.routes.end() &&
            route->kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust_route_valid, ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "mechanism cylinder requires an existing exhaust-outlet route");

        const double bore_m = parameters.bore_m.value;
        const double crank_radius_m = parameters.crank_radius_m.value;
        const double rod_length_m = parameters.connecting_rod_length_m.value;
        const double deck_height_m = parameters.deck_height_m.value;
        const double compression_height_m =
            parameters.piston_compression_height_m.value;
        const double head_volume_m3 = parameters.head_chamber_volume_m3.value;
        const double piston_displacement_m3 =
            parameters.piston_displacement_term_m3.value;
        const double journal_angle_rad = parameters.journal_angle_rad.value;
        const double ignition_wire_angle_rad = parameters.ignition_wire_angle_rad.value;

        const bool numeric_inputs_valid =
            finite_positive(bore_m) && finite_positive(crank_radius_m) &&
            finite_positive(rod_length_m) && crank_radius_m < rod_length_m &&
            finite_positive(deck_height_m) && finite_positive(compression_height_m) &&
            finite_positive(head_volume_m3) && std::isfinite(piston_displacement_m3) &&
            std::isfinite(journal_angle_rad) && std::isfinite(ignition_wire_angle_rad);
        require(report, numeric_inputs_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "centered slider-crank inputs must be finite, positive where "
                "required, and have crank radius below rod length");
        if (!numeric_inputs_valid || !identity_valid || !exhaust_route_valid ||
            !std::isfinite(crank.crank_tdc_reference_rad.value)) {
            continue;
        }

        const auto geometry = derive_legacy_cylinder_geometry(
            bore_m, crank_radius_m, rod_length_m, deck_height_m,
            compression_height_m, head_volume_m3, piston_displacement_m3);
        const double geometric_tdc_rad = legacy_wrap_2pi(
            crank.crank_tdc_reference_rad.value + journal_angle_rad - kLegacyPi / 2.0);
        const bool derived_valid = finite_positive(geometry.piston_area_m2) &&
                                   finite_positive(geometry.clearance_volume_m3) &&
                                   std::isfinite(geometric_tdc_rad);
        require(report, derived_valid, ContractIssueCode::invalid_value, path,
                "compiled slider-crank area, clearance, and phase must be valid");
        if (!derived_valid) {
            continue;
        }

        cylinders.push_back({
            {
                assembly.topology.cylinder_id,
                geometric_tdc_rad,
                geometry.piston_area_m2,
                crank_radius_m,
                rod_length_m,
                geometry.clearance_volume_m3,
                ignition_wire_angle_rad,
            },
            assembly.topology.exhaust_route_id,
        });
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
        crank.crank_tdc_reference_rad.value,
        schedule.initial_theta_rad(),
        std::move(cylinders),
        std::move(timing_curve),
        ignition.timing_curve_triangle_radius_rad_s.value,
        ignition.limiter_speed_rpm.value,
        ignition.limiter_hold_s.value,
        limiter_enabled,
        engine.methods.mechanism.value.id,
        engine.profile_id.value,
        scenario.scenario_id,
        engine.id,
    };
}

} // namespace engine_sim_offline::simulation
