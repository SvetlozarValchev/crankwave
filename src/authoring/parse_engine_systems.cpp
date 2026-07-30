#include "authoring/parse_engine_detail.hpp"

#include <string>
#include <utility>

namespace engine_sim_offline::authoring::detail {
namespace {

void parse_ignition_wire(DocumentReader &reader, JsonValue value, std::string_view path,
                         IgnitionWireDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id"});
    read_id_member(reader, value, "id", path, output.id);
}

void parse_firing_event(DocumentReader &reader, JsonValue value, std::string_view path,
                        FiringEventDefinition &output,
                        const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    reader.reject_unknown(value, path, {"wire", "crank_angle"}, subject_value);
    read_ref_member(reader, value, "wire", path, output.wire, subject_value);
    read_quantity_member(reader, value, "crank_angle", path, QuantityDimension::angle,
                         output.crank_angle, subject_value);
}

} // namespace

void parse_ignition(DocumentReader &reader, JsonValue value, std::string_view path,
                    IgnitionDefinition &output,
                    const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"timing_curve", "wires", "firing_order", "limiter"},
                          subject_value);
    read_ref_member(reader, value, "timing_curve", path, output.timing_curve,
                    subject_value);
    read_required_array(
        reader, value, "wires", path, output.wires,
        [&](JsonValue item, std::string_view item_path, IgnitionWireDefinition &wire) {
            parse_ignition_wire(reader, item, item_path, wire);
        },
        subject_value);
    read_required_array(
        reader, value, "firing_order", path, output.firing_order,
        [&](JsonValue item, std::string_view item_path, FiringEventDefinition &event) {
            parse_firing_event(reader, item, item_path, event, subject_value);
        },
        subject_value);
    const auto limiter = reader.required(value, "limiter", path, subject_value);
    const auto limiter_path = pointer_member(path, "limiter");
    if (reader.object(limiter, limiter_path, subject_value)) {
        reader.reject_unknown(limiter, limiter_path,
                              {"activation_speed", "cut_duration"}, subject_value);
        read_quantity_member(reader, limiter, "activation_speed", limiter_path,
                             QuantityDimension::angular_speed,
                             output.limiter.activation_speed, subject_value);
        read_quantity_member(reader, limiter, "cut_duration", limiter_path,
                             QuantityDimension::duration, output.limiter.cut_duration,
                             subject_value);
        require_positive(reader, output.limiter.activation_speed,
                         pointer_member(limiter_path, "activation_speed"),
                         subject_value);
        require_nonnegative(reader, output.limiter.cut_duration,
                            pointer_member(limiter_path, "cut_duration"),
                            subject_value);
    }
}

void parse_fuel(DocumentReader &reader, JsonValue value, std::string_view path,
                FuelDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "display_name", "molecular_mass", "density",
                           "lower_heating_value", "stoichiometric_air_fuel_molar_ratio",
                           "turbulence_to_flame_speed", "combustion"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("fuel", output.id.value);
    reader.string(reader.required(value, "display_name", path, owner),
                  pointer_member(path, "display_name"), output.display_name, owner);
    read_quantity_member(reader, value, "molecular_mass", path,
                         QuantityDimension::molar_mass, output.molecular_mass, owner);
    const auto density = reader.optional(value, "density");
    if (density.valid() && !density.is_null()) {
        Quantity parsed;
        reader.quantity(density, pointer_member(path, "density"),
                        QuantityDimension::density, parsed, owner);
        require_positive(reader, parsed, pointer_member(path, "density"), owner);
        output.density = std::move(parsed);
    }
    read_quantity_member(reader, value, "lower_heating_value", path,
                         QuantityDimension::energy_per_mass, output.lower_heating_value,
                         owner);
    reader.nonnegative_number(
        reader.required(value, "stoichiometric_air_fuel_molar_ratio", path, owner),
        pointer_member(path, "stoichiometric_air_fuel_molar_ratio"),
        output.stoichiometric_air_fuel_molar_ratio, owner);
    read_ref_member(reader, value, "turbulence_to_flame_speed", path,
                    output.turbulence_to_flame_speed, owner);
    parse_combustion(reader, reader.required(value, "combustion", path, owner),
                     pointer_member(path, "combustion"), output.combustion, owner);
    require_positive(reader, output.molecular_mass,
                     pointer_member(path, "molecular_mass"), owner);
    require_positive(reader, output.lower_heating_value,
                     pointer_member(path, "lower_heating_value"), owner);
    if (output.stoichiometric_air_fuel_molar_ratio <= 0.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "stoichiometric_air_fuel_molar_ratio"),
                   "stoichiometric ratio must be positive", owner);
    }
}

