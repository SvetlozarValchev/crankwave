#include "engine_sim_offline/authoring/parse.hpp"

#include "authoring/document_reader.hpp"
#include "authoring/parse_engine_detail.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine_sim_offline::authoring {
namespace {

using detail::DocumentReader;
using detail::pointer_index;
using detail::pointer_member;
using detail::read_enum;
using detail::read_id_member;
using detail::read_quantity_member;
using detail::read_ref_member;
using detail::require_nonnegative;
using detail::require_positive;

[[nodiscard]] std::optional<double>
duration_seconds(const Quantity &quantity) noexcept {
    if (quantity.unit == "s") {
        return quantity.value;
    }
    if (quantity.unit == "ms") {
        return quantity.value / 1000.0;
    }
    return std::nullopt;
}

bool parse_dimension(DocumentReader &reader, JsonValue value, std::string_view path,
                     QuantityDimension &output) {
    return read_enum(reader, value, path,
                     {
                         {"dimensionless", QuantityDimension::dimensionless},
                         {"angle", QuantityDimension::angle},
                         {"angular_speed", QuantityDimension::angular_speed},
                         {"area", QuantityDimension::area},
                         {"density", QuantityDimension::density},
                         {"duration", QuantityDimension::duration},
                         {"energy_per_mass", QuantityDimension::energy_per_mass},
                         {"force", QuantityDimension::force},
                         {"frequency", QuantityDimension::frequency},
                         {"length", QuantityDimension::length},
                         {"mass", QuantityDimension::mass},
                         {"mass_flow_rate", QuantityDimension::mass_flow_rate},
                         {"molar_mass", QuantityDimension::molar_mass},
                         {"moment_of_inertia", QuantityDimension::moment_of_inertia},
                         {"power", QuantityDimension::power},
                         {"pressure", QuantityDimension::pressure},
                         {"speed", QuantityDimension::speed},
                         {"temperature", QuantityDimension::temperature},
                         {"torque", QuantityDimension::torque},
                         {"volume", QuantityDimension::volume},
                         {"volume_flow_rate", QuantityDimension::volume_flow_rate},
                     },
                     output);
}

void parse_scalar_trajectory(DocumentReader &reader, JsonValue value,
                             std::string_view path, ScalarTrajectory &output,
                             bool fraction_values) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"interpolation", "points"});
    read_enum(
        reader, reader.required(value, "interpolation", path),
        pointer_member(path, "interpolation"),
        {{"right_continuous_hold", TrajectoryInterpolation::right_continuous_hold},
         {"linear", TrajectoryInterpolation::linear}},
        output.interpolation);
    const auto points = reader.required(value, "points", path);
    const auto points_path = pointer_member(path, "points");
    if (!reader.array(points, points_path)) {
        return;
    }
    output.points.reserve(points.size());
    double previous_time_seconds = -1.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto point = points.at(index);
        const auto point_path = pointer_index(points_path, index);
        ScalarTrajectoryPoint parsed;
        if (reader.object(point, point_path)) {
            reader.reject_unknown(point, point_path, {"time", "value"});
            read_quantity_member(reader, point, "time", point_path,
                                 QuantityDimension::duration, parsed.time);
            if (fraction_values) {
                reader.fraction(reader.required(point, "value", point_path),
                                pointer_member(point_path, "value"), parsed.value);
            } else {
                reader.number(reader.required(point, "value", point_path),
                              pointer_member(point_path, "value"), parsed.value);
            }
            require_nonnegative(reader, parsed.time,
                                pointer_member(point_path, "time"));
            const auto point_time_seconds = duration_seconds(parsed.time);
            if (point_time_seconds && *point_time_seconds <= previous_time_seconds) {
                reader.add(DiagnosticCode::inconsistent_value,
                           pointer_member(point_path, "time"),
                           "trajectory times must be strictly increasing");
            }
            if (point_time_seconds) {
                previous_time_seconds = *point_time_seconds;
            }
        }
        output.points.push_back(std::move(parsed));
    }
    if (output.points.empty()) {
        reader.add(DiagnosticCode::missing_value, points_path,
                   "trajectory requires at least one point");
    }
}

