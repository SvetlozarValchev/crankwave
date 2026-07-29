#include "identity/simulation_request_identity_writer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace engine_sim_offline::identity::detail {
namespace {

template <class Range, class WriteElement>
[[nodiscard]] bool write_array(CanonicalJsonWriter &writer, const Range &values,
                               WriteElement write_element) {
    if (!writer.begin_array()) {
        return false;
    }
    for (const auto &value : values) {
        if (!write_element(writer, value)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool write_string(CanonicalJsonWriter &writer, const std::string &value) {
    return writer.string_value(value);
}

[[nodiscard]] bool write_f64(CanonicalJsonWriter &writer, double value) {
    return writer.binary64_bits_value(value);
}

[[nodiscard]] bool write_u32(CanonicalJsonWriter &writer, std::uint32_t value) {
    return writer.uint32_value(value);
}

[[nodiscard]] bool write_u64(CanonicalJsonWriter &writer, std::uint64_t value) {
    return writer.uint64_hex_value(value);
}

[[nodiscard]] bool
write_trajectory_interpolation(CanonicalJsonWriter &writer,
                               contract::TrajectoryInterpolation interpolation) {
    switch (interpolation) {
    case contract::TrajectoryInterpolation::right_continuous_hold:
        return writer.string_value("right_continuous_hold");
    case contract::TrajectoryInterpolation::linear:
        return writer.string_value("linear");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "trajectory interpolation is unknown");
}

[[nodiscard]] bool write_rpm_sample_semantics(CanonicalJsonWriter &writer,
                                              contract::RpmSampleSemantics semantics) {
    switch (semantics) {
    case contract::RpmSampleSemantics::post_step_rpm:
        return writer.string_value("post_step_rpm");
    }
    return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                       "RPM sample semantics are unknown");
}

[[nodiscard]] bool write_ambient(CanonicalJsonWriter &writer,
                                 const contract::AmbientConditions &ambient) {
    return writer.begin_object() && writer.key("pressure_pa_abs") &&
           write_resolved(writer, ambient.pressure_pa_abs, write_f64) &&
           writer.key("temperature_k") &&
           write_resolved(writer, ambient.temperature_k, write_f64) &&
           writer.key("relative_humidity_01") &&
           write_resolved(writer, ambient.relative_humidity_01, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_fuel_definition(CanonicalJsonWriter &writer,
                                         const contract::FuelDefinition &fuel) {
    return writer.begin_object() && writer.key("fuel_id") &&
           write_resolved(writer, fuel.fuel_id, write_string) &&
           writer.key("lower_heating_value_j_per_kg") &&
           write_resolved(writer, fuel.lower_heating_value_j_per_kg, write_f64) &&
           writer.key("stoichiometric_air_fuel_mass_ratio") &&
           write_resolved(writer, fuel.stoichiometric_air_fuel_mass_ratio, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_initial_thermal_state(CanonicalJsonWriter &writer,
                            const contract::InitialThermalState &thermal_state) {
    return writer.begin_object() && writer.key("gas_temperature_k") &&
           write_resolved(writer, thermal_state.gas_temperature_k, write_f64) &&
           writer.key("wall_temperature_k") &&
           write_resolved(writer, thermal_state.wall_temperature_k, write_f64) &&
           writer.key("coolant_temperature_k") &&
           write_resolved(writer, thermal_state.coolant_temperature_k, write_f64) &&
           writer.key("oil_temperature_k") &&
           write_resolved(writer, thermal_state.oil_temperature_k, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_crankcase(CanonicalJsonWriter &writer,
                                   const contract::CrankcaseConditions &crankcase) {
    return writer.begin_object() && writer.key("pressure_pa_abs") &&
           write_resolved(writer, crankcase.pressure_pa_abs, write_f64) &&
           writer.key("temperature_k") &&
           write_resolved(writer, crankcase.temperature_k, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_operating_state(CanonicalJsonWriter &writer,
                                         const contract::OperatingState &state) {
    return writer.begin_object() && writer.key("ignition_enabled") &&
           writer.bool_value(state.ignition_enabled) && writer.key("fuel_enabled") &&
           writer.bool_value(state.fuel_enabled) && writer.key("starter_enabled") &&
           writer.bool_value(state.starter_enabled) && writer.key("dyno_enabled") &&
           writer.bool_value(state.dyno_enabled) && writer.key("limiter_enabled") &&
           writer.bool_value(state.limiter_enabled) && writer.end_object();
}

[[nodiscard]] bool
write_operating_state_point(CanonicalJsonWriter &writer,
                            const contract::OperatingStatePoint &point) {
    return writer.begin_object() && writer.key("event_id") &&
           writer.string_value(point.event_id) && writer.key("time_s") &&
           writer.binary64_bits_value(point.time_s) && writer.key("state") &&
           write_operating_state(writer, point.state) && writer.end_object();
}

[[nodiscard]] bool
write_operating_state_points(CanonicalJsonWriter &writer,
                             const std::vector<contract::OperatingStatePoint> &points) {
    return write_array(
        writer, points,
        [](CanonicalJsonWriter &output, const contract::OperatingStatePoint &point) {
            return write_operating_state_point(output, point);
        });
}

[[nodiscard]] bool
write_scalar_trajectory_point(CanonicalJsonWriter &writer,
                              const contract::ScalarTrajectoryPoint &point) {
    return writer.begin_object() && writer.key("time_s") &&
           writer.binary64_bits_value(point.time_s) && writer.key("value") &&
           writer.binary64_bits_value(point.value) && writer.end_object();
}

[[nodiscard]] bool
write_scalar_trajectory(CanonicalJsonWriter &writer,
                        const contract::ScalarTrajectory &trajectory) {
    return writer.begin_object() && writer.key("interpolation") &&
           write_trajectory_interpolation(writer, trajectory.interpolation) &&
           writer.key("points") &&
           write_array(writer, trajectory.points,
                       [](CanonicalJsonWriter &output,
                          const contract::ScalarTrajectoryPoint &point) {
                           return write_scalar_trajectory_point(output, point);
                       }) &&
           writer.key("resolution_id") &&
           writer.string_value(trajectory.resolution_id) && writer.end_object();
}

[[nodiscard]] bool
write_fixed_rate_rpm_trajectory(CanonicalJsonWriter &writer,
                                const contract::FixedRateRpmTrajectory &trajectory) {
    for (const double sample : trajectory.post_step_rpm) {
        if (!std::isfinite(sample)) {
            return writer.fail(CanonicalJsonWriter::Error::non_finite_binary64,
                               "fixed-rate RPM lane contains a non-finite sample");
        }
    }
    if (contract::canonical_binary64_le_sha256(trajectory.post_step_rpm) !=
        trajectory.samples_f64le_sha256) {
        return writer.fail(
            CanonicalJsonWriter::Error::unsupported_value,
            "fixed-rate RPM lane does not match its canonical binary64 digest");
    }
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (trajectory.post_step_rpm.size() >
            std::numeric_limits<std::uint64_t>::max()) {
            return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                               "fixed-rate RPM sample count does not fit uint64");
        }
    }
    return writer.begin_object() && writer.key("rate") &&
           write_rational_rate(writer, trajectory.rate) &&
           writer.key("first_step_index") &&
           writer.uint64_hex_value(trajectory.first_step_index) &&
           writer.key("semantics") &&
           write_rpm_sample_semantics(writer, trajectory.semantics) &&
           writer.key("sample_count") &&
           writer.uint64_hex_value(
               static_cast<std::uint64_t>(trajectory.post_step_rpm.size())) &&
           writer.key("samples_f64le_sha256") &&
           writer.sha256_value(trajectory.samples_f64le_sha256) &&
           writer.key("resolution_id") &&
           writer.string_value(trajectory.resolution_id) && writer.end_object();
}

[[nodiscard]] bool
write_rpm_trajectory_value(CanonicalJsonWriter &writer,
                           const contract::RpmTrajectory &trajectory) {
    if (!writer.begin_object() || !writer.key("rpm") || !writer.begin_object() ||
        !writer.key("kind")) {
        return false;
    }
    if (const auto *scalar = std::get_if<contract::ScalarTrajectory>(&trajectory.rpm)) {
        if (!writer.string_value("scalar_trajectory") || !writer.key("value") ||
            !write_scalar_trajectory(writer, *scalar)) {
            return false;
        }
    } else if (const auto *fixed =
                   std::get_if<contract::FixedRateRpmTrajectory>(&trajectory.rpm)) {
        if (!writer.string_value("fixed_rate_rpm") || !writer.key("value") ||
            !write_fixed_rate_rpm_trajectory(writer, *fixed)) {
            return false;
        }
    } else {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "RPM-trajectory variant is valueless or unsupported");
    }
    return writer.end_object() && writer.key("initial_theta_rad") &&
           write_resolved(writer, trajectory.initial_theta_rad, write_f64) &&
           writer.key("kinematic_resolution") &&
           write_resolved(writer, trajectory.kinematic_resolution,
                          write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool write_fixed_settling(CanonicalJsonWriter &writer,
                                        const contract::FixedSettling &settling) {
    return writer.begin_object() && writer.key("warm_up_duration_s") &&
           write_resolved(writer, settling.warm_up_duration_s, write_f64) &&
           writer.key("settling_duration_s") &&
           write_resolved(writer, settling.settling_duration_s, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_convergence_settling(CanonicalJsonWriter &writer,
                           const contract::ConvergenceSettling &settling) {
    return writer.begin_object() && writer.key("method") &&
           write_resolved(writer, settling.method, write_method_identity) &&
           writer.key("minimum_warm_up_duration_s") &&
           write_resolved(writer, settling.minimum_warm_up_duration_s, write_f64) &&
           writer.key("minimum_settling_duration_s") &&
           write_resolved(writer, settling.minimum_settling_duration_s, write_f64) &&
           writer.key("maximum_preparation_duration_s") &&
           write_resolved(writer, settling.maximum_preparation_duration_s, write_f64) &&
           writer.key("comparison_cycle_count") &&
           write_resolved(writer, settling.comparison_cycle_count, write_u32) &&
           writer.key("cycle_mean_torque_tolerance_nm") &&
           write_resolved(writer, settling.cycle_mean_torque_tolerance_nm, write_f64) &&
           writer.key("pressure_tolerance_pa") &&
           write_resolved(writer, settling.pressure_tolerance_pa, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_preparation(CanonicalJsonWriter &writer,
                                     const contract::PreparationPolicy &preparation) {
    if (!writer.begin_object() || !writer.key("kind")) {
        return false;
    }
    if (const auto *fixed = std::get_if<contract::FixedSettling>(&preparation)) {
        if (!writer.string_value("fixed_settling") || !writer.key("value") ||
            !write_fixed_settling(writer, *fixed)) {
            return false;
        }
    } else if (const auto *convergence =
                   std::get_if<contract::ConvergenceSettling>(&preparation)) {
        if (!writer.string_value("convergence_settling") || !writer.key("value") ||
            !write_convergence_settling(writer, *convergence)) {
            return false;
        }
    } else {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "scenario preparation variant is valueless or unsupported");
    }
    return writer.end_object();
}

[[nodiscard]] bool write_held_speed(CanonicalJsonWriter &writer,
                                    const contract::HeldSpeed &held_speed) {
    return writer.begin_object() && writer.key("engine_speed_rpm") &&
           write_resolved(writer, held_speed.engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, held_speed.initial_theta_rad, write_f64) &&
           writer.key("throttle_01") &&
           write_resolved(writer, held_speed.throttle_01, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_prescribed_sweep(CanonicalJsonWriter &writer,
                       const contract::PrescribedKinematicSweep &sweep) {
    return writer.begin_object() && writer.key("trajectory") &&
           write_rpm_trajectory_value(writer, sweep.trajectory) &&
           writer.key("throttle_01") &&
           write_scalar_trajectory(writer, sweep.throttle_01) && writer.end_object();
}

[[nodiscard]] bool
write_load_target(CanonicalJsonWriter &writer,
                  const contract::LoadTargetHeldCapture &load_target) {
    return writer.begin_object() && writer.key("engine_speed_rpm") &&
           write_resolved(writer, load_target.engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, load_target.initial_theta_rad, write_f64) &&
           writer.key("target_net_bmep_pa") &&
           write_resolved(writer, load_target.target_net_bmep_pa, write_f64) &&
           writer.key("target_tolerance_pa") &&
           write_resolved(writer, load_target.target_tolerance_pa, write_f64) &&
           writer.key("throttle_lower_bound_01") &&
           write_resolved(writer, load_target.throttle_lower_bound_01, write_f64) &&
           writer.key("throttle_upper_bound_01") &&
           write_resolved(writer, load_target.throttle_upper_bound_01, write_f64) &&
           writer.key("search_method") &&
           write_resolved(writer, load_target.search_method, write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool write_brake_torque_point(CanonicalJsonWriter &writer,
                                            const contract::BrakeTorquePoint &point) {
    return writer.begin_object() && writer.key("angular_speed_rad_s") &&
           writer.binary64_bits_value(point.angular_speed_rad_s) &&
           writer.key("resisting_torque_nm") &&
           writer.binary64_bits_value(point.resisting_torque_nm) && writer.end_object();
}

[[nodiscard]] bool write_inertial_dyno(CanonicalJsonWriter &writer,
                                       const contract::InertialDyno &dyno) {
    return writer.begin_object() && writer.key("initial_engine_speed_rpm") &&
           write_resolved(writer, dyno.initial_engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, dyno.initial_theta_rad, write_f64) &&
           writer.key("equivalent_inertia_kg_m2") &&
           write_resolved(writer, dyno.equivalent_inertia_kg_m2, write_f64) &&
           writer.key("throttle_01") &&
           write_scalar_trajectory(writer, dyno.throttle_01) &&
           writer.key("brake_curve") &&
           write_array(writer, dyno.brake_curve,
                       [](CanonicalJsonWriter &output,
                          const contract::BrakeTorquePoint &point) {
                           return write_brake_torque_point(output, point);
                       }) &&
           writer.key("brake_curve_resolution_id") &&
           writer.string_value(dyno.brake_curve_resolution_id) &&
           writer.key("crank_dynamics_method") &&
           write_resolved(writer, dyno.crank_dynamics_method, write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool write_scenario_mode(CanonicalJsonWriter &writer,
                                       const contract::ScenarioMode &mode) {
    if (!writer.begin_object() || !writer.key("kind")) {
        return false;
    }
    if (const auto *held = std::get_if<contract::HeldSpeed>(&mode)) {
        if (!writer.string_value("held_speed") || !writer.key("value") ||
            !write_held_speed(writer, *held)) {
            return false;
        }
    } else if (const auto *sweep =
                   std::get_if<contract::PrescribedKinematicSweep>(&mode)) {
        if (!writer.string_value("prescribed_kinematic_sweep") ||
            !writer.key("value") || !write_prescribed_sweep(writer, *sweep)) {
            return false;
        }
    } else if (const auto *load_target =
                   std::get_if<contract::LoadTargetHeldCapture>(&mode)) {
        if (!writer.string_value("load_target_held_capture") || !writer.key("value") ||
            !write_load_target(writer, *load_target)) {
            return false;
        }
    } else if (const auto *dyno = std::get_if<contract::InertialDyno>(&mode)) {
        if (!writer.string_value("inertial_dyno") || !writer.key("value") ||
            !write_inertial_dyno(writer, *dyno)) {
            return false;
        }
    } else {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "scenario-mode variant is valueless or unsupported");
    }
    return writer.end_object();
}

[[nodiscard]] bool write_render_quality(CanonicalJsonWriter &writer,
                                        const contract::RenderQuality &quality) {
    return writer.begin_object() && writer.key("profile_id") &&
           writer.string_value(quality.profile_id) && writer.key("version") &&
           writer.uint32_value(quality.version) &&
           writer.key("capture_block_capacity_frames") &&
           writer.uint32_value(quality.capture_block_capacity_frames) &&
           writer.key("event_journal_capacity_records") &&
           writer.uint32_value(quality.event_journal_capacity_records) &&
           writer.end_object();
}

} // namespace

bool write_render_scenario(CanonicalJsonWriter &writer,
                           const contract::RenderScenario &scenario) {
    if (scenario.schema_version != 1U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "simulation scenario schema version is not v1");
    }
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(scenario.schema_version) && writer.key("scenario_id") &&
           writer.string_value(scenario.scenario_id) &&
           writer.key("engine_profile_id") &&
           writer.string_value(scenario.engine_profile_id) && writer.key("ambient") &&
           write_ambient(writer, scenario.ambient) && writer.key("fuel") &&
           write_fuel_definition(writer, scenario.fuel) &&
           writer.key("initial_thermal_state") &&
           write_initial_thermal_state(writer, scenario.initial_thermal_state) &&
           writer.key("crankcase") && write_crankcase(writer, scenario.crankcase) &&
           writer.key("preparation") &&
           write_preparation(writer, scenario.preparation) &&
           writer.key("operating_state") &&
           write_resolved(writer, scenario.operating_state,
                          write_operating_state_points) &&
           writer.key("total_duration_s") &&
           write_resolved(writer, scenario.total_duration_s, write_f64) &&
           writer.key("audible_start_s") &&
           write_resolved(writer, scenario.audible_start_s, write_f64) &&
           writer.key("audible_duration_s") &&
           write_resolved(writer, scenario.audible_duration_s, write_f64) &&
           writer.key("rates") && write_render_rates(writer, scenario.rates) &&
           writer.key("rates_resolution_id") &&
           writer.string_value(scenario.rates_resolution_id) && writer.key("quality") &&
           write_resolved(writer, scenario.quality, write_render_quality) &&
           writer.key("public_seed") &&
           write_resolved(writer, scenario.public_seed, write_u64) &&
           writer.key("mode") && write_scenario_mode(writer, scenario.mode) &&
           writer.key("mode_resolution_id") &&
           writer.string_value(scenario.mode_resolution_id) &&
           writer.key("provenance_schema_id") &&
           writer.string_value(scenario.provenance_schema_id) && writer.end_object();
}

} // namespace engine_sim_offline::identity::detail
