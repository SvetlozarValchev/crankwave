#include "compile/scenario_resolver_internal.hpp"

#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/inertial_dyno_method_registry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    const double required = std::visit(
        [](const auto &profile) {
            return profile.core.mechanism.crank.crank_tdc_reference_rad.value;
        },
        context_.engine.physics_profile);
    if (std::bit_cast<std::uint64_t>(initial_theta_rad_) !=
        std::bit_cast<std::uint64_t>(required)) {
        add(authoring::DiagnosticCode::unsupported_capability,
            "/initial_state/crank_angle",
            "the current low-order executor requires the initial crank angle "
            "to equal the engine crank TDC reference exactly");
    }
}

void ScenarioResolver::compile_mode() {
    require_executable_initial_crank_angle();
    std::visit(
        [&](const auto &mode) {
            using T = std::decay_t<decltype(mode)>;
            if constexpr (std::is_same_v<T, authoring::FreeEngineMode>) {
                const auto &method = simulation::
                    warm_running_free_engine_rigid_crank_zoh_work_energy_method_identity();
                const auto method_validation = contract::validate(method);
                if (!method_validation.ok()) {
                    append_contract_report(report_, method_validation,
                                           authoring::DiagnosticCode::internal_failure,
                                           "");
                }
                if (!(request_input_.authored_initial_engine_speed_rpm > 0.0)) {
                    add(authoring::DiagnosticCode::out_of_range,
                        "/initial_state/engine_speed",
                        "warm-running free-engine execution requires a positive "
                        "initial engine speed");
                }

                contract::FreeEngine free_engine;
                free_engine.initial_engine_speed_rpm.value =
                    request_input_.authored_initial_engine_speed_rpm;
                free_engine.initial_theta_rad.value = initial_theta_rad_;
                const auto &mechanism =
                    std::get<contract::LowOrderOperatingPointV1Profile>(
                        context_.engine.physics_profile)
                        .core.mechanism;
                const auto inertia_calculation =
                    simulation::calculate_centered_slider_crank_cycle_mean_inertia(
                        mechanism);
                if (const auto *error = std::get_if<
                        simulation::CenteredSliderCrankCycleMeanInertiaError>(
                        &inertia_calculation)) {
                    add(authoring::DiagnosticCode::internal_failure, "",
                        "admitted engine mechanism could not produce its "
                        "cycle-mean crank-referred inertia; issue=" +
                            std::to_string(static_cast<std::uint32_t>(error->issue)) +
                            ", cylinder_index=" +
                            std::to_string(error->cylinder_index));
                } else {
                    free_engine.engine_baseline_inertia_kg_m2.value =
                        std::get<simulation::CenteredSliderCrankCycleMeanInertia>(
                            inertia_calculation)
                            .engine_equivalent_inertia_kg_m2;
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
                add(authoring::DiagnosticCode::unsupported_capability, "/mode/type",
                    "free-vehicle drivetrain dynamics are not implemented by "
                    "the current scenario executor");
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