void parse_quantity_trajectory(DocumentReader &reader, JsonValue value,
                               std::string_view path, QuantityTrajectory &output,
                               QuantityDimension required_dimension) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"value_dimension", "interpolation", "points"});
    parse_dimension(reader, reader.required(value, "value_dimension", path),
                    pointer_member(path, "value_dimension"), output.value_dimension);
    if (output.value_dimension != required_dimension) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "value_dimension"),
                   "trajectory value dimension is inconsistent with its scenario "
                   "owner");
    }
    read_enum(
        reader, reader.required(value, "interpolation", path),
        pointer_member(path, "interpolation"),
        {{"right_continuous_hold", TrajectoryInterpolation::right_continuous_hold},
         {"linear", TrajectoryInterpolation::linear}},
        output.interpolation);
    const auto points = reader.required(value, "points", path);
    const auto points_path = pointer_member(path, "points");
    if (!reader.array(points, points_path)) {
        return;
    }
    output.points.reserve(points.size());
    double previous_time_seconds = -1.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto point = points.at(index);
        const auto point_path = pointer_index(points_path, index);
        QuantityTrajectoryPoint parsed;
        if (reader.object(point, point_path)) {
            reader.reject_unknown(point, point_path, {"time", "value"});
            read_quantity_member(reader, point, "time", point_path,
                                 QuantityDimension::duration, parsed.time);
            read_quantity_member(reader, point, "value", point_path,
                                 output.value_dimension, parsed.value);
            require_nonnegative(reader, parsed.time,
                                pointer_member(point_path, "time"));
            const auto point_time_seconds = duration_seconds(parsed.time);
            if (point_time_seconds && *point_time_seconds <= previous_time_seconds) {
                reader.add(DiagnosticCode::inconsistent_value,
                           pointer_member(point_path, "time"),
                           "trajectory times must be strictly increasing");
            }
            if (point_time_seconds) {
                previous_time_seconds = *point_time_seconds;
            }
        }
        output.points.push_back(std::move(parsed));
    }
    if (output.points.empty()) {
        reader.add(DiagnosticCode::missing_value, points_path,
                   "trajectory requires at least one point");
    }
}

void parse_ambient(DocumentReader &reader, JsonValue value, std::string_view path,
                   AmbientConditions &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"pressure", "temperature", "relative_humidity_01"});
    read_quantity_member(reader, value, "pressure", path, QuantityDimension::pressure,
                         output.pressure);
    read_quantity_member(reader, value, "temperature", path,
                         QuantityDimension::temperature, output.temperature);
    reader.fraction(reader.required(value, "relative_humidity_01", path),
                    pointer_member(path, "relative_humidity_01"),
                    output.relative_humidity_01);
    require_positive(reader, output.pressure, pointer_member(path, "pressure"));
}

void parse_thermal(DocumentReader &reader, JsonValue value, std::string_view path,
                   InitialThermalState &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"gas_temperature", "wall_temperature", "coolant_temperature",
                           "oil_temperature"});
    read_quantity_member(reader, value, "gas_temperature", path,
                         QuantityDimension::temperature, output.gas_temperature);
    read_quantity_member(reader, value, "wall_temperature", path,
                         QuantityDimension::temperature, output.wall_temperature);
    read_quantity_member(reader, value, "coolant_temperature", path,
                         QuantityDimension::temperature, output.coolant_temperature);
    read_quantity_member(reader, value, "oil_temperature", path,
                         QuantityDimension::temperature, output.oil_temperature);
}

void parse_crankcase(DocumentReader &reader, JsonValue value, std::string_view path,
                     CrankcaseConditions &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"pressure", "temperature"});
    read_quantity_member(reader, value, "pressure", path, QuantityDimension::pressure,
                         output.pressure);
    read_quantity_member(reader, value, "temperature", path,
                         QuantityDimension::temperature, output.temperature);
    require_positive(reader, output.pressure, pointer_member(path, "pressure"));
}

void parse_initial_state(DocumentReader &reader, JsonValue value, std::string_view path,
                         InitialOperatingState &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"engine_speed", "crank_angle", "ignition_enabled",
                           "fuel_enabled", "starter_enabled", "dyno_enabled",
                           "limiter_enabled"});
    read_quantity_member(reader, value, "engine_speed", path,
                         QuantityDimension::angular_speed, output.engine_speed);
    read_quantity_member(reader, value, "crank_angle", path, QuantityDimension::angle,
                         output.crank_angle);
    reader.boolean(reader.required(value, "ignition_enabled", path),
                   pointer_member(path, "ignition_enabled"), output.ignition_enabled);
    reader.boolean(reader.required(value, "fuel_enabled", path),
                   pointer_member(path, "fuel_enabled"), output.fuel_enabled);
    reader.boolean(reader.required(value, "starter_enabled", path),
                   pointer_member(path, "starter_enabled"), output.starter_enabled);
    reader.boolean(reader.required(value, "dyno_enabled", path),
                   pointer_member(path, "dyno_enabled"), output.dyno_enabled);
    reader.boolean(reader.required(value, "limiter_enabled", path),
                   pointer_member(path, "limiter_enabled"), output.limiter_enabled);
    require_nonnegative(reader, output.engine_speed,
                        pointer_member(path, "engine_speed"));
}

