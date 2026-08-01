#include "compile/scenario_resolver_internal.hpp"

#include "simulation/bounded_dyno_constraint.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/free_vehicle_method_registry.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace engine_sim_offline::compile::detail::scenario_resolution {

void ScenarioResolver::require_initial_speed(double mode_speed_rpm,
                                             std::string_view mode_path) {
    if (!approximately_equal(request_input_.authored_initial_engine_speed_rpm,
                             mode_speed_rpm)) {
        add(authoring::DiagnosticCode::inconsistent_value,
            "/initial_state/engine_speed",
            "initial engine speed must match " + std::string{mode_path} +
                " because the current executable has one speed authority at "
                "time zero");
    }
}

void ScenarioResolver::require_executable_initial_crank_angle() {
    const auto required = std::visit(
        [](const auto &profile) {
            const auto *crank = contract::find_output_crank(profile.core.mechanism);
            return crank == nullptr
                       ? std::optional<double>{}
                       : std::optional<double>{crank->crank_tdc_reference_rad.value};
        },
        context_.engine.physics_profile);
    if (!required.has_value()) {
        add(authoring::DiagnosticCode::internal_failure, "",
            "admitted engine has no unique resolved output crankshaft");
        return;
    }
    if (std::bit_cast<std::uint64_t>(initial_theta_rad_) !=
        std::bit_cast<std::uint64_t>(*required)) {
        add(authoring::DiagnosticCode::unsupported_capability,
            "/initial_state/crank_angle",
            "the current low-order executor requires the initial crank angle "
            "to equal the engine crank TDC reference exactly");
    }
}