void parse_combustion(DocumentReader &reader, JsonValue value, std::string_view path,
                      CombustionDefinition &output,
                      const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"maximum_efficiency_01", "cycle_variation_01",
                           "low_efficiency_attenuation_01", "maximum_turbulence_effect",
                           "maximum_dilution_effect"},
                          subject_value);
    reader.fraction(
        reader.required(value, "maximum_efficiency_01", path, subject_value),
        pointer_member(path, "maximum_efficiency_01"), output.maximum_efficiency_01,
        subject_value);
    reader.fraction(reader.required(value, "cycle_variation_01", path, subject_value),
                    pointer_member(path, "cycle_variation_01"),
                    output.cycle_variation_01, subject_value);
    reader.fraction(
        reader.required(value, "low_efficiency_attenuation_01", path, subject_value),
        pointer_member(path, "low_efficiency_attenuation_01"),
        output.low_efficiency_attenuation_01, subject_value);
    reader.nonnegative_number(
        reader.required(value, "maximum_turbulence_effect", path, subject_value),
        pointer_member(path, "maximum_turbulence_effect"),
        output.maximum_turbulence_effect, subject_value);
    reader.nonnegative_number(
        reader.required(value, "maximum_dilution_effect", path, subject_value),
        pointer_member(path, "maximum_dilution_effect"),
        output.maximum_dilution_effect, subject_value);
    if (output.maximum_dilution_effect <= 0.0) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "maximum_dilution_effect"),
                   "maximum dilution effect must be positive", subject_value);
    }
}

void parse_losses(DocumentReader &reader, JsonValue value, std::string_view path,
                  EngineLossDefinition &output,
                  const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path, subject_value),
                  pointer_member(path, "type"), type, subject_value);
    if (type != "chen_flynn_cycle_mean") {
        if (!type.empty()) {
            reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                       "unknown engine-loss type '" + type + "'", subject_value);
        }
        return;
    }

    reader.reject_unknown(
        value, path,
        {"type", "constant_fmep", "peak_pressure_coefficient",
         "mean_piston_speed_coefficient",
         "mean_piston_speed_squared_coefficient", "required_oil_temperature",
         "accessory_configuration_id"},
        subject_value);
    ChenFlynnLossDefinition parsed;
    read_quantity_member(reader, value, "constant_fmep", path,
                         QuantityDimension::pressure, parsed.constant_fmep,
                         subject_value);
    reader.nonnegative_number(
        reader.required(value, "peak_pressure_coefficient", path, subject_value),
        pointer_member(path, "peak_pressure_coefficient"),
        parsed.peak_pressure_coefficient, subject_value);
    read_quantity_member(reader, value, "mean_piston_speed_coefficient", path,
                         QuantityDimension::pressure_per_speed,
                         parsed.mean_piston_speed_coefficient, subject_value);
    read_quantity_member(reader, value, "mean_piston_speed_squared_coefficient", path,
                         QuantityDimension::pressure_per_speed_squared,
                         parsed.mean_piston_speed_squared_coefficient, subject_value);
    read_quantity_member(reader, value, "required_oil_temperature", path,
                         QuantityDimension::temperature,
                         parsed.required_oil_temperature, subject_value);
    reader.id(reader.required(value, "accessory_configuration_id", path,
                              subject_value),
              pointer_member(path, "accessory_configuration_id"),
              parsed.accessory_configuration_id, subject_value);
    require_nonnegative(reader, parsed.constant_fmep,
                        pointer_member(path, "constant_fmep"), subject_value);
    require_nonnegative(reader, parsed.mean_piston_speed_coefficient,
                        pointer_member(path, "mean_piston_speed_coefficient"),
                        subject_value);
    require_nonnegative(
        reader, parsed.mean_piston_speed_squared_coefficient,
        pointer_member(path, "mean_piston_speed_squared_coefficient"),
        subject_value);
    output = std::move(parsed);
}