void parse_preparation(DocumentReader &reader, JsonValue value, std::string_view path,
                       Preparation &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "fixed_settling") {
        reader.reject_unknown(value, path,
                              {"type", "warm_up_duration", "settling_duration"});
        FixedSettlingPreparation parsed;
        read_quantity_member(reader, value, "warm_up_duration", path,
                             QuantityDimension::duration, parsed.warm_up_duration);
        read_quantity_member(reader, value, "settling_duration", path,
                             QuantityDimension::duration, parsed.settling_duration);
        require_nonnegative(reader, parsed.warm_up_duration,
                            pointer_member(path, "warm_up_duration"));
        require_nonnegative(reader, parsed.settling_duration,
                            pointer_member(path, "settling_duration"));
        output = std::move(parsed);
    } else if (type == "fixed_horizon") {
        reader.reject_unknown(
            value, path,
            {"type", "preparation_duration", "trailing_complete_cycle_count"});
        FixedHorizonPreparation parsed;
        read_quantity_member(reader, value, "preparation_duration", path,
                             QuantityDimension::duration, parsed.preparation_duration);
        reader.uint32(reader.required(value, "trailing_complete_cycle_count", path),
                      pointer_member(path, "trailing_complete_cycle_count"),
                      parsed.trailing_complete_cycle_count);
        require_nonnegative(reader, parsed.preparation_duration,
                            pointer_member(path, "preparation_duration"));
        if (parsed.trailing_complete_cycle_count == 0U) {
            reader.add(DiagnosticCode::out_of_range,
                       pointer_member(path, "trailing_complete_cycle_count"),
                       "cycle count must be positive");
        }
        output = std::move(parsed);
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown preparation type '" + type + "'");
    }
}