void ScenarioResolver::compile_mode() {
    const bool contains_master_rod =
        std::ranges::any_of(context_.engine.cylinders, [](const auto &cylinder) {
            return cylinder.master_rod_attachment.has_value();
        });
    if (contains_master_rod &&
        !std::holds_alternative<authoring::ExternalSpeedMode>(document_.mode)) {
        add(authoring::DiagnosticCode::unsupported_capability, "/mode/type",
            "one-level master-rod engines currently admit only external_speed "
            "prescribed motion");
        return;
    }

    require_executable_initial_crank_angle();
    const auto resolve_engine_baseline_inertia = [&]() -> std::optional<double> {
        const auto *profile = std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &context_.engine.physics_profile);
        if (profile == nullptr) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "admitted engine has no low-order profile for mechanism-plan "
                "compilation");
            return std::nullopt;
        }
        auto result = simulation::compile_mechanism_kinematics_plan(context_.engine,
                                                                    profile->core);
        if (const auto *nested = std::get_if<contract::ValidationReport>(&result)) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "admitted engine mechanism could not produce its shared direct "
                "kinematics plan; issue_count=" +
                    std::to_string(nested->issues.size()));
            return std::nullopt;
        }
        const auto &shared =
            std::get<simulation::SharedMechanismKinematicsPlan>(result);
        const auto *direct = simulation::direct_mechanism_kinematics_plan(shared);
        if (direct == nullptr) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "admitted engine mechanism did not compile a direct kinematics "
                "plan");
            return std::nullopt;
        }
        return direct->cycle_mean_inertia.engine_equivalent_inertia_kg_m2;
    };
    std::visit(
        [&](const auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, authoring::FreeEngineMode>) {
                const auto &method = simulation::
                    nonnegative_speed_free_engine_centered_slider_crank_method_identity();
                const auto method_validation = contract::validate(method);
                if (!method_validation.ok()) {
                    append_contract_report(report_, method_validation,
                                           authoring::DiagnosticCode::internal_failure,
                                           "");
                }
                if (!std::isfinite(request_input_.authored_initial_engine_speed_rpm) ||
                    request_input_.authored_initial_engine_speed_rpm < 0.0 ||
                    (request_input_.authored_initial_engine_speed_rpm == 0.0 &&
                     std::signbit(request_input_.authored_initial_engine_speed_rpm))) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/initial_state/engine_speed",
                        "free-engine execution requires a finite canonical "
                        "nonnegative initial engine speed");
                }

                contract::FreeEngine free_engine;
                free_engine.initial_engine_speed_rpm.value =
                    request_input_.authored_initial_engine_speed_rpm;
                free_engine.initial_theta_rad.value = initial_theta_rad_;
                if (const auto inertia = resolve_engine_baseline_inertia();
                    inertia.has_value()) {
                    free_engine.engine_baseline_inertia_kg_m2.value = *inertia;
                }
                if (mode.attached_inertia.has_value()) {
                    free_engine.attached_inertia_kg_m2.value =
                        quantity(*mode.attached_inertia,
                                 authoring::QuantityDimension::moment_of_inertia,
                                 "/mode/attached_inertia");
                } else {
                    free_engine.attached_inertia_kg_m2.value = 0.0;
                }
                if (!std::isfinite(free_engine.attached_inertia_kg_m2.value) ||
                    free_engine.attached_inertia_kg_m2.value < 0.0 ||
                    std::signbit(free_engine.attached_inertia_kg_m2.value)) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/mode/attached_inertia",
                        "attached inertia must be finite and "
                        "positive-zero-or-positive");
                }
                free_engine.total_equivalent_inertia_kg_m2.value =
                    free_engine.engine_baseline_inertia_kg_m2.value +
                    free_engine.attached_inertia_kg_m2.value;
                if (!std::isfinite(free_engine.total_equivalent_inertia_kg_m2.value) ||
                    !(free_engine.total_equivalent_inertia_kg_m2.value > 0.0)) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/mode/attached_inertia",
                        "engine and attached inertia do not form a finite positive "
                        "total");
                }
                free_engine.throttle_01 = scalar_trajectory(
                    mode.throttle_01, "/mode/throttle_01", true, true);
                if (mode.external_resisting_torque.has_value()) {
                    free_engine.external_resisting_torque_nm =
                        torque_trajectory(*mode.external_resisting_torque,
                                          "/mode/external_resisting_torque");
                } else {
                    free_engine.external_resisting_torque_nm = {
                        contract::TrajectoryInterpolation::right_continuous_hold,
                        {{0.0, 0.0}},
                        {},
                    };
                }
                free_engine.crank_dynamics_method.value = method;
                scenario_.mode = std::move(free_engine);
            } else if constexpr (std::is_same_v<T, authoring::FreeVehicleMode>) {
                const auto &crank_method = simulation::
                    nonnegative_speed_free_engine_centered_slider_crank_method_identity();
                const auto &road_load_method =
                    simulation::forward_vehicle_road_load_method_identity();
                const auto &clutch_method =
                    simulation::bounded_clutch_coupling_method_identity();
                const auto &drivetrain_method =
                    simulation::bounded_forward_vehicle_drivetrain_method_identity();
                for (const auto *method : {&crank_method, &road_load_method,
                                           &clutch_method, &drivetrain_method}) {
                    const auto method_validation = contract::validate(*method);
                    if (!method_validation.ok()) {
                        append_contract_report(
                            report_, method_validation,
                            authoring::DiagnosticCode::internal_failure, "");
                    }
                }
                if (!std::isfinite(request_input_.authored_initial_engine_speed_rpm) ||
                    request_input_.authored_initial_engine_speed_rpm < 0.0 ||
                    (request_input_.authored_initial_engine_speed_rpm == 0.0 &&
                     std::signbit(request_input_.authored_initial_engine_speed_rpm))) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/initial_state/engine_speed",
                        "free-vehicle execution requires a finite canonical "
                        "nonnegative initial engine speed");
                }
                if (context_.rig == nullptr) {
                    add(authoring::DiagnosticCode::dangling_reference, "/mode/rig",
                        "free-vehicle rig reference does not resolve in the compiled "
                        "engine package");
                    return;
                }
                if (context_.rig->semantic_id.value != mode.rig.value) {
                    add(authoring::DiagnosticCode::dangling_reference, "/mode/rig",
                        "free-vehicle rig reference does not name the compiled rig");
                    return;
                }
                if (!context_.rig->vehicle.has_value() ||
                    !context_.rig->transmission.has_value()) {
                    add(authoring::DiagnosticCode::unsupported_capability, "/mode/rig",
                        "free-vehicle execution requires a rig with both a vehicle and "
                        "a transmission");
                    return;
                }
                const auto *operating_profile =
                    std::get_if<contract::LowOrderOperatingPointV1Profile>(
                        &context_.engine.physics_profile);
                if (operating_profile == nullptr) {
                    add(authoring::DiagnosticCode::unsupported_capability, "/mode/type",
                        "free-vehicle execution requires a low-order operating-"
                        "point engine profile");
                    return;
                }

                contract::FreeVehicle free_vehicle;
                free_vehicle.initial_engine_speed_rpm.value =
                    request_input_.authored_initial_engine_speed_rpm;
                free_vehicle.initial_theta_rad.value = initial_theta_rad_;
                free_vehicle.initial_vehicle_speed_m_s.value = quantity(
                    mode.initial_vehicle_speed, authoring::QuantityDimension::speed,
                    "/mode/initial_vehicle_speed");
                if (!std::isfinite(free_vehicle.initial_vehicle_speed_m_s.value) ||
                    free_vehicle.initial_vehicle_speed_m_s.value < 0.0 ||
                    (free_vehicle.initial_vehicle_speed_m_s.value == 0.0 &&
                     std::signbit(free_vehicle.initial_vehicle_speed_m_s.value))) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/mode/initial_vehicle_speed",
                        "initial vehicle speed must be finite canonical nonnegative");
                }

                if (const auto inertia = resolve_engine_baseline_inertia();
                    inertia.has_value()) {
                    free_vehicle.engine_baseline_inertia_kg_m2.value = *inertia;
                }

                const auto &source_rig = *context_.rig;
                const auto &source_vehicle = *source_rig.vehicle;
                const auto &source_transmission = *source_rig.transmission;
                free_vehicle.rig.id = {source_rig.runtime_id};
                free_vehicle.rig.semantic_id = source_rig.semantic_id;
                free_vehicle.rig.vehicle = {
                    {source_vehicle.runtime_id},
                    source_vehicle.semantic_id,
                    source_vehicle.mass_kg,
                    source_vehicle.drag_coefficient,
                    source_vehicle.frontal_area_m2,
                    source_vehicle.differential_ratio,
                    source_vehicle.tire_radius_m,
                    source_vehicle.rolling_resistance_force_n,
                    source_vehicle.maximum_service_brake_force_n,
                };
                free_vehicle.rig.transmission.id = {source_transmission.runtime_id};
                free_vehicle.rig.transmission.semantic_id =
                    source_transmission.semantic_id;
                free_vehicle.rig.transmission.maximum_clutch_torque_nm =
                    source_transmission.maximum_clutch_torque_nm;
                free_vehicle.rig.transmission.gears.reserve(
                    source_transmission.gears.size());
                for (const auto &gear : source_transmission.gears) {
                    free_vehicle.rig.transmission.gears.push_back({
                        {gear.runtime_id},
                        gear.authored_ordinal,
                        gear.semantic_id,
                        gear.ratio,
                    });
                }

                const auto find_gear = [&](std::string_view semantic_id)
                    -> const contract::ForwardGearSpec * {
                    const auto found = std::ranges::find(
                        free_vehicle.rig.transmission.gears, semantic_id,
                        [](const auto &gear) -> const std::string & {
                            return gear.semantic_id.value;
                        });
                    return found == free_vehicle.rig.transmission.gears.end() ? nullptr
                                                                              : &*found;
                };
                const auto resolve_gear =
                    [&](const std::optional<authoring::GearRef> &ref,
                        std::string_view path) -> std::optional<contract::GearId> {
                    if (!ref.has_value()) {
                        return std::nullopt;
                    }
                    const auto *gear = find_gear(ref->value);
                    if (gear == nullptr) {
                        add(authoring::DiagnosticCode::dangling_reference, path,
                            "gear reference '" + ref->value +
                                "' does not resolve in "
                                "the selected transmission");
                        return std::nullopt;
                    }
                    return gear->id;
                };

                free_vehicle.throttle_01 = scalar_trajectory(
                    mode.throttle_01, "/mode/throttle_01", true, true);
                free_vehicle.selected_gear.value = {{
                    "initial-gear",
                    0.0,
                    resolve_gear(mode.initial_gear, "/mode/initial_gear"),
                }};
                free_vehicle.clutch_engagement_01.value = {{
                    "initial-clutch-engagement",
                    0.0,
                    mode.initial_clutch_engagement_01,
                }};
                free_vehicle.service_brake_application_01.value = {{
                    "initial-service-brake-application",
                    0.0,
                    mode.initial_service_brake_application_01,
                }};

                std::optional<std::uint64_t> previous_gear_frame;
                std::optional<std::uint64_t> previous_clutch_frame;
                std::optional<std::uint64_t> previous_brake_frame;
                for (std::size_t index = 0; index < document_.events.size(); ++index) {
                    const auto &event = document_.events[index];
                    const auto base = "/events/" + std::to_string(index);
                    const bool drivetrain_event =
                        std::holds_alternative<authoring::SelectGearEvent>(
                            event.payload) ||
                        std::holds_alternative<authoring::SetClutchEngagementEvent>(
                            event.payload) ||
                        std::holds_alternative<
                            authoring::SetServiceBrakeApplicationEvent>(event.payload);
                    if (!drivetrain_event) {
                        continue;
                    }
                    if (!contract::is_valid_semantic_id(event.id.value)) {
                        add(authoring::DiagnosticCode::invalid_value, base + "/id",
                            "executable event IDs use the canonical lowercase "
                            "semantic-ID grammar");
                    }
                    const double time_s =
                        quantity(event.time, authoring::QuantityDimension::duration,
                                 base + "/time");
                    const auto frame = physics_frame(time_s, base + "/time");
                    if (!frame.has_value()) {
                        continue;
                    }
                    if (*frame >= request_input_.total_physics_frames) {
                        add(authoring::DiagnosticCode::unsupported_capability,
                            base + "/time",
                            "an event at or after the final physics step cannot be "
                            "enacted");
                        continue;
                    }

                    const auto append_or_replace =
                        [&](auto &lane, std::optional<std::uint64_t> &previous_frame,
                            auto point, std::string_view lane_name) {
                            if (previous_frame.has_value() &&
                                *previous_frame == *frame) {
                                add(authoring::DiagnosticCode::unsupported_capability,
                                    base + "/time",
                                    "two " + std::string{lane_name} +
                                        " events cannot occupy one physics boundary");
                                return;
                            }
                            previous_frame = *frame;
                            if (*frame == 0U) {
                                lane.front() = std::move(point);
                            } else {
                                lane.push_back(std::move(point));
                            }
                        };
                    if (const auto *selection =
                            std::get_if<authoring::SelectGearEvent>(&event.payload)) {
                        append_or_replace(
                            free_vehicle.selected_gear.value, previous_gear_frame,
                            contract::GearSelectionPoint{
                                event.id.value, time_s,
                                resolve_gear(selection->gear, base + "/payload/gear")},
                            "gear-selection");
                    } else if (const auto *clutch =
                                   std::get_if<authoring::SetClutchEngagementEvent>(
                                       &event.payload)) {
                        append_or_replace(
                            free_vehicle.clutch_engagement_01.value,
                            previous_clutch_frame,
                            contract::ScalarControlPoint{event.id.value, time_s,
                                                         clutch->engagement_01},
                            "clutch-engagement");
                    } else if (const auto *brake = std::get_if<
                                   authoring::SetServiceBrakeApplicationEvent>(
                                   &event.payload)) {
                        append_or_replace(
                            free_vehicle.service_brake_application_01.value,
                            previous_brake_frame,
                            contract::ScalarControlPoint{event.id.value, time_s,
                                                         brake->application_01},
                            "service-brake");
                    }
                }

                const bool brake_requested = std::ranges::any_of(
                    free_vehicle.service_brake_application_01.value,
                    [](const auto &point) { return point.value > 0.0; });
                if (brake_requested &&
                    !free_vehicle.rig.vehicle.maximum_service_brake_force_n
                         .has_value()) {
                    add(authoring::DiagnosticCode::unsupported_capability,
                        "/mode/initial_service_brake_application_01",
                        "nonzero service-brake application requires the selected "
                        "vehicle to declare maximum_service_brake_force");
                }

                free_vehicle.crank_dynamics_method.value = crank_method;
                free_vehicle.road_load_method.value = road_load_method;
                free_vehicle.clutch_coupling_method.value = clutch_method;
                free_vehicle.drivetrain_dynamics_method.value = drivetrain_method;
                scenario_.mode = std::move(free_vehicle);
            } else if constexpr (std::is_same_v<T, authoring::HeldSpeedMode>) {
                const auto speed_rpm = engine_speed_rpm(mode.target_engine_speed,
                                                        "/mode/target_engine_speed");
                require_initial_speed(speed_rpm, "held target_engine_speed");
                const auto throttle = scalar_trajectory(
                    mode.throttle_01, "/mode/throttle_01", true, false);
                if (throttle.points.empty()) {
                    return;
                }
                const auto constant =
                    std::ranges::all_of(throttle.points, [&](const auto &point) {
                        return point.value == throttle.points.front().value;
                    });
                if (!constant) {
                    add(authoring::DiagnosticCode::unsupported_capability,
                        "/mode/throttle_01",
                        "held-speed execution currently accepts one constant "
                        "throttle value");
                }
                scenario_.mode = contract::HeldSpeed{
                    {speed_rpm, {}},
                    {initial_theta_rad_, {}},
                    {throttle.points.front().value, {}},
                };
            } else if constexpr (std::is_same_v<T, authoring::HeldDynoMode>) {
                const auto &method =
                    simulation::bounded_held_dyno_constraint_method_identity();
                const auto method_validation = contract::validate(method);
                if (!method_validation.ok()) {
                    append_contract_report(report_, method_validation,
                                           authoring::DiagnosticCode::internal_failure,
                                           "");
                }
                auto target = speed_trajectory(mode.target_engine_speed,
                                               "/mode/target_engine_speed");
                if (!target.points.empty()) {
                    require_initial_speed(target.points.front().value,
                                          "held-dyno target_engine_speed at time zero");
                }
                auto target_lane = materialize_rpm_lane(target);
                auto throttle = scalar_trajectory(mode.throttle_01, "/mode/throttle_01",
                                                  true, true);
                contract::HeldDyno dyno;
                dyno.initial_engine_speed_rpm.value =
                    request_input_.authored_initial_engine_speed_rpm;
                dyno.initial_theta_rad.value = initial_theta_rad_;
                dyno.target_engine_speed_rpm = std::move(target_lane);
                dyno.throttle_01 = std::move(throttle);
                dyno.maximum_absorbing_torque_nm.value = quantity(
                    mode.maximum_absorbing_torque, authoring::QuantityDimension::torque,
                    "/mode/maximum_absorbing_torque");
                dyno.maximum_driving_torque_nm.value = quantity(
                    mode.maximum_driving_torque, authoring::QuantityDimension::torque,
                    "/mode/maximum_driving_torque");
                dyno.constraint_method.value = method;
                scenario_.mode = std::move(dyno);
            } else if constexpr (std::is_same_v<T, authoring::LoadTargetHeldMode>) {
                if (!context_.execution_methods.load_target_search.has_value()) {
                    add(authoring::DiagnosticCode::unsupported_capability, "/mode/type",
                        "this executable build does not provide a load-target "
                        "search method");
                    return;
                }
                const auto method_validation =
                    contract::validate(*context_.execution_methods.load_target_search);
                if (!method_validation.ok()) {
                    append_contract_report(report_, method_validation,
                                           authoring::DiagnosticCode::internal_failure,
                                           "");
                }
                const auto speed_rpm = engine_speed_rpm(mode.target_engine_speed,
                                                        "/mode/target_engine_speed");
                require_initial_speed(speed_rpm, "load-target target_engine_speed");
                scenario_.mode = contract::LoadTargetHeldCapture{
                    {speed_rpm, {}},
                    {initial_theta_rad_, {}},
                    {quantity(mode.target_net_bmep,
                              authoring::QuantityDimension::pressure,
                              "/mode/target_net_bmep"),
                     {}},
                    {quantity(mode.target_tolerance,
                              authoring::QuantityDimension::pressure,
                              "/mode/target_tolerance"),
                     {}},
                    {mode.throttle_lower_bound_01, {}},
                    {mode.throttle_upper_bound_01, {}},
                    {*context_.execution_methods.load_target_search, {}},
                };
            } else if constexpr (std::is_same_v<T, authoring::ExternalSpeedMode>) {
                auto source = speed_trajectory(mode.engine_speed, "/mode/engine_speed");
                if (!source.points.empty()) {
                    require_initial_speed(source.points.front().value,
                                          "external engine_speed at time zero");
                }
                auto throttle = scalar_trajectory(mode.throttle_01, "/mode/throttle_01",
                                                  true, true);
                auto rpm = materialize_rpm_lane(source);
                scenario_.mode = contract::PrescribedKinematicSweep{
                    {
                        std::move(rpm),
                        {initial_theta_rad_, {}},
                        {fixed_rate_post_step_rpm_method_identity(), {}},
                    },
                    std::move(throttle),
                };
            } else if constexpr (std::is_same_v<T, authoring::InertialDynoMode>) {
                for (
                    const auto *method :
                    {&simulation::rigid_crank_zoh_work_energy_method_identity(),
                     &simulation::
                         piecewise_linear_positive_speed_passive_brake_method_identity()}) {
                    const auto validation = contract::validate(*method);
                    if (!validation.ok()) {
                        append_contract_report(
                            report_, validation,
                            authoring::DiagnosticCode::internal_failure, "");
                    }
                }
                auto throttle = scalar_trajectory(mode.throttle_01, "/mode/throttle_01",
                                                  true, true);

                contract::InertialDyno dyno;
                dyno.initial_engine_speed_rpm.value =
                    request_input_.authored_initial_engine_speed_rpm;
                dyno.initial_theta_rad.value = initial_theta_rad_;
                dyno.equivalent_inertia_kg_m2.value =
                    quantity(mode.equivalent_inertia,
                             authoring::QuantityDimension::moment_of_inertia,
                             "/mode/equivalent_inertia");
                dyno.throttle_01 = std::move(throttle);
                dyno.brake_curve.reserve(mode.brake_curve.size());
                for (std::size_t index = 0; index < mode.brake_curve.size(); ++index) {
                    const auto path = "/mode/brake_curve/" + std::to_string(index);
                    dyno.brake_curve.push_back({
                        quantity(mode.brake_curve[index].engine_speed,
                                 authoring::QuantityDimension::angular_speed,
                                 path + "/engine_speed"),
                        quantity(mode.brake_curve[index].resisting_torque,
                                 authoring::QuantityDimension::torque,
                                 path + "/resisting_torque"),
                    });
                }
                dyno.crank_dynamics_method.value =
                    simulation::rigid_crank_zoh_work_energy_method_identity();
                dyno.target_engine_speed_rpm.value = engine_speed_rpm(
                    mode.target_engine_speed, "/mode/target_engine_speed");
                dyno.brake_torque_method.value = simulation::
                    piecewise_linear_positive_speed_passive_brake_method_identity();
                scenario_.mode = std::move(dyno);
            }
        },
        document_.mode);
}

} // namespace engine_sim_offline::compile::detail::scenario_resolution
