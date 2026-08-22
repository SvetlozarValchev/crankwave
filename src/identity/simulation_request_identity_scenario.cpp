#include "identity/simulation_request_identity_writer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>

namespace crankwave::identity::detail {
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

template <class Id>
[[nodiscard]] bool write_stable_id(CanonicalJsonWriter &writer, Id id) {
    if (!id.valid()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "scenario contains an invalid stable ID");
    }
    return writer.uint32_value(id.value);
}

template <class Id>
[[nodiscard]] bool write_optional_stable_id(CanonicalJsonWriter &writer,
                                            const std::optional<Id> &id) {
    return id.has_value() ? write_stable_id(writer, *id) : writer.null_value();
}

template <class T, class WriteValue>
[[nodiscard]] bool
write_optional_resolved(CanonicalJsonWriter &writer,
                        const std::optional<contract::ResolvedValue<T>> &value,
                        WriteValue write_value) {
    return value.has_value() ? write_resolved(writer, *value, write_value)
                             : writer.null_value();
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

[[nodiscard]] bool write_fixed_horizon_cycle_sampling(
    CanonicalJsonWriter &writer, const contract::FixedHorizonCycleSampling &sampling) {
    return writer.begin_object() && writer.key("method") &&
           write_resolved(writer, sampling.method, write_method_identity) &&
           writer.key("fixed_preparation_horizon_s") &&
           write_resolved(writer, sampling.fixed_preparation_horizon_s, write_f64) &&
           writer.key("trailing_complete_cycle_count") &&
           write_resolved(writer, sampling.trailing_complete_cycle_count, write_u32) &&
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
    } else if (const auto *sampling =
                   std::get_if<contract::FixedHorizonCycleSampling>(&preparation)) {
        if (!writer.string_value("fixed_horizon_cycle_sampling") ||
            !writer.key("value") ||
            !write_fixed_horizon_cycle_sampling(writer, *sampling)) {
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

[[nodiscard]] bool write_held_dyno(CanonicalJsonWriter &writer,
                                   const contract::HeldDyno &dyno) {
    return writer.begin_object() && writer.key("initial_engine_speed_rpm") &&
           write_resolved(writer, dyno.initial_engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, dyno.initial_theta_rad, write_f64) &&
           writer.key("target_engine_speed_rpm") &&
           write_fixed_rate_rpm_trajectory(writer, dyno.target_engine_speed_rpm) &&
           writer.key("throttle_01") &&
           write_scalar_trajectory(writer, dyno.throttle_01) &&
           writer.key("maximum_absorbing_torque_nm") &&
           write_resolved(writer, dyno.maximum_absorbing_torque_nm, write_f64) &&
           writer.key("maximum_driving_torque_nm") &&
           write_resolved(writer, dyno.maximum_driving_torque_nm, write_f64) &&
           writer.key("constraint_method") &&
           write_resolved(writer, dyno.constraint_method, write_method_identity) &&
           writer.end_object();
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
           writer.key("target_engine_speed_rpm") &&
           write_resolved(writer, dyno.target_engine_speed_rpm, write_f64) &&
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
           writer.key("brake_torque_method") &&
           write_resolved(writer, dyno.brake_torque_method, write_method_identity) &&
           writer.key("crank_dynamics_method") &&
           write_resolved(writer, dyno.crank_dynamics_method, write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool write_free_engine(CanonicalJsonWriter &writer,
                                     const contract::FreeEngine &free_engine) {
    return writer.begin_object() && writer.key("initial_engine_speed_rpm") &&
           write_resolved(writer, free_engine.initial_engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, free_engine.initial_theta_rad, write_f64) &&
           writer.key("engine_baseline_inertia_kg_m2") &&
           write_resolved(writer, free_engine.engine_baseline_inertia_kg_m2,
                          write_f64) &&
           writer.key("attached_inertia_kg_m2") &&
           write_resolved(writer, free_engine.attached_inertia_kg_m2, write_f64) &&
           writer.key("total_equivalent_inertia_kg_m2") &&
           write_resolved(writer, free_engine.total_equivalent_inertia_kg_m2,
                          write_f64) &&
           writer.key("throttle_01") &&
           write_scalar_trajectory(writer, free_engine.throttle_01) &&
           writer.key("external_resisting_torque_nm") &&
           write_scalar_trajectory(writer, free_engine.external_resisting_torque_nm) &&
           writer.key("crank_dynamics_method") &&
           write_resolved(writer, free_engine.crank_dynamics_method,
                          write_method_identity) &&
           writer.end_object();
}

[[nodiscard]] bool write_forward_gear_spec(CanonicalJsonWriter &writer,
                                           const contract::ForwardGearSpec &gear) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, gear.id) && writer.key("authored_ordinal") &&
           write_resolved(writer, gear.authored_ordinal, write_u32) &&
           writer.key("semantic_id") &&
           write_resolved(writer, gear.semantic_id, write_string) &&
           writer.key("ratio") && write_resolved(writer, gear.ratio, write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool
write_forward_transmission_spec(CanonicalJsonWriter &writer,
                                const contract::ForwardTransmissionSpec &transmission) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, transmission.id) && writer.key("semantic_id") &&
           write_resolved(writer, transmission.semantic_id, write_string) &&
           writer.key("maximum_clutch_torque_nm") &&
           write_resolved(writer, transmission.maximum_clutch_torque_nm, write_f64) &&
           writer.key("gears") &&
           write_array(
               writer, transmission.gears,
               [](CanonicalJsonWriter &output, const contract::ForwardGearSpec &gear) {
                   return write_forward_gear_spec(output, gear);
               }) &&
           writer.end_object();
}

[[nodiscard]] bool
write_forward_vehicle_spec(CanonicalJsonWriter &writer,
                           const contract::ForwardVehicleSpec &vehicle) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, vehicle.id) && writer.key("semantic_id") &&
           write_resolved(writer, vehicle.semantic_id, write_string) &&
           writer.key("mass_kg") &&
           write_resolved(writer, vehicle.mass_kg, write_f64) &&
           writer.key("drag_coefficient") &&
           write_resolved(writer, vehicle.drag_coefficient, write_f64) &&
           writer.key("frontal_area_m2") &&
           write_resolved(writer, vehicle.frontal_area_m2, write_f64) &&
           writer.key("differential_ratio") &&
           write_resolved(writer, vehicle.differential_ratio, write_f64) &&
           writer.key("tire_radius_m") &&
           write_resolved(writer, vehicle.tire_radius_m, write_f64) &&
           writer.key("rolling_resistance_force_n") &&
           write_resolved(writer, vehicle.rolling_resistance_force_n, write_f64) &&
           writer.key("maximum_service_brake_force_n") &&
           write_optional_resolved(writer, vehicle.maximum_service_brake_force_n,
                                   write_f64) &&
           writer.end_object();
}

[[nodiscard]] bool write_free_vehicle_rig(CanonicalJsonWriter &writer,
                                          const contract::FreeVehicleRig &rig) {
    return writer.begin_object() && writer.key("id") &&
           write_stable_id(writer, rig.id) && writer.key("semantic_id") &&
           write_resolved(writer, rig.semantic_id, write_string) &&
           writer.key("vehicle") && write_forward_vehicle_spec(writer, rig.vehicle) &&
           writer.key("transmission") &&
           write_forward_transmission_spec(writer, rig.transmission) &&
           writer.end_object();
}

[[nodiscard]] bool
write_gear_selection_point(CanonicalJsonWriter &writer,
                           const contract::GearSelectionPoint &point) {
    return writer.begin_object() && writer.key("event_id") &&
           writer.string_value(point.event_id) && writer.key("time_s") &&
           writer.binary64_bits_value(point.time_s) && writer.key("gear_id") &&
           write_optional_stable_id(writer, point.gear_id) && writer.end_object();
}

[[nodiscard]] bool
write_gear_selection_points(CanonicalJsonWriter &writer,
                            const std::vector<contract::GearSelectionPoint> &points) {
    return write_array(
        writer, points,
        [](CanonicalJsonWriter &output, const contract::GearSelectionPoint &point) {
            return write_gear_selection_point(output, point);
        });
}

[[nodiscard]] bool
write_scalar_control_point(CanonicalJsonWriter &writer,
                           const contract::ScalarControlPoint &point) {
    return writer.begin_object() && writer.key("event_id") &&
           writer.string_value(point.event_id) && writer.key("time_s") &&
           writer.binary64_bits_value(point.time_s) && writer.key("value") &&
           writer.binary64_bits_value(point.value) && writer.end_object();
}

[[nodiscard]] bool
write_scalar_control_points(CanonicalJsonWriter &writer,
                            const std::vector<contract::ScalarControlPoint> &points) {
    return write_array(
        writer, points,
        [](CanonicalJsonWriter &output, const contract::ScalarControlPoint &point) {
            return write_scalar_control_point(output, point);
        });
}

[[nodiscard]] bool write_free_vehicle(CanonicalJsonWriter &writer,
                                      const contract::FreeVehicle &free_vehicle) {
    return writer.begin_object() && writer.key("initial_engine_speed_rpm") &&
           write_resolved(writer, free_vehicle.initial_engine_speed_rpm, write_f64) &&
           writer.key("initial_theta_rad") &&
           write_resolved(writer, free_vehicle.initial_theta_rad, write_f64) &&
           writer.key("engine_baseline_inertia_kg_m2") &&
           write_resolved(writer, free_vehicle.engine_baseline_inertia_kg_m2,
                          write_f64) &&
           writer.key("initial_vehicle_speed_m_s") &&
           write_resolved(writer, free_vehicle.initial_vehicle_speed_m_s, write_f64) &&
           writer.key("rig") && write_free_vehicle_rig(writer, free_vehicle.rig) &&
           writer.key("throttle_01") &&
           write_scalar_trajectory(writer, free_vehicle.throttle_01) &&
           writer.key("selected_gear") &&
           write_resolved(writer, free_vehicle.selected_gear,
                          write_gear_selection_points) &&
           writer.key("clutch_engagement_01") &&
           write_resolved(writer, free_vehicle.clutch_engagement_01,
                          write_scalar_control_points) &&
           writer.key("service_brake_application_01") &&
           write_resolved(writer, free_vehicle.service_brake_application_01,
                          write_scalar_control_points) &&
           writer.key("crank_dynamics_method") &&
           write_resolved(writer, free_vehicle.crank_dynamics_method,
                          write_method_identity) &&
           writer.key("road_load_method") &&
           write_resolved(writer, free_vehicle.road_load_method,
                          write_method_identity) &&
           writer.key("clutch_coupling_method") &&
           write_resolved(writer, free_vehicle.clutch_coupling_method,
                          write_method_identity) &&
           writer.key("drivetrain_dynamics_method") &&
           write_resolved(writer, free_vehicle.drivetrain_dynamics_method,
                          write_method_identity) &&
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
    } else if (const auto *held_dyno = std::get_if<contract::HeldDyno>(&mode)) {
        if (!writer.string_value("held_dyno") || !writer.key("value") ||
            !write_held_dyno(writer, *held_dyno)) {
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
    } else if (const auto *free_engine = std::get_if<contract::FreeEngine>(&mode)) {
        if (!writer.string_value("free_engine") || !writer.key("value") ||
            !write_free_engine(writer, *free_engine)) {
            return false;
        }
    } else if (const auto *free_vehicle = std::get_if<contract::FreeVehicle>(&mode)) {
        if (!writer.string_value("free_vehicle") || !writer.key("value") ||
            !write_free_vehicle(writer, *free_vehicle)) {
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

} // namespace crankwave::identity::detail