void parse_mode(DocumentReader &reader, JsonValue value, std::string_view path,
                ScenarioMode &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "free_engine") {
        reader.reject_unknown(
            value, path,
            {"type", "attached_inertia", "throttle_01", "external_resisting_torque"});
        FreeEngineMode parsed;
        const auto attached_inertia = reader.optional(value, "attached_inertia");
        if (attached_inertia.valid()) {
            Quantity quantity;
            reader.quantity(attached_inertia, pointer_member(path, "attached_inertia"),
                            QuantityDimension::moment_of_inertia, quantity);
            require_nonnegative(reader, quantity,
                                pointer_member(path, "attached_inertia"));
            parsed.attached_inertia = std::move(quantity);
        }
        parse_scalar_trajectory(reader, reader.required(value, "throttle_01", path),
                                pointer_member(path, "throttle_01"), parsed.throttle_01,
                                true);
        const auto external_resisting_torque =
            reader.optional(value, "external_resisting_torque");
        if (external_resisting_torque.valid()) {
            QuantityTrajectory trajectory;
            parse_quantity_trajectory(reader, external_resisting_torque,
                                      pointer_member(path, "external_resisting_torque"),
                                      trajectory, QuantityDimension::torque);
            parsed.external_resisting_torque = std::move(trajectory);
        }
        output = std::move(parsed);
    } else if (type == "free_vehicle") {
        reader.reject_unknown(value, path,
                              {"type", "rig", "initial_gear",
                               "initial_clutch_position_01", "throttle_01"});
        FreeVehicleMode parsed;
        read_ref_member(reader, value, "rig", path, parsed.rig);
        read_ref_member(reader, value, "initial_gear", path, parsed.initial_gear);
        reader.fraction(reader.required(value, "initial_clutch_position_01", path),
                        pointer_member(path, "initial_clutch_position_01"),
                        parsed.initial_clutch_position_01);
        parse_scalar_trajectory(reader, reader.required(value, "throttle_01", path),
                                pointer_member(path, "throttle_01"), parsed.throttle_01,
                                true);
        output = std::move(parsed);
    } else if (type == "held_speed") {
        reader.reject_unknown(value, path,
                              {"type", "target_engine_speed", "throttle_01"});
        HeldSpeedMode parsed;
        read_quantity_member(reader, value, "target_engine_speed", path,
                             QuantityDimension::angular_speed,
                             parsed.target_engine_speed);
        parse_scalar_trajectory(reader, reader.required(value, "throttle_01", path),
                                pointer_member(path, "throttle_01"), parsed.throttle_01,
                                true);
        require_positive(reader, parsed.target_engine_speed,
                         pointer_member(path, "target_engine_speed"));
        output = std::move(parsed);
    } else if (type == "load_target_held") {
        reader.reject_unknown(value, path,
                              {"type", "target_engine_speed", "target_net_bmep",
                               "target_tolerance", "throttle_lower_bound_01",
                               "throttle_upper_bound_01"});
        LoadTargetHeldMode parsed;
        read_quantity_member(reader, value, "target_engine_speed", path,
                             QuantityDimension::angular_speed,
                             parsed.target_engine_speed);
        read_quantity_member(reader, value, "target_net_bmep", path,
                             QuantityDimension::pressure, parsed.target_net_bmep);
        read_quantity_member(reader, value, "target_tolerance", path,
                             QuantityDimension::pressure, parsed.target_tolerance);
        reader.fraction(reader.required(value, "throttle_lower_bound_01", path),
                        pointer_member(path, "throttle_lower_bound_01"),
                        parsed.throttle_lower_bound_01);
        reader.fraction(reader.required(value, "throttle_upper_bound_01", path),
                        pointer_member(path, "throttle_upper_bound_01"),
                        parsed.throttle_upper_bound_01);
        require_positive(reader, parsed.target_engine_speed,
                         pointer_member(path, "target_engine_speed"));
        require_positive(reader, parsed.target_tolerance,
                         pointer_member(path, "target_tolerance"));
        if (parsed.throttle_lower_bound_01 > parsed.throttle_upper_bound_01) {
            reader.add(DiagnosticCode::inconsistent_value,
                       pointer_member(path, "throttle_upper_bound_01"),
                       "upper throttle bound is below lower bound");
        }
        output = std::move(parsed);
    } else if (type == "external_speed") {
        reader.reject_unknown(value, path, {"type", "engine_speed", "throttle_01"});
        ExternalSpeedMode parsed;
        parse_quantity_trajectory(reader, reader.required(value, "engine_speed", path),
                                  pointer_member(path, "engine_speed"),
                                  parsed.engine_speed,
                                  QuantityDimension::angular_speed);
        parse_scalar_trajectory(reader, reader.required(value, "throttle_01", path),
                                pointer_member(path, "throttle_01"), parsed.throttle_01,
                                true);
        output = std::move(parsed);
    } else if (type == "inertial_dyno") {
        reader.reject_unknown(value, path,
                              {"type", "equivalent_inertia", "throttle_01",
                               "brake_curve", "target_engine_speed"});
        InertialDynoMode parsed;
        read_quantity_member(reader, value, "equivalent_inertia", path,
                             QuantityDimension::moment_of_inertia,
                             parsed.equivalent_inertia);
        parse_scalar_trajectory(reader, reader.required(value, "throttle_01", path),
                                pointer_member(path, "throttle_01"), parsed.throttle_01,
                                true);
        detail::read_required_array(
            reader, value, "brake_curve", path, parsed.brake_curve,
            [&](JsonValue item, std::string_view item_path, BrakeTorquePoint &point) {
                if (!reader.object(item, item_path)) {
                    return;
                }
                reader.reject_unknown(item, item_path,
                                      {"engine_speed", "resisting_torque"});
                read_quantity_member(reader, item, "engine_speed", item_path,
                                     QuantityDimension::angular_speed,
                                     point.engine_speed);
                read_quantity_member(reader, item, "resisting_torque", item_path,
                                     QuantityDimension::torque, point.resisting_torque);
                require_nonnegative(reader, point.engine_speed,
                                    pointer_member(item_path, "engine_speed"));
                require_nonnegative(reader, point.resisting_torque,
                                    pointer_member(item_path, "resisting_torque"));
            });
        read_quantity_member(reader, value, "target_engine_speed", path,
                             QuantityDimension::angular_speed,
                             parsed.target_engine_speed);
        require_positive(reader, parsed.equivalent_inertia,
                         pointer_member(path, "equivalent_inertia"));
        require_positive(reader, parsed.target_engine_speed,
                         pointer_member(path, "target_engine_speed"));
        output = std::move(parsed);
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown scenario mode '" + type + "'");
    }
}

void parse_optional_boolean(DocumentReader &reader, JsonValue object,
                            std::string_view member, std::string_view path,
                            std::optional<bool> &output) {
    const auto value = reader.optional(object, member);
    if (!value.valid() || value.is_null()) {
        return;
    }
    bool parsed = false;
    if (reader.boolean(value, pointer_member(path, member), parsed)) {
        output = parsed;
    }
}

void parse_optional_nonnegative_number(DocumentReader &reader, JsonValue object,
                                       std::string_view member, std::string_view path,
                                       std::optional<double> &output) {
    const auto value = reader.optional(object, member);
    if (!value.valid() || value.is_null()) {
        return;
    }
    double parsed = 0.0;
    if (reader.nonnegative_number(value, pointer_member(path, member), parsed)) {
        output = parsed;
    }
}