void parse_throttle_controller(DocumentReader &reader, JsonValue value,
                               std::string_view path,
                               ThrottleControllerDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path), pointer_member(path, "type"),
                  type);
    if (type == "direct") {
        reader.reject_unknown(value, path, {"id", "type", "gamma"});
    } else if (type == "governor") {
        reader.reject_unknown(value, path,
                              {"id", "type", "minimum_engine_speed",
                               "maximum_engine_speed", "minimum_velocity",
                               "maximum_velocity", "k_s", "k_d", "gamma"});
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown throttle-controller type '" + type + "'");
    }
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("throttle_controller", output.id.value);
    if (type == "direct") {
        DirectThrottleController parsed;
        reader.nonnegative_number(reader.required(value, "gamma", path, owner),
                                  pointer_member(path, "gamma"), parsed.gamma, owner);
        if (parsed.gamma <= 0.0) {
            reader.add(DiagnosticCode::out_of_range, pointer_member(path, "gamma"),
                       "throttle gamma must be positive", owner);
        }
        output.kind = std::move(parsed);
    } else if (type == "governor") {
        GovernorThrottleController parsed;
        read_quantity_member(reader, value, "minimum_engine_speed", path,
                             QuantityDimension::angular_speed,
                             parsed.minimum_engine_speed, owner);
        read_quantity_member(reader, value, "maximum_engine_speed", path,
                             QuantityDimension::angular_speed,
                             parsed.maximum_engine_speed, owner);
        reader.number(reader.required(value, "minimum_velocity", path, owner),
                      pointer_member(path, "minimum_velocity"),
                      parsed.minimum_velocity, owner);
        reader.number(reader.required(value, "maximum_velocity", path, owner),
                      pointer_member(path, "maximum_velocity"),
                      parsed.maximum_velocity, owner);
        reader.nonnegative_number(reader.required(value, "k_s", path, owner),
                                  pointer_member(path, "k_s"), parsed.k_s,
                                  owner);
        reader.nonnegative_number(reader.required(value, "k_d", path, owner),
                                  pointer_member(path, "k_d"), parsed.k_d,
                                  owner);
        reader.nonnegative_number(reader.required(value, "gamma", path, owner),
                                  pointer_member(path, "gamma"), parsed.gamma, owner);
        if (parsed.minimum_engine_speed.value > parsed.maximum_engine_speed.value) {
            reader.add(DiagnosticCode::inconsistent_value,
                       pointer_member(path, "maximum_engine_speed"),
                       "maximum engine speed is below minimum engine speed", owner);
        }
        if (parsed.minimum_velocity > parsed.maximum_velocity) {
            reader.add(DiagnosticCode::inconsistent_value,
                       pointer_member(path, "maximum_velocity"),
                       "maximum velocity is below minimum velocity", owner);
        }
        if (parsed.gamma <= 0.0) {
            reader.add(DiagnosticCode::out_of_range, pointer_member(path, "gamma"),
                       "governor gamma must be positive", owner);
        }
        output.kind = std::move(parsed);
    }
}

void parse_starter(DocumentReader &reader, JsonValue value, std::string_view path,
                   StarterDefinition &output,
                   const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path, subject_value),
                  pointer_member(path, "type"), type, subject_value);
    if (type == "mechanically_disengaged") {
        reader.reject_unknown(value, path, {"type"}, subject_value);
        output = MechanicallyDisengagedStarter{};
    } else if (type == "cranking") {
        reader.reject_unknown(
            value, path, {"type", "torque", "target_speed", "release_speed"},
            subject_value);
        CrankingStarter parsed;
        read_quantity_member(reader, value, "torque", path,
                             QuantityDimension::torque, parsed.torque,
                             subject_value);
        read_quantity_member(reader, value, "target_speed", path,
                             QuantityDimension::angular_speed, parsed.target_speed,
                             subject_value);
        const auto release = reader.optional(value, "release_speed");
        if (release.valid() && !release.is_null()) {
            Quantity parsed_release;
            reader.quantity(release, pointer_member(path, "release_speed"),
                            QuantityDimension::angular_speed, parsed_release,
                            subject_value);
            require_positive(reader, parsed_release,
                             pointer_member(path, "release_speed"), subject_value);
            parsed.release_speed = std::move(parsed_release);
        }
        require_nonnegative(reader, parsed.torque, pointer_member(path, "torque"),
                            subject_value);
        require_nonnegative(reader, parsed.target_speed,
                            pointer_member(path, "target_speed"), subject_value);
        output = std::move(parsed);
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown starter type '" + type + "'", subject_value);
    }
}

} // namespace engine_sim_offline::authoring::detail