void parse_optional_fraction(DocumentReader &reader, JsonValue object,
                             std::string_view member, std::string_view path,
                             std::optional<double> &output) {
    const auto value = reader.optional(object, member);
    if (!value.valid() || value.is_null()) {
        return;
    }
    double parsed = 0.0;
    if (reader.fraction(value, pointer_member(path, member), parsed)) {
        output = parsed;
    }
}

void parse_event_payload(DocumentReader &reader, JsonValue value, std::string_view path,
                         ScenarioEventPayload &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);

    if (type == "operating_state_patch") {
        reader.reject_unknown(value, path,
                              {"type", "ignition_enabled", "fuel_enabled",
                               "starter_enabled", "dyno_enabled", "limiter_enabled"});
        OperatingStatePatch parsed;
        parse_optional_boolean(reader, value, "ignition_enabled", path,
                               parsed.ignition_enabled);
        parse_optional_boolean(reader, value, "fuel_enabled", path,
                               parsed.fuel_enabled);
        parse_optional_boolean(reader, value, "starter_enabled", path,
                               parsed.starter_enabled);
        parse_optional_boolean(reader, value, "dyno_enabled", path,
                               parsed.dyno_enabled);
        parse_optional_boolean(reader, value, "limiter_enabled", path,
                               parsed.limiter_enabled);
        if (!parsed.ignition_enabled && !parsed.fuel_enabled &&
            !parsed.starter_enabled && !parsed.dyno_enabled &&
            !parsed.limiter_enabled) {
            reader.add(DiagnosticCode::missing_value, path,
                       "operating-state patch must set at least one state");
        }
        output = std::move(parsed);
    } else if (type == "select_gear") {
        reader.reject_unknown(value, path, {"type", "gear"});
        SelectGearEvent parsed;
        read_ref_member(reader, value, "gear", path, parsed.gear);
        output = std::move(parsed);
    } else if (type == "set_clutch") {
        reader.reject_unknown(value, path, {"type", "clutch_position_01"});
        SetClutchEvent parsed;
        reader.fraction(reader.required(value, "clutch_position_01", path),
                        pointer_member(path, "clutch_position_01"),
                        parsed.clutch_position_01);
        output = std::move(parsed);
    } else if (type == "set_route_monitoring") {
        reader.reject_unknown(value, path,
                              {"type", "route", "muted", "gain_linear", "wet_mix_01"});
        SetRouteMonitoringEvent parsed;
        read_ref_member(reader, value, "route", path, parsed.route);
        parse_optional_boolean(reader, value, "muted", path, parsed.muted);
        parse_optional_nonnegative_number(reader, value, "gain_linear", path,
                                          parsed.gain_linear);
        parse_optional_fraction(reader, value, "wet_mix_01", path, parsed.wet_mix_01);
        if (!parsed.muted && !parsed.gain_linear && !parsed.wet_mix_01) {
            reader.add(DiagnosticCode::missing_value, path,
                       "route-monitoring event must set at least one value");
        }
        output = std::move(parsed);
    } else if (type == "set_master_monitoring") {
        reader.reject_unknown(value, path, {"type", "gain_linear"});
        SetMasterMonitoringEvent parsed;
        reader.nonnegative_number(reader.required(value, "gain_linear", path),
                                  pointer_member(path, "gain_linear"),
                                  parsed.gain_linear);
        output = std::move(parsed);
    } else if (type == "set_conditioning_monitoring") {
        reader.reject_unknown(
            value, path,
            {"type", "jitter_scale", "derivative_mix_01", "air_noise_mix_01"});
        SetConditioningMonitoringEvent parsed;
        parse_optional_nonnegative_number(reader, value, "jitter_scale", path,
                                          parsed.jitter_scale);
        parse_optional_fraction(reader, value, "derivative_mix_01", path,
                                parsed.derivative_mix_01);
        parse_optional_fraction(reader, value, "air_noise_mix_01", path,
                                parsed.air_noise_mix_01);
        if (!parsed.jitter_scale && !parsed.derivative_mix_01 &&
            !parsed.air_noise_mix_01) {
            reader.add(DiagnosticCode::missing_value, path,
                       "conditioning-monitoring event must set at least one value");
        }
        output = std::move(parsed);
    } else if (type == "lifecycle") {
        reader.reject_unknown(value, path, {"type", "action"});
        LifecycleEvent parsed;
        read_enum(reader, reader.required(value, "action", path),
                  pointer_member(path, "action"),
                  {{"startup", LifecycleAction::startup},
                   {"shutdown", LifecycleAction::shutdown},
                   {"reset", LifecycleAction::reset}},
                  parsed.action);
        output = std::move(parsed);
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown scenario event type '" + type + "'");
    }
}

void parse_event(DocumentReader &reader, JsonValue value, std::string_view path,
                 ScenarioEvent &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "time", "payload"});
    read_id_member(reader, value, "id", path, output.id);
    read_quantity_member(reader, value, "time", path, QuantityDimension::duration,
                         output.time);
    require_nonnegative(reader, output.time, pointer_member(path, "time"));
    parse_event_payload(reader, reader.required(value, "payload", path),
                        pointer_member(path, "payload"), output.payload);
}

void parse_rates(DocumentReader &reader, JsonValue value, std::string_view path,
                 ScenarioRates &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"physics", "capture", "source_processing", "acoustics", "delivery"});
    reader.rational_rate(reader.required(value, "physics", path),
                         pointer_member(path, "physics"), output.physics);
    reader.rational_rate(reader.required(value, "capture", path),
                         pointer_member(path, "capture"), output.capture);
    reader.rational_rate(reader.required(value, "source_processing", path),
                         pointer_member(path, "source_processing"),
                         output.source_processing);
    reader.rational_rate(reader.required(value, "acoustics", path),
                         pointer_member(path, "acoustics"), output.acoustics);
    reader.rational_rate(reader.required(value, "delivery", path),
                         pointer_member(path, "delivery"), output.delivery);
}

struct QualityIdentifierTag;

void parse_quality(DocumentReader &reader, JsonValue value, std::string_view path,
                   ScenarioQuality &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "process_block_capacity_frames",
                           "event_queue_capacity", "telemetry_capacity_frames"});
    StableId<QualityIdentifierTag> id;
    reader.id(reader.required(value, "id", path), pointer_member(path, "id"), id);
    output.id = std::move(id.value);
    reader.uint32(reader.required(value, "process_block_capacity_frames", path),
                  pointer_member(path, "process_block_capacity_frames"),
                  output.process_block_capacity_frames);
    reader.uint32(reader.required(value, "event_queue_capacity", path),
                  pointer_member(path, "event_queue_capacity"),
                  output.event_queue_capacity);
    reader.uint32(reader.required(value, "telemetry_capacity_frames", path),
                  pointer_member(path, "telemetry_capacity_frames"),
                  output.telemetry_capacity_frames);

    const auto require_capacity = [&](std::uint32_t capacity, std::uint32_t maximum,
                                      std::string_view member,
                                      std::string_view meaning) {
        const auto member_path = pointer_member(path, member);
        if (capacity == 0U) {
            reader.add(DiagnosticCode::out_of_range, member_path,
                       std::string{meaning} + " must be positive");
        } else if (capacity > maximum) {
            reader.add(DiagnosticCode::resource_limit, member_path,
                       std::string{meaning} +
                           " exceeds the configured authoring limit of " +
                           std::to_string(maximum));
        }
    };
    require_capacity(output.process_block_capacity_frames,
                     reader.limits().maximum_process_block_capacity_frames,
                     "process_block_capacity_frames",
                     "maximum delivery-frame process-call capacity");
    require_capacity(output.event_queue_capacity,
                     reader.limits().maximum_event_queue_capacity,
                     "event_queue_capacity", "caller control-command queue capacity");
    require_capacity(output.telemetry_capacity_frames,
                     reader.limits().maximum_telemetry_capacity_frames,
                     "telemetry_capacity_frames", "returned telemetry-frame capacity");
}

void parse_output(DocumentReader &reader, JsonValue value, std::string_view path,
                  OutputSelection &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"buses", "telemetry_channels"});
    detail::read_required_array(
        reader, value, "buses", path, output.buses,
        [&](JsonValue item, std::string_view item_path, AudioBusRef &reference) {
            reader.ref(item, item_path, reference);
        });
    detail::read_required_array(reader, value, "telemetry_channels", path,
                                output.telemetry_channels,
                                [&](JsonValue item, std::string_view item_path,
                                    TelemetryChannelRef &reference) {
                                    reader.ref(item, item_path, reference);
                                });
}

template <class StableValue>
void reject_duplicate_stable_values(DocumentReader &reader,
                                    const std::vector<StableValue> &values,
                                    std::string_view path,
                                    std::string_view description) {
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto &[value] = values[index];
        if (!value.empty() && !seen.insert(value).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_index(path, index),
                       "duplicate " + std::string{description} + " '" + value + "'");
        }
    }
}

void parse_scenario_root(DocumentReader &reader, JsonValue value,
                         ScenarioDocument &output) {
    if (!reader.object(value, "")) {
        return;
    }
    reader.reject_unknown(value, "",
                          {"schema", "id", "engine", "fuel", "ambient",
                           "initial_thermal_state", "crankcase", "initial_state",
                           "preparation", "mode", "events", "rates", "quality",
                           "total_duration", "audible_start", "audible_duration",
                           "public_seed", "output"});
    reader.string(reader.required(value, "schema", ""), "/schema", output.schema);
    if (!output.schema.empty() && output.schema != "engine-sim-offline/scenario") {
        reader.add(DiagnosticCode::unsupported_schema, "/schema",
                   "expected schema 'engine-sim-offline/scenario'");
    }
    read_id_member(reader, value, "id", "", output.id);
    read_ref_member(reader, value, "engine", "", output.engine);
    read_ref_member(reader, value, "fuel", "", output.fuel);
    parse_ambient(reader, reader.required(value, "ambient", ""), "/ambient",
                  output.ambient);
    parse_thermal(reader, reader.required(value, "initial_thermal_state", ""),
                  "/initial_thermal_state", output.initial_thermal_state);
    parse_crankcase(reader, reader.required(value, "crankcase", ""), "/crankcase",
                    output.crankcase);
    parse_initial_state(reader, reader.required(value, "initial_state", ""),
                        "/initial_state", output.initial_state);
    parse_preparation(reader, reader.required(value, "preparation", ""), "/preparation",
                      output.preparation);
    parse_mode(reader, reader.required(value, "mode", ""), "/mode", output.mode);
    detail::read_required_array(
        reader, value, "events", "", output.events,
        [&](JsonValue item, std::string_view item_path, ScenarioEvent &event) {
            parse_event(reader, item, item_path, event);
        });
    parse_rates(reader, reader.required(value, "rates", ""), "/rates", output.rates);
    parse_quality(reader, reader.required(value, "quality", ""), "/quality",
                  output.quality);
    read_quantity_member(reader, value, "total_duration", "",
                         QuantityDimension::duration, output.total_duration);
    read_quantity_member(reader, value, "audible_start", "",
                         QuantityDimension::duration, output.audible_start);
    read_quantity_member(reader, value, "audible_duration", "",
                         QuantityDimension::duration, output.audible_duration);
    reader.uint64(reader.required(value, "public_seed", ""), "/public_seed",
                  output.public_seed);
    parse_output(reader, reader.required(value, "output", ""), "/output",
                 output.output);

    require_positive(reader, output.total_duration, "/total_duration");
    require_nonnegative(reader, output.audible_start, "/audible_start");
    require_positive(reader, output.audible_duration, "/audible_duration");

    const auto total_seconds = duration_seconds(output.total_duration);
    const auto audible_start_seconds = duration_seconds(output.audible_start);
    const auto audible_duration_seconds = duration_seconds(output.audible_duration);
    if (total_seconds && audible_start_seconds && audible_duration_seconds &&
        *audible_start_seconds + *audible_duration_seconds > *total_seconds) {
        reader.add(DiagnosticCode::inconsistent_value, "/audible_duration",
                   "audible interval extends beyond total duration");
    }

    std::unordered_set<std::string> event_ids;
    double previous_event_time_seconds = -1.0;
    for (std::size_t index = 0; index < output.events.size(); ++index) {
        const auto &event = output.events[index];
        const auto event_path = pointer_index("/events", index);
        if (!event.id.value.empty() && !event_ids.insert(event.id.value).second) {
            reader.add(DiagnosticCode::duplicate_id, pointer_member(event_path, "id"),
                       "duplicate scenario event ID '" + event.id.value + "'");
        }
        const auto event_time_seconds = duration_seconds(event.time);
        if (event_time_seconds) {
            if (*event_time_seconds < previous_event_time_seconds) {
                reader.add(DiagnosticCode::inconsistent_value,
                           pointer_member(event_path, "time"),
                           "scenario events must be ordered by time");
            }
            if (total_seconds && *event_time_seconds > *total_seconds) {
                reader.add(DiagnosticCode::inconsistent_value,
                           pointer_member(event_path, "time"),
                           "scenario event occurs after total duration");
            }
            previous_event_time_seconds =
                std::max(previous_event_time_seconds, *event_time_seconds);
        }
    }
    reject_duplicate_stable_values(reader, output.output.buses, "/output/buses",
                                   "output bus reference");
    reject_duplicate_stable_values(reader, output.output.telemetry_channels,
                                   "/output/telemetry_channels",
                                   "telemetry-channel reference");
}

[[nodiscard]] DiagnosticReport resource_diagnostic(std::string message) {
    Diagnostic diagnostic;
    diagnostic.code = DiagnosticCode::resource_limit;
    diagnostic.message = std::move(message);
    return DiagnosticReport{{std::move(diagnostic)}};
}

template <class Definitions, class Ref>
[[nodiscard]] bool contains_id(const Definitions &definitions, const Ref &reference) {
    return std::ranges::any_of(definitions, [&](const auto &definition) {
        return definition.id.value == reference.value;
    });
}

void add_dangling(DiagnosticReport &report, std::string path, std::string message) {
    report.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::error,
        DiagnosticCode::dangling_reference,
        std::move(path),
        std::nullopt,
        std::nullopt,
        std::move(message),
        {},
    });
}

} // namespace

ScenarioDocumentParseResult
parse_scenario_document(std::string_view json, AuthoringParseLimits limits) noexcept {
    try {
        auto json_result = parse_json(json, limits.json);
        if (const auto *error = std::get_if<JsonParseError>(&json_result)) {
            return detail::syntax_diagnostic(*error);
        }
        auto document = std::get<JsonDocument>(std::move(json_result));
        DocumentReader reader{std::move(limits)};
        ScenarioDocument output;
        parse_scenario_root(reader, document.root(), output);
        if (!reader.ok()) {
            return std::move(reader).finish();
        }
        return output;
    } catch (const std::bad_alloc &) {
        return resource_diagnostic("allocation failed while parsing scenario document");
    } catch (const std::exception &exception) {
        return detail::internal_diagnostic("unexpected scenario parser failure: " +
                                           std::string{exception.what()});
    } catch (...) {
        return detail::internal_diagnostic(
            "unexpected non-standard scenario parser failure");
    }
}

DiagnosticReport
validate_scenario_references(const ScenarioDocument &scenario,
                             const EnginePackageDocument &package) noexcept {
    try {
        DiagnosticReport report;
        if (scenario.engine.value != package.engine.identity.id.value) {
            add_dangling(report, "/engine",
                         "engine reference '" + scenario.engine.value +
                             "' does not name the supplied engine");
        }
        if (!contains_id(package.engine.fuels, scenario.fuel)) {
            add_dangling(report, "/fuel",
                         "fuel reference '" + scenario.fuel.value +
                             "' does not resolve");
        }
        for (std::size_t index = 0; index < scenario.output.buses.size(); ++index) {
            const auto &bus = scenario.output.buses[index];
            if (!contains_id(package.presentation.buses, bus)) {
                add_dangling(report, pointer_index("/output/buses", index),
                             "audio bus reference '" + bus.value +
                                 "' does not resolve");
            }
        }

        const TransmissionDefinition *transmission = nullptr;
        if (package.rig && package.rig->transmission) {
            transmission = &*package.rig->transmission;
        }
        if (const auto *mode = std::get_if<FreeVehicleMode>(&scenario.mode)) {
            if (!package.rig || mode->rig.value != package.rig->id.value) {
                add_dangling(report, "/mode/rig",
                             "rig reference '" + mode->rig.value +
                                 "' does not resolve");
            }
            if (transmission == nullptr ||
                !contains_id(transmission->gears, mode->initial_gear)) {
                add_dangling(report, "/mode/initial_gear",
                             "initial gear reference '" + mode->initial_gear.value +
                                 "' does not resolve");
            }
        }

        for (std::size_t index = 0; index < scenario.events.size(); ++index) {
            const auto &payload = scenario.events[index].payload;
            const auto payload_path =
                pointer_member(pointer_index("/events", index), "payload");
            if (const auto *event = std::get_if<SelectGearEvent>(&payload)) {
                if (transmission == nullptr ||
                    !contains_id(transmission->gears, event->gear)) {
                    add_dangling(report, pointer_member(payload_path, "gear"),
                                 "gear reference '" + event->gear.value +
                                     "' does not resolve");
                }
            } else if (const auto *event =
                           std::get_if<SetRouteMonitoringEvent>(&payload)) {
                if (!contains_id(package.engine.source_routes, event->route)) {
                    add_dangling(report, pointer_member(payload_path, "route"),
                                 "source route reference '" + event->route.value +
                                     "' does not resolve");
                }
            }
        }
        return report;
    } catch (const std::bad_alloc &) {
        return resource_diagnostic(
            "allocation failed while validating scenario references");
    } catch (const std::exception &exception) {
        return detail::internal_diagnostic(
            "unexpected scenario reference-validator failure: " +
            std::string{exception.what()});
    } catch (...) {
        return detail::internal_diagnostic(
            "unexpected non-standard scenario reference-validator failure");
    }
}

} // namespace engine_sim_offline::authoring
